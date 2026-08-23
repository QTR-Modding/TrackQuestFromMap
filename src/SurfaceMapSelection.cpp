#include "SurfaceMapSelection.h"

#include "StarMapSelectionUtil.h"
#include "SurfaceMap.h"

namespace TrackQuestFromMap::SurfaceMapSelection
{
	namespace
	{
		constexpr auto kLargeQuestMarkerType = RE::StarMap::SurfaceMarkerType::kQuest;

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
				!StarMapSelection::Detail::ReadGFxBooleanMember(nested, "visible", visible))
			{
				return std::nullopt;
			}
			return visible;
		}

		[[nodiscard]] std::optional<SurfaceMap::Request> FindHoveredQuestMarker(
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
			if (!StarMapSelection::Detail::ReadGFxBooleanMember(
					surfaceMap,
					"visible",
					surfaceMapVisible) ||
				!surfaceMapVisible)
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
			const auto childCountValueUnsigned =
				StarMapSelection::Detail::ReadGFxUInt(childCountValue);
			if (!childCountValueUnsigned ||
				*childCountValueUnsigned > StarMapSelection::Detail::kMaximumUIChildren)
			{
				logger::debug(
					"Select release preserved vanilla: invalid marker child count (maximum={})",
					StarMapSelection::Detail::kMaximumUIChildren);
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
				if (!StarMapSelection::Detail::ReadGFxBooleanMember(
						child,
						"visible",
						childVisible) ||
					!childVisible)
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
				if (!StarMapSelection::Detail::ReadGFxBooleanMember(
						child,
						"hasQuestTarget",
						hasQuestTarget) ||
					!StarMapSelection::Detail::ReadGFxBooleanMember(
						child,
						"hasActiveQuest",
						hasActiveQuest) ||
					!StarMapSelection::Detail::ReadGFxBooleanMember(
						child,
						"IsLocation",
						isLocation))
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

				const auto handle = StarMapSelection::Detail::ReadGFxUInt(handleValue);
				const auto markerType = StarMapSelection::Detail::ReadGFxUInt(markerTypeValue);
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
					if (!StarMapSelection::Detail::ReadGFxStringMember(
							markerData,
							"sNameText",
							candidate.nameText) ||
						!StarMapSelection::Detail::ReadGFxStringMember(
							markerData,
							"sExtraText",
							candidate.extraText))
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
					if (!StarMapSelection::Detail::ReadGFxStringMember(
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
	}

	std::optional<bool> IsVisible(const RE::Scaleform::GFx::Value& a_hostRoot)
	{
		RE::Scaleform::GFx::Value surfaceMap;
		bool visible{};
		if (!a_hostRoot.GetMember("SurfaceMap_mc", std::addressof(surfaceMap)) ||
			!surfaceMap.IsObject() ||
			!StarMapSelection::Detail::ReadGFxBooleanMember(
				surfaceMap,
				"visible",
				visible))
		{
			return std::nullopt;
		}
		return visible;
	}

	bool TryActivateHoveredQuestMarker(RE::Scaleform::GFx::Value& a_hostRoot)
	{
		const auto request = FindHoveredQuestMarker(a_hostRoot);
		return request && SurfaceMap::TryActivate(*request);
	}
}
