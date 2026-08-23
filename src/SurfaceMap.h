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

	using GatherSurfaceQuestTargets = void (*)(RE::StarMap::SurfaceMapState*);
	using ComposeSurfaceQuestTarget = bool (*)(void*, void*);

	void SetOriginalFunctions(
		GatherSurfaceQuestTargets a_gatherSurfaceQuestTargets,
		ComposeSurfaceQuestTarget a_composeSurfaceQuestTarget) noexcept;

	[[nodiscard]] bool CaptureAndComposeSurfaceQuestTarget(void* a_context, void* a_target) noexcept;
	void BuildAndSnapshot(RE::StarMap::SurfaceMapState* a_surfaceState) noexcept;
	[[nodiscard]] bool TryActivate(const Request& a_request);
}
