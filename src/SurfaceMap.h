#pragma once

#include "RE/S/StarMap.h"

#include <cstdint>
#include <string>

namespace TrackQuestSurface::SurfaceMap
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
		bool          isLocation{};
		MarkerVariant variant{};

		// Owned immediately from the matching MarkerData string fields. They are
		// only exact within-generation discriminators and are never parsed as quest
		// identity.
		std::string nameText;
		std::string extraText;
		std::string questTargetText;
	};

	using BuildSurfaceMarkers = void (*)(RE::StarMap::SurfaceMapState*);
	using ComposeQuestTarget = bool (*)(void*, void*);

	void SetOriginalFunctions(
		BuildSurfaceMarkers a_buildSurfaceMarkers,
		ComposeQuestTarget  a_composeQuestTarget,
		bool                a_surfaceRefreshValidated) noexcept;

	[[nodiscard]] bool CaptureAndComposeQuestTarget(void* a_context, void* a_target) noexcept;
	void               BuildAndSnapshot(RE::StarMap::SurfaceMapState* a_surfaceState) noexcept;

	// Invalidates all cached marker handles after a rejected/failed generation.
	void ResetCache() noexcept;

	// Returns true only when the exact inactive quest represented by the marker's
	// visible target label was accepted for SFSE's main-thread task queue.
	[[nodiscard]] bool TryActivate(const Request& a_request) noexcept;
}
