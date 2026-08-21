#pragma once

#include "RE/B/BSInputEventUser.h"

namespace TrackQuestSurface::StarMapInput
{
	using DispatchButtonEvent = void (*)(RE::BSInputEventUser*, const RE::ButtonEvent*);

	void SetOriginalDispatcher(DispatchButtonEvent a_dispatchButtonEvent) noexcept;
	void OnStarMapButton(RE::BSInputEventUser* a_user, const RE::ButtonEvent* a_event) noexcept;
}
