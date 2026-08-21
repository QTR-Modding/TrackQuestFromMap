# Corresponding source and build provenance

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
