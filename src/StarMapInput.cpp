#include "PCH.h"

#include "StarMapInput.h"

#include "GalaxyMap.h"
#include "SurfaceMap.h"

namespace TrackQuestSurface::StarMapInput
{
	namespace
	{
		constexpr auto             kLargeQuestMarkerType = RE::StarMap::SurfaceMarkerType::kQuest;
		constexpr std::size_t      kMaximumUIChildren = 4096;
		constexpr std::size_t      kMaximumMissionDescendants = 128;
		constexpr std::size_t      kMaximumMissionDepth = 8;
		constexpr std::size_t      kMaximumQuestTargetTextBytes = 4096;
		constexpr std::string_view kSelectUserEvent = "Select";

		DispatchButtonEvent originalDispatchButtonEvent{};

		[[nodiscard]] std::optional<std::uint32_t> ReadGFxUInt(
			const RE::Scaleform::GFx::Value& a_value) noexcept
		{
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
					number <= static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
					return static_cast<std::uint32_t>(number);
				}
			}
			return std::nullopt;
		}

		[[nodiscard]] bool ReadGFxBooleanMember(
			const RE::Scaleform::GFx::Value& a_object,
			const std::string_view           a_name,
			bool&                            a_result)
		{
			if (!a_object.IsObject()) {
				return false;
			}
			RE::Scaleform::GFx::Value value;
			if (!a_object.GetMember(a_name, std::addressof(value)) || !value.IsBoolean()) {
				return false;
			}
			a_result = value.GetBoolean();
			return true;
		}

		[[nodiscard]] bool ReadGFxStringMember(
			const RE::Scaleform::GFx::Value& a_object,
			const std::string_view           a_name,
			std::string&                     a_result)
		{
			if (!a_object.IsObject()) {
				return false;
			}
			RE::Scaleform::GFx::Value value;
			if (!a_object.GetMember(a_name, std::addressof(value)) || !value.IsString()) {
				return false;
			}

			// GetString may point into a managed GFx value. Copy it while `value` is
			// alive and never retain the pointer beyond this call.
			const auto* text = value.GetString();
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

		[[nodiscard]] std::optional<bool> ReadGFxNestedVisible(
			const RE::Scaleform::GFx::Value& a_object,
			const std::string_view           a_memberName)
		{
			if (!a_object.IsObject()) {
				return std::nullopt;
			}
			RE::Scaleform::GFx::Value nested;
			bool                      visible{};
			if (!a_object.GetMember(a_memberName, std::addressof(nested)) ||
				!nested.IsObject() ||
				!ReadGFxBooleanMember(nested, "visible", visible)) {
				return std::nullopt;
			}
			return visible;
		}

		[[nodiscard]] bool ResolveHostRoot(
			RE::BSInputEventUser*      a_user,
			RE::Scaleform::GFx::Value& a_hostRoot)
		{
			if (!a_user) {
				logger::info("Select release preserved vanilla: input recipient unavailable");
				return false;
			}

			auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				logger::info("Select release preserved vanilla: UI singleton unavailable");
				return false;
			}

			const RE::BSFixedString menuName{ RE::StarMap::StarMapMenu::MENU_NAME.data() };
			auto                    menu = ui->GetMenu(menuName);
			if (!menu) {
				logger::info("Select release preserved vanilla: GalaxyStarMapMenu unavailable");
				return false;
			}
			if (static_cast<RE::BSInputEventUser*>(menu.get()) != a_user) {
				logger::info(
					"Select release preserved vanilla: live GalaxyStarMapMenu does not match input recipient");
				return false;
			}
			if (!menu->uiMovie || !menu->uiMovie->asMovieRoot) {
				logger::info("Select release preserved vanilla: GalaxyStarMapMenu movie unavailable");
				return false;
			}

			const char* rootPath = menu->GetRootPath();
			auto*       movieRoot = menu->uiMovie->asMovieRoot.get();
			if (!rootPath || !movieRoot->GetVariable(std::addressof(a_hostRoot), rootPath) ||
				!a_hostRoot.IsObject()) {
				logger::info("Select release preserved vanilla: GalaxyStarMapMenu root unavailable");
				return false;
			}
			return true;
		}

		[[nodiscard]] std::optional<bool> SurfaceMapIsVisible(
			const RE::Scaleform::GFx::Value& a_hostRoot)
		{
			RE::Scaleform::GFx::Value surfaceMap;
			bool                      visible{};
			if (!a_hostRoot.GetMember("SurfaceMap_mc", std::addressof(surfaceMap)) ||
				!surfaceMap.IsObject() ||
				!ReadGFxBooleanMember(surfaceMap, "visible", visible)) {
				return std::nullopt;
			}
			return visible;
		}

		[[nodiscard]] std::optional<SurfaceMap::Request> FindHoveredSurfaceQuestMarker(
			const RE::Scaleform::GFx::Value& a_hostRoot)
		{
			RE::Scaleform::GFx::Value surfaceMap;
			RE::Scaleform::GFx::Value map;
			RE::Scaleform::GFx::Value markers;
			if (!a_hostRoot.GetMember("SurfaceMap_mc", std::addressof(surfaceMap)) ||
				!surfaceMap.IsObject() ||
				!surfaceMap.GetMember("Map_mc", std::addressof(map)) ||
				!map.IsObject() ||
				!map.GetMember("MarkersContainer_mc", std::addressof(markers)) ||
				!markers.IsObject()) {
				logger::info(
					"Select release preserved vanilla: public SurfaceMap_mc.Map_mc.MarkersContainer_mc path unavailable");
				return std::nullopt;
			}

			bool surfaceMapVisible{};
			if (!ReadGFxBooleanMember(surfaceMap, "visible", surfaceMapVisible) || !surfaceMapVisible) {
				logger::info("Select release preserved vanilla: SurfaceMap_mc is not visibly active");
				return std::nullopt;
			}

			RE::Scaleform::GFx::Value childCountValue;
			if (!markers.GetMember("numChildren", std::addressof(childCountValue))) {
				logger::info("Select release preserved vanilla: marker child count unavailable");
				return std::nullopt;
			}
			const auto childCountValueUnsigned = ReadGFxUInt(childCountValue);
			if (!childCountValueUnsigned || *childCountValueUnsigned > kMaximumUIChildren) {
				logger::info(
					"Select release preserved vanilla: invalid marker child count (maximum={})",
					kMaximumUIChildren);
				return std::nullopt;
			}

			const auto childCount = static_cast<std::size_t>(*childCountValueUnsigned);
			for (std::size_t reverseIndex = childCount; reverseIndex > 0; --reverseIndex) {
				const auto                index = reverseIndex - 1;
				RE::Scaleform::GFx::Value child;
				RE::Scaleform::GFx::Value childIndex{ static_cast<std::uint32_t>(index) };
				if (!markers.Invoke(
						"getChildAt",
						std::addressof(child),
						std::addressof(childIndex),
						1)) {
					logger::info("Select release preserved vanilla: getChildAt({}) failed", index);
					return std::nullopt;
				}
				if (!child.IsObject()) {
					continue;
				}

				bool childVisible{};
				if (!ReadGFxBooleanMember(child, "visible", childVisible) || !childVisible) {
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
					logger::info(
						"Select release preserved vanilla: topmost hovered marker has invalid Boolean metadata (index={})",
						index);
					return std::nullopt;
				}

				RE::Scaleform::GFx::Value handleValue;
				RE::Scaleform::GFx::Value markerData;
				RE::Scaleform::GFx::Value markerTypeValue;
				if (!child.GetMember("handleBits", std::addressof(handleValue)) ||
					!child.GetMember("MarkerData", std::addressof(markerData)) ||
					!markerData.IsObject() ||
					!markerData.GetMember("iMarkerType", std::addressof(markerTypeValue))) {
					logger::info(
						"Select release preserved vanilla: topmost hovered marker has invalid identity metadata (index={})",
						index);
					return std::nullopt;
				}

				const auto handle = ReadGFxUInt(handleValue);
				const auto markerType = ReadGFxUInt(markerTypeValue);
				if (!handle || !markerType) {
					logger::info(
						"Select release preserved vanilla: topmost hovered marker has non-integral identity metadata (index={})",
						index);
					return std::nullopt;
				}

				SurfaceMap::Request candidate{
					.markerHandleBits = *handle,
					.markerType = *markerType,
					.isLocation = isLocation
				};
				const bool largeNameplate =
					nameplateVisible &&
					*markerType == static_cast<std::uint16_t>(kLargeQuestMarkerType) &&
					!isLocation && !hasQuestTarget && !hasActiveQuest;
				const bool smallQuestTarget =
					questTargetVisible && hasQuestTarget && !hasActiveQuest;

				if (largeNameplate) {
					candidate.variant = SurfaceMap::MarkerVariant::kLargeNameplate;
					if (!ReadGFxStringMember(markerData, "sNameText", candidate.nameText) ||
						!ReadGFxStringMember(markerData, "sExtraText", candidate.extraText)) {
						logger::info(
							"Select release preserved vanilla: large marker has invalid name/extra text (index={})",
							index);
						return std::nullopt;
					}
				} else if (smallQuestTarget) {
					candidate.variant = SurfaceMap::MarkerVariant::kQuestTarget;
					if (!ReadGFxStringMember(
							markerData,
							"sQuestTargetText",
							candidate.questTargetText)) {
						logger::info(
							"Select release preserved vanilla: quest marker has invalid target text (index={})",
							index);
						return std::nullopt;
					}
				} else {
					logger::info(
						"Select release preserved vanilla: topmost hovered marker is ineligible (index={}, handle=0x{:08X}, type={}, location={}, questTarget={}, active={}, nameplate={}, questLabel={})",
						index,
						*handle,
						*markerType,
						isLocation,
						hasQuestTarget,
						hasActiveQuest,
						nameplateVisible,
						questTargetVisible);
					return std::nullopt;
				}

				logger::info(
					"Select release resolved topmost {} marker: handle=0x{:08X}, type={}, index={}, children={}, nameBytes={}, extraBytes={}, questTextBytes={}",
					candidate.variant == SurfaceMap::MarkerVariant::kLargeNameplate ?
						"large-nameplate" :
						"quest-target",
					candidate.markerHandleBits,
					candidate.markerType,
					index,
					childCount,
					candidate.nameText.size(),
					candidate.extraText.size(),
					candidate.questTargetText.size());
				return candidate;
			}

			logger::info(
				"Select release preserved vanilla: no visible hovered marker label among {} direct children",
				childCount);
			return std::nullopt;
		}

		[[nodiscard]] std::optional<double> ReadGFxNumberMember(
			const RE::Scaleform::GFx::Value& a_object,
			const std::string_view           a_name)
		{
			RE::Scaleform::GFx::Value value;
			if (!a_object.IsObject() ||
				!a_object.GetMember(a_name, std::addressof(value))) {
				return std::nullopt;
			}
			if (value.IsNumber()) {
				const auto number = value.GetNumber();
				return std::isfinite(number) ? std::optional<double>{ number } : std::nullopt;
			}
			if (value.IsInt()) {
				return static_cast<double>(value.GetInt());
			}
			if (value.IsUInt()) {
				return static_cast<double>(value.GetUInt());
			}
			return std::nullopt;
		}

		[[nodiscard]] std::optional<std::size_t> ReadDisplayChildCount(
			const RE::Scaleform::GFx::Value& a_object)
		{
			RE::Scaleform::GFx::Value countValue;
			if (!a_object.IsObject() ||
				!a_object.GetMember("numChildren", std::addressof(countValue))) {
				return std::nullopt;
			}
			const auto count = ReadGFxUInt(countValue);
			if (!count || *count > kMaximumUIChildren) {
				return std::nullopt;
			}
			return static_cast<std::size_t>(*count);
		}

		[[nodiscard]] bool ReadQuestNameplateText(
			const RE::Scaleform::GFx::Value& a_missionContainer,
			std::string&                     a_result)
		{
			RE::Scaleform::GFx::Value questNameplate;
			RE::Scaleform::GFx::Value nameplateBase;
			RE::Scaleform::GFx::Value textContainer;
			RE::Scaleform::GFx::Value textField;
			return a_missionContainer.GetMember("Nameplate_mc", std::addressof(questNameplate)) &&
			       questNameplate.IsObject() &&
			       questNameplate.GetMember("Nameplate_mc", std::addressof(nameplateBase)) &&
			       nameplateBase.IsObject() &&
			       nameplateBase.GetMember("NameplateText_mc", std::addressof(textContainer)) &&
			       textContainer.IsObject() &&
			       textContainer.GetMember("text_tf", std::addressof(textField)) &&
			       textField.IsObject() &&
			       ReadGFxStringMember(textField, "text", a_result);
		}

		[[nodiscard]] std::optional<bool> HitTestAtStageCursor(
			RE::Scaleform::GFx::Value& a_displayObject)
		{
			RE::Scaleform::GFx::Value stage;
			if (!a_displayObject.IsObject() ||
				!a_displayObject.GetMember("stage", std::addressof(stage)) ||
				!stage.IsObject()) {
				return std::nullopt;
			}

			const auto mouseX = ReadGFxNumberMember(stage, "mouseX");
			const auto mouseY = ReadGFxNumberMember(stage, "mouseY");
			if (!mouseX || !mouseY) {
				return std::nullopt;
			}

			std::array<RE::Scaleform::GFx::Value, 3> arguments{
				RE::Scaleform::GFx::Value{ *mouseX },
				RE::Scaleform::GFx::Value{ *mouseY },
				RE::Scaleform::GFx::Value{ true }
			};
			RE::Scaleform::GFx::Value hit;
			if (!a_displayObject.Invoke(
					"hitTestPoint",
					std::addressof(hit),
					arguments.data(),
					arguments.size()) ||
				!hit.IsBoolean()) {
				return std::nullopt;
			}
			return hit.GetBoolean();
		}

		enum class MissionSearchResult : std::uint8_t
		{
			kNone,
			kMatch,
			kInvalid
		};

		[[nodiscard]] MissionSearchResult FindInactiveMissionIcon(
			RE::Scaleform::GFx::Value& a_object,
			const std::size_t          a_depth,
			std::size_t&               a_visited,
			std::string&               a_questTargetText)
		{
			if (!a_object.IsObject() || a_depth > kMaximumMissionDepth ||
				++a_visited > kMaximumMissionDescendants) {
				return MissionSearchResult::kInvalid;
			}
			bool objectVisible{};
			if (!ReadGFxBooleanMember(a_object, "visible", objectVisible)) {
				return MissionSearchResult::kInvalid;
			}
			if (!objectVisible) {
				return MissionSearchResult::kNone;
			}

			RE::Scaleform::GFx::Value inactiveIcon;
			if (a_object.GetMember("ObjectiveAtPOIInactive_mc", std::addressof(inactiveIcon))) {
				RE::Scaleform::GFx::Value activeIcon;
				bool                      inactiveVisible{};
				bool                      activeVisible{};
				if (!inactiveIcon.IsObject() ||
					!a_object.GetMember("ObjectiveAtPOI_mc", std::addressof(activeIcon)) ||
					!activeIcon.IsObject() ||
					!ReadGFxBooleanMember(inactiveIcon, "visible", inactiveVisible) ||
					!ReadGFxBooleanMember(activeIcon, "visible", activeVisible)) {
					return MissionSearchResult::kInvalid;
				}
				if (!inactiveVisible || activeVisible) {
					return MissionSearchResult::kNone;
				}
				const auto hit = HitTestAtStageCursor(inactiveIcon);
				if (!hit) {
					return MissionSearchResult::kInvalid;
				}
				if (!*hit) {
					return MissionSearchResult::kNone;
				}
				return ReadQuestNameplateText(a_object, a_questTargetText) ?
				           MissionSearchResult::kMatch :
				           MissionSearchResult::kInvalid;
			}

			const auto childCount = ReadDisplayChildCount(a_object);
			if (!childCount) {
				return MissionSearchResult::kNone;
			}
			for (std::size_t reverseIndex = *childCount; reverseIndex > 0; --reverseIndex) {
				RE::Scaleform::GFx::Value child;
				RE::Scaleform::GFx::Value childIndex{
					static_cast<std::uint32_t>(reverseIndex - 1)
				};
				if (!a_object.Invoke(
						"getChildAt",
						std::addressof(child),
						std::addressof(childIndex),
						1)) {
					return MissionSearchResult::kInvalid;
				}
				if (!child.IsObject()) {
					continue;
				}
				const auto result = FindInactiveMissionIcon(
					child,
					a_depth + 1,
					a_visited,
					a_questTargetText);
				if (result != MissionSearchResult::kNone) {
					return result;
				}
			}
			return MissionSearchResult::kNone;
		}

		[[nodiscard]] std::optional<GalaxyMap::Request> FindHoveredGalaxyQuestMarker(
			RE::Scaleform::GFx::Value& a_hostRoot)
		{
			RE::Scaleform::GFx::Value markersRoot;
			RE::Scaleform::GFx::Value systemMarkers;
			RE::Scaleform::GFx::Value bodyMarkers;
			bool                      markersVisible{};
			bool                      systemVisible{};
			bool                      bodyVisible{};
			if (!a_hostRoot.GetMember("Markers_mc", std::addressof(markersRoot)) ||
				!markersRoot.IsObject() ||
				!ReadGFxBooleanMember(markersRoot, "visible", markersVisible) ||
				!markersVisible ||
				!markersRoot.GetMember("SystemMarkerContainer_mc", std::addressof(systemMarkers)) ||
				!systemMarkers.IsObject() ||
				!markersRoot.GetMember("BodyMarkerContainer_mc", std::addressof(bodyMarkers)) ||
				!bodyMarkers.IsObject() ||
				!ReadGFxBooleanMember(systemMarkers, "visible", systemVisible) ||
				!ReadGFxBooleanMember(bodyMarkers, "visible", bodyVisible) ||
				systemVisible == bodyVisible) {
				logger::info(
					"Select release preserved vanilla: Galaxy/System marker containers are unavailable or ambiguous");
				return std::nullopt;
			}

			const auto view = systemVisible ? GalaxyMap::View::kGalaxy : GalaxyMap::View::kSystem;
			auto&      container = systemVisible ? systemMarkers : bodyMarkers;
			const auto childCount = ReadDisplayChildCount(container);
			if (!childCount) {
				logger::info("Select release preserved vanilla: Galaxy/System marker count unavailable");
				return std::nullopt;
			}

			for (std::size_t reverseIndex = *childCount; reverseIndex > 0; --reverseIndex) {
				const auto                index = reverseIndex - 1;
				RE::Scaleform::GFx::Value marker;
				RE::Scaleform::GFx::Value markerIndex{ static_cast<std::uint32_t>(index) };
				if (!container.Invoke(
						"getChildAt",
						std::addressof(marker),
						std::addressof(markerIndex),
						1)) {
					return std::nullopt;
				}
				if (!marker.IsObject()) {
					continue;
				}

				bool markerVisible{};
				if (!ReadGFxBooleanMember(marker, "visible", markerVisible) || !markerVisible) {
					continue;
				}

				std::size_t visited{};
				std::string questTargetText;
				const auto  missionResult = FindInactiveMissionIcon(
					marker,
					0,
					visited,
					questTargetText);
				if (missionResult == MissionSearchResult::kInvalid) {
					logger::info(
						"Select release preserved vanilla: invalid Galaxy/System mission-icon tree (index={})",
						index);
					return std::nullopt;
				}
				if (missionResult != MissionSearchResult::kMatch) {
					continue;
				}

				RE::Scaleform::GFx::Value bodyIDValue;
				if (!marker.GetMember("bodyID", std::addressof(bodyIDValue))) {
					return std::nullopt;
				}
				const auto bodyID = ReadGFxUInt(bodyIDValue);
				if (!bodyID || *bodyID == 0 || questTargetText.empty()) {
					return std::nullopt;
				}

				logger::info(
					"Select release resolved {} mission icon: markerID={}, index={}, labelBytes={}",
					view == GalaxyMap::View::kGalaxy ? "Galaxy" : "System",
					*bodyID,
					index,
					questTargetText.size());
				return GalaxyMap::Request{
					.view = view,
					.markerLocationID = *bodyID,
					.questTargetText = std::move(questTargetText)
				};
			}

			logger::info(
				"Select release preserved vanilla: no inactive mission icon under the cursor in {} visible markers",
				*childCount);
			return std::nullopt;
		}

		[[nodiscard]] bool IsExactSelectRelease(const RE::ButtonEvent* a_event)
		{
			if (!a_event ||
				a_event->eventType != RE::InputEvent::EventType::kButton ||
				a_event->status == RE::InputEvent::Status::kStop ||
				!std::isfinite(a_event->value) ||
				!std::isfinite(a_event->heldDownSecs) ||
				a_event->heldDownSecs < 0.0F ||
				a_event->value != 0.0F ||
				a_event->disabled) {
				return false;
			}

			const auto& userEvent = a_event->QUserEvent();
			return std::string_view{ userEvent.c_str(), userEvent.length() } == kSelectUserEvent;
		}
	}

	void SetOriginalDispatcher(const DispatchButtonEvent a_dispatchButtonEvent) noexcept
	{
		originalDispatchButtonEvent = a_dispatchButtonEvent;
	}

	// Exact ABI at StarMapMenu::OnButtonEvent + 0x10C: RCX is the
	// BSInputEventUser subobject and
	// RDX is the ButtonEvent. IMenu::OnButtonEvent is called exactly once unless a
	// unique marker request was accepted and queued.
	void OnStarMapButton(
		RE::BSInputEventUser*  a_user,
		const RE::ButtonEvent* a_event) noexcept
	{
		bool consumed = false;
		try {
			if (IsExactSelectRelease(a_event)) {
				RE::Scaleform::GFx::Value hostRoot;
				if (ResolveHostRoot(a_user, hostRoot)) {
					const auto surfaceVisible = SurfaceMapIsVisible(hostRoot);
					if (!surfaceVisible) {
						logger::info(
							"Select release preserved vanilla: Surface Map visibility state unavailable");
					} else if (*surfaceVisible) {
						const auto request = FindHoveredSurfaceQuestMarker(hostRoot);
						if (request) {
							consumed = SurfaceMap::TryActivate(*request);
						}
					} else {
						const auto request = FindHoveredGalaxyQuestMarker(hostRoot);
						if (request) {
							consumed = GalaxyMap::TryActivate(*request);
						}
					}
					if (!consumed) {
						logger::info(
							"Select release preserved vanilla: no exact inactive quest request was accepted");
					}
				}
			}
		} catch (const std::exception& error) {
			try {
				logger::error("Star Map Select resolver failed; preserving vanilla: {}", error.what());
			} catch (...) {
			}
		} catch (...) {
			try {
				logger::error("Star Map Select resolver failed unexpectedly; preserving vanilla");
			} catch (...) {
			}
		}

		if (consumed) {
			const_cast<RE::ButtonEvent*>(a_event)->status = RE::InputEvent::Status::kStop;
			try {
				logger::info("Consumed Star Map Select release after queuing quest activation");
			} catch (...) {
			}
			return;
		}

		if (!originalDispatchButtonEvent) {
			try {
				logger::critical("Star Map input hook has no vanilla dispatcher");
			} catch (...) {
			}
			std::terminate();
		}
		originalDispatchButtonEvent(a_user, a_event);
	}
}
