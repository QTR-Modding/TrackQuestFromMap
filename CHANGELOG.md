# Changelog

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
