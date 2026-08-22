#pragma once

namespace TrackQuestFromMap::SurfaceMap
{
	enum class MarkerVariant : std::uint8_t
	{
		kQuestTarget,
		kLargeNameplate,
	};

	struct Request
	{
		std::uint32_t markerHandleBits{};
		std::uint32_t markerType{};
		bool isLocation{};
		MarkerVariant variant{};
		std::string nameText;
		std::string extraText;
		std::string questTargetText;
	};

	using BuildSurfaceMarkers = void (*)(RE::StarMap::SurfaceMapState*);
	using ComposeQuestTarget = bool (*)(void*, void*);

	void SetOriginalFunctions(
		BuildSurfaceMarkers a_buildSurfaceMarkers,
		ComposeQuestTarget a_composeQuestTarget,
		bool a_surfaceRefreshValidated) noexcept;

	[[nodiscard]] bool CaptureAndComposeQuestTarget(void* a_context, void* a_target) noexcept;
	void BuildAndSnapshot(RE::StarMap::SurfaceMapState* a_surfaceState) noexcept;
	[[nodiscard]] bool TryActivate(const Request& a_request);
}
