#include "StarMapSelection.h"

#include "GalaxyMapSelection.h"
#include "SurfaceMapSelection.h"

namespace TrackQuestFromMap::StarMapSelection
{
	std::optional<bool> SurfaceMapIsVisible(
		const RE::Scaleform::GFx::Value& a_hostRoot)
	{
		return SurfaceMapSelection::IsVisible(a_hostRoot);
	}

	bool TryActivateHoveredQuestMarker(
		RE::Scaleform::GFx::Value& a_hostRoot,
		const bool a_surfaceMapVisible)
	{
		return a_surfaceMapVisible
			? SurfaceMapSelection::TryActivateHoveredQuestMarker(a_hostRoot)
			: GalaxyMapSelection::TryActivateHoveredQuestMarker(a_hostRoot);
	}
}
