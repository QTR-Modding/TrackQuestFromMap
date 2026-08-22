#include "GalaxyMap.h"
#include "QuestTracking.h"

namespace TrackQuestSurface::GalaxyMap
{
	namespace
	{
		constexpr std::ptrdiff_t kComposeQuestOffset = 0x8;
		constexpr std::size_t    kMaximumCapturedTargets = 4096;
		constexpr std::size_t    kMaximumQuestTargetTextBytes = 4096;

		struct CapturedRecord
		{
			std::uint32_t        systemLocationID{};
			std::uint32_t        bodyLocationID{};
			RE::QuestInstanceKey quest{};
			RE::BSFixedString    questTargetText;
			bool                 questActive{};
		};

		struct GenerationCapture
		{
			std::array<CapturedRecord, kMaximumCapturedTargets> records{};
			std::size_t                                         count{};
			std::uint64_t                                       generation{};
			bool                                                invalid{};

			void Reset(const std::uint64_t a_generation) noexcept
			{
				for (std::size_t index = 0; index < count; ++index) {
					records[index] = {};
				}
				count = 0;
				generation = a_generation;
				invalid = false;
			}

			void Add(const CapturedRecord& a_record) noexcept
			{
				if (count >= records.size()) {
					invalid = true;
					return;
				}
				records[count++] = a_record;
			}
		};

		struct MarkerRecord
		{
			std::uint32_t        systemLocationID{};
			std::uint32_t        bodyLocationID{};
			RE::QuestInstanceKey quest{};
			std::string          questTargetText;
			bool                 questActive{};
		};

		BuildQuestTargetTree     originalBuildQuestTargetTree{};
		ComposeQuestTargetMarker originalComposeQuestTargetMarker{};
		InsertQuestTargetMarker  originalInsertQuestTargetMarker{};
		bool                     refreshValidated{};

		thread_local GenerationCapture    threadCapture;
		thread_local GenerationCapture*   activeCapture{};
		thread_local RE::QuestInstanceKey activeContributor{};
		thread_local bool                 contributorValid{};
		thread_local std::size_t          buildDepth{};
		thread_local std::size_t          composeDepth{};

		std::atomic<std::uint64_t> nextGeneration{ 1 };
		std::atomic<std::uint64_t> latestStartedGeneration{};
		std::mutex                 cacheMutex;
		std::uint64_t              publishedGeneration{};
		std::vector<MarkerRecord>  markerCache;

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

		void ClearLatestGeneration(const std::uint64_t a_generation) noexcept
		{
			try {
				std::scoped_lock lock{ cacheMutex };
				if (latestStartedGeneration.load(std::memory_order_acquire) == a_generation) {
					markerCache.clear();
					publishedGeneration = 0;
				}
			} catch (...) {
			}
		}

		void Publish(GenerationCapture& a_capture) noexcept
		{
			if (a_capture.invalid ||
				latestStartedGeneration.load(std::memory_order_acquire) != a_capture.generation) {
				ClearLatestGeneration(a_capture.generation);
				return;
			}

			try {
				std::vector<MarkerRecord> next;
				next.reserve(a_capture.count);
				for (std::size_t index = 0; index < a_capture.count; ++index) {
					const auto& captured = a_capture.records[index];
					next.push_back(MarkerRecord{
						.systemLocationID = captured.systemLocationID,
						.bodyLocationID = captured.bodyLocationID,
						.quest = captured.quest,
						.questTargetText = std::string{
							captured.questTargetText.c_str(),
							captured.questTargetText.length() },
						.questActive = captured.questActive });
				}

				std::scoped_lock lock{ cacheMutex };
				if (latestStartedGeneration.load(std::memory_order_acquire) != a_capture.generation) {
					return;
				}
				markerCache = std::move(next);
				publishedGeneration = a_capture.generation;
			} catch (...) {
				ClearLatestGeneration(a_capture.generation);
				a_capture.invalid = true;
			}
		}

		[[nodiscard]] std::optional<RE::QuestInstanceKey> ResolveRequest(
			const Request& a_request) noexcept
		{
			std::optional<RE::QuestInstanceKey> resolved;
			try {
				std::scoped_lock lock{ cacheMutex };
				const auto       latest = latestStartedGeneration.load(std::memory_order_acquire);
				if (publishedGeneration == 0 || publishedGeneration != latest) {
					return std::nullopt;
				}

				if (a_request.view == View::kGalaxy) {
					const MarkerRecord* displayed{};
					for (const auto& marker : markerCache) {
						if (marker.systemLocationID != a_request.markerLocationID) {
							continue;
						}
						if (marker.questActive) {
							return std::nullopt;
						}
						if (!displayed || marker.bodyLocationID > displayed->bodyLocationID) {
							displayed = std::addressof(marker);
						}
					}
					if (!displayed || displayed->questTargetText != a_request.questTargetText) {
						return std::nullopt;
					}
					resolved = displayed->quest;
				} else {
					for (const auto& marker : markerCache) {
						if (marker.bodyLocationID != a_request.markerLocationID ||
							marker.questTargetText != a_request.questTargetText ||
							marker.questActive) {
							continue;
						}
						if (resolved && *resolved != marker.quest) {
							return std::nullopt;
						}
						resolved = marker.quest;
					}
				}
			} catch (...) {
				return std::nullopt;
			}
			return resolved;
		}

		void RefreshCurrentStarMap() noexcept
		{
			if (!refreshValidated) {
				return;
			}

			try {
				const auto* ui = RE::UI::GetSingleton();
				if (!ui) {
					return;
				}

				const RE::BSFixedString menuName{ RE::StarMap::StarMapMenu::MENU_NAME.data() };
				const auto                    menu = ui->GetMenu(menuName);
				if (!menu) {
					return;
				}

				std::uintptr_t menuVtable{};
				std::memcpy(std::addressof(menuVtable), menu.get(), sizeof(menuVtable));
				if (menuVtable != RE::StarMap::StarMapMenu::PRIMARY_VTABLE.address()) {
					logger::warn("Skipped Galaxy/System repaint: unexpected StarMapMenu vtable");
					return;
				}

				auto* starMapMenu = static_cast<RE::StarMap::StarMapMenu*>(menu.get());
				starMapMenu->RefreshQuestTargets();
				logger::info("Refreshed live Star Map states after quest activation");
			} catch (const std::exception& error) {
				try {
					logger::warn("Galaxy/System repaint failed safely: {}", error.what());
				} catch (...) {
				}
			} catch (...) {
				try {
					logger::warn("Galaxy/System repaint failed safely");
				} catch (...) {
				}
			}
		}
	}

	void SetOriginalFunctions(
		const BuildQuestTargetTree     a_buildQuestTargetTree,
		const ComposeQuestTargetMarker a_composeQuestTargetMarker,
		const InsertQuestTargetMarker  a_insertQuestTargetMarker,
		const bool                     a_refreshValidated) noexcept
	{
		originalBuildQuestTargetTree = a_buildQuestTargetTree;
		originalComposeQuestTargetMarker = a_composeQuestTargetMarker;
		originalInsertQuestTargetMarker = a_insertQuestTargetMarker;
		refreshValidated = a_refreshValidated;
	}

	void BuildAndPublishQuestTargetTree(void* a_playerCharacter, void* a_outputTree) noexcept
	{
		if (!originalBuildQuestTargetTree) {
			std::terminate();
		}

		++buildDepth;
		if (buildDepth != 1) {
			if (activeCapture) {
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

		Publish(threadCapture);
		try {
			logger::info(
				"Captured Star Map quest-target generation {}: records={}, valid={}",
				generation,
				threadCapture.count,
				!threadCapture.invalid);
		} catch (...) {
		}
	}

	bool CaptureAndComposeQuestTargetMarker(void* a_context, void* a_target) noexcept
	{
		if (!originalComposeQuestTargetMarker) {
			std::terminate();
		}

		++composeDepth;
		if (!activeCapture || buildDepth != 1 || composeDepth != 1) {
			if (activeCapture && composeDepth != 1) {
				activeCapture->invalid = true;
			}
			const bool result = originalComposeQuestTargetMarker(a_context, a_target);
			--composeDepth;
			return result;
		}

		contributorValid = false;
		const auto* quest = a_context ?
		                        ReadAt<const RE::TESQuest*>(a_context, kComposeQuestOffset) :
		                        nullptr;
		if (quest) {
			activeContributor = quest->GetInstanceKey();
			contributorValid = activeContributor.formID != 0;
		}
		if (!contributorValid) {
			activeCapture->invalid = true;
		}

		const bool result = originalComposeQuestTargetMarker(a_context, a_target);
		contributorValid = false;
		--composeDepth;
		return result;
	}

	void* CaptureInsertedQuestTargetMarker(
		void* const                               a_nestedTree,
		InsertResult* const                       a_result,
		RE::StarMap::QuestTargetMarkerData* const a_markerData) noexcept
	{
		if (!originalInsertQuestTargetMarker) {
			std::terminate();
		}

		CapturedRecord captured{};
		bool           captureValid = false;
		const bool     captureExpected =
			activeCapture && buildDepth == 1 && composeDepth == 1;
		if (captureExpected &&
			(!contributorValid || !a_nestedTree || !a_result || !a_markerData)) {
			activeCapture->invalid = true;
		}
		if (captureExpected && contributorValid && a_nestedTree && a_result && a_markerData) {
			captured.systemLocationID = ReadAt<std::uint32_t>(a_nestedTree, -0x8);
			captured.bodyLocationID = a_markerData->bodyLocationID;
			captured.quest = activeContributor;
			if (a_markerData->questActive <= 1 &&
				captured.systemLocationID != 0 && captured.bodyLocationID != 0 &&
				a_markerData->questTargetText.length() <= kMaximumQuestTargetTextBytes) {
				captured.questTargetText = a_markerData->questTargetText;
				captured.questActive = a_markerData->questActive == 1;
				captureValid = true;
			} else {
				activeCapture->invalid = true;
			}
		}

		void* const result = originalInsertQuestTargetMarker(a_nestedTree, a_result, a_markerData);
		if (captureValid && a_result->inserted) {
			activeCapture->Add(captured);
		}
		return result;
	}

	bool TryActivate(const Request& a_request) noexcept
	{
		try {
			const auto key = ResolveRequest(a_request);
			if (!key) {
				logger::info(
					"Rejected {} marker {}: no exact inactive quest-target ownership match",
					a_request.view == View::kGalaxy ? "Galaxy" : "System",
					a_request.markerLocationID);
				return false;
			}

			const auto source = a_request.view == View::kGalaxy ?
			                        QuestTracking::Source::kGalaxy :
			                        QuestTracking::Source::kSystem;
			if (!QuestTracking::QueueTrack(*key, source, RefreshCurrentStarMap)) {
				return false;
			}

			logger::info(
				"Accepted {} marker {}: quest=0x{:08X}, instance={}, labelBytes={}",
				a_request.view == View::kGalaxy ? "Galaxy" : "System",
				a_request.markerLocationID,
				key->formID,
				key->instanceID,
				a_request.questTargetText.size());
			return true;
		} catch (const std::exception& error) {
			try {
				logger::error("Galaxy/System quest activation failed: {}", error.what());
			} catch (...) {
			}
		} catch (...) {
			try {
				logger::error("Galaxy/System quest activation failed unexpectedly");
			} catch (...) {
			}
		}
		return false;
	}
}
