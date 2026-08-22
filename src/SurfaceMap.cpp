#include "SurfaceMap.h"
#include "QuestTracking.h"

namespace TrackQuestSurface::SurfaceMap
{
	namespace
	{
		constexpr std::ptrdiff_t kComposeQuestOffset = 0x10;

		constexpr std::size_t kMaximumMarkers = 4096;
		constexpr std::size_t kMaximumOwners = 128;
		constexpr std::size_t kMaximumQuestTargetTextBytes = 4096;
		constexpr std::size_t kQuestCaptureSlotCount = 4096;
		static_assert((kQuestCaptureSlotCount & (kQuestCaptureSlotCount - 1)) == 0);

		using QuestKey = RE::QuestInstanceKey;

		struct MarkerRecord
		{
			std::uint32_t                  markerHandleBits{};
			RE::StarMap::SurfaceMarkerType markerType{};
			bool                           isLocation{};
			bool                           hasQuestTarget{};
			bool                           questActive{};
			std::string                    nameText;
			std::string                    extraText;
			std::string                    questTargetText;
			std::size_t                    rawOwnerCount{};
			std::optional<QuestKey>        visibleOwner;
			std::vector<QuestKey>          owners;
		};

		struct CopiedMarkerRecord
		{
			std::uint32_t                  markerHandleBits{};
			RE::StarMap::SurfaceMarkerType markerType{};
			bool                           isLocation{};
			bool                           hasQuestTarget{};
			bool                           questActive{};
			std::string                    nameText;
			std::string                    extraText;
			std::string                    questTargetText;
			std::vector<RE::TESFormID>     ownerFormIDs;
		};

		enum class MarkerCopyFailure : std::uint8_t
		{
			kNone [[maybe_unused]],
			kInvalidMarkerRange,
			kImplausibleMarkerCount
		};

		struct CopiedMarkerGeneration
		{
			std::vector<CopiedMarkerRecord> rows;
			MarkerCopyFailure               failure{};
			std::size_t                     markerCount{};
			std::size_t                     rejectedRows{};
		};

		enum class CapturedQuestState : std::uint8_t
		{
			kEmpty,
			kUnique,
			kAmbiguous
		};

		struct CapturedQuestSlot
		{
			QuestKey           key;
			CapturedQuestState state{};
		};

		// The compose callback runs synchronously inside GatherSurfaceQuestTargets. Keep capture
		// fixed-capacity and primitive-only so it does not allocate, log, resolve
		// forms, or retain engine pointers while the gather routine is in progress.
		struct QuestPairCapture
		{
			std::array<CapturedQuestSlot, kQuestCaptureSlotCount> slots{};
			std::size_t                                           contributorCalls{};
			std::size_t                                           uniqueForms{};
			std::size_t                                           ambiguousForms{};
			bool                                                  overflow{};
			bool                                                  invalidInvocation{};

			void Reset() noexcept
			{
				slots.fill(CapturedQuestSlot{});
				contributorCalls = 0;
				uniqueForms = 0;
				ambiguousForms = 0;
				overflow = false;
				invalidInvocation = false;
			}

			void Record(const QuestKey& a_key) noexcept
			{
				++contributorCalls;
				if (a_key.formID == 0) {
					invalidInvocation = true;
					return;
				}

				constexpr std::uint32_t goldenRatio = 0x9E3779B1U;
				const auto              first =
					static_cast<std::size_t>(a_key.formID * goldenRatio) &
					(kQuestCaptureSlotCount - 1);
				for (std::size_t probe = 0; probe < kQuestCaptureSlotCount; ++probe) {
					auto& slot = slots[(first + probe) & (kQuestCaptureSlotCount - 1)];
					if (slot.state == CapturedQuestState::kEmpty) {
						slot.key = a_key;
						slot.state = CapturedQuestState::kUnique;
						++uniqueForms;
						return;
					}
					if (slot.key.formID != a_key.formID) {
						continue;
					}
					if (slot.state == CapturedQuestState::kUnique &&
						slot.key.instanceID != a_key.instanceID) {
						slot.state = CapturedQuestState::kAmbiguous;
						--uniqueForms;
						++ambiguousForms;
					}
					return;
				}
				overflow = true;
			}

			[[nodiscard]] std::optional<QuestKey> Resolve(
				const std::uint32_t a_formID) const noexcept
			{
				if (a_formID == 0) {
					return std::nullopt;
				}

				constexpr std::uint32_t goldenRatio = 0x9E3779B1U;
				const auto              first =
					static_cast<std::size_t>(a_formID * goldenRatio) &
					(kQuestCaptureSlotCount - 1);
				for (std::size_t probe = 0; probe < kQuestCaptureSlotCount; ++probe) {
					const auto& slot = slots[(first + probe) & (kQuestCaptureSlotCount - 1)];
					if (slot.state == CapturedQuestState::kEmpty) {
						return std::nullopt;
					}
					if (slot.key.formID == a_formID) {
						return slot.state == CapturedQuestState::kUnique ?
						           std::optional<QuestKey>{ slot.key } :
						           std::nullopt;
					}
				}
				return std::nullopt;
			}
		};

		// Large marker variants can publish several native rows with the same
		// sentinel handle. Preserve every row; a handle alone is not a unique key.
		using MarkerCache = std::vector<MarkerRecord>;

		std::mutex                     cacheMutex;
		MarkerCache                    markerCache;
		BuildSurfaceMarkers            originalBuildSurfaceMarkers{};
		ComposeQuestTarget             originalComposeQuestTarget{};
		thread_local QuestPairCapture  threadQuestCapture;
		thread_local QuestPairCapture* activeQuestCapture{};
		thread_local std::size_t       surfaceGatherDepth{};
		bool                           surfaceRefreshValidated{};

		class ScopedQuestCapture
		{
		public:
			explicit ScopedQuestCapture(QuestPairCapture* a_capture) noexcept :
				previous_(std::exchange(activeQuestCapture, a_capture))
			{}

			explicit ScopedQuestCapture(QuestPairCapture& a_capture) noexcept :
				ScopedQuestCapture(std::addressof(a_capture))
			{}

			~ScopedQuestCapture() { activeQuestCapture = previous_; }

			ScopedQuestCapture(const ScopedQuestCapture&) = delete;
			ScopedQuestCapture& operator=(const ScopedQuestCapture&) = delete;

		private:
			QuestPairCapture* previous_{};
		};

		class ScopedGatherDepth
		{
		public:
			explicit ScopedGatherDepth(std::size_t& a_depth) noexcept :
				depth_(a_depth)
			{
				++depth_;
			}

			~ScopedGatherDepth() { --depth_; }

			ScopedGatherDepth(const ScopedGatherDepth&) = delete;
			ScopedGatherDepth& operator=(const ScopedGatherDepth&) = delete;

		private:
			std::size_t& depth_;
		};

		[[nodiscard]] std::optional<std::string> CopyNativeText(
			const RE::BSFixedString& a_text)
		{
			const auto length = static_cast<std::size_t>(a_text.length());
			if (length > kMaximumQuestTargetTextBytes) {
				return std::nullopt;
			}
			return std::string{ a_text.c_str(), length };
		}

		[[nodiscard]] CopiedMarkerGeneration CopyMarkerGeneration(
			const RE::StarMap::SurfaceMapState* a_surfaceState)
		{
			CopiedMarkerGeneration copied;
			if (!a_surfaceState) {
				return copied;
			}

			const auto& markers = a_surfaceState->surfaceMarkers;
			const auto  markerBegin = reinterpret_cast<std::uintptr_t>(markers.begin());
			const auto  markerEnd = reinterpret_cast<std::uintptr_t>(markers.end());
			const auto  markerCapacity =
				reinterpret_cast<std::uintptr_t>(markers.capacity_end());
			if (!markerBegin || markerEnd < markerBegin || markerCapacity < markerEnd ||
				(markerEnd - markerBegin) % sizeof(RE::StarMap::SurfaceMarkerStaticData) != 0 ||
				(markerCapacity - markerBegin) % sizeof(RE::StarMap::SurfaceMarkerStaticData) != 0) {
				copied.failure = MarkerCopyFailure::kInvalidMarkerRange;
				return copied;
			}

			copied.markerCount = static_cast<std::size_t>(
				(markerEnd - markerBegin) / sizeof(RE::StarMap::SurfaceMarkerStaticData));
			if (copied.markerCount > kMaximumMarkers) {
				copied.failure = MarkerCopyFailure::kImplausibleMarkerCount;
				return copied;
			}
			copied.rows.reserve(copied.markerCount);

			for (std::size_t index = 0; index < copied.markerCount; ++index) {
				const auto& marker = markers.begin()[index];
				const auto  ownerBegin =
					reinterpret_cast<std::uintptr_t>(marker.questOwners.begin());
				const auto ownerEnd =
					reinterpret_cast<std::uintptr_t>(marker.questOwners.end());
				const auto ownerCapacity =
					reinterpret_cast<std::uintptr_t>(marker.questOwners.capacity_end());
				if (ownerBegin == ownerEnd) {
					continue;
				}

				if (marker.isLocation > 1 || marker.hasQuestTarget > 1 || marker.questActive > 1) {
					++copied.rejectedRows;
					continue;
				}

				const auto nameText = CopyNativeText(marker.nameText);
				const auto extraText = CopyNativeText(marker.extraText);
				const auto questTargetText = CopyNativeText(marker.questTargetText);
				if (!nameText || !extraText || !questTargetText) {
					++copied.rejectedRows;
					continue;
				}

				if (!ownerBegin || ownerEnd < ownerBegin || ownerCapacity < ownerEnd ||
					(ownerEnd - ownerBegin) % sizeof(RE::TESFormID) != 0 ||
					(ownerCapacity - ownerBegin) % sizeof(RE::TESFormID) != 0) {
					++copied.rejectedRows;
					continue;
				}

				const auto ownerCount = static_cast<std::size_t>(
					(ownerEnd - ownerBegin) / sizeof(RE::TESFormID));
				if (ownerCount == 0 || ownerCount > kMaximumOwners) {
					++copied.rejectedRows;
					continue;
				}

				CopiedMarkerRecord incoming{
					.markerHandleBits = marker.markerHandleBits,
					.markerType = marker.markerType,
					.isLocation = marker.IsLocation(),
					.hasQuestTarget = marker.HasQuestTarget(),
					.questActive = marker.IsQuestActive(),
					.nameText = *nameText,
					.extraText = *extraText,
					.questTargetText = *questTargetText
				};
				incoming.ownerFormIDs.reserve(ownerCount);
				for (std::size_t ownerIndex = 0; ownerIndex < ownerCount; ++ownerIndex) {
					incoming.ownerFormIDs.push_back(marker.questOwners.begin()[ownerIndex]);
				}
				copied.rows.push_back(std::move(incoming));
			}

			return copied;
		}

		void PublishCache(MarkerCache a_next)
		{
			std::scoped_lock lock(cacheMutex);
			markerCache = std::move(a_next);
		}

		void RebuildCurrentSurfaceMap() noexcept
		{
			if (!surfaceRefreshValidated) {
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
					logger::warn("Skipped Surface Map repaint: unexpected StarMapMenu vtable");
					return;
				}

				auto* starMapMenu = static_cast<RE::StarMap::StarMapMenu*>(menu.get());
				auto* surfaceState = starMapMenu->GetSurfaceMapState();
				if (!surfaceState) {
					return;
				}

				std::uintptr_t stateVtable{};
				std::memcpy(std::addressof(stateVtable), surfaceState, sizeof(stateVtable));
				if (stateVtable != RE::StarMap::SurfaceMapState::PRIMARY_VTABLE.address()) {
					logger::warn("Skipped Surface Map repaint: unexpected SurfaceMapState vtable");
					return;
				}

				surfaceState->Refresh();
				logger::info("Rebuilt the open Surface Map after quest activation");
			} catch (const std::exception& error) {
				try {
					logger::warn("Surface Map repaint failed safely: {}", error.what());
				} catch (...) {
				}
			} catch (...) {
				try {
					logger::warn("Surface Map repaint failed safely");
				} catch (...) {
				}
			}
		}

		void SnapshotMarkerOwners(
			RE::StarMap::SurfaceMapState* a_surfaceState,
			const QuestPairCapture&       a_capture)
		{
			// Phase A is the only phase that touches the owner-thread-only native
			// vectors. It copies all text and FormIDs before this function resolves
			// forms, logs, or publishes anything.
			const auto copied = CopyMarkerGeneration(a_surfaceState);

			if (copied.failure == MarkerCopyFailure::kInvalidMarkerRange) {
				logger::warn("Rejected invalid SurfaceMap marker range");
				PublishCache({});
				return;
			}
			if (copied.failure == MarkerCopyFailure::kImplausibleMarkerCount) {
				logger::warn("Rejected implausible SurfaceMap marker count {}", copied.markerCount);
				PublishCache({});
				return;
			}
			if (copied.rejectedRows != 0) {
				logger::warn(
					"Rejected entire SurfaceMap ownership generation: {} invalid nonempty-owner row(s)",
					copied.rejectedRows);
				PublishCache({});
				return;
			}

			MarkerCache next;
			next.reserve(copied.rows.size());
			std::size_t rejectedRows = 0;
			for (const auto& copiedMarker : copied.rows) {
				MarkerRecord incoming{
					.markerHandleBits = copiedMarker.markerHandleBits,
					.markerType = copiedMarker.markerType,
					.isLocation = copiedMarker.isLocation,
					.hasQuestTarget = copiedMarker.hasQuestTarget,
					.questActive = copiedMarker.questActive,
					.nameText = copiedMarker.nameText,
					.extraText = copiedMarker.extraText,
					.questTargetText = copiedMarker.questTargetText,
					.rawOwnerCount = copiedMarker.ownerFormIDs.size()
				};

				bool          valid = true;
				RE::TESFormID visibleFormID{};
				incoming.owners.reserve(copiedMarker.ownerFormIDs.size());
				for (const auto formID : copiedMarker.ownerFormIDs) {
					// For an eligible inactive row, ComposeSurfaceQuestTarget overwrites questTargetText
					// before each append, so the raw final owner produced the visible text.
					// Preserve order across deduplication: Q1,Q2,Q1 represents Q1.
					visibleFormID = formID;
					const auto key = a_capture.Resolve(formID);
					if (!key || !QuestTracking::ResolveQuest(*key)) {
						valid = false;
						break;
					}
					if (std::ranges::find(incoming.owners, *key) == incoming.owners.end()) {
						incoming.owners.push_back(*key);
					}
				}

				const auto visibleOwner = a_capture.Resolve(visibleFormID);
				if (!valid || incoming.owners.empty() || !visibleOwner ||
					std::ranges::find(incoming.owners, *visibleOwner) == incoming.owners.end()) {
					++rejectedRows;
					continue;
				}
				incoming.visibleOwner = *visibleOwner;
				next.push_back(std::move(incoming));
			}

			// Publishing a partial generation could let an unreadable row share an
			// exact UI tuple with a retained row and activate the wrong quest.
			if (rejectedRows != 0) {
				logger::warn(
					"Rejected entire SurfaceMap ownership generation: {} invalid nonempty-owner row(s)",
					rejectedRows);
				PublishCache({});
				return;
			}

			std::size_t questTargetLocations = 0;
			std::size_t inactiveQuestTargetLocations = 0;
			std::size_t eligibleQuestTargetLocations = 0;
			std::size_t largeNameplateQuestMarkers = 0;
			std::size_t eligibleLargeNameplateQuestMarkers = 0;
			std::size_t multiOwner = 0;
			for (const auto& marker : next) {
				if (marker.isLocation && marker.hasQuestTarget) {
					++questTargetLocations;
					if (!marker.questActive) {
						++inactiveQuestTargetLocations;
						if (marker.visibleOwner) {
							++eligibleQuestTargetLocations;
						}
					}
				}
				if (marker.markerType == RE::StarMap::SurfaceMarkerType::kQuest &&
					!marker.isLocation && !marker.hasQuestTarget) {
					++largeNameplateQuestMarkers;
					if (!marker.questActive && marker.rawOwnerCount == 1 && marker.visibleOwner) {
						++eligibleLargeNameplateQuestMarkers;
					}
				}
				if (marker.owners.size() != 1) {
					++multiOwner;
				}
			}

			const auto publishedSize = next.size();
			PublishCache(std::move(next));
			try {
				logger::info(
					"Published SurfaceMap ownership generation: markers={}, questTargetLocations={}, inactiveQuestTargetLocations={}, eligibleQuestTargetLocations={}, largeNameplates={}, eligibleLargeNameplates={}, multiOwner={}, rejected={}, capturedCalls={}, capturedUniqueForms={}, capturedAmbiguousForms={}",
					publishedSize,
					questTargetLocations,
					inactiveQuestTargetLocations,
					eligibleQuestTargetLocations,
					largeNameplateQuestMarkers,
					eligibleLargeNameplateQuestMarkers,
					multiOwner,
					rejectedRows,
					a_capture.contributorCalls,
					a_capture.uniqueForms,
					a_capture.ambiguousForms);
			} catch (...) {
			}
		}

		[[nodiscard]] bool CaptureAndComposeQuestTargetImpl(
			void* a_context,
			void* a_target) noexcept
		{
			if (auto* capture = activeQuestCapture) {
				RE::TESQuest* quest{};
				if (a_context) {
					std::memcpy(
						std::addressof(quest),
						static_cast<const std::byte*>(a_context) + kComposeQuestOffset,
						sizeof(quest));
				}
				if (quest) {
					capture->Record(quest->GetInstanceKey());
				} else {
					capture->invalidInvocation = true;
				}
			}
			return originalComposeQuestTarget(a_context, a_target);
		}

		void BuildAndSnapshotImpl(RE::StarMap::SurfaceMapState* a_surfaceState) noexcept
		{
			ScopedGatherDepth gatherDepth{ surfaceGatherDepth };

			// This state is owner-thread-only and non-reentrant. Suppress every nested
			// capture and only let the true outermost call reset or publish a generation.
			if (surfaceGatherDepth > 1) {
				if (auto* outerCapture = activeQuestCapture) {
					outerCapture->invalidInvocation = true;
				}
				ScopedQuestCapture suppressCapture{ nullptr };
				originalBuildSurfaceMarkers(a_surfaceState);
				return;
			}

			// A reused handle must never resolve against the preceding generation while
			// native entries are being repopulated.
			ResetCache();
			threadQuestCapture.Reset();
			{
				ScopedQuestCapture captureScope{ threadQuestCapture };
				originalBuildSurfaceMarkers(a_surfaceState);
			}

			if (threadQuestCapture.overflow || threadQuestCapture.invalidInvocation) {
				try {
					logger::error(
						"Rejected SurfaceMap ownership generation: captureOverflow={}, invalidComposeInvocation={}",
						threadQuestCapture.overflow,
						threadQuestCapture.invalidInvocation);
				} catch (...) {
				}
				return;
			}

			try {
				SnapshotMarkerOwners(a_surfaceState, threadQuestCapture);
			} catch (const std::exception& error) {
				try {
					logger::error("SurfaceMap ownership snapshot failed: {}", error.what());
				} catch (...) {
				}
				ResetCache();
			} catch (...) {
				try {
					logger::error("SurfaceMap ownership snapshot failed unexpectedly");
				} catch (...) {
				}
				ResetCache();
			}
		}

		[[nodiscard]] std::optional<QuestKey> ResolveRequest(const Request& a_request)
		{
			std::scoped_lock lock(cacheMutex);
			if (a_request.markerType > std::numeric_limits<std::uint16_t>::max()) {
				logger::warn("Rejected out-of-range SurfaceMap marker type {}", a_request.markerType);
				return std::nullopt;
			}

			const auto requestedType =
				static_cast<RE::StarMap::SurfaceMarkerType>(a_request.markerType);
			std::vector<QuestKey> uniqueOwners;
			std::size_t           matchingRows = 0;
			bool                  invalidMatchingRow = false;
			for (const auto& marker : markerCache) {
				if (marker.markerHandleBits != a_request.markerHandleBits ||
					marker.markerType != requestedType ||
					marker.isLocation != a_request.isLocation) {
					continue;
				}

				bool exactMatch;
				switch (a_request.variant) {
				case MarkerVariant::kQuestTarget:
					exactMatch = marker.hasQuestTarget && !marker.questActive &&
					             marker.questTargetText == a_request.questTargetText;
					break;
				case MarkerVariant::kLargeNameplate:
					exactMatch =
						a_request.markerType ==
							static_cast<std::uint16_t>(RE::StarMap::SurfaceMarkerType::kQuest) &&
						!a_request.isLocation &&
						marker.markerType == RE::StarMap::SurfaceMarkerType::kQuest &&
						!marker.isLocation && !marker.hasQuestTarget && !marker.questActive &&
						marker.nameText == a_request.nameText &&
						marker.extraText == a_request.extraText;
					break;
				default:
					logger::warn("Rejected SurfaceMap marker request with unknown variant");
					return std::nullopt;
				}
				if (!exactMatch) {
					continue;
				}

				++matchingRows;
				if (!marker.visibleOwner ||
					(a_request.variant == MarkerVariant::kLargeNameplate && marker.rawOwnerCount != 1)) {
					invalidMatchingRow = true;
					continue;
				}
				if (std::ranges::find(uniqueOwners, *marker.visibleOwner) == uniqueOwners.end()) {
					uniqueOwners.push_back(*marker.visibleOwner);
				}
			}

			if (matchingRows == 0) {
				logger::warn(
					"Rejected unknown {} SurfaceMap marker tuple: handle=0x{:08X}, type={}, location={}, nameBytes={}, extraBytes={}, questTextBytes={}",
					a_request.variant == MarkerVariant::kLargeNameplate ?
						"large-nameplate" :
						"quest-target",
					a_request.markerHandleBits,
					a_request.markerType,
					a_request.isLocation,
					a_request.nameText.size(),
					a_request.extraText.size(),
					a_request.questTargetText.size());
				return std::nullopt;
			}

			if (invalidMatchingRow || uniqueOwners.size() != 1) {
				logger::warn(
					"Rejected ambiguous {} SurfaceMap marker tuple: handle=0x{:08X}, matchingRows={}, uniqueOwners={}, invalidMatchingRow={}",
					a_request.variant == MarkerVariant::kLargeNameplate ?
						"large-nameplate" :
						"quest-target",
					a_request.markerHandleBits,
					matchingRows,
					uniqueOwners.size(),
					invalidMatchingRow);
				return std::nullopt;
			}

			const auto owner = uniqueOwners.front();
			logger::info(
				"Resolved {} SurfaceMap marker 0x{:08X}: visible quest 0x{:08X}, instance={}, matchingRows={}",
				a_request.variant == MarkerVariant::kLargeNameplate ?
					"large-nameplate" :
					"quest-target",
				a_request.markerHandleBits,
				owner.formID,
				owner.instanceID,
				matchingRows);
			return owner;
		}

	}

	void SetOriginalFunctions(
		const BuildSurfaceMarkers a_buildSurfaceMarkers,
		const ComposeQuestTarget  a_composeQuestTarget,
		const bool                a_surfaceRefreshValidated) noexcept
	{
		originalBuildSurfaceMarkers = a_buildSurfaceMarkers;
		originalComposeQuestTarget = a_composeQuestTarget;
		surfaceRefreshValidated = a_surfaceRefreshValidated;
	}

	bool CaptureAndComposeQuestTarget(void* a_context, void* a_target) noexcept
	{
		return CaptureAndComposeQuestTargetImpl(a_context, a_target);
	}

	void BuildAndSnapshot(RE::StarMap::SurfaceMapState* a_surfaceState) noexcept
	{
		BuildAndSnapshotImpl(a_surfaceState);
	}

	void ResetCache() noexcept
	{
		try {
			PublishCache({});
		} catch (...) {
			// Never unwind into Scaleform or an engine hook.
		}
	}

	bool TryActivate(const Request& a_request) noexcept
	{
		try {
			const auto owner = ResolveRequest(a_request);
			if (!owner) {
				return false;
			}

			// ResolveRequest releases cacheMutex before the shared live-state check.
			// The queued task repeats it because the vanilla helper toggles tracking.
			if (!QuestTracking::QueueTrack(
					*owner,
					QuestTracking::Source::kSurface,
					RebuildCurrentSurfaceMap)) {
				return false;
			}
			try {
				logger::info(
					"Accepted {} SurfaceMap marker 0x{:08X}; queued quest 0x{:08X}, instance={}",
					a_request.variant == MarkerVariant::kLargeNameplate ?
						"large-nameplate" :
						"quest-target",
					a_request.markerHandleBits,
					owner->formID,
					owner->instanceID);
			} catch (...) {
			}
			return true;
		} catch (const std::exception& error) {
			try {
				logger::error("Track Quest request failed: {}", error.what());
			} catch (...) {
			}
		} catch (...) {
			try {
				logger::error("Track Quest request failed unexpectedly");
			} catch (...) {
			}
		}
		return false;
	}
}
