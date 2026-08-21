# Release verification manifest

- Public name: Track Quest from Map
- Internal plugin/DLL name: TrackQuestSurfaceNativeOnly
- Version: 0.2.2.0
- Author: Quantumyilmaz
- Target runtime: Steam Starfield 1.16.244.0
- Required SFSE: 0.2.21
- Required Address Library: matching 1.16.244.0 database
- Starfield executable SHA-256 used for ABI review:
  `7E9ADB1414A8E1B325E5E1F097B9B17B78DEB7EEBEDA37A333351A43A60F9D28`
- Gameplay-tested DLL SHA-256:
  `98B558A42E96CCDA5FD63AB57ED4284AFBC05A4C8145F29BFF996766018220E9`
- Release ZIP SHA-256:
  `20CB0204C69F81F08ABDD28A13EEAE8A523AEC315A74EC2ED246CD3C8BC58698`
- DLL size: 615,424 bytes
- ZIP size: 243,877 bytes
- Exports: `SFSEPlugin_Load`, `SFSEPlugin_Version`
- Archive payload: exactly
  `SFSE/Plugins/TrackQuestSurfaceNativeOnly.dll`
- CommonLibSF-QTR:
  `765219c66f58f729f854771a95cdd59bbd76b84b`
- commonlib-shared:
  `5470284e964d5510aa001dca3e0bb5548b6356a4`
- `src/SurfaceActivation.cpp` SHA-256:
  `F90788B6C7BE9A60D2539EC59AFA551F157199EF03EF5B821A9586910DF61BC5`
- `src/SurfaceActivation.h` SHA-256:
  `D374C7E037F27A8CD0BDAF8A989F3275D2DFBB6D0AD7497280C1827175ED2844`
- `src/main.cpp` SHA-256:
  `73746B94E7B4BCA9B000C592288FBF6156159AA09AD40D6CC77666505D25EB8C`
- SWFs, plugins, INIs, Papyrus files, Address Library files, PDBs, and save
  data: none

## Gameplay proof

The exact DLL hash above was loaded through SFSE and verified on 2026-08-21.
The log confirmed successful owner resolution, queueing, tracking, and live
Surface Map rebuild for both:

- ordinary quest-bearing location overlays; and
- standalone large type-`0x48` quest markers, including nonzero quest instance
  IDs.

This is gameplay proof for the stated cases, not blanket compatibility proof
for other runtimes, map menus, or UI replacements.
