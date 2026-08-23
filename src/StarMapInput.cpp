#include "StarMapInput.h"

#include "StarMapSelection.h"

namespace TrackQuestFromMap::StarMapInput
{
	namespace
	{
		constexpr std::string_view kSelectUserEvent = "Select";

		DispatchButtonEvent originalDispatchButtonEvent{};

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

			const auto surfaceVisible = StarMapSelection::SurfaceMapIsVisible(hostRoot);
			if (!surfaceVisible)
			{
				logger::debug(
					"Select release preserved vanilla: Surface Map visibility state unavailable");
				return false;
			}

			const bool consumed =
				StarMapSelection::TryActivateHoveredQuestMarker(hostRoot, *surfaceVisible);
			if (!consumed)
			{
				logger::debug(
					"Select release preserved vanilla: no exact inactive quest request was accepted");
			}
			return consumed;
		}
	}

	void SetOriginalDispatcher(const DispatchButtonEvent a_dispatchButtonEvent) noexcept
	{
		originalDispatchButtonEvent = a_dispatchButtonEvent;
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
