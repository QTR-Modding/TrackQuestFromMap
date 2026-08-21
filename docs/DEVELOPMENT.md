# Development rules

## Style

- C++23, Windows x64, all-extra warnings.
- Match the surrounding file and keep format-only churn out of behavior or ABI
  changes. Canonical workspace guidance does not yet choose one indentation or
  brace-placement standard, so this repository does not invent one.
- Types and functions use `PascalCase`; variables use `lowerCamelCase`;
  constants and enumerators use `kPascalCase`; parameters use `a_name`.
- Use fixed-width integer types at ABI and serialized boundaries.
- Keep translation units responsibility-based and few. Centralize runtime IDs,
  offsets, signatures, and bounds.
- Reusable engine mappings belong in QTR CommonLibSF; plugin-specific hooks and
  safety policy remain here.
- Missing reusable engine contracts are added as focused, upstream-ready
  commits to the QTR CommonLibSF fork and consumed by an exact gitlink. Never
  open an upstream CommonLibSF pull request without explicit permission.

The tagged 0.2.2 source remains byte-identical to the gameplay-tested prototype
source. The development refactor uses the required uppercase `PCH.h`, focused
translation units, and QTR `logger::` style. Its changed DLL hash requires a
fresh gameplay regression before any versioned release.

## ABI rules

- State the audited Starfield runtime for every inferred signature or layout.
- Verify callsite bytes and decode the original target.
- Use `static_assert` for exact ABI-sized data.
- Do not call a CommonLib wrapper whose declared ABI disagrees with the
  executable.
- Never reintroduce the 0.2.0 copied `QUserEvent` temporary. Starfield
  1.16.244 returns a reference while the superseded wrapper declared a value.

## Locks, ownership, and threads

- The quest-composition callback runs under a PlayerCharacter-owned
  `BSSpinLock`; perform only bounded primitive/thread-local capture there.
- The post-gather marker vector is owner-thread-only, non-reentrant, and not
  protected by that lock. Copy its relevant rows synchronously before the
  vanilla caller resumes; never retain a pointer or view.
- Do not allocate, log, resolve forms, or inspect UI inside the composition
  callback. Resolve and publish only after native rows are fully owned.
- Copy native and GFx text immediately into bounded owned storage.
- Queue quest mutation through SFSE's main-thread interface.
- Re-resolve FormID plus instance ID and recheck state before calling a toggle
  helper.

## GFx rules

- Inspect only the documented public hierarchy.
- Validate object types, scalar ranges, visibility, text lengths, and child
  counts.
- Never retain a GFx value or borrowed string after its call.
- Never parse localized label text into quest identity.
- Ambiguity preserves vanilla behavior.

## Hooks and failure behavior

- Validate all required signatures and targets before mutation.
- Allocate all branch islands before mutation.
- Verify every write.
- Restore and verify all original calls on partial failure.
- A required-hook failure rejects plugin load.
- Optional repaint failure skips only repaint.
- Original input dispatch occurs exactly once unless activation was accepted
  and queued.

## Logging

- Do not allow a C++ exception to unwind into Starfield.
- Catch allocation and standard exceptions at native callback/task boundaries.
- Do not claim C++ exception handling catches access violations.
- Log identifiers, counts, classification, and rejection reason; do not log the
  displayed quest text.
