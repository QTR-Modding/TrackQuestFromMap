#pragma once

#include "RE/T/TESQuest.h"

namespace TrackQuestSurface::QuestTracking
{
	enum class Source : std::uint8_t
	{
		kSurface,
		kGalaxy,
		kSystem
	};

	using PostTrack = void (*)() noexcept;

	[[nodiscard]] RE::TESQuest* ResolveQuest(const RE::QuestInstanceKey& a_key) noexcept;
	[[nodiscard]] bool          IsInactiveTrackable(const RE::QuestInstanceKey& a_key) noexcept;

	// Returns true only after the exact inactive quest was accepted for the
	// SFSE main-thread task queue. The task repeats all live-state checks before
	// calling the engine's toggle-semantic helper.
	[[nodiscard]] bool QueueTrack(
		const RE::QuestInstanceKey& a_key,
		Source                      a_source,
		PostTrack                   a_postTrack = nullptr) noexcept;
}
