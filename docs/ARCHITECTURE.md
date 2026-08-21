# Architecture

## User contract

On the visible Surface Map, releasing the normal Select control while hovering
an inactive quest marker queues tracking for exactly the quest represented by
that marker. If identity is not exact, the plugin performs no mod action and
passes the event to vanilla once.

## Why this is native

Surface Map Flash data contains visual text, flags, and a marker handle but not
the owning quest FormID/instance pair required by Starfield's tracking helper.
The native marker rows retain owner FormIDs that are dropped before the data is
sent to Flash. The plugin bridges those two views without modifying a SWF.

## Ownership capture

The unreleased development refactor preserves version 0.2.2's transactional
Surface Map gather and quest-composition hooks while tightening the capture
boundary. The inner composition callback executes while the
engine holds a PlayerCharacter-owned `BSSpinLock`; it copies only bounded
primitive FormID/instance pairs into fixed-capacity thread-local storage. It
does not allocate, log, look up forms, touch UI, or retain engine pointers in
that callback.

The engine releases that lock before gather returns. The outer hook then runs
synchronously on the Surface Map state's owner/UI thread, before the vanilla
caller resumes and walks the same marker vector. Unlike the immutable 0.2.2
release, the refactor first copies every relevant
native row into plugin-owned storage without retaining a pointer or view. Only
after that copy completes does it resolve forms, log, validate, and publish the
generation:

- handle, type, location/target/active flags;
- the representation-specific raw label fields; and
- exact quest owner keys.

Rows are kept individually because marker handles are not identities and can
repeat.

This lifetime is same-thread and call-path bounded; it is not a claim that the
marker vector is mutex-protected or generically thread-safe. Rebuild, refresh,
state transition, and destruction invalidate its native storage.

## Marker representations

### Ordinary quest overlay

An ordinary location marker carries `bHasQuestTarget=true`. Its small quest
overlay displays `sQuestTargetText`, and the inactive row's final contributor
is the owner represented by that exact label.

### Large standalone quest marker

A standalone quest marker uses type `0x48`, `bIsLocation=false`, and
`bHasQuestTarget=false`. It displays `sNameText` plus `sExtraText` through the
nameplate path. Such rows can share a sentinel handle, so 0.2.2 matches the
full representation/type/location/text tuple and requires exactly one owner.

Text remains a byte-for-byte, within-generation discriminator. It is never
parsed and never converted into a quest ID.

## Input and GFx inspection

The third transactional hook wraps the reviewed Star Map call to the generic
user-event dispatcher. It acts only on a finite Select release and only while
`SurfaceMap_mc` is visibly active.

The resolver follows the public Surface Map hierarchy to the marker container
and scans its bounded direct children in reverse display-list order. Vanilla
raises the currently hovered marker to the top. All GFx values and string
pointers remain local to that synchronous call and are copied immediately.

The event is consumed only after exact native resolution and successful
main-thread task queueing. Every other path calls the original dispatcher once.

## Activation

The queued task re-resolves the same FormID/instance pair, requires the quest to
be running, not stopped, and not already tracked, then calls Starfield's
reviewed tracking helper. Because the helper toggles rather than sets, the
second inactive check prevents double releases from turning the quest back off.

## Repaint

After tracking is verified, the task reacquires the live Star Map menu and
Surface Map state, validates both vtables, and calls the reviewed Surface Map
refresh handler. Repaint is optional: failure to reacquire or validate it does
not undo successful tracking.

## Reviewed 1.16.244 contracts

- Surface rebuild:
  `RE::ID::StarMap::SurfaceMapState::RebuildSurfaceMarkers` (95000) `+ 0x77`
  to `GatherSurfaceQuestTargets` (95012)
- Quest composition: gather (95012) `+ 0x237` to
  `RE::ID::StarMap::ComposeSurfaceQuestTarget` (95013)
- Star Map input: `RE::ID::StarMap::StarMapMenu::OnButtonEvent` (94684)
  `+ 0x10C` to `RE::ID::IMenu::OnButtonEvent` (130632)
- Tracking helper: `RE::TESQuest::ToggleTracking` (91440)
- Current Surface Map state: `RE::StarMap::StarMapMenu::GetSurfaceMapState`
  (94755)
- Surface Map repaint: `RE::StarMap::SurfaceMapState::Refresh` (95003)
- Primary vtables: `RE::StarMap::StarMapMenu::PRIMARY_VTABLE` (446845) and
  `RE::StarMap::SurfaceMapState::PRIMARY_VTABLE` (447074)

Those reusable APIs, layouts, flags, marker types, and relocation IDs live in
the QTR CommonLibSF fork. The plugin keeps only its chosen callsite offsets and
signatures, the incomplete composition-context offset, GFx member names, and
its matching, caching, and transactional-install policy.

Detailed offsets and the exact executable hash are retained in source and
`MANIFEST.md`. They are not portable contracts.
