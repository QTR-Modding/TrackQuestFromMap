#pragma once

#include <cstdint>
#include <string>

namespace TrackQuestSurface::SurfaceActivation {
enum class MarkerVariant : std::uint8_t {
  kQuestTarget,
  kLargeNameplate,
};

struct Request {
  std::uint32_t markerHandleBits{};
  std::uint32_t markerType{};
  bool isLocation{};
  MarkerVariant variant{};
  // Owned immediately from the matching MarkerData string fields. They are
  // only exact within-generation discriminators and are never parsed as quest
  // identity.
  std::string nameText;
  std::string extraText;
  std::string questTargetText;
};

// Transactionally installs the guarded surface-marker ownership hooks and the
// GalaxyStarMapMenu Select dispatcher hook. No engine or Scaleform pointer is
// retained after its originating call.
[[nodiscard]] bool Install() noexcept;

// Invalidates all cached marker handles after a rejected/failed generation.
void ResetCache() noexcept;

// Returns true only when the exact inactive quest represented by the marker's
// visible target label was accepted for SFSE's main-thread task queue.
[[nodiscard]] bool TryActivate(const Request &a_request) noexcept;
} // namespace TrackQuestSurface::SurfaceActivation
