#include "PCH.h"

#include "Hooks.h"

#include "StarMapInput.h"
#include "SurfaceMap.h"

namespace TrackQuestSurface::Hooks
{
	namespace
	{
		// Reviewed against Starfield.exe 1.16.244.0 and its v5 Address Library.
		constexpr std::ptrdiff_t kQuestGatherCallOffset = 0x77;
		constexpr std::ptrdiff_t kQuestComposeCallOffset = 0x237;
		constexpr std::ptrdiff_t kStarMapInputCallOffset = 0x10C;

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
			const std::uintptr_t              a_address,
			const std::array<std::uint8_t, N>& a_bytes) noexcept
		{
			return std::memcmp(
				reinterpret_cast<const void*>(a_address),
				a_bytes.data(),
				a_bytes.size()) == 0;
		}

		template <std::size_t N>
		[[nodiscard]] bool Restore(
			const std::uintptr_t              a_address,
			const std::array<std::uint8_t, N>& a_original) noexcept
		{
			return REL::WriteSafe(a_address, a_original.data(), a_original.size()) &&
			       Matches(a_address, a_original);
		}
	}

	bool Install() noexcept
	{
		try {
			const auto gatherCallsite =
				RE::ID::StarMap::SurfaceMapState::RebuildSurfaceMarkers.address() +
				kQuestGatherCallOffset;
			const auto composeCallsite =
				RE::ID::StarMap::SurfaceMapState::GatherSurfaceQuestTargets.address() +
				kQuestComposeCallOffset;
			const auto inputCallsite =
				RE::ID::StarMap::StarMapMenu::OnButtonEvent.address() + kStarMapInputCallOffset;

			const auto expectedGatherTarget =
				RE::ID::StarMap::SurfaceMapState::GatherSurfaceQuestTargets.address();
			const auto expectedComposeTarget =
				RE::ID::StarMap::ComposeSurfaceQuestTarget.address();
			const auto expectedInputTarget = RE::ID::IMenu::OnButtonEvent.address();

			if (!REL::Pattern<
					"48 8B CF E8 E4 27 00 00 48 83 BF E8 08 00 00 00">()
					.match(gatherCallsite - 3)) {
				logger::error("SurfaceMap gather-hook signature mismatch at 0x{:X}", gatherCallsite);
				return false;
			}
			if (!REL::Pattern<"48 8B 13 48 8D 4D C7 E8 D4 00 00 00">()
					.match(composeCallsite - 7)) {
				logger::error("SurfaceMap compose-hook signature mismatch at 0x{:X}", composeCallsite);
				return false;
			}
			if (!REL::Pattern<
					"77 0F 84 C0 75 0B 48 8B D3 49 8B CF E8 6F 8F E9 00 40 84 ED">()
					.match(inputCallsite - 12)) {
				logger::error("Star Map input-hook signature mismatch at 0x{:X}", inputCallsite);
				return false;
			}
			if (!REL::Pattern<
					"48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57">()
					.match(expectedInputTarget)) {
				logger::error(
					"Star Map vanilla dispatcher signature mismatch at 0x{:X}",
					expectedInputTarget);
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
					"Surface Map repaint signatures do not match; quest tracking will remain enabled but visual refresh is disabled");
			}

			const auto actualGatherTarget = REL::ASM::CALL5::TARGET(gatherCallsite);
			if (actualGatherTarget != expectedGatherTarget) {
				logger::error(
					"SurfaceMap gather-hook target mismatch: expected 0x{:X}, found 0x{:X}",
					expectedGatherTarget,
					actualGatherTarget);
				return false;
			}
			const auto actualComposeTarget = REL::ASM::CALL5::TARGET(composeCallsite);
			if (actualComposeTarget != expectedComposeTarget) {
				logger::error(
					"SurfaceMap compose-hook target mismatch: expected 0x{:X}, found 0x{:X}",
					expectedComposeTarget,
					actualComposeTarget);
				return false;
			}
			const auto actualInputTarget = REL::ASM::CALL5::TARGET(inputCallsite);
			if (actualInputTarget != expectedInputTarget) {
				logger::error(
					"Star Map input-hook target mismatch: expected 0x{:X}, found 0x{:X}",
					expectedInputTarget,
					actualInputTarget);
				return false;
			}

			SurfaceMap::SetOriginalFunctions(
				reinterpret_cast<SurfaceMap::BuildSurfaceMarkers>(expectedGatherTarget),
				reinterpret_cast<SurfaceMap::ComposeQuestTarget>(expectedComposeTarget),
				surfaceRefreshValidated);
			StarMapInput::SetOriginalDispatcher(
				reinterpret_cast<StarMapInput::DispatchButtonEvent>(expectedInputTarget));

			auto& trampoline = REL::GetTrampoline();
			constexpr std::size_t requiredTrampolineBytes = 42;
			if (trampoline.free_size() < requiredTrampolineBytes) {
				logger::error(
					"SurfaceMap hooks require {} trampoline bytes; {} remain",
					requiredTrampolineBytes,
					trampoline.free_size());
				return false;
			}

			// Allocate every branch island before touching executable callsites.
			const auto composeBranch = trampoline.allocate_branch5(
				reinterpret_cast<std::uintptr_t>(SurfaceMap::CaptureAndComposeQuestTarget));
			const auto gatherBranch = trampoline.allocate_branch5(
				reinterpret_cast<std::uintptr_t>(SurfaceMap::BuildAndSnapshot));
			const auto inputBranch = trampoline.allocate_branch5(
				reinterpret_cast<std::uintptr_t>(StarMapInput::OnStarMapButton));

			if (!IsRel32Reachable(composeCallsite, composeBranch) ||
				!IsRel32Reachable(gatherCallsite, gatherBranch) ||
				!IsRel32Reachable(inputCallsite, inputBranch)) {
				logger::error("One or more allocated hook branches are outside signed rel32 reach");
				return false;
			}

			const REL::ASM::CALL5 composePatch{ composeCallsite, composeBranch };
			const REL::ASM::CALL5 gatherPatch{ gatherCallsite, gatherBranch };
			const REL::ASM::CALL5 inputPatch{ inputCallsite, inputBranch };

			std::array<std::uint8_t, sizeof(REL::ASM::CALL5)> originalComposeCall{};
			std::array<std::uint8_t, sizeof(REL::ASM::CALL5)> originalGatherCall{};
			std::array<std::uint8_t, sizeof(REL::ASM::CALL5)> originalInputCall{};
			std::memcpy(
				originalComposeCall.data(),
				reinterpret_cast<const void*>(composeCallsite),
				originalComposeCall.size());
			std::memcpy(
				originalGatherCall.data(),
				reinterpret_cast<const void*>(gatherCallsite),
				originalGatherCall.size());
			std::memcpy(
				originalInputCall.data(),
				reinterpret_cast<const void*>(inputCallsite),
				originalInputCall.size());

			try {
				const bool composeWritten =
					REL::WriteSafeData(composeCallsite, composePatch) &&
					std::memcmp(
						reinterpret_cast<const void*>(composeCallsite),
						std::addressof(composePatch),
						sizeof(composePatch)) == 0 &&
					REL::ASM::CALL5::TARGET(composeCallsite) == composeBranch;
				const bool gatherWritten =
					composeWritten &&
					REL::WriteSafeData(gatherCallsite, gatherPatch) &&
					std::memcmp(
						reinterpret_cast<const void*>(gatherCallsite),
						std::addressof(gatherPatch),
						sizeof(gatherPatch)) == 0 &&
					REL::ASM::CALL5::TARGET(gatherCallsite) == gatherBranch;
				const bool inputWritten =
					gatherWritten &&
					REL::WriteSafeData(inputCallsite, inputPatch) &&
					std::memcmp(
						reinterpret_cast<const void*>(inputCallsite),
						std::addressof(inputPatch),
						sizeof(inputPatch)) == 0 &&
					REL::ASM::CALL5::TARGET(inputCallsite) == inputBranch;
				if (!composeWritten || !gatherWritten || !inputWritten) {
					throw std::runtime_error("one or more hook writes failed verification");
				}
			} catch (...) {
				// Reverse the installation order and do not short-circuit: every original
				// CALL is restored and read back even if an earlier restoration fails.
				const bool inputRestored = Restore(inputCallsite, originalInputCall);
				const bool gatherRestored = Restore(gatherCallsite, originalGatherCall);
				const bool composeRestored = Restore(composeCallsite, originalComposeCall);
				if (!inputRestored || !gatherRestored || !composeRestored) {
					try {
						logger::critical(
							"Could not restore original SurfaceMap/input callsites after hook installation failure");
					} catch (...) {
					}
					std::terminate();
				}
				logger::error(
					"SurfaceMap/input hook transaction failed; all original calls restored");
				return false;
			}

			try {
				logger::info(
					"Installed transactional SurfaceMap hooks: gather=0x{:X}, compose=0x{:X}, input=0x{:X}",
					gatherCallsite,
					composeCallsite,
					inputCallsite);
			} catch (...) {
				// Logging cannot turn a successfully committed hook set into a reported
				// plugin-load failure.
			}
			return true;
		} catch (const std::exception& error) {
			try {
				logger::error("Could not install SurfaceMap/input hooks: {}", error.what());
			} catch (...) {
			}
		} catch (...) {
			try {
				logger::error("Could not install SurfaceMap/input hooks");
			} catch (...) {
			}
		}
		return false;
	}
}
