#include "SurfaceActivation.h"

#include "RE/Starfield.h"
#include "REL/ASM.h"
#include "REL/Relocation.h"
#include "REL/Utility.h"
#include "RE/I/IMenu.h"
#include "RE/S/ScaleformGFxASMovieRootBase.h"
#include "RE/S/ScaleformGFxValue.h"
#include "RE/U/UI.h"
#include "REX/LOG.h"
#include "SFSE/SFSE.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace TrackQuestSurface::SurfaceActivation {
namespace {
// Reviewed against Starfield.exe 1.16.244.0 and its v5 Address Library.
constexpr REL::ID kSurfaceRebuildID{95000}; // RVA 0x16DB640
constexpr REL::ID kQuestGatherID{95012};    // RVA 0x16DDEA0
constexpr REL::ID kQuestComposeID{95013};   // RVA 0x16DE1B0
constexpr REL::ID kToggleTrackingID{91440}; // RVA 0x15DD710
constexpr REL::ID kStarMapButtonHandlerID{94684}; // RVA 0x16BA350
constexpr REL::ID kVanillaButtonDispatcherID{130632}; // RVA 0x25533D0
constexpr REL::ID kGetSurfaceMapStateID{94755}; // RVA 0x16C1D40
constexpr REL::ID kRefreshSurfaceMapStateID{95003}; // RVA 0x16DBA10
constexpr REL::ID kStarMapMenuVtableID{446845}; // RVA 0x4C97BC0
constexpr REL::ID kSurfaceMapStateVtableID{447074}; // RVA 0x4C99A00
constexpr std::ptrdiff_t kQuestGatherCallOffset = 0x77;
constexpr std::ptrdiff_t kQuestComposeCallOffset = 0x237;
constexpr std::ptrdiff_t kStarMapInputCallOffset = 0x10C;

constexpr std::ptrdiff_t kMarkerBeginOffset = 0x8B8;
constexpr std::ptrdiff_t kMarkerEndOffset = 0x8C0;
constexpr std::ptrdiff_t kMarkerCapacityOffset = 0x8C8;
constexpr std::size_t kMarkerStride = 0x80;

constexpr std::ptrdiff_t kNameTextOffset = 0x28;
constexpr std::ptrdiff_t kExtraTextOffset = 0x30;
constexpr std::ptrdiff_t kQuestTargetTextOffset = 0x38;
constexpr std::ptrdiff_t kHandleOffset = 0x40;
constexpr std::ptrdiff_t kOwnerBeginOffset = 0x48;
constexpr std::ptrdiff_t kOwnerEndOffset = 0x50;
constexpr std::ptrdiff_t kOwnerCapacityOffset = 0x58;
constexpr std::ptrdiff_t kMarkerTypeOffset = 0x60;
constexpr std::ptrdiff_t kIsLocationOffset = 0x68;
constexpr std::ptrdiff_t kHasQuestTargetOffset = 0x69;
constexpr std::ptrdiff_t kQuestActiveOffset = 0x6A;

constexpr std::ptrdiff_t kQuestInstanceIDOffset = 0x70;
constexpr std::ptrdiff_t kQuestStateOffset = 0x114;
constexpr std::ptrdiff_t kComposeQuestOffset = 0x10;
constexpr std::ptrdiff_t kQuestFormIDOffset = 0x28;
constexpr std::uint32_t kQuestRunning = 1U << 0;
constexpr std::uint32_t kQuestStopped = 1U << 1;
constexpr std::uint32_t kQuestTracked = 1U << 11;
constexpr std::uint16_t kLargeQuestMarkerType = 0x48;

constexpr std::size_t kMaximumMarkers = 4096;
constexpr std::size_t kMaximumOwners = 128;
constexpr std::size_t kMaximumUIChildren = 4096;
constexpr std::size_t kMaximumQuestTargetTextBytes = 4096;
constexpr std::size_t kQuestCaptureSlotCount = 4096;
static_assert((kQuestCaptureSlotCount & (kQuestCaptureSlotCount - 1)) == 0);

constexpr std::string_view kGalaxyStarMapMenuName = "GalaxyStarMapMenu";
constexpr std::string_view kSelectUserEvent = "Select";

struct QuestKey {
  std::uint32_t formID{};
  std::uint32_t instanceID{};

  [[nodiscard]] bool operator==(const QuestKey &) const noexcept = default;
};
static_assert(sizeof(QuestKey) == 0x8);

struct MarkerRecord {
  std::uint32_t markerHandleBits{};
  std::uint16_t markerType{};
  bool isLocation{};
  bool hasQuestTarget{};
  bool questActive{};
  std::string nameText;
  std::string extraText;
  std::string questTargetText;
  std::size_t rawOwnerCount{};
  std::optional<QuestKey> visibleOwner;
  std::vector<QuestKey> owners;
};

enum class CapturedQuestState : std::uint8_t {
  kEmpty,
  kUnique,
  kAmbiguous,
};

struct CapturedQuestSlot {
  QuestKey key;
  CapturedQuestState state{};
};

// REL 95012 holds the native map-state lock while it calls REL 95013. Keep
// this capture fixed-capacity and primitive-only: the wrapper must not
// allocate, log, resolve forms, or retain an engine pointer in that window.
struct QuestPairCapture {
  std::array<CapturedQuestSlot, kQuestCaptureSlotCount> slots{};
  std::size_t contributorCalls{};
  std::size_t uniqueForms{};
  std::size_t ambiguousForms{};
  bool overflow{};
  bool invalidInvocation{};

  void Reset() noexcept {
    slots.fill(CapturedQuestSlot{});
    contributorCalls = 0;
    uniqueForms = 0;
    ambiguousForms = 0;
    overflow = false;
    invalidInvocation = false;
  }

  void Record(const QuestKey &a_key) noexcept {
    ++contributorCalls;
    if (a_key.formID == 0) {
      invalidInvocation = true;
      return;
    }

    constexpr std::uint32_t goldenRatio = 0x9E3779B1U;
    const auto first = static_cast<std::size_t>(a_key.formID * goldenRatio) &
                       (kQuestCaptureSlotCount - 1);
    for (std::size_t probe = 0; probe < kQuestCaptureSlotCount; ++probe) {
      auto &slot = slots[(first + probe) & (kQuestCaptureSlotCount - 1)];
      if (slot.state == CapturedQuestState::kEmpty) {
        slot.key = a_key;
        slot.state = CapturedQuestState::kUnique;
        ++uniqueForms;
        return;
      }
      if (slot.key.formID != a_key.formID) {
        continue;
      }
      if (slot.state == CapturedQuestState::kUnique &&
          slot.key.instanceID != a_key.instanceID) {
        slot.state = CapturedQuestState::kAmbiguous;
        --uniqueForms;
        ++ambiguousForms;
      }
      return;
    }
    overflow = true;
  }

  [[nodiscard]] std::optional<QuestKey>
  Resolve(const std::uint32_t a_formID) const noexcept {
    if (a_formID == 0) {
      return std::nullopt;
    }

    constexpr std::uint32_t goldenRatio = 0x9E3779B1U;
    const auto first = static_cast<std::size_t>(a_formID * goldenRatio) &
                       (kQuestCaptureSlotCount - 1);
    for (std::size_t probe = 0; probe < kQuestCaptureSlotCount; ++probe) {
      const auto &slot =
          slots[(first + probe) & (kQuestCaptureSlotCount - 1)];
      if (slot.state == CapturedQuestState::kEmpty) {
        return std::nullopt;
      }
      if (slot.key.formID == a_formID) {
        return slot.state == CapturedQuestState::kUnique
                   ? std::optional<QuestKey>{slot.key}
                   : std::nullopt;
      }
    }
    return std::nullopt;
  }
};

// Large marker variants can publish several native rows with the same sentinel
// handle. Preserve every row; a handle alone is not a unique cache key.
using MarkerCache = std::vector<MarkerRecord>;
using BuildSurfaceMarkers = void (*)(void *);
using ComposeQuestTarget = bool (*)(void *, void *);
using ToggleTracking = void (*)(const QuestKey *);
using DispatchButtonEvent = void (*)(RE::BSInputEventUser *,
                                     const RE::ButtonEvent *);
using GetSurfaceMapState = void *(*)(void *);
using RefreshSurfaceMapState = void (*)(void *, void *);

std::mutex cacheMutex;
MarkerCache markerCache;
BuildSurfaceMarkers originalBuildSurfaceMarkers{};
ComposeQuestTarget originalComposeQuestTarget{};
DispatchButtonEvent originalDispatchButtonEvent{};
thread_local QuestPairCapture threadQuestCapture;
thread_local QuestPairCapture *activeQuestCapture{};
thread_local std::size_t surfaceGatherDepth{};
bool surfaceRefreshValidated{};

template <class T>
[[nodiscard]] T ReadAt(const void *a_base,
                       const std::ptrdiff_t a_offset) noexcept {
  T result{};
  std::memcpy(std::addressof(result),
              static_cast<const std::byte *>(a_base) + a_offset, sizeof(T));
  return result;
}

[[nodiscard]] std::optional<std::string>
CopyNativeText(const std::byte *a_marker, const std::ptrdiff_t a_offset) {
  // These fields are in-place BSFixedStrings. Binding by reference avoids any
  // acquire/release while the bytes are copied into plugin-owned storage.
  const auto &text = *reinterpret_cast<const RE::BSFixedString *>(
      a_marker + a_offset);
  const auto length = static_cast<std::size_t>(text.length());
  if (length > kMaximumQuestTargetTextBytes) {
    return std::nullopt;
  }
  return std::string{text.c_str(), length};
}

class ScopedQuestCapture {
public:
  explicit ScopedQuestCapture(QuestPairCapture *a_capture) noexcept
      : previous_(std::exchange(activeQuestCapture, a_capture)) {}
  explicit ScopedQuestCapture(QuestPairCapture &a_capture) noexcept
      : ScopedQuestCapture(std::addressof(a_capture)) {}
  ~ScopedQuestCapture() { activeQuestCapture = previous_; }

  ScopedQuestCapture(const ScopedQuestCapture &) = delete;
  ScopedQuestCapture &operator=(const ScopedQuestCapture &) = delete;

private:
  QuestPairCapture *previous_{};
};

class ScopedGatherDepth {
public:
  explicit ScopedGatherDepth(std::size_t &a_depth) noexcept
      : depth_(a_depth) {
    ++depth_;
  }
  ~ScopedGatherDepth() { --depth_; }

  ScopedGatherDepth(const ScopedGatherDepth &) = delete;
  ScopedGatherDepth &operator=(const ScopedGatherDepth &) = delete;

private:
  std::size_t &depth_;
};

// Exact 1.16.244 ABI at REL 95012 + 0x237: RCX is the contributor context,
// RDX is the target data, and AL is the Boolean result. The context owns the
// current TESQuest pointer at +0x10. Only an exact primitive pair is copied.
[[nodiscard]] bool CaptureAndComposeQuestTarget(void *a_context,
                                                void *a_target) noexcept {
  if (auto *capture = activeQuestCapture) {
    const auto *quest =
        a_context ? ReadAt<const void *>(a_context, kComposeQuestOffset)
                  : nullptr;
    if (quest) {
      capture->Record(
          QuestKey{.formID =
                       ReadAt<std::uint32_t>(quest, kQuestFormIDOffset),
                   .instanceID =
                       ReadAt<std::uint32_t>(quest, kQuestInstanceIDOffset)});
    } else {
      capture->invalidInvocation = true;
    }
  }
  return originalComposeQuestTarget(a_context, a_target);
}

[[nodiscard]] std::optional<std::uint32_t>
ReadGFxUInt(const RE::Scaleform::GFx::Value &a_value) noexcept {
  if (a_value.IsUInt()) {
    return a_value.GetUInt();
  }
  if (a_value.IsInt()) {
    return static_cast<std::uint32_t>(a_value.GetInt());
  }
  if (a_value.IsNumber()) {
    const double number = a_value.GetNumber();
    if (std::isfinite(number) && number >= 0.0 &&
        std::trunc(number) == number &&
        number <=
            static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
      return static_cast<std::uint32_t>(number);
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool
ReadGFxBooleanMember(const RE::Scaleform::GFx::Value &a_object,
                     const std::string_view a_name, bool &a_result) {
  if (!a_object.IsObject()) {
    return false;
  }
  RE::Scaleform::GFx::Value value;
  if (!a_object.GetMember(a_name, std::addressof(value)) ||
      !value.IsBoolean()) {
    return false;
  }
  a_result = value.GetBoolean();
  return true;
}

[[nodiscard]] bool
ReadGFxStringMember(const RE::Scaleform::GFx::Value &a_object,
                    const std::string_view a_name, std::string &a_result) {
  if (!a_object.IsObject()) {
    return false;
  }
  RE::Scaleform::GFx::Value value;
  if (!a_object.GetMember(a_name, std::addressof(value)) ||
      !value.IsString()) {
    return false;
  }

  // GetString may point into a managed GFx value. Copy it while `value` is
  // alive and never retain the pointer beyond this call.
  const auto *text = value.GetString();
  if (!text) {
    return false;
  }
  std::size_t length = 0;
  while (length <= kMaximumQuestTargetTextBytes && text[length] != '\0') {
    ++length;
  }
  if (length > kMaximumQuestTargetTextBytes) {
    return false;
  }
  a_result.assign(text, length);
  return true;
}

[[nodiscard]] std::optional<bool>
ReadGFxNestedVisible(const RE::Scaleform::GFx::Value &a_object,
                     const std::string_view a_memberName) {
  if (!a_object.IsObject()) {
    return std::nullopt;
  }
  RE::Scaleform::GFx::Value nested;
  bool visible{};
  if (!a_object.GetMember(a_memberName, std::addressof(nested)) ||
      !nested.IsObject() ||
      !ReadGFxBooleanMember(nested, "visible", visible)) {
    return std::nullopt;
  }
  return visible;
}

[[nodiscard]] std::optional<Request>
FindHoveredQuestMarker(RE::BSInputEventUser *a_user) {
  auto *ui = RE::UI::GetSingleton();
  if (!ui) {
    REX::INFO("Select release preserved vanilla: UI singleton unavailable");
    return std::nullopt;
  }

  const RE::BSFixedString menuName{kGalaxyStarMapMenuName.data()};
  auto menu = ui->GetMenu(menuName);
  if (!menu) {
    REX::INFO(
        "Select release preserved vanilla: GalaxyStarMapMenu unavailable");
    return std::nullopt;
  }
  if (static_cast<RE::BSInputEventUser *>(menu.get()) != a_user) {
    REX::INFO("Select release preserved vanilla: live GalaxyStarMapMenu does "
              "not match input recipient");
    return std::nullopt;
  }
  if (!menu->uiMovie || !menu->uiMovie->asMovieRoot) {
    REX::INFO(
        "Select release preserved vanilla: GalaxyStarMapMenu movie unavailable");
    return std::nullopt;
  }

  const char *rootPath = menu->GetRootPath();
  RE::Scaleform::GFx::Value hostRoot;
  auto *movieRoot = menu->uiMovie->asMovieRoot.get();
  if (!rootPath || !movieRoot->GetVariable(std::addressof(hostRoot), rootPath) ||
      !hostRoot.IsObject()) {
    REX::INFO("Select release preserved vanilla: GalaxyStarMapMenu root "
              "unavailable");
    return std::nullopt;
  }

  RE::Scaleform::GFx::Value surfaceMap;
  RE::Scaleform::GFx::Value map;
  RE::Scaleform::GFx::Value markers;
  if (!hostRoot.GetMember("SurfaceMap_mc", std::addressof(surfaceMap)) ||
      !surfaceMap.IsObject() ||
      !surfaceMap.GetMember("Map_mc", std::addressof(map)) ||
      !map.IsObject() ||
      !map.GetMember("MarkersContainer_mc", std::addressof(markers)) ||
      !markers.IsObject()) {
    REX::INFO("Select release preserved vanilla: public SurfaceMap_mc.Map_mc."
              "MarkersContainer_mc path unavailable");
    return std::nullopt;
  }

  bool surfaceMapVisible{};
  if (!ReadGFxBooleanMember(surfaceMap, "visible", surfaceMapVisible) ||
      !surfaceMapVisible) {
    REX::INFO("Select release preserved vanilla: SurfaceMap_mc is not "
              "visibly active");
    return std::nullopt;
  }

  RE::Scaleform::GFx::Value childCountValue;
  if (!markers.GetMember("numChildren", std::addressof(childCountValue))) {
    REX::INFO(
        "Select release preserved vanilla: marker child count unavailable");
    return std::nullopt;
  }
  const auto childCountValueUnsigned = ReadGFxUInt(childCountValue);
  if (!childCountValueUnsigned ||
      *childCountValueUnsigned > kMaximumUIChildren) {
    REX::INFO("Select release preserved vanilla: invalid marker child count "
              "(maximum={})",
              kMaximumUIChildren);
    return std::nullopt;
  }

  const auto childCount =
      static_cast<std::size_t>(*childCountValueUnsigned);
  for (std::size_t reverseIndex = childCount; reverseIndex > 0;
       --reverseIndex) {
    const auto index = reverseIndex - 1;
    RE::Scaleform::GFx::Value child;
    RE::Scaleform::GFx::Value childIndex{
        static_cast<std::uint32_t>(index)};
    if (!markers.Invoke("getChildAt", std::addressof(child),
                        std::addressof(childIndex), 1)) {
      REX::INFO("Select release preserved vanilla: getChildAt({}) failed",
                index);
      return std::nullopt;
    }
    if (!child.IsObject()) {
      continue;
    }

    bool childVisible{};
    if (!ReadGFxBooleanMember(child, "visible", childVisible) ||
        !childVisible) {
      continue;
    }

    const bool questTargetVisible =
        ReadGFxNestedVisible(child, "QuestTargetText_mc").value_or(false);
    const bool nameplateVisible =
        ReadGFxNestedVisible(child, "Nameplate_mc").value_or(false);
    if (!questTargetVisible && !nameplateVisible) {
      continue;
    }

    // SurfaceMarkerContainer moves CurrentHoveredMarker to the top of the
    // display list. Reverse order therefore selects the same marker vanilla
    // will dispatch, even when a large nameplate overlaps a smaller marker.
    bool hasQuestTarget{};
    bool hasActiveQuest{};
    bool isLocation{};
    if (!ReadGFxBooleanMember(child, "hasQuestTarget", hasQuestTarget) ||
        !ReadGFxBooleanMember(child, "hasActiveQuest", hasActiveQuest) ||
        !ReadGFxBooleanMember(child, "IsLocation", isLocation)) {
      REX::INFO("Select release preserved vanilla: topmost hovered marker "
                "has invalid Boolean metadata (index={})",
                index);
      return std::nullopt;
    }

    RE::Scaleform::GFx::Value handleValue;
    RE::Scaleform::GFx::Value markerData;
    RE::Scaleform::GFx::Value markerTypeValue;
    if (!child.GetMember("handleBits", std::addressof(handleValue)) ||
        !child.GetMember("MarkerData", std::addressof(markerData)) ||
        !markerData.IsObject() ||
        !markerData.GetMember("iMarkerType",
                              std::addressof(markerTypeValue))) {
      REX::INFO("Select release preserved vanilla: topmost hovered marker "
                "has invalid identity metadata (index={})",
                index);
      return std::nullopt;
    }

    const auto handle = ReadGFxUInt(handleValue);
    const auto markerType = ReadGFxUInt(markerTypeValue);
    if (!handle || !markerType) {
      REX::INFO("Select release preserved vanilla: topmost hovered marker "
                "has non-integral identity metadata (index={})",
                index);
      return std::nullopt;
    }

    Request candidate{.markerHandleBits = *handle,
                      .markerType = *markerType,
                      .isLocation = isLocation};
    const bool largeNameplate =
        nameplateVisible && *markerType == kLargeQuestMarkerType &&
        !isLocation && !hasQuestTarget && !hasActiveQuest;
    const bool smallQuestTarget =
        questTargetVisible && hasQuestTarget && !hasActiveQuest;

    if (largeNameplate) {
      candidate.variant = MarkerVariant::kLargeNameplate;
      if (!ReadGFxStringMember(markerData, "sNameText", candidate.nameText) ||
          !ReadGFxStringMember(markerData, "sExtraText",
                               candidate.extraText)) {
        REX::INFO("Select release preserved vanilla: large marker has "
                  "invalid name/extra text (index={})",
                  index);
        return std::nullopt;
      }
    } else if (smallQuestTarget) {
      candidate.variant = MarkerVariant::kQuestTarget;
      if (!ReadGFxStringMember(markerData, "sQuestTargetText",
                               candidate.questTargetText)) {
        REX::INFO("Select release preserved vanilla: quest marker has "
                  "invalid target text (index={})",
                  index);
        return std::nullopt;
      }
    } else {
      REX::INFO("Select release preserved vanilla: topmost hovered marker "
                "is ineligible (index={}, handle=0x{:08X}, type={}, "
                "location={}, questTarget={}, active={}, nameplate={}, "
                "questLabel={})",
                index, *handle, *markerType, isLocation, hasQuestTarget,
                hasActiveQuest, nameplateVisible, questTargetVisible);
      return std::nullopt;
    }

    REX::INFO("Select release resolved topmost {} marker: handle=0x{:08X}, "
              "type={}, index={}, children={}, nameBytes={}, extraBytes={}, "
              "questTextBytes={}",
              candidate.variant == MarkerVariant::kLargeNameplate
                  ? "large-nameplate"
                  : "quest-target",
              candidate.markerHandleBits, candidate.markerType, index,
              childCount, candidate.nameText.size(), candidate.extraText.size(),
              candidate.questTargetText.size());
    return candidate;
  }

  REX::INFO("Select release preserved vanilla: no visible hovered marker "
            "label among {} direct children",
            childCount);
  return std::nullopt;
}

[[nodiscard]] bool
IsExactSelectRelease(const RE::ButtonEvent *a_event) {
  if (!a_event ||
      a_event->eventType != RE::InputEvent::EventType::kButton ||
      a_event->status == RE::InputEvent::Status::kStop ||
      !std::isfinite(a_event->value) ||
      !std::isfinite(a_event->heldDownSecs) || a_event->heldDownSecs < 0.0F ||
      a_event->value != 0.0F || a_event->disabled) {
    return false;
  }

  // Starfield 1.16.244 returns a pointer/reference from the QUserEvent vslot,
  // while this CommonLibSF snapshot declares a by-value return. Calling that
  // wrapper therefore applies the wrong hidden-return ABI. Read the incoming
  // ButtonEvent's owned string in place; its lifetime covers this dispatch and
  // no reference-counted copy or release is performed.
  const auto &userEvent = a_event->strUserEvent;
  return std::string_view{userEvent.c_str(), userEvent.length()} ==
         kSelectUserEvent;
}

// Exact ABI at REL 94684 + 0x10C: RCX is the BSInputEventUser subobject and
// RDX is the ButtonEvent. Vanilla REL 130632 is called exactly once unless a
// unique marker request was accepted and queued.
void OnStarMapButton(RE::BSInputEventUser *a_user,
                     const RE::ButtonEvent *a_event) noexcept {
  bool consumed = false;
  try {
    if (IsExactSelectRelease(a_event)) {
      const auto request = FindHoveredQuestMarker(a_user);
      if (request) {
        consumed = TryActivate(*request);
        if (!consumed) {
          REX::INFO("Select release preserved vanilla: native ownership/live "
                    "quest validation rejected marker 0x{:08X}",
                    request->markerHandleBits);
        }
      }
    }
  } catch (const std::exception &error) {
    REX::ERROR("Star Map Select resolver failed; preserving vanilla: {}",
               error.what());
  } catch (...) {
    REX::ERROR(
        "Star Map Select resolver failed unexpectedly; preserving vanilla");
  }

  if (consumed) {
    const_cast<RE::ButtonEvent *>(a_event)->status =
        RE::InputEvent::Status::kStop;
    REX::INFO("Consumed Star Map Select release after queuing quest "
              "activation");
    return;
  }

  if (!originalDispatchButtonEvent) {
    REX::FAIL("Star Map input hook has no vanilla dispatcher");
  }
  originalDispatchButtonEvent(a_user, a_event);
}

[[nodiscard]] std::uintptr_t
ReadRelativeCallTarget(const std::uintptr_t a_callsite) noexcept {
  std::int32_t displacement{};
  std::memcpy(std::addressof(displacement),
              reinterpret_cast<const void *>(a_callsite + 1),
              sizeof(displacement));
  return static_cast<std::uintptr_t>(
      static_cast<std::intptr_t>(a_callsite + 5) + displacement);
}

void PublishCache(MarkerCache a_next) {
  std::scoped_lock lock(cacheMutex);
  markerCache = std::move(a_next);
}

[[nodiscard]] RE::TESQuest *ResolveQuest(const QuestKey &a_key) noexcept {
  auto *quest = RE::TESForm::LookupByID<RE::TESQuest>(a_key.formID);
  if (!quest || ReadAt<std::uint32_t>(quest, kQuestInstanceIDOffset) !=
                    a_key.instanceID) {
    return nullptr;
  }
  return quest;
}

[[nodiscard]] bool IsInactiveTrackableQuest(const QuestKey &a_key) noexcept {
  const auto *quest = ResolveQuest(a_key);
  if (!quest) {
    return false;
  }

  const auto state = ReadAt<std::uint32_t>(quest, kQuestStateOffset);
  return (state & kQuestRunning) != 0 && (state & kQuestStopped) == 0 &&
         (state & kQuestTracked) == 0;
}

void RebuildCurrentSurfaceMap() noexcept {
  if (!surfaceRefreshValidated) {
    return;
  }

  try {
    auto *ui = RE::UI::GetSingleton();
    if (!ui) {
      return;
    }

    const RE::BSFixedString menuName{kGalaxyStarMapMenuName.data()};
    auto menu = ui->GetMenu(menuName);
    if (!menu) {
      return;
    }
    if (ReadAt<std::uintptr_t>(menu.get(), 0) !=
        kStarMapMenuVtableID.address()) {
      REX::WARN("Skipped Surface Map repaint: unexpected StarMapMenu vtable");
      return;
    }

    static REL::Relocation<GetSurfaceMapState> getSurfaceMapState{
        kGetSurfaceMapStateID};
    void *surfaceState = getSurfaceMapState(menu.get());
    if (!surfaceState) {
      return;
    }
    if (ReadAt<std::uintptr_t>(surfaceState, 0) !=
        kSurfaceMapStateVtableID.address()) {
      REX::WARN("Skipped Surface Map repaint: unexpected SurfaceMapState "
                "vtable");
      return;
    }

    static REL::Relocation<RefreshSurfaceMapState> refreshSurfaceMapState{
        kRefreshSurfaceMapStateID};
    refreshSurfaceMapState(surfaceState, nullptr);
    REX::INFO("Rebuilt the open Surface Map after quest activation");
  } catch (const std::exception &error) {
    REX::WARN("Surface Map repaint failed safely: {}", error.what());
  } catch (...) {
    REX::WARN("Surface Map repaint failed safely");
  }
}

void SnapshotMarkerOwners(void *a_surfaceState,
                          const QuestPairCapture &a_capture) {
  MarkerCache next;
  if (!a_surfaceState) {
    PublishCache(std::move(next));
    return;
  }

  const auto markerBegin =
      ReadAt<std::uintptr_t>(a_surfaceState, kMarkerBeginOffset);
  const auto markerEnd =
      ReadAt<std::uintptr_t>(a_surfaceState, kMarkerEndOffset);
  const auto markerCapacity =
      ReadAt<std::uintptr_t>(a_surfaceState, kMarkerCapacityOffset);
  if (!markerBegin || markerEnd < markerBegin || markerCapacity < markerEnd ||
      (markerEnd - markerBegin) % kMarkerStride != 0 ||
      (markerCapacity - markerBegin) % kMarkerStride != 0) {
    REX::WARN("Rejected invalid SurfaceMap marker range");
    PublishCache(std::move(next));
    return;
  }

  const auto markerCount =
      static_cast<std::size_t>((markerEnd - markerBegin) / kMarkerStride);
  if (markerCount > kMaximumMarkers) {
    REX::WARN("Rejected implausible SurfaceMap marker count {}", markerCount);
    PublishCache(std::move(next));
    return;
  }
  next.reserve(markerCount);

  std::size_t rejectedRows = 0;
  for (std::size_t index = 0; index < markerCount; ++index) {
    const auto *marker = reinterpret_cast<const std::byte *>(
        markerBegin + index * kMarkerStride);
    const auto handle = ReadAt<std::uint32_t>(marker, kHandleOffset);
    const auto markerType =
        ReadAt<std::uint16_t>(marker, kMarkerTypeOffset);
    const auto ownerBegin = ReadAt<std::uintptr_t>(marker, kOwnerBeginOffset);
    const auto ownerEnd = ReadAt<std::uintptr_t>(marker, kOwnerEndOffset);
    const auto ownerCapacity =
        ReadAt<std::uintptr_t>(marker, kOwnerCapacityOffset);
    if (ownerBegin == ownerEnd) {
      continue;
    }

    const auto rawLocation = ReadAt<std::uint8_t>(marker, kIsLocationOffset);
    const auto rawHasTarget =
        ReadAt<std::uint8_t>(marker, kHasQuestTargetOffset);
    const auto rawActive = ReadAt<std::uint8_t>(marker, kQuestActiveOffset);
    if (rawLocation > 1 || rawHasTarget > 1 || rawActive > 1) {
      ++rejectedRows;
      continue;
    }

    const auto nameText = CopyNativeText(marker, kNameTextOffset);
    const auto extraText = CopyNativeText(marker, kExtraTextOffset);
    const auto questTargetText =
        CopyNativeText(marker, kQuestTargetTextOffset);
    if (!nameText || !extraText || !questTargetText) {
      ++rejectedRows;
      continue;
    }

    MarkerRecord incoming{.markerHandleBits = handle,
                          .markerType = markerType,
                          .isLocation = rawLocation != 0,
                          .hasQuestTarget = rawHasTarget != 0,
                          .questActive = rawActive != 0,
                          .nameText = *nameText,
                          .extraText = *extraText,
                          .questTargetText = *questTargetText};

    if (!ownerBegin || ownerEnd < ownerBegin || ownerCapacity < ownerEnd ||
        (ownerEnd - ownerBegin) % sizeof(std::uint32_t) != 0 ||
        (ownerCapacity - ownerBegin) % sizeof(std::uint32_t) != 0) {
      ++rejectedRows;
      continue;
    }

    const auto ownerCount = static_cast<std::size_t>((ownerEnd - ownerBegin) /
                                                     sizeof(std::uint32_t));
    incoming.rawOwnerCount = ownerCount;
    if (ownerCount == 0 || ownerCount > kMaximumOwners) {
      ++rejectedRows;
      continue;
    }

    bool valid = true;
    std::uint32_t visibleFormID{};
    incoming.owners.reserve(ownerCount);
    for (std::size_t ownerIndex = 0; ownerIndex < ownerCount; ++ownerIndex) {
      const auto formID = ReadAt<std::uint32_t>(
          reinterpret_cast<const void *>(ownerBegin),
          static_cast<std::ptrdiff_t>(ownerIndex * sizeof(std::uint32_t)));
      // For an eligible inactive row, REL 95013 overwrites +0x38 before each
      // append, so the raw final element produced sQuestTargetText. Preserve
      // order across deduplication: Q1,Q2,Q1 visibly represents Q1, not Q2.
      // Active rows are never eligible; a large row must have one raw owner.
      visibleFormID = formID;
      const auto key = a_capture.Resolve(formID);
      if (!key || !ResolveQuest(*key)) {
        valid = false;
        break;
      }
      if (std::ranges::find(incoming.owners, *key) == incoming.owners.end()) {
        incoming.owners.push_back(*key);
      }
    }

    const auto visibleOwner = a_capture.Resolve(visibleFormID);
    if (!valid || incoming.owners.empty() || !visibleOwner ||
        std::ranges::find(incoming.owners, *visibleOwner) ==
            incoming.owners.end()) {
      ++rejectedRows;
      continue;
    }
    incoming.visibleOwner = *visibleOwner;

    next.push_back(std::move(incoming));
  }

  // Publishing a partial generation could let an unreadable row share an
  // exact UI tuple with a retained row and activate the wrong quest. Duplicate
  // handles are valid now; any genuinely rejected nonempty-owner row instead
  // invalidates this whole generation.
  if (rejectedRows != 0) {
    REX::WARN("Rejected entire SurfaceMap ownership generation: {} invalid "
              "nonempty-owner row(s)",
              rejectedRows);
    PublishCache({});
    return;
  }

  std::size_t questTargetLocations = 0;
  std::size_t inactiveQuestTargetLocations = 0;
  std::size_t eligibleQuestTargetLocations = 0;
  std::size_t largeNameplateQuestMarkers = 0;
  std::size_t eligibleLargeNameplateQuestMarkers = 0;
  std::size_t multiOwner = 0;
  for (const auto &marker : next) {
    if (marker.isLocation && marker.hasQuestTarget) {
      ++questTargetLocations;
      if (!marker.questActive) {
        ++inactiveQuestTargetLocations;
        if (marker.visibleOwner) {
          ++eligibleQuestTargetLocations;
        }
      }
    }
    if (marker.markerType == kLargeQuestMarkerType && !marker.isLocation &&
        !marker.hasQuestTarget) {
      ++largeNameplateQuestMarkers;
      if (!marker.questActive && marker.rawOwnerCount == 1 &&
          marker.visibleOwner) {
        ++eligibleLargeNameplateQuestMarkers;
      }
    }
    if (marker.owners.size() != 1) {
      ++multiOwner;
    }
  }
  REX::INFO("Published SurfaceMap ownership generation: markers={}, "
            "questTargetLocations={}, inactiveQuestTargetLocations={}, "
            "eligibleQuestTargetLocations={}, largeNameplates={}, "
            "eligibleLargeNameplates={}, multiOwner={}, rejected={}, "
            "capturedCalls={}, capturedUniqueForms={}, "
            "capturedAmbiguousForms={}",
            next.size(), questTargetLocations, inactiveQuestTargetLocations,
            eligibleQuestTargetLocations, largeNameplateQuestMarkers,
            eligibleLargeNameplateQuestMarkers, multiOwner,
            rejectedRows, a_capture.contributorCalls,
            a_capture.uniqueForms, a_capture.ambiguousForms);
  PublishCache(std::move(next));
}

void BuildAndSnapshot(void *a_surfaceState) noexcept {
  ScopedGatherDepth gatherDepth{surfaceGatherDepth};

  // Recursion is not expected, but every nested depth must remain suppressed
  // and only the true outermost call may reset or publish the TLS generation.
  if (surfaceGatherDepth > 1) {
    if (auto *outerCapture = activeQuestCapture) {
      outerCapture->invalidInvocation = true;
    }
    ScopedQuestCapture suppressCapture{nullptr};
    originalBuildSurfaceMarkers(a_surfaceState);
    return;
  }

  // Make the rebuild window fail closed. A reused handle must never resolve
  // against the preceding generation while native entries are repopulated.
  ResetCache();
  threadQuestCapture.Reset();
  {
    ScopedQuestCapture captureScope{threadQuestCapture};
    originalBuildSurfaceMarkers(a_surfaceState);
  }
  if (threadQuestCapture.overflow || threadQuestCapture.invalidInvocation) {
    REX::ERROR("Rejected SurfaceMap ownership generation: captureOverflow={}, "
               "invalidComposeInvocation={}",
               threadQuestCapture.overflow,
               threadQuestCapture.invalidInvocation);
    return;
  }
  try {
    SnapshotMarkerOwners(a_surfaceState, threadQuestCapture);
  } catch (const std::exception &error) {
    REX::ERROR("SurfaceMap ownership snapshot failed: {}", error.what());
    ResetCache();
  } catch (...) {
    REX::ERROR("SurfaceMap ownership snapshot failed unexpectedly");
    ResetCache();
  }
}

[[nodiscard]] std::optional<QuestKey> ResolveRequest(const Request &a_request) {
  std::scoped_lock lock(cacheMutex);
  if (a_request.markerType > std::numeric_limits<std::uint16_t>::max()) {
    REX::WARN("Rejected out-of-range SurfaceMap marker type {}",
              a_request.markerType);
    return std::nullopt;
  }

  std::vector<QuestKey> uniqueOwners;
  std::size_t matchingRows = 0;
  bool invalidMatchingRow = false;
  for (const auto &marker : markerCache) {
    if (marker.markerHandleBits != a_request.markerHandleBits ||
        marker.markerType !=
            static_cast<std::uint16_t>(a_request.markerType) ||
        marker.isLocation != a_request.isLocation) {
      continue;
    }

    bool exactMatch = false;
    switch (a_request.variant) {
    case MarkerVariant::kQuestTarget:
      exactMatch = marker.hasQuestTarget && !marker.questActive &&
                   marker.questTargetText == a_request.questTargetText;
      break;
    case MarkerVariant::kLargeNameplate:
      exactMatch = a_request.markerType == kLargeQuestMarkerType &&
                   !a_request.isLocation &&
                   marker.markerType == kLargeQuestMarkerType &&
                   !marker.isLocation && !marker.hasQuestTarget &&
                   !marker.questActive &&
                   marker.nameText == a_request.nameText &&
                   marker.extraText == a_request.extraText;
      break;
    default:
      REX::WARN("Rejected SurfaceMap marker request with unknown variant");
      return std::nullopt;
    }
    if (!exactMatch) {
      continue;
    }

    ++matchingRows;
    if (!marker.visibleOwner ||
        (a_request.variant == MarkerVariant::kLargeNameplate &&
         marker.rawOwnerCount != 1)) {
      invalidMatchingRow = true;
      continue;
    }
    if (std::ranges::find(uniqueOwners, *marker.visibleOwner) ==
        uniqueOwners.end()) {
      uniqueOwners.push_back(*marker.visibleOwner);
    }
  }

  if (matchingRows == 0) {
    REX::WARN("Rejected unknown {} SurfaceMap marker tuple: handle=0x{:08X}, "
              "type={}, location={}, nameBytes={}, extraBytes={}, "
              "questTextBytes={}",
              a_request.variant == MarkerVariant::kLargeNameplate
                  ? "large-nameplate"
                  : "quest-target",
              a_request.markerHandleBits, a_request.markerType,
              a_request.isLocation, a_request.nameText.size(),
              a_request.extraText.size(), a_request.questTargetText.size());
    return std::nullopt;
  }

  if (invalidMatchingRow || uniqueOwners.size() != 1) {
    REX::WARN("Rejected ambiguous {} SurfaceMap marker tuple: "
              "handle=0x{:08X}, matchingRows={}, uniqueOwners={}, "
              "invalidMatchingRow={}",
              a_request.variant == MarkerVariant::kLargeNameplate
                  ? "large-nameplate"
                  : "quest-target",
              a_request.markerHandleBits, matchingRows, uniqueOwners.size(),
              invalidMatchingRow);
    return std::nullopt;
  }

  const auto owner = uniqueOwners.front();
  REX::INFO("Resolved {} SurfaceMap marker 0x{:08X}: visible quest "
            "0x{:08X}, instance={}, matchingRows={}",
            a_request.variant == MarkerVariant::kLargeNameplate
                ? "large-nameplate"
                : "quest-target",
            a_request.markerHandleBits, owner.formID, owner.instanceID,
            matchingRows);
  return owner;
}

void ActivateOnMainThread(const QuestKey a_key) noexcept {
  // REL 91440 toggles bit 11, so this second check is mandatory. It
  // also makes duplicate mouse/Select requests harmless.
  if (!IsInactiveTrackableQuest(a_key)) {
    REX::WARN("Skipped stale/already-active quest 0x{:08X}, instance={}",
              a_key.formID, a_key.instanceID);
    return;
  }

  static REL::Relocation<ToggleTracking> toggleTracking{kToggleTrackingID};
  toggleTracking(std::addressof(a_key));

  const auto *quest = ResolveQuest(a_key);
  const bool tracked =
      quest &&
      (ReadAt<std::uint32_t>(quest, kQuestStateOffset) & kQuestTracked) != 0;
  if (tracked) {
    REX::INFO("Tracked SurfaceMap quest 0x{:08X}, instance={}", a_key.formID,
              a_key.instanceID);
    RebuildCurrentSurfaceMap();
  } else {
    REX::WARN("Vanilla helper rejected quest 0x{:08X}, instance={}",
              a_key.formID, a_key.instanceID);
  }
}
} // namespace

bool Install() noexcept {
  try {
    const auto gatherCallsite =
        kSurfaceRebuildID.address() + kQuestGatherCallOffset;
    const auto composeCallsite =
        kQuestGatherID.address() + kQuestComposeCallOffset;
    const auto inputCallsite =
        kStarMapButtonHandlerID.address() + kStarMapInputCallOffset;
    const auto expectedGatherTarget = kQuestGatherID.address();
    const auto expectedComposeTarget = kQuestComposeID.address();
    const auto expectedInputTarget = kVanillaButtonDispatcherID.address();
    constexpr std::array<std::uint8_t, 16> expectedGatherBytes{
        0x48, 0x8B, 0xCF, 0xE8, 0xE4, 0x27, 0x00, 0x00,
        0x48, 0x83, 0xBF, 0xE8, 0x08, 0x00, 0x00, 0x00};
    constexpr std::array<std::uint8_t, 12> expectedComposeBytes{
        0x48, 0x8B, 0x13, 0x48, 0x8D, 0x4D,
        0xC7, 0xE8, 0xD4, 0x00, 0x00, 0x00};
    constexpr std::array<std::uint8_t, 20> expectedInputBytes{
        0x77, 0x0F, 0x84, 0xC0, 0x75, 0x0B, 0x48, 0x8B, 0xD3, 0x49,
        0x8B, 0xCF, 0xE8, 0x6F, 0x8F, 0xE9, 0x00, 0x40, 0x84, 0xED};
    constexpr std::array<std::uint8_t, 16> expectedInputTargetBytes{
        0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57,
        0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
    constexpr std::array<std::uint8_t, 18> expectedGetSurfaceStateBytes{
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8D, 0x99,
        0xF0, 0x11, 0x00, 0x00, 0xC6, 0x44, 0x24, 0x30, 0x03};
    constexpr std::array<std::uint8_t, 26> expectedRefreshSurfaceStateBytes{
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24,
        0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C,
        0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x40};
    if (std::memcmp(reinterpret_cast<const void *>(gatherCallsite - 3),
                    expectedGatherBytes.data(),
                    expectedGatherBytes.size()) != 0) {
      REX::ERROR("SurfaceMap gather-hook signature mismatch at 0x{:X}",
                 gatherCallsite);
      return false;
    }
    if (std::memcmp(reinterpret_cast<const void *>(composeCallsite - 7),
                    expectedComposeBytes.data(),
                    expectedComposeBytes.size()) != 0) {
      REX::ERROR("SurfaceMap compose-hook signature mismatch at 0x{:X}",
                 composeCallsite);
      return false;
    }
    if (std::memcmp(reinterpret_cast<const void *>(inputCallsite - 12),
                    expectedInputBytes.data(), expectedInputBytes.size()) !=
        0) {
      REX::ERROR("Star Map input-hook signature mismatch at 0x{:X}",
                 inputCallsite);
      return false;
    }
    if (std::memcmp(reinterpret_cast<const void *>(expectedInputTarget),
                    expectedInputTargetBytes.data(),
                    expectedInputTargetBytes.size()) != 0) {
      REX::ERROR("Star Map vanilla dispatcher signature mismatch at 0x{:X}",
                 expectedInputTarget);
      return false;
    }

    surfaceRefreshValidated =
        std::memcmp(reinterpret_cast<const void *>(
                        kGetSurfaceMapStateID.address()),
                    expectedGetSurfaceStateBytes.data(),
                    expectedGetSurfaceStateBytes.size()) == 0 &&
        std::memcmp(reinterpret_cast<const void *>(
                        kRefreshSurfaceMapStateID.address()),
                    expectedRefreshSurfaceStateBytes.data(),
                    expectedRefreshSurfaceStateBytes.size()) == 0;
    if (!surfaceRefreshValidated) {
      REX::WARN("Surface Map repaint signatures do not match; quest tracking "
                "will remain enabled but visual refresh is disabled");
    }

    const auto actualGatherTarget = ReadRelativeCallTarget(gatherCallsite);
    if (actualGatherTarget != expectedGatherTarget) {
      REX::ERROR("SurfaceMap gather-hook target mismatch: expected 0x{:X}, "
                 "found 0x{:X}",
                 expectedGatherTarget, actualGatherTarget);
      return false;
    }
    const auto actualComposeTarget = ReadRelativeCallTarget(composeCallsite);
    if (actualComposeTarget != expectedComposeTarget) {
      REX::ERROR("SurfaceMap compose-hook target mismatch: expected 0x{:X}, "
                 "found 0x{:X}",
                 expectedComposeTarget, actualComposeTarget);
      return false;
    }
    const auto actualInputTarget = ReadRelativeCallTarget(inputCallsite);
    if (actualInputTarget != expectedInputTarget) {
      REX::ERROR("Star Map input-hook target mismatch: expected 0x{:X}, "
                 "found 0x{:X}",
                 expectedInputTarget, actualInputTarget);
      return false;
    }

    originalBuildSurfaceMarkers =
        reinterpret_cast<BuildSurfaceMarkers>(expectedGatherTarget);
    originalComposeQuestTarget =
        reinterpret_cast<ComposeQuestTarget>(expectedComposeTarget);
    originalDispatchButtonEvent =
        reinterpret_cast<DispatchButtonEvent>(expectedInputTarget);

    auto &trampoline = REL::GetTrampoline();
    constexpr std::size_t requiredTrampolineBytes = 42;
    if (trampoline.free_size() < requiredTrampolineBytes) {
      REX::ERROR("SurfaceMap hooks require {} trampoline bytes; {} remain",
                 requiredTrampolineBytes, trampoline.free_size());
      return false;
    }

    // Allocate every branch island before touching executable callsites.
    // Once writes begin, any failure or exception restores all three original
    // CALL instructions before plugin load is allowed to fail.
    const auto composeBranch = trampoline.allocate_branch5(
        reinterpret_cast<std::uintptr_t>(CaptureAndComposeQuestTarget));
    const auto gatherBranch = trampoline.allocate_branch5(
        reinterpret_cast<std::uintptr_t>(BuildAndSnapshot));
    const auto inputBranch = trampoline.allocate_branch5(
        reinterpret_cast<std::uintptr_t>(OnStarMapButton));
    const REL::ASM::CALL5 composePatch{composeCallsite, composeBranch};
    const REL::ASM::CALL5 gatherPatch{gatherCallsite, gatherBranch};
    const REL::ASM::CALL5 inputPatch{inputCallsite, inputBranch};

    std::array<std::uint8_t, sizeof(REL::ASM::CALL5)> originalComposeCall{};
    std::array<std::uint8_t, sizeof(REL::ASM::CALL5)> originalGatherCall{};
    std::array<std::uint8_t, sizeof(REL::ASM::CALL5)> originalInputCall{};
    std::memcpy(originalComposeCall.data(),
                reinterpret_cast<const void *>(composeCallsite),
                originalComposeCall.size());
    std::memcpy(originalGatherCall.data(),
                reinterpret_cast<const void *>(gatherCallsite),
                originalGatherCall.size());
    std::memcpy(originalInputCall.data(),
                reinterpret_cast<const void *>(inputCallsite),
                originalInputCall.size());

    const auto matches = [](const std::uintptr_t a_address,
                            const auto &a_bytes) noexcept {
      return std::memcmp(reinterpret_cast<const void *>(a_address),
                         std::addressof(a_bytes), sizeof(a_bytes)) == 0;
    };
    const auto restore = [](const std::uintptr_t a_address,
                            const auto &a_original) noexcept {
      return REL::WriteSafe(a_address, a_original.data(), a_original.size()) &&
             std::memcmp(reinterpret_cast<const void *>(a_address),
                         a_original.data(), a_original.size()) == 0;
    };

    try {
      const bool composeWritten =
          REL::WriteSafeData(composeCallsite, composePatch) &&
          matches(composeCallsite, composePatch) &&
          ReadRelativeCallTarget(composeCallsite) == composeBranch;
      const bool gatherWritten =
          composeWritten && REL::WriteSafeData(gatherCallsite, gatherPatch) &&
          matches(gatherCallsite, gatherPatch) &&
          ReadRelativeCallTarget(gatherCallsite) == gatherBranch;
      const bool inputWritten =
          gatherWritten && REL::WriteSafeData(inputCallsite, inputPatch) &&
          matches(inputCallsite, inputPatch) &&
          ReadRelativeCallTarget(inputCallsite) == inputBranch;
      if (!composeWritten || !gatherWritten || !inputWritten) {
        throw std::runtime_error("one or more hook writes failed verification");
      }
    } catch (...) {
      // Do not short-circuit: attempt every restoration even if one fails.
      const bool inputRestored = restore(inputCallsite, originalInputCall);
      const bool gatherRestored = restore(gatherCallsite, originalGatherCall);
      const bool composeRestored =
          restore(composeCallsite, originalComposeCall);
      if (!inputRestored || !gatherRestored || !composeRestored) {
        REX::FAIL("Could not restore original SurfaceMap/input callsites "
                  "after hook installation failure");
      }
      REX::ERROR("SurfaceMap/input hook transaction failed; all original "
                 "calls restored");
      return false;
    }

    try {
      REX::INFO("Installed transactional SurfaceMap hooks: gather=0x{:X}, "
                "compose=0x{:X}, input=0x{:X}",
                gatherCallsite, composeCallsite, inputCallsite);
    } catch (...) {
      // Logging cannot turn a successfully committed hook set into a reported
      // plugin-load failure.
    }
    return true;
  } catch (const std::exception &error) {
    REX::ERROR("Could not install SurfaceMap/input hooks: {}", error.what());
  } catch (...) {
    REX::ERROR("Could not install SurfaceMap/input hooks");
  }
  return false;
}

void ResetCache() noexcept {
  try {
    PublishCache({});
  } catch (...) {
    // Never unwind into Scaleform or an engine hook.
  }
}

bool TryActivate(const Request &a_request) noexcept {
  try {
    const auto owner = ResolveRequest(a_request);
    if (!owner) {
      return false;
    }
    // ResolveRequest releases cacheMutex before any live form lookup. The
    // queued task repeats this exact check because REL 91440 is a toggle.
    if (!IsInactiveTrackableQuest(*owner)) {
      REX::WARN("Rejected stale/already-active {} SurfaceMap quest "
                "0x{:08X}, instance={} before queue",
                a_request.variant == MarkerVariant::kLargeNameplate
                    ? "large-nameplate"
                    : "quest-target",
                owner->formID, owner->instanceID);
      return false;
    }

    const auto *tasks = SFSE::GetTaskInterface();
    if (!tasks) {
      REX::ERROR("SFSE TaskInterface is unavailable");
      return false;
    }

    tasks->AddTask([key = *owner] { ActivateOnMainThread(key); });
    REX::INFO("Accepted {} SurfaceMap marker 0x{:08X}; queued quest "
              "0x{:08X}, instance={}",
              a_request.variant == MarkerVariant::kLargeNameplate
                  ? "large-nameplate"
                  : "quest-target",
              a_request.markerHandleBits, owner->formID, owner->instanceID);
    return true;
  } catch (const std::exception &error) {
    REX::ERROR("Track Quest request failed: {}", error.what());
  } catch (...) {
    REX::ERROR("Track Quest request failed unexpectedly");
  }
  return false;
}
} // namespace TrackQuestSurface::SurfaceActivation
