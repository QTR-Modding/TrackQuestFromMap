# Contributing

Track Quest from Map is runtime-specific native code. A patch that compiles can
still corrupt input state, select the wrong quest instance, or crash inside the
game. Keep changes narrow and evidence-backed.

## Scope

The 0.2.2 baseline covers two Surface Map representations:

- an ordinary location row with a small quest overlay; and
- a standalone type-`0x48` large quest marker.

New menus, UI assets, persistent data, dependencies, or runtime support are
explicit design changes. Add each map through its own pull request.

## Prerequisites

- Windows x64
- MSVC with C++23 support and a Windows SDK
- Xmake 3.0.9 or newer
- the repository's recursive submodules
- SFSE, the matching Address Library, and a legally installed Starfield for
  runtime testing

Never redistribute a Starfield executable or Bethesda assets.

## Build

```powershell
git submodule update --init --recursive
xmake f -c -m release -a x64 -p windows -y
xmake -r -y TrackQuestSurfaceNativeOnly
```

`COMMONLIBSF_PATH` is a developer-only override. Release builds use the pinned
`lib/commonlibsf` submodule. The build target must remain side-effect free.

## Change workflow

1. Write the exact user-visible acceptance case.
2. Identify the affected layer: input, GFx inspection, ownership capture,
   activation, or repaint.
3. For a native contract change, record exact-runtime disassembly and prove
   register/stack ABI, layout, lock, lifetime, and caller behavior.
4. Implement the smallest production change. Keep diagnostics bounded and
   isolate disposable instrumentation.
5. Build without deployment and complete the static checks.
6. Test the changed case in-game plus the vanilla-fallback regressions.
7. Record payload, dependency, compatibility, runtime, and save-impact changes.

Do not weaken the 1.16.244 gate to claim another runtime. Add a separately
audited mapping/build.

## Pull requests

Every pull request must stand alone and include:

- the problem and exact acceptance behavior;
- why the chosen layer is correct;
- supported runtime and dependencies;
- build result;
- ABI/layout/signature evidence where applicable;
- gameplay and regression evidence;
- compatibility, payload, and save-impact changes;
- rollback behavior; and
- a statement that vanilla dispatch still occurs exactly once on every
  rejected path.

Keep style-only refactors separate from behavior changes. In particular, the
tagged 0.2.2 source preserves the exact gameplay-tested prototype layout for
binary provenance; its PCH and logging normalization belongs in a distinct,
no-behavior-change pull request.

## License and provenance

Contributions are accepted under the repository license. Do not add code or
assets whose provenance and redistribution terms are unclear. Update
`SOURCE.md` and `THIRD_PARTY_NOTICES.md` when a linked dependency changes.
