#include "GalaxyMapSelection.h"

#include "GalaxyMap.h"
#include "StarMapSelectionUtil.h"

namespace TrackQuestFromMap::GalaxyMapSelection
{
	namespace
	{
		constexpr std::size_t kMaximumMissionDescendants = 128;
		constexpr std::size_t kMaximumMissionDepth = 8;

		[[nodiscard]] bool ReadQuestNameplateText(
			const RE::Scaleform::GFx::Value& a_missionContainer,
			std::string& a_result)
		{
			RE::Scaleform::GFx::Value questNameplate;
			RE::Scaleform::GFx::Value nameplateBase;
			RE::Scaleform::GFx::Value textContainer;
			RE::Scaleform::GFx::Value textField;
			return a_missionContainer.GetMember(
					"Nameplate_mc",
					std::addressof(questNameplate)) &&
				questNameplate.IsObject() &&
				questNameplate.GetMember("Nameplate_mc", std::addressof(nameplateBase)) &&
				nameplateBase.IsObject() &&
				nameplateBase.GetMember(
					"NameplateText_mc",
					std::addressof(textContainer)) &&
				textContainer.IsObject() &&
				textContainer.GetMember("text_tf", std::addressof(textField)) &&
				textField.IsObject() &&
				StarMapSelection::Detail::ReadGFxStringMember(
					textField,
					"text",
					a_result);
		}

		[[nodiscard]] bool ReadQuestNameplateSelected(
			const RE::Scaleform::GFx::Value& a_missionContainer,
			bool& a_result)
		{
			RE::Scaleform::GFx::Value questNameplate;
			RE::Scaleform::GFx::Value nameplateBase;
			std::string currentLabel;
			if (!a_missionContainer.GetMember(
					"Nameplate_mc",
					std::addressof(questNameplate)) ||
				!questNameplate.IsObject() ||
				!questNameplate.GetMember("Nameplate_mc", std::addressof(nameplateBase)) ||
				!nameplateBase.IsObject() ||
				!StarMapSelection::Detail::ReadGFxStringMember(
					nameplateBase,
					"currentLabel",
					currentLabel))
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
			if (!StarMapSelection::Detail::ReadGFxBooleanMember(
					a_object,
					"visible",
					objectVisible))
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
					!StarMapSelection::Detail::ReadGFxBooleanMember(
						inactiveIcon,
						"visible",
						inactiveVisible) ||
					!StarMapSelection::Detail::ReadGFxBooleanMember(
						activeIcon,
						"visible",
						activeVisible))
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

			const auto childCount = StarMapSelection::Detail::ReadDisplayChildCount(a_object);
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

		[[nodiscard]] std::optional<GalaxyMap::Request> FindHoveredQuestMarker(
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
				!StarMapSelection::Detail::ReadGFxBooleanMember(
					markersRoot,
					"visible",
					markersVisible) ||
				!markersVisible ||
				!markersRoot.GetMember(
					"SystemMarkerContainer_mc",
					std::addressof(systemMarkers)) ||
				!systemMarkers.IsObject() ||
				!markersRoot.GetMember(
					"BodyMarkerContainer_mc",
					std::addressof(bodyMarkers)) ||
				!bodyMarkers.IsObject() ||
				!StarMapSelection::Detail::ReadGFxBooleanMember(
					systemMarkers,
					"visible",
					systemVisible) ||
				!StarMapSelection::Detail::ReadGFxBooleanMember(
					bodyMarkers,
					"visible",
					bodyVisible) ||
				systemVisible == bodyVisible)
			{
				logger::debug(
					"Select release preserved vanilla: Galaxy/System marker containers are unavailable or ambiguous");
				return std::nullopt;
			}

			const auto view = systemVisible ? GalaxyMap::View::kGalaxy : GalaxyMap::View::kSystem;
			auto& container = systemVisible ? systemMarkers : bodyMarkers;
			const auto childCount = StarMapSelection::Detail::ReadDisplayChildCount(container);
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
				if (!StarMapSelection::Detail::ReadGFxBooleanMember(
						marker,
						"visible",
						markerVisible) ||
					!markerVisible)
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
				const auto bodyID = StarMapSelection::Detail::ReadGFxUInt(bodyIDValue);
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
	}

	bool TryActivateHoveredQuestMarker(RE::Scaleform::GFx::Value& a_hostRoot)
	{
		const auto request = FindHoveredQuestMarker(a_hostRoot);
		return request && GalaxyMap::TryActivate(*request);
	}
}
