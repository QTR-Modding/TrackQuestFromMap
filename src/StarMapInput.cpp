#include "StarMapInput.h"
#include "GalaxyMap.h"
#include "SurfaceMap.h"

namespace TrackQuestFromMap::StarMapInput
{
	namespace
	{
		constexpr auto kLargeQuestMarkerType = RE::StarMap::SurfaceMarkerType::kQuest;
		constexpr std::size_t kMaximumUIChildren = 4096;
		constexpr std::size_t kMaximumMissionDescendants = 128;
		constexpr std::size_t kMaximumMissionDepth = 8;
		constexpr std::size_t kMaximumQuestTargetTextBytes = 4096;
		constexpr std::string_view kSelectUserEvent = "Select";

		DispatchButtonEvent originalDispatchButtonEvent{};

		[[nodiscard]] std::optional<std::uint32_t> ReadGFxUInt(
			const RE::Scaleform::GFx::Value& a_value) noexcept
		{
			if (a_value.IsUInt())
			{
				return a_value.GetUInt();
			}
			if (a_value.IsInt())
			{
				const auto value = a_value.GetInt();
				return value >= 0
					? std::optional{static_cast<std::uint32_t>(value)}
					: std::nullopt;
			}
			if (a_value.IsNumber())
			{
				const double number = a_value.GetNumber();
				if (std::isfinite(number) && number >= 0.0 &&
					std::trunc(number) == number &&
					number <= static_cast<double>(std::numeric_limits<std::uint32_t>::max()))
				{
					return static_cast<std::uint32_t>(number);
				}
			}
			return std::nullopt;
		}

		[[nodiscard]] bool ReadGFxBooleanMember(
			const RE::Scaleform::GFx::Value& a_object,
			const std::string_view a_name,
			bool& a_result)
		{
			if (!a_object.IsObject())
			{
				return false;
			}
			RE::Scaleform::GFx::Value value;
			if (!a_object.GetMember(a_name, std::addressof(value)) || !value.IsBoolean())
			{
				return false;
			}
			a_result = value.GetBoolean();
			return true;
		}

		[[nodiscard]] bool ReadGFxStringMember(
			const RE::Scaleform::GFx::Value& a_object,
			const std::string_view a_name,
			std::string& a_result)
		{
			if (!a_object.IsObject())
			{
				return false;
			}
			RE::Scaleform::GFx::Value value;
			if (!a_object.GetMember(a_name, std::addressof(value)) || !value.IsString())
			{
				return false;
			}

			// GetString may point into a managed GFx value. Copy it while `value` is
			// alive and never retain the pointer beyond this call.
			const auto text = value.GetString();
			if (!text)
			{
				return false;
			}
			std::size_t length = 0;
			while (length <= kMaximumQuestTargetTextBytes && text[length] != '\0')
			{
				++length;
			}
			if (length > kMaximumQuestTargetTextBytes)
			{
				return false;
			}
			a_result.assign(text, length);
			return true;
		}

		[[nodiscard]] std::optional<bool> ReadGFxNestedVisible(
			const RE::Scaleform::GFx::Value& a_object,
			const std::string_view a_memberName)
		{
			if (!a_object.IsObject())
			{
				return std::nullopt;
			}
			RE::Scaleform::GFx::Value nested;
			bool visible{};
			if (!a_object.GetMember(a_memberName, std::addressof(nested)) ||
				!nested.IsObject() ||
				!ReadGFxBooleanMember(nested, "visible", visible))
			{
				return std::nullopt;
			}
			return visible;
		}

		[[nodiscard]] bool ResolveHostRoot(
			const RE::BSInputEventUser* a_user,
			RE::Scaleform::GFx::Value& a_hostRoot)
		{
			if (!a_user)
			{
				logger::debug("Select release preserved vanilla: input recipient unavailable");
				return false;
			}

			const auto ui = RE::UI::GetSingleton();
			if (!ui)
			{
				logger::debug("Select release preserved vanilla: UI singleton unavailable");
				return false;
			}

			const RE::BSFixedString menuName{RE::StarMap::StarMapMenu::MENU_NAME};
			const auto menu = ui->GetMenu(menuName);
			const auto starMapMenu = menu
				? starfield_cast<RE::StarMap::StarMapMenu*>(menu.get())
				: nullptr;
			if (!starMapMenu)
			{
				logger::debug("Select preserved vanilla: Star Map menu unavailable");
				return false;
			}
			if (static_cast<RE::BSInputEventUser*>(starMapMenu) != a_user)
			{
				logger::debug("Select preserved vanilla: input recipient mismatch");
				return false;
			}
			if (!starMapMenu->uiMovie || !starMapMenu->uiMovie->asMovieRoot)
			{
				logger::debug("Select preserved vanilla: Star Map movie unavailable");
				return false;
			}

			const char* rootPath = starMapMenu->GetRootPath();
			const auto movieRoot = starMapMenu->uiMovie->asMovieRoot.get();
			if (!rootPath || !movieRoot->GetVariable(std::addressof(a_hostRoot), rootPath) ||
				!a_hostRoot.IsObject())
			{
				logger::debug("Select release preserved vanilla: GalaxyStarMapMenu root unavailable");
				return false;
			}
			return true;
		}

		[[nodiscard]] std::optional<bool> SurfaceMapIsVisible(
			const RE::Scaleform::GFx::Value& a_hostRoot)
		{
			RE::Scaleform::GFx::Value surfaceMap;
			bool visible{};
			if (!a_hostRoot.GetMember("SurfaceMap_mc", std::addressof(surfaceMap)) ||
				!surfaceMap.IsObject() ||
				!ReadGFxBooleanMember(surfaceMap, "visible", visible))
			{
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
				!markers.IsObject())
			{
				logger::debug(
					"Select release preserved vanilla: public SurfaceMap_mc.Map_mc.MarkersContainer_mc path unavailable");
				return std::nullopt;
			}

			bool surfaceMapVisible{};
			if (!ReadGFxBooleanMember(surfaceMap, "visible", surfaceMapVisible) || !surfaceMapVisible)
			{
				logger::debug("Select release preserved vanilla: SurfaceMap_mc is not visibly active");
				return std::nullopt;
			}

			RE::Scaleform::GFx::Value childCountValue;
			if (!markers.GetMember("numChildren", std::addressof(childCountValue)))
			{
				logger::debug("Select release preserved vanilla: marker child count unavailable");
				return std::nullopt;
			}
			const auto childCountValueUnsigned = ReadGFxUInt(childCountValue);
			if (!childCountValueUnsigned || *childCountValueUnsigned > kMaximumUIChildren)
			{
				logger::debug(
					"Select release preserved vanilla: invalid marker child count (maximum={})",
					kMaximumUIChildren);
				return std::nullopt;
			}

			const auto childCount = static_cast<std::size_t>(*childCountValueUnsigned);
			for (std::size_t reverseIndex = childCount; reverseIndex > 0; --reverseIndex)
			{
				const auto index = reverseIndex - 1;
				RE::Scaleform::GFx::Value child;
				RE::Scaleform::GFx::Value childIndex{static_cast<std::uint32_t>(index)};
				if (!markers.Invoke(
					"getChildAt",
					std::addressof(child),
					std::addressof(childIndex),
					1))
				{
					logger::debug("Select release preserved vanilla: getChildAt({}) failed", index);
					return std::nullopt;
				}
				if (!child.IsObject())
				{
					continue;
				}

				bool childVisible{};
				if (!ReadGFxBooleanMember(child, "visible", childVisible) || !childVisible)
				{
					continue;
				}

				const bool questTargetVisible =
					ReadGFxNestedVisible(child, "QuestTargetText_mc").value_or(false);
				const bool nameplateVisible =
					ReadGFxNestedVisible(child, "Nameplate_mc").value_or(false);
				if (!questTargetVisible && !nameplateVisible)
				{
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
					!ReadGFxBooleanMember(child, "IsLocation", isLocation))
				{
					logger::debug(
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
					!markerData.GetMember("iMarkerType", std::addressof(markerTypeValue)))
				{
					logger::debug(
						"Select release preserved vanilla: topmost hovered marker has invalid identity metadata (index={})",
						index);
					return std::nullopt;
				}

				const auto handle = ReadGFxUInt(handleValue);
				const auto markerType = ReadGFxUInt(markerTypeValue);
				if (!handle || !markerType)
				{
					logger::debug(
						"Select release preserved vanilla: topmost hovered marker has non-integral identity metadata (index={})",
						index);
					return std::nullopt;
				}

				SurfaceMap::Request candidate{};
				candidate.markerHandleBits = *handle;
				candidate.markerType = *markerType;
				candidate.isLocation = isLocation;
				const bool largeNameplate =
					nameplateVisible &&
					*markerType == static_cast<std::uint16_t>(kLargeQuestMarkerType) &&
					!isLocation && !hasQuestTarget && !hasActiveQuest;
				const bool smallQuestTarget =
					questTargetVisible && hasQuestTarget && !hasActiveQuest;

				if (largeNameplate)
				{
					candidate.variant = SurfaceMap::MarkerVariant::kLargeNameplate;
					if (!ReadGFxStringMember(markerData, "sNameText", candidate.nameText) ||
						!ReadGFxStringMember(markerData, "sExtraText", candidate.extraText))
					{
						logger::debug(
							"Select release preserved vanilla: large marker has invalid name/extra text (index={})",
							index);
						return std::nullopt;
					}
				}
				else if (smallQuestTarget)
				{
					candidate.variant = SurfaceMap::MarkerVariant::kQuestTarget;
					if (!ReadGFxStringMember(
						markerData,
						"sQuestTargetText",
						candidate.questTargetText))
					{
						logger::debug(
							"Select release preserved vanilla: quest marker has invalid target text (index={})",
							index);
						return std::nullopt;
					}
				}
				else
				{
					logger::debug(
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

				logger::debug(
					"Select release resolved topmost {} marker: handle=0x{:08X}, type={}, index={}, children={}, nameBytes={}, extraBytes={}, questTextBytes={}",
					candidate.variant == SurfaceMap::MarkerVariant::kLargeNameplate
						? "large-nameplate"
						: "quest-target",
					candidate.markerHandleBits,
					candidate.markerType,
					index,
					childCount,
					candidate.nameText.size(),
					candidate.extraText.size(),
					candidate.questTargetText.size());
				return candidate;
			}

			logger::debug(
				"Select release preserved vanilla: no visible hovered marker label among {} direct children",
				childCount);
			return std::nullopt;
		}

		[[nodiscard]] std::optional<std::size_t> ReadDisplayChildCount(
			const RE::Scaleform::GFx::Value& a_object)
		{
			RE::Scaleform::GFx::Value countValue;
			if (!a_object.IsObject() ||
				!a_object.GetMember("numChildren", std::addressof(countValue)))
			{
				return std::nullopt;
			}
			const auto count = ReadGFxUInt(countValue);
			if (!count || *count > kMaximumUIChildren)
			{
				return std::nullopt;
			}
			return *count;
		}

		[[nodiscard]] bool ReadQuestNameplateText(
			const RE::Scaleform::GFx::Value& a_missionContainer,
			std::string& a_result)
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

		[[nodiscard]] bool ReadQuestNameplateSelected(
			const RE::Scaleform::GFx::Value& a_missionContainer,
			bool& a_result)
		{
			RE::Scaleform::GFx::Value questNameplate;
			RE::Scaleform::GFx::Value nameplateBase;
			std::string currentLabel;
			if (!a_missionContainer.GetMember("Nameplate_mc", std::addressof(questNameplate)) ||
				!questNameplate.IsObject() ||
				!questNameplate.GetMember("Nameplate_mc", std::addressof(nameplateBase)) ||
				!nameplateBase.IsObject() ||
				!ReadGFxStringMember(nameplateBase, "currentLabel", currentLabel))
			{
				return false;
			}
			a_result = currentLabel == "system_selected";
			return true;
		}

		struct MissionSelection
		{
			bool inactive{};
			std::string questTargetText;
		};

		[[nodiscard]] bool FindHighlightedMissionIcon(
			RE::Scaleform::GFx::Value& a_object,
			const std::size_t a_depth,
			std::size_t& a_visited,
			std::optional<MissionSelection>& a_selection)
		{
			if (!a_object.IsObject() || a_depth > kMaximumMissionDepth ||
				++a_visited > kMaximumMissionDescendants)
			{
				return false;
			}
			bool objectVisible{};
			if (!ReadGFxBooleanMember(a_object, "visible", objectVisible))
			{
				return false;
			}
			if (!objectVisible)
			{
				return true;
			}

			RE::Scaleform::GFx::Value inactiveIcon;
			if (a_object.GetMember("ObjectiveAtPOIInactive_mc", std::addressof(inactiveIcon)))
			{
				RE::Scaleform::GFx::Value activeIcon;
				bool inactiveVisible{};
				bool activeVisible{};
				if (!inactiveIcon.IsObject() ||
					!a_object.GetMember("ObjectiveAtPOI_mc", std::addressof(activeIcon)) ||
					!activeIcon.IsObject() ||
					!ReadGFxBooleanMember(inactiveIcon, "visible", inactiveVisible) ||
					!ReadGFxBooleanMember(activeIcon, "visible", activeVisible))
				{
					return false;
				}
				bool highlighted{};
				if (!ReadQuestNameplateSelected(a_object, highlighted))
				{
					return false;
				}
				if (!highlighted)
				{
					return true;
				}
				if (inactiveVisible == activeVisible || a_selection)
				{
					return false;
				}

				std::string questTargetText;
				if (!ReadQuestNameplateText(a_object, questTargetText) ||
					questTargetText.empty())
				{
					return false;
				}
				a_selection = MissionSelection{
					.inactive = inactiveVisible,
					.questTargetText = std::move(questTargetText)
				};
				return true;
			}

			const auto childCount = ReadDisplayChildCount(a_object);
			if (!childCount)
			{
				return true;
			}
			for (std::size_t reverseIndex = *childCount; reverseIndex > 0; --reverseIndex)
			{
				RE::Scaleform::GFx::Value child;
				RE::Scaleform::GFx::Value childIndex{
					static_cast<std::uint32_t>(reverseIndex - 1)
				};
				if (!a_object.Invoke(
					"getChildAt",
					std::addressof(child),
					std::addressof(childIndex),
					1))
				{
					return false;
				}
				if (!child.IsObject())
				{
					continue;
				}
				if (!FindHighlightedMissionIcon(
						child,
						a_depth + 1,
						a_visited,
						a_selection))
				{
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] std::optional<GalaxyMap::Request> FindHoveredGalaxyQuestMarker(
			RE::Scaleform::GFx::Value& a_hostRoot)
		{
			RE::Scaleform::GFx::Value markersRoot;
			RE::Scaleform::GFx::Value systemMarkers;
			RE::Scaleform::GFx::Value bodyMarkers;
			bool markersVisible{};
			bool systemVisible{};
			bool bodyVisible{};
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
				systemVisible == bodyVisible)
			{
				logger::debug(
					"Select release preserved vanilla: Galaxy/System marker containers are unavailable or ambiguous");
				return std::nullopt;
			}

			const auto view = systemVisible ? GalaxyMap::View::kGalaxy : GalaxyMap::View::kSystem;
			auto& container = systemVisible ? systemMarkers : bodyMarkers;
			const auto childCount = ReadDisplayChildCount(container);
			if (!childCount)
			{
				logger::debug("Select release preserved vanilla: Galaxy/System marker count unavailable");
				return std::nullopt;
			}

			std::optional<GalaxyMap::Request> selectedRequest;
			bool highlightedMarkerFound = false;
			for (std::size_t reverseIndex = *childCount; reverseIndex > 0; --reverseIndex)
			{
				const auto index = reverseIndex - 1;
				RE::Scaleform::GFx::Value marker;
				RE::Scaleform::GFx::Value markerIndex{static_cast<std::uint32_t>(index)};
				if (!container.Invoke(
					"getChildAt",
					std::addressof(marker),
					std::addressof(markerIndex),
					1))
				{
					return std::nullopt;
				}
				if (!marker.IsObject())
				{
					continue;
				}

				bool markerVisible{};
				if (!ReadGFxBooleanMember(marker, "visible", markerVisible) || !markerVisible)
				{
					continue;
				}

				std::size_t visited{};
				std::optional<MissionSelection> missionSelection;
				if (!FindHighlightedMissionIcon(
					marker,
					0,
					visited,
					missionSelection))
				{
					logger::debug(
						"Select release preserved vanilla: invalid Galaxy/System mission-icon tree (index={})",
						index);
					return std::nullopt;
				}
				if (!missionSelection)
				{
					continue;
				}
				if (highlightedMarkerFound)
				{
					logger::debug(
						"Select release preserved vanilla: multiple highlighted Galaxy/System quest markers");
					return std::nullopt;
				}
				highlightedMarkerFound = true;
				if (!missionSelection->inactive)
				{
					continue;
				}

				RE::Scaleform::GFx::Value bodyIDValue;
				if (!marker.GetMember("bodyID", std::addressof(bodyIDValue)))
				{
					return std::nullopt;
				}
				const auto bodyID = ReadGFxUInt(bodyIDValue);
				if (!bodyID || *bodyID == 0)
				{
					return std::nullopt;
				}

				selectedRequest = GalaxyMap::Request{
					.view = view,
					.markerLocationID = *bodyID,
					.questTargetText = std::move(missionSelection->questTargetText)
				};
			}

			if (selectedRequest)
			{
				logger::debug(
					"Select release resolved highlighted {} mission icon: markerID={}, labelBytes={}",
					view == GalaxyMap::View::kGalaxy ? "Galaxy" : "System",
					selectedRequest->markerLocationID,
					selectedRequest->questTargetText.size());
				return selectedRequest;
			}

			logger::debug(
				"Select release preserved vanilla: no uniquely highlighted inactive mission icon among {} visible markers",
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
				a_event->disabled)
			{
				return false;
			}

			const auto& userEvent = a_event->QUserEvent();
			return std::string_view{userEvent.c_str(), userEvent.length()} == kSelectUserEvent;
		}
	}

	void SetOriginalDispatcher(const DispatchButtonEvent a_dispatchButtonEvent) noexcept
	{
		originalDispatchButtonEvent = a_dispatchButtonEvent;
	}

	namespace
	{
		[[nodiscard]] bool TryHandleStarMapSelect(
			const RE::BSInputEventUser* a_user,
			const RE::ButtonEvent* a_event)
		{
			if (!IsExactSelectRelease(a_event))
			{
				return false;
			}

			RE::Scaleform::GFx::Value hostRoot;
			if (!ResolveHostRoot(a_user, hostRoot))
			{
				return false;
			}

			const auto surfaceVisible = SurfaceMapIsVisible(hostRoot);
			if (!surfaceVisible)
			{
				logger::debug(
					"Select release preserved vanilla: Surface Map visibility state unavailable");
				return false;
			}

			bool consumed;
			if (*surfaceVisible)
			{
				const auto request = FindHoveredSurfaceQuestMarker(hostRoot);
				consumed = request && SurfaceMap::TryActivate(*request);
			}
			else
			{
				const auto request = FindHoveredGalaxyQuestMarker(hostRoot);
				consumed = request && GalaxyMap::TryActivate(*request);
			}
			if (!consumed)
			{
				logger::debug(
					"Select release preserved vanilla: no exact inactive quest request was accepted");
			}
			return consumed;
		}
	}

	// Exact ABI at StarMapMenu::OnButtonEvent + 0x10C: RCX is the
	// BSInputEventUser subobject and RDX is the ButtonEvent.
	void OnStarMapButton(
		RE::BSInputEventUser* a_user,
		const RE::ButtonEvent* a_event) noexcept
	{
		bool consumed = false;
		try
		{
			consumed = TryHandleStarMapSelect(a_user, a_event);
		}
		catch (const std::exception& error)
		{
			logger::error("Star Map Select resolver failed; preserving vanilla: {}", error.what());
		}

		if (consumed)
		{
			const_cast<RE::ButtonEvent*>(a_event)->status = RE::InputEvent::Status::kStop;
			logger::debug("Consumed Star Map Select");
			return;
		}

		if (!originalDispatchButtonEvent)
		{
			logger::critical("Star Map input hook has no vanilla dispatcher");
			std::terminate();
		}
		originalDispatchButtonEvent(a_user, a_event);
	}
}
