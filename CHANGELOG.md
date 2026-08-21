# Changelog

## Unreleased

- Moved reusable input, quest, vector-bound, and Surface Map engine contracts
  into focused commits on the QTR CommonLibSF fork.
- Split the native plugin into entrypoint, hook transaction, Star Map input,
  and Surface Map ownership/activation units with a real `PCH.h` and QTR
  `logger::` usage.
- Tightened native marker capture into an owner-thread copy phase followed by
  form resolution, validation, logging, and cache publication from owned data.
- Added pinned Windows CI plus deterministic recursive-source and one-DLL
  payload verification. CI does not publish artifacts or claim gameplay proof.
- No development DLL from this refactor is a release candidate until its exact
  hash passes the gameplay regression matrix.

## 0.2.2 — 2026-08-21

- Added support for large standalone Surface Map quest markers.
- Preserved duplicate and sentinel marker handles as distinct native rows.
- Matched the hovered marker through exact representation-specific fields and
  required one exact quest FormID/instance owner.
- Rejected incomplete ownership generations instead of publishing a partial
  cache.
- Gameplay-verified small location-overlay markers, large standalone markers,
  quest tracking, and open-map refresh on Starfield 1.16.244.0.

## 0.2.1 — 2026-08-21

- Fixed a confirmed input-release crash caused by a CommonLibSF wrapper whose
  declared `QUserEvent` return ABI did not match Starfield 1.16.244.
- Read the incoming `ButtonEvent` user-event field by reference without
  creating or releasing a false temporary.

## 0.2.0 — 2026-08-21

- Replaced the early SWF bridge prototype with a DLL-only runtime GFx resolver.
- Added native Select interception, quest-owner capture, tracking, and Surface
  Map refresh.
- Withdrawn: contains the fixed `QUserEvent` ABI crash.
