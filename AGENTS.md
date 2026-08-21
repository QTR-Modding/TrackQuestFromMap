# Project guidance

This file contains only Track Quest from Map-specific constraints. General QTR
working rules remain outside this repository.

## Objective

Releasing Select over an inactive Surface Map quest marker must track exactly
the quest represented by the marker's visible label. Both ordinary
quest-bearing location overlays and standalone large quest markers are in the
0.2.2 baseline. Ambiguous, invalid, unsupported, or stale state must preserve
vanilla input.

## Boundaries

- Keep the default release a native SFSE DLL. Do not add SWFs, a Bethesda
  plugin, Papyrus, INIs, or bundled Address Library files without an explicitly
  reviewed scope change.
- Text is an exact discriminator, never quest identity. Native quest identity
  is FormID plus instance ID.
- Never assume marker handles are unique.
- Tracking is core behavior. Repaint is optional and must never undo or gate a
  successful track.
- Support only runtimes whose ABIs, layouts, signatures, original targets,
  vtables, locking, and caller paths were re-audited against the executable.
- Missing UI members and rejected activation must fail open to vanilla input.
- Detect same-callsite native conflicts before mutating code.
- Builds must have no game, mod-manager, install, or launch side effect.
- Pin release dependencies. A mutable local CommonLib checkout is not release
  provenance.
- Add each new map/menu through a focused pull request. Do not opportunistically
  broaden menu support while fixing Surface Map.
- Public author identity is exactly `Quantumyilmaz`. Do not add tool or editor
  attribution to project metadata.

See `CONTRIBUTING.md`, `docs/DEVELOPMENT.md`, and
`docs/RELEASE_CHECKLIST.md` before changing runtime code.
