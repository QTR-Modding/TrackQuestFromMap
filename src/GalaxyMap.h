#pragma once

#include "RE/S/StarMap.h"

#include <string>

namespace TrackQuestSurface::GalaxyMap
{
	enum class View : std::uint8_t
	{
		kGalaxy,
		kSystem
	};

	struct Request
	{
		View          view{};
		std::uint32_t markerLocationID{};
		std::string   questTargetText;
	};

	struct InsertResult
	{
		void*     node{};
		bool      inserted{};
		std::byte pad009[0x7]{};
	};
	static_assert(sizeof(InsertResult) == 0x10);

	using BuildQuestTargetTree = void (*)(void*, void*);
	using ComposeQuestTargetMarker = bool (*)(void*, void*);
	using InsertQuestTargetMarker = void* (*)(void*,
		InsertResult*,
		RE::StarMap::QuestTargetMarkerData*);

	void SetOriginalFunctions(
		BuildQuestTargetTree     a_buildQuestTargetTree,
		ComposeQuestTargetMarker a_composeQuestTargetMarker,
		InsertQuestTargetMarker  a_insertQuestTargetMarker,
		bool                     a_refreshValidated) noexcept;

	// ABI thunks installed only at reviewed Starfield 1.16.244 direct CALL sites.
	void                BuildAndPublishQuestTargetTree(void* a_playerCharacter, void* a_outputTree) noexcept;
	[[nodiscard]] bool  CaptureAndComposeQuestTargetMarker(void* a_context, void* a_target) noexcept;
	[[nodiscard]] void* CaptureInsertedQuestTargetMarker(
		void*                               a_nestedTree,
		InsertResult*                       a_result,
		RE::StarMap::QuestTargetMarkerData* a_markerData) noexcept;

	// Returns true only when the exact inactive quest represented by the visible
	// Galaxy/System mission label was accepted for the main-thread task queue.
	[[nodiscard]] bool TryActivate(const Request& a_request) noexcept;
}
