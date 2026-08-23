#pragma once

namespace TrackQuestFromMap::SurfaceMapSelection
{
	[[nodiscard]] std::optional<bool> IsVisible(
		const RE::Scaleform::GFx::Value& a_hostRoot);
	[[nodiscard]] bool TryActivateHoveredQuestMarker(
		RE::Scaleform::GFx::Value& a_hostRoot);
}
