#include "Hooks.h"
#include "GalaxyMap.h"
#include "StarMapInput.h"
#include "SurfaceMap.h"

namespace TrackQuestSurface::Hooks
{
	namespace
	{
		// Plugin-specific direct CALL sites reviewed against Starfield.exe
		// 1.16.244. Reusable function identities and layouts live in QTR CommonLib.
		constexpr std::ptrdiff_t kSurfaceGatherCallOffset = 0x77;
		constexpr std::ptrdiff_t kSurfaceComposeCallOffset = 0x237;
		constexpr std::ptrdiff_t kStarMapInputCallOffset = 0x10C;
		constexpr std::ptrdiff_t kQuestTreeComposeCallOffset = 0x118;
		constexpr std::ptrdiff_t kQuestTreeInsertCallOffsetA = 0x38D;
		constexpr std::ptrdiff_t kQuestTreeInsertCallOffsetB = 0x580;

		constexpr std::array<std::size_t, 7> kQuestTreeCallRvas{
			0x16A0FE8,
			0x16AE219,
			0x16B78AF,
			0x16B9420,
			0x16BA07C,
			0x16BB1DA,
			0x16BE91E
		};

		struct CallPatch
		{
			std::uintptr_t                                    address{};
			std::uintptr_t                                    branch{};
			std::array<std::uint8_t, sizeof(REL::ASM::CALL5)> original{};
		};

		[[nodiscard]] bool IsRel32Reachable(
			const std::uintptr_t a_callsite,
			const std::uintptr_t a_target) noexcept
		{
			constexpr auto callSize = sizeof(REL::ASM::CALL5);
			if (a_callsite > std::numeric_limits<std::uintptr_t>::max() - callSize) {
				return false;
			}

			const auto nextInstruction = a_callsite + callSize;
			if (a_target >= nextInstruction) {
				return a_target - nextInstruction <=
				       static_cast<std::uintptr_t>(std::numeric_limits<std::int32_t>::max());
			}

			constexpr auto negativeLimit =
				static_cast<std::uintptr_t>(std::numeric_limits<std::int32_t>::max()) + 1;
			return nextInstruction - a_target <= negativeLimit;
		}

		template <std::size_t N>
		[[nodiscard]] bool Matches(
			const std::uintptr_t               a_address,
			const std::array<std::uint8_t, N>& a_bytes) noexcept
		{
			return std::memcmp(
					   reinterpret_cast<const void*>(a_address),
					   a_bytes.data(),
					   a_bytes.size()) == 0;
		}

		template <std::size_t N>
		[[nodiscard]] bool Restore(
			const std::uintptr_t               a_address,
			const std::array<std::uint8_t, N>& a_original) noexcept
		{
			return REL::WriteSafe(a_address, a_original.data(), a_original.size()) &&
			       Matches(a_address, a_original);
		}

		[[nodiscard]] bool ValidateCallTarget(
			const std::uintptr_t   a_callsite,
			const std::uintptr_t   a_expected,
			const std::string_view a_name)
		{
			const auto actual = REL::ASM::CALL5::TARGET(a_callsite);
			if (actual == a_expected) {
				return true;
			}
			logger::error(
				"{} target mismatch: expected 0x{:X}, found 0x{:X}",
				a_name,
				a_expected,
				actual);
			return false;
		}
	}

	bool Install() noexcept
	{
		try {
			const auto surfaceGatherCallsite =
				RE::ID::StarMap::SurfaceMapState::RebuildSurfaceMarkers.address() +
				kSurfaceGatherCallOffset;
			const auto surfaceComposeCallsite =
				RE::ID::StarMap::SurfaceMapState::GatherSurfaceQuestTargets.address() +
				kSurfaceComposeCallOffset;
			const auto inputCallsite =
				RE::ID::StarMap::StarMapMenu::OnButtonEvent.address() + kStarMapInputCallOffset;

			const auto questTreeBuildTarget = RE::ID::StarMap::BuildQuestTargetTree.address();
			const auto questTreeComposeTarget = RE::ID::StarMap::ComposeQuestTargetMarker.address();
			const auto questTreeInsertTarget = RE::ID::StarMap::InsertQuestTargetMarker.address();
			const auto questTreeComposeCallsite =
				questTreeBuildTarget + kQuestTreeComposeCallOffset;
			const auto questTreeInsertCallsiteA =
				questTreeComposeTarget + kQuestTreeInsertCallOffsetA;
			const auto questTreeInsertCallsiteB =
				questTreeComposeTarget + kQuestTreeInsertCallOffsetB;

			std::array<std::uintptr_t, kQuestTreeCallRvas.size()> questTreeCallsites{};
			for (std::size_t index = 0; index < kQuestTreeCallRvas.size(); ++index) {
				questTreeCallsites[index] = REL::Offset{ kQuestTreeCallRvas[index] }.address();
			}

			const auto surfaceGatherTarget =
				RE::ID::StarMap::SurfaceMapState::GatherSurfaceQuestTargets.address();
			const auto surfaceComposeTarget =
				RE::ID::StarMap::ComposeSurfaceQuestTarget.address();
			const auto inputTarget = RE::ID::IMenu::OnButtonEvent.address();

			if (!REL::Pattern<
					"48 8B CF E8 E4 27 00 00 48 83 BF E8 08 00 00 00">()
					.match(surfaceGatherCallsite - 3) ||
				!REL::Pattern<"48 8B 13 48 8D 4D C7 E8 D4 00 00 00">()
					.match(surfaceComposeCallsite - 7)) {
				logger::error("Surface Map ownership-hook signature mismatch");
				return false;
			}
			if (!REL::Pattern<
					"77 0F 84 C0 75 0B 48 8B D3 49 8B CF E8 6F 8F E9 00 40 84 ED">()
					.match(inputCallsite - 12) ||
				!REL::Pattern<
					"48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57">()
					.match(inputTarget)) {
				logger::error("Star Map input-hook signature mismatch");
				return false;
			}

			for (std::size_t index = 0; index + 1 < questTreeCallsites.size(); ++index) {
				if (!REL::Pattern<
						"48 8D 54 24 ?? 48 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ??">()
						.match(questTreeCallsites[index] - 12)) {
					logger::error(
						"Star Map quest-tree caller {} signature mismatch at 0x{:X}",
						index,
						questTreeCallsites[index]);
					return false;
				}
			}
			if (!REL::Pattern<
					"48 8D 55 B0 48 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ??">()
					.match(questTreeCallsites.back() - 11) ||
				!REL::Pattern<
					"48 8B 13 48 8D 4C 24 20 E8 83 A1 FF FF 85 C0">()
					.match(questTreeComposeCallsite - 8) ||
				!REL::Pattern<
					"4C 8D 45 A0 48 8D 55 00 48 8D 4B 28 E8 3E 58 00 00 90">()
					.match(questTreeInsertCallsiteA - 12) ||
				!REL::Pattern<
					"4C 8D 45 B8 48 8D 55 20 48 8D 4B 28 E8 4B 56 00 00 90">()
					.match(questTreeInsertCallsiteB - 12) ||
				!REL::Pattern<
					"48 89 54 24 10 48 89 4C 24 08 53 55 56 57 41 54">()
					.match(questTreeBuildTarget)) {
				logger::error("Star Map quest-tree ownership-hook signature mismatch");
				return false;
			}

			for (std::size_t index = 0; index < questTreeCallsites.size(); ++index) {
				if (!ValidateCallTarget(
						questTreeCallsites[index],
						questTreeBuildTarget,
						"Star Map quest-tree caller")) {
					return false;
				}
			}
			if (!ValidateCallTarget(
					questTreeComposeCallsite,
					questTreeComposeTarget,
					"Star Map quest-tree compose") ||
				!ValidateCallTarget(
					questTreeInsertCallsiteA,
					questTreeInsertTarget,
					"Star Map quest-tree insert A") ||
				!ValidateCallTarget(
					questTreeInsertCallsiteB,
					questTreeInsertTarget,
					"Star Map quest-tree insert B") ||
				!ValidateCallTarget(
					surfaceGatherCallsite,
					surfaceGatherTarget,
					"Surface Map gather") ||
				!ValidateCallTarget(
					surfaceComposeCallsite,
					surfaceComposeTarget,
					"Surface Map compose") ||
				!ValidateCallTarget(inputCallsite, inputTarget, "Star Map input")) {
				return false;
			}

			const bool surfaceRefreshValidated =
				REL::Pattern<
					"40 53 48 83 EC 20 48 8D 99 F0 11 00 00 C6 44 24 30 03">()
					.match(RE::ID::StarMap::StarMapMenu::GetSurfaceMapState.address()) &&
				REL::Pattern<
					"48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 40">()
					.match(RE::ID::StarMap::SurfaceMapState::Refresh.address());
			if (!surfaceRefreshValidated) {
				logger::warn(
					"Surface Map repaint signatures do not match; tracking stays enabled but forced repaint is disabled");
			}
			const bool starMapRefreshValidated =
				REL::Pattern<
					"48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 30">()
					.match(RE::ID::StarMap::StarMapMenu::RefreshQuestTargets.address());
			if (!starMapRefreshValidated) {
				logger::warn(
					"Galaxy/System repaint signature does not match; tracking stays enabled but forced repaint is disabled");
			}

			GalaxyMap::SetOriginalFunctions(
				reinterpret_cast<GalaxyMap::BuildQuestTargetTree>(questTreeBuildTarget),
				reinterpret_cast<GalaxyMap::ComposeQuestTargetMarker>(questTreeComposeTarget),
				reinterpret_cast<GalaxyMap::InsertQuestTargetMarker>(questTreeInsertTarget),
				starMapRefreshValidated);
			SurfaceMap::SetOriginalFunctions(
				reinterpret_cast<SurfaceMap::BuildSurfaceMarkers>(surfaceGatherTarget),
				reinterpret_cast<SurfaceMap::ComposeQuestTarget>(surfaceComposeTarget),
				surfaceRefreshValidated);
			StarMapInput::SetOriginalDispatcher(
				reinterpret_cast<StarMapInput::DispatchButtonEvent>(inputTarget));

			auto&                 trampoline = REL::GetTrampoline();
			constexpr std::size_t requiredTrampolineBytes = 84;
			if (trampoline.free_size() < requiredTrampolineBytes) {
				logger::error(
					"Track Quest from Map hooks require {} trampoline bytes; {} remain",
					requiredTrampolineBytes,
					trampoline.free_size());
				return false;
			}

			// Allocate every branch island before touching executable callsites.
			const auto questTreeInsertBranch = trampoline.allocate_branch5(
				reinterpret_cast<std::uintptr_t>(GalaxyMap::CaptureInsertedQuestTargetMarker));
			const auto questTreeComposeBranch = trampoline.allocate_branch5(
				reinterpret_cast<std::uintptr_t>(GalaxyMap::CaptureAndComposeQuestTargetMarker));
			const auto questTreeBuildBranch = trampoline.allocate_branch5(
				reinterpret_cast<std::uintptr_t>(GalaxyMap::BuildAndPublishQuestTargetTree));
			const auto surfaceComposeBranch = trampoline.allocate_branch5(
				reinterpret_cast<std::uintptr_t>(SurfaceMap::CaptureAndComposeQuestTarget));
			const auto surfaceGatherBranch = trampoline.allocate_branch5(
				reinterpret_cast<std::uintptr_t>(SurfaceMap::BuildAndSnapshot));
			const auto inputBranch = trampoline.allocate_branch5(
				reinterpret_cast<std::uintptr_t>(StarMapInput::OnStarMapButton));

			std::array<CallPatch, 13> patches{};
			std::size_t               patchIndex{};
			patches[patchIndex++] = { questTreeInsertCallsiteA, questTreeInsertBranch };
			patches[patchIndex++] = { questTreeInsertCallsiteB, questTreeInsertBranch };
			patches[patchIndex++] = { questTreeComposeCallsite, questTreeComposeBranch };
			for (const auto callsite : questTreeCallsites) {
				patches[patchIndex++] = { callsite, questTreeBuildBranch };
			}
			patches[patchIndex++] = { surfaceComposeCallsite, surfaceComposeBranch };
			patches[patchIndex++] = { surfaceGatherCallsite, surfaceGatherBranch };
			patches[patchIndex++] = { inputCallsite, inputBranch };
			if (patchIndex != patches.size()) {
				std::terminate();
			}

			for (auto& patch : patches) {
				if (!IsRel32Reachable(patch.address, patch.branch)) {
					logger::error("Allocated hook branch is outside signed rel32 reach at 0x{:X}", patch.address);
					return false;
				}
				std::memcpy(
					patch.original.data(),
					reinterpret_cast<const void*>(patch.address),
					patch.original.size());
			}

			try {
				// Fail-safe exposure order: inert insertion wrappers first; their owning
				// compose/build wrappers next; Surface ownership next; input last.
				for (const auto& patch : patches) {
					const REL::ASM::CALL5 call{ patch.address, patch.branch };
					const bool            written =
						REL::WriteSafeData(patch.address, call) &&
						std::memcmp(
							reinterpret_cast<const void*>(patch.address),
							std::addressof(call),
							sizeof(call)) == 0 &&
						REL::ASM::CALL5::TARGET(patch.address) == patch.branch;
					if (!written) {
						throw std::runtime_error("one or more hook writes failed verification");
					}
				}
			} catch (...) {
				bool restored = true;
				for (auto iterator = patches.rbegin(); iterator != patches.rend(); ++iterator) {
					restored = Restore(iterator->address, iterator->original) && restored;
				}
				if (!restored) {
					try {
						logger::critical(
							"Could not restore original Track Quest from Map callsites after installation failure");
					} catch (...) {
					}
					std::terminate();
				}
				logger::error("Hook transaction failed; all original calls restored");
				return false;
			}

			try {
				logger::info(
					"Installed transactional Surface/Galaxy/System hooks: callsites={}, input=0x{:X}",
					patches.size(),
					inputCallsite);
			} catch (...) {
			}
			return true;
		} catch (const std::exception& error) {
			try {
				logger::error("Could not install Track Quest from Map hooks: {}", error.what());
			} catch (...) {
			}
		} catch (...) {
			try {
				logger::error("Could not install Track Quest from Map hooks");
			} catch (...) {
			}
		}
		return false;
	}
}
