#pragma once

namespace TrackQuestSurface::Hooks
{
	// Transactionally installs the guarded Surface and shared Galaxy/System
	// quest-target ownership hooks plus the GalaxyStarMapMenu Select dispatcher.
	// No engine or Scaleform pointer is retained after its originating call.
	[[nodiscard]] bool Install() noexcept;
}
