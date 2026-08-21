# Corresponding source and build provenance

## Current development branch

The Galaxy/System feature branch pins QTR CommonLibSF commit
`c741c4a6a29cadf2db1f2eb53a9b62ead060dbd6`. It extends the Surface API series
at `04a3d88e2925806355000190c9c3a9df586ebf3f` with the verified shared Star Map
quest-target-tree layout, builder/insertion IDs, Galaxy/System state accessors,
and the menu-owned all-state quest-target refresh API.
The underlying five-commit Surface series adds
the verified input-event ABI correction, maps the generic menu button-event
handler, exposes quest-instance tracking state, adds raw engine-vector bounds,
and provides typed Surface Map runtime contracts consumed by the refactor. It
is development provenance, not a claim about the released 0.2.2 DLL or an
untested 0.3.0 candidate.

## Released 0.2.2 artifact

The distributed `TrackQuestSurfaceNativeOnly.dll` statically incorporates code
from these exact revisions:

- [QTR CommonLibSF](https://github.com/QTR-Modding/commonlibsf) commit
  `765219c66f58f729f854771a95cdd59bbd76b84b`
- [commonlib-shared](https://github.com/libxse/commonlib-shared) commit
  `5470284e964d5510aa001dca3e0bb5548b6356a4`
- [spdlog](https://github.com/gabime/spdlog) v1.16.0, commit
  `486b55554f11c9cccc913e11a87085b2a91f706f`

The Git repository pins CommonLibSF and spdlog directly. CommonLibSF pins its
nested commonlib-shared revision. Xmake builds spdlog from its locked v1.16.0
package recipe; the additional spdlog gitlink records the corresponding source
revision.

GitHub's automatic source archives do not expand submodules. Every binary
release must therefore include a separate version-matched source archive with
the project and all three dependency trees expanded.

The exact gameplay-tested 0.2.2 binary and source hashes are in `MANIFEST.md`.
Build instructions are in `README.md`. Complete license and exception texts
are in `COPYING`, `EXCEPTIONS`, and `LICENSES/`.
