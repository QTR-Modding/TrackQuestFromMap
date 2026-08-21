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

Version 0.2.2 transactionally hooks the reviewed Surface Map gather and
quest-composition calls. While the game's map-state lock is held, the inner
hook copies only bounded primitive FormID/instance pairs into thread-local
storage. It does not allocate, log, look up forms, touch UI, or retain engine
pointers under that lock.

After gather returns, the plugin copies each native marker row into owned
storage:

- handle, type, location/target/active flags;
- the representation-specific raw label fields; and
- exact quest owner keys.

Rows are kept individually because marker handles are not identities and can
repeat.

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

- Surface rebuild: `REL::ID(95000) + 0x77 -> REL::ID(95012)`
- Quest composition: `REL::ID(95012) + 0x237 -> REL::ID(95013)`
- Star Map input: `REL::ID(94684) + 0x10C -> REL::ID(130632)`
- Tracking helper: `REL::ID(91440)`
- Current Surface Map state accessor: `REL::ID(94755)`
- Surface Map refresh: `REL::ID(95003)`
- Star Map and Surface Map primary vtables: `REL::ID(446845)` and
  `REL::ID(447074)`

Detailed offsets and the exact executable hash are retained in source and
`MANIFEST.md`. They are not portable contracts.
