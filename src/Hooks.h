#pragma once

namespace TrackQuestSurface::Hooks
{
	// Transactionally installs the guarded surface-marker ownership hooks and the
	// GalaxyStarMapMenu Select dispatcher hook. No engine or Scaleform pointer is
	// retained after its originating call.
	[[nodiscard]] bool Install() noexcept;
}
