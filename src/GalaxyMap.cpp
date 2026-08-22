#include "GalaxyMap.h"
#include "QuestTracking.h"

namespace TrackQuestFromMap::GalaxyMap
{
	namespace
	{
		constexpr std::ptrdiff_t kComposeQuestOffset = 0x8;
		constexpr std::size_t kMaximumCapturedTargets = 4096;
		constexpr std::size_t kMaximumQuestTargetTextBytes = 4096;

		struct CapturedRecord
		{
			std::uint32_t systemLocationID{};
			std::uint32_t bodyLocationID{};
			RE::QuestInstanceKey quest{};
			RE::BSFixedString questTargetText;
			bool questActive{};
		};

		struct GenerationCapture
		{
			std::array<CapturedRecord, kMaximumCapturedTargets> records{};
			std::size_t count{};
			std::uint64_t generation{};
			bool invalid{};

			void Reset(const std::uint64_t a_generation) noexcept
			{
				for (std::size_t index = 0; index < count; ++index)
				{
					records[index] = {};
				}
				count = 0;
				generation = a_generation;
				invalid = false;
			}

			void Add(const CapturedRecord& a_record) noexcept
			{
				if (count >= records.size())
				{
					invalid = true;
					return;
				}
				records[count++] = a_record;
			}
		};

		struct MarkerRecord
		{
			std::uint32_t systemLocationID{};
			std::uint32_t bodyLocationID{};
			RE::QuestInstanceKey quest{};
			std::string questTargetText;
			bool questActive{};
		};

		BuildQuestTargetTree originalBuildQuestTargetTree{};
		ComposeQuestTargetMarker originalComposeQuestTargetMarker{};
		InsertQuestTargetMarker originalInsertQuestTargetMarker{};
		bool refreshValidated{};

		thread_local GenerationCapture threadCapture;
		thread_local GenerationCapture* activeCapture{};
		thread_local RE::QuestInstanceKey activeContributor{};
		thread_local bool contributorValid{};
		thread_local std::size_t buildDepth{};
		thread_local std::size_t composeDepth{};

		std::atomic<std::uint64_t> nextGeneration{1};
		std::atomic<std::uint64_t> latestStartedGeneration{};
		std::mutex cacheMutex;
		std::uint64_t publishedGeneration{};
		std::vector<MarkerRecord> markerCache;

		template <class T>
		[[nodiscard]] T ReadAt(const void* a_base, const std::ptrdiff_t a_offset) noexcept
		{
			T result{};
			std::memcpy(
				std::addressof(result),
				static_cast<const std::byte*>(a_base) + a_offset,
				sizeof(result));
			return result;
		}

		void ClearLatestGeneration(const std::uint64_t a_generation)
		{
			std::scoped_lock lock{cacheMutex};
			if (latestStartedGeneration.load(std::memory_order_acquire) == a_generation)
			{
				markerCache.clear();
				publishedGeneration = 0;
			}
		}

		void Publish(GenerationCapture& a_capture)
		{
			if (a_capture.invalid ||
				latestStartedGeneration.load(std::memory_order_acquire) != a_capture.generation)
			{
				ClearLatestGeneration(a_capture.generation);
				return;
			}

			std::vector<MarkerRecord> next;
			next.reserve(a_capture.count);
			for (std::size_t index = 0; index < a_capture.count; ++index)
			{
				const auto& captured = a_capture.records[index];
				next.push_back(MarkerRecord{
					.systemLocationID = captured.systemLocationID,
					.bodyLocationID = captured.bodyLocationID,
					.quest = captured.quest,
					.questTargetText = std::string{
						captured.questTargetText.c_str(),
						captured.questTargetText.length()
					},
					.questActive = captured.questActive
				});
			}

			std::scoped_lock lock{cacheMutex};
			if (latestStartedGeneration.load(std::memory_order_acquire) != a_capture.generation)
			{
				return;
			}
			markerCache = std::move(next);
			publishedGeneration = a_capture.generation;
		}

		[[nodiscard]] std::optional<RE::QuestInstanceKey> ResolveRequest(
			const Request& a_request)
		{
			std::optional<RE::QuestInstanceKey> resolved;
			std::scoped_lock lock{cacheMutex};
			const auto latest = latestStartedGeneration.load(std::memory_order_acquire);
			if (publishedGeneration == 0 || publishedGeneration != latest)
			{
				return std::nullopt;
			}

			for (const auto& marker : markerCache)
			{
				const bool locationMatches = a_request.view == View::kGalaxy
					? marker.systemLocationID == a_request.markerLocationID
					: marker.bodyLocationID == a_request.markerLocationID;
				if (!locationMatches ||
					marker.questTargetText != a_request.questTargetText ||
					marker.questActive)
				{
					continue;
				}
				if (resolved && *resolved != marker.quest)
				{
					logger::warn(
						"Rejected ambiguous {} quest marker {}",
						a_request.view == View::kGalaxy ? "Galaxy" : "System",
						a_request.markerLocationID);
					return std::nullopt;
				}
				resolved = marker.quest;
			}
			return resolved;
		}

		void RefreshCurrentStarMap()
		{
			if (!refreshValidated)
			{
				return;
			}

			const auto* ui = RE::UI::GetSingleton();
			if (!ui)
			{
				return;
			}

			const RE::BSFixedString menuName{RE::StarMap::StarMapMenu::MENU_NAME.data()};
			const auto menu = ui->GetMenu(menuName);
			auto* starMapMenu = menu
				? starfield_cast<RE::StarMap::StarMapMenu*>(menu.get())
				: nullptr;
			if (!starMapMenu)
			{
				logger::warn("Skipped Star Map refresh: unexpected menu type");
				return;
			}

			starMapMenu->RefreshQuestTargets();
			logger::debug("Refreshed Star Map quest targets");
		}
	}

	void SetOriginalFunctions(
		const BuildQuestTargetTree a_buildQuestTargetTree,
		const ComposeQuestTargetMarker a_composeQuestTargetMarker,
		const InsertQuestTargetMarker a_insertQuestTargetMarker,
		const bool a_refreshValidated) noexcept
	{
		originalBuildQuestTargetTree = a_buildQuestTargetTree;
		originalComposeQuestTargetMarker = a_composeQuestTargetMarker;
		originalInsertQuestTargetMarker = a_insertQuestTargetMarker;
		refreshValidated = a_refreshValidated;
	}

	void BuildAndPublishQuestTargetTree(void* a_playerCharacter, void* a_outputTree) noexcept
	{
		if (!originalBuildQuestTargetTree)
		{
			logger::critical("Star Map hook has no vanilla quest-target tree builder");
			std::terminate();
		}

		++buildDepth;
		if (buildDepth != 1)
		{
			if (activeCapture)
			{
				activeCapture->invalid = true;
			}
			originalBuildQuestTargetTree(a_playerCharacter, a_outputTree);
			--buildDepth;
			return;
		}

		const auto generation = nextGeneration.fetch_add(1, std::memory_order_acq_rel);
		latestStartedGeneration.store(generation, std::memory_order_release);
		threadCapture.Reset(generation);
		activeCapture = std::addressof(threadCapture);
		originalBuildQuestTargetTree(a_playerCharacter, a_outputTree);
		activeCapture = nullptr;
		--buildDepth;

		try
		{
			Publish(threadCapture);
		}
		catch (const std::exception& error)
		{
			threadCapture.invalid = true;
			logger::error("Star Map quest-target capture failed: {}", error.what());
		}
	}

	bool CaptureAndComposeQuestTargetMarker(void* a_context, void* a_target) noexcept
	{
		if (!originalComposeQuestTargetMarker)
		{
			logger::critical("Star Map hook has no vanilla quest-target marker composer");
			std::terminate();
		}

		++composeDepth;
		if (!activeCapture || buildDepth != 1 || composeDepth != 1)
		{
			if (activeCapture && composeDepth != 1)
			{
				activeCapture->invalid = true;
			}
			const bool result = originalComposeQuestTargetMarker(a_context, a_target);
			--composeDepth;
			return result;
		}

		contributorValid = false;
		const auto* quest = a_context ? ReadAt<const RE::TESQuest*>(a_context, kComposeQuestOffset) : nullptr;
		if (quest)
		{
			activeContributor = quest->GetInstanceKey();
			contributorValid = activeContributor.formID != 0;
		}
		if (!contributorValid)
		{
			activeCapture->invalid = true;
		}

		const bool result = originalComposeQuestTargetMarker(a_context, a_target);
		contributorValid = false;
		--composeDepth;
		return result;
	}

	void* CaptureInsertedQuestTargetMarker(
		void* const a_nestedTree,
		InsertResult* const a_result,
		RE::StarMap::QuestTargetMarkerData* const a_markerData) noexcept
	{
		if (!originalInsertQuestTargetMarker)
		{
			logger::critical("Star Map hook has no vanilla quest-target marker inserter");
			std::terminate();
		}

		CapturedRecord captured{};
		bool captureValid = false;
		const bool captureExpected =
			activeCapture && buildDepth == 1 && composeDepth == 1;
		if (captureExpected &&
			(!contributorValid || !a_nestedTree || !a_result || !a_markerData))
		{
			activeCapture->invalid = true;
		}
		if (captureExpected && contributorValid && a_nestedTree && a_result && a_markerData)
		{
			captured.systemLocationID = ReadAt<std::uint32_t>(a_nestedTree, -0x8);
			captured.bodyLocationID = a_markerData->bodyLocationID;
			captured.quest = activeContributor;
			if (a_markerData->questActive <= 1 &&
				captured.systemLocationID != 0 && captured.bodyLocationID != 0 &&
				a_markerData->questTargetText.length() <= kMaximumQuestTargetTextBytes)
			{
				captured.questTargetText = a_markerData->questTargetText;
				captured.questActive = a_markerData->questActive == 1;
				captureValid = true;
			}
			else
			{
				activeCapture->invalid = true;
			}
		}

		void* const result = originalInsertQuestTargetMarker(a_nestedTree, a_result, a_markerData);
		if (captureValid && a_result->inserted)
		{
			activeCapture->Add(captured);
		}
		return result;
	}

	bool TryActivate(const Request& a_request)
	{
		const auto key = ResolveRequest(a_request);
		if (!key)
		{
			logger::debug(
				"No inactive quest matches the selected {} marker",
				a_request.view == View::kGalaxy ? "Galaxy" : "System");
			return false;
		}

		const auto source = a_request.view == View::kGalaxy
			                    ? QuestTracking::Source::kGalaxy
			                    : QuestTracking::Source::kSystem;
		if (!QuestTracking::QueueTrack(*key, source, RefreshCurrentStarMap))
		{
			return false;
		}

		logger::info(
			"Queued {} quest 0x{:08X}, instance={}",
			a_request.view == View::kGalaxy ? "Galaxy Map" : "System Map",
			key->formID,
			key->instanceID);
		return true;
	}
}
