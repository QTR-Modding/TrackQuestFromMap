#pragma once

#include "RE/T/TESQuest.h"

namespace TrackQuestFromMap::QuestTracking
{
	enum class Source : std::uint8_t
	{
		kSurface,
		kGalaxy,
		kSystem
	};

	using PostTrack = void (*)();

	[[nodiscard]] RE::TESQuest* ResolveQuest(const RE::QuestInstanceKey& a_key) noexcept;
	[[nodiscard]] bool IsInactiveTrackable(const RE::QuestInstanceKey& a_key) noexcept;
	[[nodiscard]] bool QueueTrack(
		const RE::QuestInstanceKey& a_key,
		Source a_source,
		PostTrack a_postTrack = nullptr) noexcept;
}
