#pragma once

namespace TrackQuestFromMap::StarMapSelection
{
	[[nodiscard]] std::optional<bool> SurfaceMapIsVisible(
		const RE::Scaleform::GFx::Value& a_hostRoot);
	[[nodiscard]] bool TryActivateHoveredQuestMarker(
		RE::Scaleform::GFx::Value& a_hostRoot,
		bool a_surfaceMapVisible);
}
