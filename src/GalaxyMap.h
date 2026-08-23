#pragma once

namespace TrackQuestFromMap::GalaxyMap
{
	enum class View : std::uint8_t
	{
		kGalaxy,
		kSystem
	};

	struct Request
	{
		View view{};
		std::uint32_t markerLocationID{};
		std::string questTargetText;
	};

	using BuildQuestTargetTree = void (*)(void*, void*);
	using ComposeQuestTargetMarker = bool (*)(void*, void*);
	using InsertQuestTargetMarker = void* (*)(void*,
	                                          RE::StarMap::QuestTargetMarkerInsertResult*,
	                                          RE::StarMap::QuestTargetMarkerData*);

	void SetOriginalFunctions(
		BuildQuestTargetTree a_buildQuestTargetTree,
		ComposeQuestTargetMarker a_composeQuestTargetMarker,
		InsertQuestTargetMarker a_insertQuestTargetMarker,
		bool a_refreshValidated) noexcept;
	void BuildAndPublishQuestTargetTree(void* a_playerCharacter, void* a_outputTree) noexcept;
	[[nodiscard]] bool CaptureAndComposeQuestTargetMarker(void* a_context, void* a_target) noexcept;
	[[nodiscard]] void* CaptureInsertedQuestTargetMarker(
		void* a_nestedTree,
		RE::StarMap::QuestTargetMarkerInsertResult* a_result,
		RE::StarMap::QuestTargetMarkerData* a_markerData) noexcept;
	[[nodiscard]] bool TryActivate(const Request& a_request);
}
