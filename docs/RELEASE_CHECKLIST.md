# Release checklist

## Scope and metadata

- [ ] Acceptance behavior and menu scope are explicit.
- [ ] Payload, dependency, compatibility, runtime, and save impact are reviewed.
- [ ] No test-only code remains in production.
- [ ] `xmake.lua`, `PluginVersionData`, the load log, changelog, tag, archive,
      and release title use the same version.
- [ ] Author is exactly `Quantumyilmaz`; no tool/editor attribution appears.

## Dependency and runtime pins

- [ ] CommonLibSF and nested dependency commits are public and recorded.
- [ ] Xmake's package lock is current and reviewed.
- [ ] Starfield, SFSE, Address Library, executable hash, and toolchain are
      recorded.
- [ ] Every relocation, callsite, target, ABI, offset, vtable, lock, and caller
      assumption is rechecked for the supported runtime.

## Clean build

- [ ] Configure and rebuild Release x64 from a clean recursive clone.
- [ ] No build command deploys, installs, enables, or launches anything.
- [ ] Warnings are reviewed.
- [ ] A second clean build in another path matches before claiming reproducible
      DLL bytes.
- [ ] Exact source and dependency provenance for the shipped DLL is retained.

## Binary and archive

- [ ] PE architecture is x64.
- [ ] Exports are exactly `SFSEPlugin_Load` and `SFSEPlugin_Version`.
- [ ] Imports are only expected MSVC/UCRT and Windows system dependencies.
- [ ] Embedded runtime gate and plugin metadata are inspected.
- [ ] Package DLL hash equals the reviewed build DLL hash.
- [ ] Archive contains exactly
      `SFSE/Plugins/TrackQuestSurfaceNativeOnly.dll`.
- [ ] No SWF, plugin record, INI, script, Address Library file, PDB, LIB, EXP,
      log, or build tree is present.
- [ ] DLL and archive SHA-256 values are recorded in `MANIFEST.md`.
- [ ] Version-matched corresponding-source archive expands all submodules when
      binary distribution requires it.

## Static compatibility

- [ ] All required hook signatures and decoded targets pass.
- [ ] Transactional rollback is independently reviewed.
- [ ] Effective UI preserves the public Surface Map path/properties.
- [ ] Same-callsite hard conflicts and unsupported runtimes fail safely.

## Gameplay matrix

- [ ] Inactive small quest-bearing location overlay.
- [ ] Inactive standalone large type-`0x48` marker.
- [ ] Already tracked marker never toggles off.
- [ ] Rapid repeated Select.
- [ ] Multiple quests at one location, including mixed active state.
- [ ] Duplicate handles and duplicate labels.
- [ ] Non-quest markers and ordinary location activation.
- [ ] Press, hold, release, and non-Select inputs.
- [ ] Surface Map hidden and other Star Map views.
- [ ] Open/close and marker-provider rebuild cycles.
- [ ] RB/Surface Map transition crash regression.
- [ ] Marker repaint without leaving the map.
- [ ] Safe tracking when repaint cannot run.
- [ ] Vanilla UI and a representative compatible UI replacement.

## Publication

- [ ] Tag the exact reviewed commit.
- [ ] Attach the reviewed binary archive, not only GitHub's source archive.
- [ ] Release notes state requirements, payload, UI/native conflicts, save
      impact, rollback, and replace-not-merge upgrade instructions.
- [ ] Read back repository privacy, commit, tag, release asset name, size, and
      hash from GitHub before calling it published.
