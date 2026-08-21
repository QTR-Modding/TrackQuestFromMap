# Track Quest from Map

Track Quest from Map is a native SFSE plugin that lets you track the quest
represented by a marker directly from Starfield's map. Version 0.2.2 supports
the **Surface Map**: hover an inactive quest marker and release the normal
**Activate/Select** control.

Both Surface Map representations are supported:

- the small quest overlay attached to a location; and
- the large standalone quest marker.

The exact 0.2.2 DLL in this repository's release was verified in-game on Steam
Starfield 1.16.244.0 with both representations. Quest tracking and the open-map
marker refresh completed successfully.

## Requirements

- Steam Starfield 1.16.244.0
- SFSE 0.2.21
- the Address Library matching Starfield 1.16.244.0

Other runtimes are deliberately rejected. The plugin uses reviewed native
layouts and call sites; a Bethesda update requires a separately audited build.

## Installation

Install `TrackQuestFromMap-v0.2.2.zip` with a mod manager. Its complete payload
is:

```text
SFSE/Plugins/TrackQuestSurfaceNativeOnly.dll
```

Launch Starfield through SFSE.

If upgrading from a pre-0.2.0 prototype, replace the old mod instead of merging
it. Remove its `surfacemap.swf` and `surfacemap_lrg.swf`; version 0.2.2 is
DLL-only and must not be combined with those obsolete prototype files.

## Compatibility

Version 0.2.2 ships no SWF, Bethesda plugin, Papyrus script, INI, or Address
Library file. It therefore does not overwrite UI mods. At runtime it reads the
public Surface Map display hierarchy and marker properties. UI replacements
remain compatible when they preserve that contract; missing or incompatible
members fail open to vanilla input.

Another native plugin patching any of the same three direct call sites is a
hard conflict. All signatures and original targets are checked before any
write, and hook installation is transactional. On a mismatch this plugin
refuses to install its hooks rather than stacking an unknown patch.

See [Compatibility](docs/COMPATIBILITY.md) for the exact boundary.

## Safety model

- Quest identity is captured natively as FormID plus instance ID.
- Displayed text is used only as an exact, within-generation discriminator. It
  is never parsed into quest identity.
- Marker handles are not assumed unique; large quest markers can share a
  sentinel handle.
- Ambiguous, stale, active, malformed, or unsupported state preserves vanilla
  input.
- Quest mutation is queued to SFSE's main thread and revalidated immediately
  before the vanilla tracking helper is called.
- Rapid repeated Activate presses cannot toggle the newly tracked quest off.
- The plugin stores no save data.

See [Architecture](docs/ARCHITECTURE.md) for the implementation contracts.

## Current scope and known limitation

Only the Surface Map is implemented. Galaxy, system, orbital/planet overview,
and other map views have different native state and marker data. Each new map
will be added through its own reviewed pull request; support will not be guessed
from Surface Map layouts.

On a small location marker shared by both an active and inactive quest,
Starfield exposes an aggregate active flag. Version 0.2.2 fails open instead of
risking activation of the wrong owner. This mixed-state case remains tracked as
a Surface Map limitation.

## Uninstallation

Remove `TrackQuestSurfaceNativeOnly.dll`. The plugin adds no records, scripts,
configuration, or save-baked state, so no clean-save procedure is required.

## Building

Clone recursively so the pinned QTR CommonLibSF revision and its nested
dependency are present:

```powershell
git clone --recursive https://github.com/QTR-Modding/TrackQuestFromMap.git
cd TrackQuestFromMap
xmake f -c -m release -a x64 -p windows -y
xmake -r -y TrackQuestSurfaceNativeOnly
```

The build always consumes the repository-pinned `lib/commonlibsf` submodule.
Building has no install, deploy, mod-manager, or game-launch step.

The tested 0.2.2 dependency revisions and the separate development dependency
pin are listed in [SOURCE.md](SOURCE.md). A CI build is compile evidence, not a
gameplay-tested release candidate.

## Development

Surface Map is the reviewed baseline. New menu support and behavioral changes
belong in focused pull requests with exact runtime evidence and gameplay
verification. See [CONTRIBUTING.md](CONTRIBUTING.md) and the
[release checklist](docs/RELEASE_CHECKLIST.md).

## License

Track Quest from Map is licensed under GPL-3.0-or-later with the CommonLibSF
Modding Exception and GPL-3.0 Linking Exception (with Corresponding Source).
See [COPYING](COPYING), [EXCEPTIONS](EXCEPTIONS), and
[third-party notices](THIRD_PARTY_NOTICES.md).

## Credits

- Quantumyilmaz
- CommonLibSF, commonlib-shared, and SFSE maintainers and contributors
- meh321 for Address Library for SFSE Plugins
