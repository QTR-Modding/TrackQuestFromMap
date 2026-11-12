#include "Hooks.h"
#include "GalaxyMap.h"
#include "StarMapInput.h"
#include "SurfaceMap.h"

#include <SF1SDK/Hooks.h>
#include <algorithm>
#include <charconv>
#include <cstring>
#include <ranges>

namespace TrackQuestFromMap::Hooks
{
	namespace
	{
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
			std::uintptr_t address{};
			std::uintptr_t target{};
			std::uintptr_t branch{};
			std::array<std::uint8_t, SF1::Hooks::callSize> original{};
		};

		[[nodiscard]] bool MatchesSignature(std::uintptr_t address, std::string_view pattern) noexcept
		{
			const auto bytes = reinterpret_cast<const std::uint8_t*>(address);
			for (std::size_t offset = 0; offset < pattern.size(); offset += 3)
			{
				const auto token = pattern.substr(offset, 2);
				if (token == "??")
				{
					continue;
				}
				std::uint8_t expected{};
				const auto end = token.data() + token.size();
				const auto result = std::from_chars(token.data(), end, expected, 16);
				if (result.ec != std::errc{} || result.ptr != end || bytes[offset / 3] != expected)
				{
					return false;
				}
			}
			return true;
		}

		template <std::size_t N>
		[[nodiscard]] bool Matches(
			const std::uintptr_t a_address,
			const std::array<std::uint8_t, N>& a_bytes) noexcept
		{
			const REL::Relocation<const std::uint8_t*> source{a_address};
			return std::memcmp(
				source.get(),
				a_bytes.data(),
				a_bytes.size()) == 0;
		}

		template <std::size_t N>
		[[nodiscard]] bool Restore(
			const std::uintptr_t a_address,
			const std::array<std::uint8_t, N>& a_original) noexcept
		{
			return SF1::Hooks::WriteCode(a_address, a_original);
		}

		[[nodiscard]] bool ValidateCallTarget(
			const std::uintptr_t a_callsite,
			const std::uintptr_t a_expected,
			const std::string_view a_name)
		{
			const auto actual = SF1::Hooks::CallTarget(a_callsite);
			if (actual == a_expected)
			{
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

	static bool InstallImpl()
	{
		const REL::Relocation<SurfaceMap::GatherSurfaceQuestTargets> surfaceGather{
			RE::ID::StarMap::SurfaceMapState::GatherSurfaceQuestTargets
		};
		const REL::Relocation<SurfaceMap::ComposeSurfaceQuestTarget> surfaceCompose{
			RE::ID::StarMap::ComposeSurfaceQuestTarget
		};
		const REL::Relocation<StarMapInput::DispatchButtonEvent> inputDispatcher{
			RE::ID::IMenu::OnButtonEvent
		};
		const REL::Relocation<GalaxyMap::BuildQuestTargetTree> questTreeBuild{
			RE::ID::StarMap::BuildQuestTargetTree
		};
		const REL::Relocation<GalaxyMap::ComposeQuestTargetMarker> questTreeCompose{
			RE::ID::StarMap::ComposeQuestTargetMarker
		};
		const REL::Relocation<GalaxyMap::InsertQuestTargetMarker> questTreeInsert{
			RE::ID::StarMap::InsertQuestTargetMarker
		};

		const auto surfaceGatherCallsite =
			SF1::ResolveID(RE::ID::StarMap::SurfaceMapState::RebuildSurfaceMarkers.value) +
			kSurfaceGatherCallOffset;
		const auto surfaceComposeCallsite =
			SF1::ResolveID(RE::ID::StarMap::SurfaceMapState::GatherSurfaceQuestTargets.value) +
			kSurfaceComposeCallOffset;
		const auto inputCallsite =
			SF1::ResolveID(RE::ID::StarMap::StarMapMenu::OnButtonEvent.value) + kStarMapInputCallOffset;

		const auto questTreeBuildTarget = questTreeBuild.address();
		const auto questTreeComposeTarget = questTreeCompose.address();
		const auto questTreeInsertTarget = questTreeInsert.address();
		const auto questTreeComposeCallsite =
			questTreeBuildTarget + kQuestTreeComposeCallOffset;
		const auto questTreeInsertCallsiteA =
			questTreeComposeTarget + kQuestTreeInsertCallOffsetA;
		const auto questTreeInsertCallsiteB =
			questTreeComposeTarget + kQuestTreeInsertCallOffsetB;

		std::array<std::uintptr_t, kQuestTreeCallRvas.size()> questTreeCallsites{};
		for (std::size_t index = 0; index < kQuestTreeCallRvas.size(); ++index)
		{
			questTreeCallsites[index] = SF1::Resolve(kQuestTreeCallRvas[index]);
		}

		const auto surfaceGatherTarget = surfaceGather.address();
		const auto surfaceComposeTarget = surfaceCompose.address();
		const auto inputTarget = inputDispatcher.address();

		if (!MatchesSignature(surfaceGatherCallsite - 3,
				"48 8B CF E8 E4 27 00 00 48 83 BF E8 08 00 00 00") ||
			!MatchesSignature(surfaceComposeCallsite - 7,
				"48 8B 13 48 8D 4D C7 E8 D4 00 00 00"))
		{
			logger::error("Surface Map ownership-hook signature mismatch");
			return false;
		}
		if (!MatchesSignature(inputCallsite - 12,
				"77 0F 84 C0 75 0B 48 8B D3 49 8B CF E8 6F 8F E9 00 40 84 ED") ||
			!MatchesSignature(inputTarget,
				"48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57"))
		{
			logger::error("Star Map input-hook signature mismatch");
			return false;
		}

		for (std::size_t index = 0; index + 1 < questTreeCallsites.size(); ++index)
		{
			if (!MatchesSignature(questTreeCallsites[index] - 12,
				"48 8D 54 24 ?? 48 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ??"))
			{
				logger::error(
					"Star Map quest-tree caller {} signature mismatch at 0x{:X}",
					index,
					questTreeCallsites[index]);
				return false;
			}
		}
		if (!MatchesSignature(questTreeCallsites.back() - 11,
				"48 8D 55 B0 48 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ??") ||
			!MatchesSignature(questTreeComposeCallsite - 8,
				"48 8B 13 48 8D 4C 24 20 E8 83 A1 FF FF 85 C0") ||
			!MatchesSignature(questTreeInsertCallsiteA - 12,
				"4C 8D 45 A0 48 8D 55 00 48 8D 4B 28 E8 3E 58 00 00 90") ||
			!MatchesSignature(questTreeInsertCallsiteB - 12,
				"4C 8D 45 B8 48 8D 55 20 48 8D 4B 28 E8 4B 56 00 00 90") ||
			!MatchesSignature(questTreeBuildTarget,
				"48 89 54 24 10 48 89 4C 24 08 53 55 56 57 41 54"))
		{
			logger::error("Star Map quest-tree ownership-hook signature mismatch");
			return false;
		}

		for (const auto questTreeCallsite : questTreeCallsites)
		{
			if (!ValidateCallTarget(
				questTreeCallsite,
				questTreeBuildTarget,
				"Star Map quest-tree caller"))
			{
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
			!ValidateCallTarget(inputCallsite, inputTarget, "Star Map input"))
		{
			return false;
		}

		const bool surfaceRefreshValidated =
			MatchesSignature(SF1::ResolveID(RE::ID::StarMap::StarMapMenu::GetSurfaceMapState.value),
				"40 53 48 83 EC 20 48 8D 99 F0 11 00 00 C6 44 24 30 03") &&
			MatchesSignature(SF1::ResolveID(RE::ID::StarMap::SurfaceMapState::Refresh.value),
				"48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 40");
		if (!surfaceRefreshValidated)
		{
			logger::warn(
				"Surface Map repaint signatures do not match; tracking stays enabled but forced repaint is disabled");
		}
		const bool starMapRefreshValidated =
			MatchesSignature(SF1::ResolveID(RE::ID::StarMap::StarMapMenu::RefreshQuestTargets.value),
				"48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 30");
		if (!starMapRefreshValidated)
		{
			logger::warn(
				"Galaxy/System repaint signature does not match; tracking stays enabled but forced repaint is disabled");
		}

		GalaxyMap::SetOriginalFunctions(
			questTreeBuild.get(),
			questTreeCompose.get(),
			questTreeInsert.get(),
			starMapRefreshValidated);
		SurfaceMap::SetOriginalFunctions(
			surfaceGather.get(),
			surfaceCompose.get(),
			surfaceRefreshValidated);
		StarMapInput::SetOriginalDispatcher(inputDispatcher.get());

		auto& trampoline = REL::GetTrampoline();
		constexpr auto requiredTrampolineBytes = 6 * sizeof(REL::ASM::JMP14);
		if (trampoline.free_size() < requiredTrampolineBytes)
		{
			logger::error(
				"Map hooks require {} trampoline bytes; {} remain",
				requiredTrampolineBytes,
				trampoline.free_size());
			return false;
		}

		std::array<CallPatch, kQuestTreeCallRvas.size() + 6> patches{};
		patches[0] = {
			.address = questTreeInsertCallsiteA,
			.target = reinterpret_cast<std::uintptr_t>(&GalaxyMap::CaptureInsertedQuestTargetMarker)
		};
		patches[1] = {
			.address = questTreeInsertCallsiteB,
			.target = reinterpret_cast<std::uintptr_t>(&GalaxyMap::CaptureInsertedQuestTargetMarker)
		};
		patches[2] = {
			.address = questTreeComposeCallsite,
			.target = reinterpret_cast<std::uintptr_t>(&GalaxyMap::CaptureAndComposeQuestTargetMarker)
		};
		std::size_t patchIndex = 3;
		for (const auto callsite : questTreeCallsites)
		{
			patches[patchIndex++] = {
				.address = callsite,
				.target = reinterpret_cast<std::uintptr_t>(&GalaxyMap::BuildAndPublishQuestTargetTree)
			};
		}
		patches[patchIndex++] = {
			.address = surfaceComposeCallsite,
			.target = reinterpret_cast<std::uintptr_t>(&SurfaceMap::CaptureAndComposeSurfaceQuestTarget)
		};
		patches[patchIndex++] = {
			.address = surfaceGatherCallsite,
			.target = reinterpret_cast<std::uintptr_t>(&SurfaceMap::BuildAndSnapshot)
		};
		patches[patchIndex] = {
			.address = inputCallsite,
			.target = reinterpret_cast<std::uintptr_t>(&StarMapInput::OnStarMapButton)
		};

		for (auto current = patches.begin(); current != patches.end(); ++current)
		{
			auto& patch = *current;
			const auto reused = std::find_if(patches.begin(), current, [&patch](const CallPatch& previous) {
				return previous.target == patch.target;
			});
			patch.branch = reused != current ? reused->branch :
				SF1::Hooks::AllocateCallRelay(trampoline, patch.target);
			if (!SF1::Hooks::CanReachCall(patch.address, patch.branch))
			{
				logger::error("Hook branch is outside signed rel32 reach at 0x{:X}", patch.address);
				return false;
			}
			const REL::Relocation<const std::uint8_t*> source{patch.address};
			std::memcpy(
				patch.original.data(),
				source.get(),
				patch.original.size());
		}

		std::size_t attempted = 0;
		bool writesVerified = true;
		for (const auto& patch : patches)
		{
			if (!Matches(patch.address, patch.original))
			{
				writesVerified = false;
				break;
			}
			++attempted;
			if (!SF1::Hooks::WriteCall(patch.address, patch.branch))
			{
				writesVerified = false;
				break;
			}
		}
		if (!writesVerified)
		{
			bool restored = true;
			for (const auto& patch : std::span{patches}.first(attempted) | std::views::reverse)
			{
				restored = Restore(patch.address, patch.original) && restored;
			}
			if (!restored)
			{
				logger::critical("Could not restore original map callsites");
				std::terminate();
			}
			logger::error("Hook installation failed; original calls restored");
			return false;
		}

		logger::info("Installed Surface, Galaxy, and System Map hooks");
		return true;
	}

	bool Install() noexcept
	{
		try
		{
			return InstallImpl();
		}
		catch (const std::exception& error)
		{
			logger::error("Could not install map hooks: {}", error.what());
			return false;
		}
	}
}
