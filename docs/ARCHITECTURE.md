# Architecture

## User contract

On Surface, Galaxy, or System view, releasing the normal Select control over an
inactive quest glyph queues tracking for exactly the quest represented by that
glyph. If identity is not exact, the plugin performs no mod action and passes
the event to vanilla once.

## Why this is native

Star Map Flash data contains visual text, flags, and map identities but not
the owning quest FormID/instance pair required by Starfield's tracking helper.
Native Surface rows and the shared Galaxy/System quest-target tree retain that
ownership before it is dropped. The plugin bridges those views without
modifying a SWF.

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

### Galaxy/System quest-target tree

Bethesda builds one shared nested tree keyed first by system location and then
by body location. Seven reviewed vanilla callsites invoke the same builder ABI.
The plugin wraps all seven calls so each capture has an explicit start and a
complete post-return publication boundary.

While the builder holds its PlayerCharacter-owned `BSSpinLock`, two inner hooks
copy only fixed-capacity primitive data: system/body IDs, active state, exact
quest FormID/instance, and a bounded copy of the native label. No allocation,
logging, form lookup, UI access, or engine pointer retention occurs there.
Publication into the mutex-protected owned cache happens only after the full
builder returns. Concurrent generations are sequence-gated; input can use only
the latest fully published generation.

The native insertion helper keeps the first target for an existing body key.
Capture therefore records only successful insertions, preserving Bethesda's
actual displayed owner rather than guessing from every contributor.

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

The final transactional hook wraps the reviewed Star Map call to the generic
user-event dispatcher. It acts only on a finite Select release.

The resolver follows the public Surface Map hierarchy to the marker container
and scans its bounded direct children in reverse display-list order. Vanilla
raises the currently hovered marker to the top. All GFx values and string
pointers remain local to that synchronous call and are copied immediately.

The event is consumed only after exact native resolution and successful
main-thread task queueing. Every other path calls the original dispatcher once.

When `SurfaceMap_mc` is not visible, the resolver follows public
`Markers_mc` members. Exactly one of `SystemMarkerContainer_mc` or
`BodyMarkerContainer_mc` must be visible. It scans visible marker children in
reverse display order and bounded descendants for the public inactive mission
glyph. `hitTestPoint(stage.mouseX, stage.mouseY, true)` provides an immediate
cursor test without waiting for rollover state. The visible nameplate text is
copied as an exact generation discriminator; it is never parsed as identity.

Galaxy view matches the marker's public system ID to the outer native key. If
no target in that system is active, Bethesda displays the final/highest body
key, and the plugin applies the same rule. System view matches the public body
ID and exact native label and requires one unique quest instance.

## Activation

The queued task re-resolves the same FormID/instance pair, requires the quest to
be running, not stopped, and not already tracked, then calls Starfield's
reviewed tracking helper. Because the helper toggles rather than sets, the
second inactive check prevents double releases from turning the quest back off.

## Repaint

After Surface tracking is verified, the task reacquires the live Star Map menu
and Surface state, validates both vtables, and calls the reviewed Surface
refresh handler. After Galaxy/System tracking, it reacquires and pins the live
Star Map menu, validates its primary vtable, and calls Bethesda's reviewed
menu-owned quest-target refresh. That routine rebuilds the shared target tree
and synchronously refreshes every live Star Map state. Either repaint path may
fail or be signature-disabled without undoing successful quest tracking.

## Reviewed 1.16.244 contracts

- Surface rebuild:
  `RE::ID::StarMap::SurfaceMapState::RebuildSurfaceMarkers` (95000) `+ 0x77`
  to `GatherSurfaceQuestTargets` (95012)
- Quest composition: gather (95012) `+ 0x237` to
  `RE::ID::StarMap::ComposeSurfaceQuestTarget` (95013)
- Star Map input: `RE::ID::StarMap::StarMapMenu::OnButtonEvent` (94684)
  `+ 0x10C` to `RE::ID::IMenu::OnButtonEvent` (130632)
- Shared quest-target tree: seven reviewed direct calls to
  `RE::ID::StarMap::BuildQuestTargetTree` (94759)
- Shared target composition: builder (94759) `+ 0x118` to
  `RE::ID::StarMap::ComposeQuestTargetMarker` (94701)
- Shared target insertion: composition (94701) `+ 0x38D` and `+ 0x580` to
  `RE::ID::StarMap::InsertQuestTargetMarker` (94758)
- Tracking helper: `RE::TESQuest::ToggleTracking` (91440)
- All-state Star Map quest-target refresh:
  `RE::StarMap::StarMapMenu::RefreshQuestTargets` (94682)
- Current Surface Map state: `RE::StarMap::StarMapMenu::GetSurfaceMapState`
  (94755)
- Surface Map repaint: `RE::StarMap::SurfaceMapState::Refresh` (95003)
- Primary vtables: `RE::StarMap::StarMapMenu::PRIMARY_VTABLE` (446845) and
  `RE::StarMap::SurfaceMapState::PRIMARY_VTABLE` (447074)

Those reusable APIs, layouts, flags, marker types, and relocation IDs live in
the QTR CommonLibSF fork. The plugin keeps only its chosen callsite offsets and
signatures, incomplete composition-context offsets, GFx member names, and its
matching, caching, and transactional-install policy.

Detailed offsets and the exact executable hash are retained in source and
`MANIFEST.md`. They are not portable contracts.
