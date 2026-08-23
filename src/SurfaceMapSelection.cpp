#include "StarMapSelectionUtil.h"
#include "SurfaceMap.h"

namespace TrackQuestFromMap::SurfaceMapSelection
{
	namespace
	{
		constexpr auto kLargeQuestMarkerType = RE::StarMap::SurfaceMarkerType::kQuest;

		struct CursorPosition
		{
			double x{};
			double y{};
		};

		struct SelectedQuestMarker
		{
			SurfaceMap::Request request;
			RE::Scaleform::GFx::Value displayObject;
		};

		[[nodiscard]] std::optional<double> ReadGFxNumber(
			const RE::Scaleform::GFx::Value& a_value) noexcept
		{
			if (a_value.IsNumber())
			{
				const double value = a_value.GetNumber();
				return std::isfinite(value) ? std::optional{value} : std::nullopt;
			}
			if (a_value.IsInt())
			{
				return static_cast<double>(a_value.GetInt());
			}
			if (a_value.IsUInt())
			{
				return static_cast<double>(a_value.GetUInt());
			}
			return std::nullopt;
		}

		[[nodiscard]] std::optional<CursorPosition> ReadCursorPosition(
			const RE::Scaleform::GFx::Value& a_displayObject)
		{
			RE::Scaleform::GFx::Value stage;
			RE::Scaleform::GFx::Value mouseXValue;
			RE::Scaleform::GFx::Value mouseYValue;
			if (!a_displayObject.IsObject() ||
				!a_displayObject.GetMember("stage", std::addressof(stage)) ||
				!stage.IsObject() ||
				!stage.GetMember("mouseX", std::addressof(mouseXValue)) ||
				!stage.GetMember("mouseY", std::addressof(mouseYValue)))
			{
				return std::nullopt;
			}

			const auto mouseX = ReadGFxNumber(mouseXValue);
			const auto mouseY = ReadGFxNumber(mouseYValue);
			if (!mouseX || !mouseY)
			{
				return std::nullopt;
			}
			return CursorPosition{.x = *mouseX, .y = *mouseY};
		}

		[[nodiscard]] std::optional<bool> HitTest(
			RE::Scaleform::GFx::Value& a_displayObject,
			const CursorPosition& a_cursor)
		{
			std::array<RE::Scaleform::GFx::Value, 3> arguments{
				RE::Scaleform::GFx::Value{a_cursor.x},
				RE::Scaleform::GFx::Value{a_cursor.y},
				RE::Scaleform::GFx::Value{true}
			};
			RE::Scaleform::GFx::Value result;
			if (!a_displayObject.Invoke(
					"hitTestPoint",
					std::addressof(result),
					arguments.data(),
					arguments.size()) ||
				!result.IsBoolean())
			{
				return std::nullopt;
			}
			return result.GetBoolean();
		}

		[[nodiscard]] bool IgnoresMouseInput(
			const RE::Scaleform::GFx::Value& a_displayObject)
		{
			bool mouseEnabled{};
			bool mouseChildren{};
			return StarMapSelection::Detail::ReadGFxBooleanMember(
					a_displayObject,
					"mouseEnabled",
					mouseEnabled) &&
				StarMapSelection::Detail::ReadGFxBooleanMember(
					a_displayObject,
					"mouseChildren",
					mouseChildren) &&
				!mouseEnabled && !mouseChildren;
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
				!StarMapSelection::Detail::ReadGFxBooleanMember(nested, "visible", visible))
			{
				return std::nullopt;
			}
			return visible;
		}

		[[nodiscard]] std::optional<SelectedQuestMarker> FindHoveredQuestMarker(
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

			const auto cursor = ReadCursorPosition(markers);
			if (!cursor)
			{
				logger::debug(
					"Select release preserved vanilla: Surface Map cursor position unavailable");
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
					!childVisible || IgnoresMouseInput(child))
				{
					continue;
				}

				const auto hit = HitTest(child, *cursor);
				if (!hit)
				{
					logger::debug(
						"Select release preserved vanilla: hitTestPoint failed (index={})",
						index);
					return std::nullopt;
				}
				if (!*hit)
				{
					continue;
				}

				const bool questTargetVisible =
					ReadGFxNestedVisible(child, "QuestTargetText_mc").value_or(false);
				const bool nameplateVisible =
					ReadGFxNestedVisible(child, "Nameplate_mc").value_or(false);
				if (!questTargetVisible && !nameplateVisible)
				{
					logger::debug(
						"Select release preserved vanilla: topmost hit Surface Map object is not a quest marker (index={})",
						index);
					return std::nullopt;
				}

				// SurfaceMarkerContainer moves hovered markers to the top of the display
				// list. Reverse order plus a stage-coordinate hit test rejects stale
				// labels and preserves vanilla clicks on waypoints and other markers.
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
				return SelectedQuestMarker{
					.request = std::move(candidate),
					.displayObject = std::move(child)
				};
			}

			logger::debug(
				"Select release preserved vanilla: no Surface Map object under the cursor among {} direct children",
				childCount);
			return std::nullopt;
		}

		void ShowQuestMarkerAsActive(SelectedQuestMarker& a_selection)
		{
			RE::Scaleform::GFx::Value markerData;
			const RE::Scaleform::GFx::Value active{true};
			if (!a_selection.displayObject.GetMember(
					"MarkerData",
					std::addressof(markerData)) ||
				!markerData.IsObject() ||
				!markerData.SetMember("bQuestActive", active))
			{
				logger::debug("Queued Surface Map quest marker could not update its active state");
				return;
			}

			if (a_selection.request.markerType ==
					static_cast<std::uint16_t>(kLargeQuestMarkerType) &&
				!a_selection.request.isLocation)
			{
				RE::Scaleform::GFx::Value argument{true};
				if (!a_selection.displayObject.Invoke("ClearLocation") ||
					!a_selection.displayObject.Invoke(
						"SetQuestLocation",
						nullptr,
						std::addressof(argument),
						1))
				{
					logger::debug("Queued Surface Map quest marker could not repaint its icon");
				}
				return;
			}

			RE::Scaleform::GFx::Value objective;
			if (!a_selection.displayObject.GetMember(
					"ObjectiveAtPOI_mc",
					std::addressof(objective)) ||
				!objective.IsDisplayObject() ||
				!objective.GotoAndStop("Active"))
			{
				logger::debug("Queued Surface Map quest marker could not repaint its objective");
			}
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
		auto selection = FindHoveredQuestMarker(a_hostRoot);
		if (!selection || !SurfaceMap::TryActivate(selection->request))
		{
			return false;
		}

		ShowQuestMarkerAsActive(*selection);
		return true;
	}
}
