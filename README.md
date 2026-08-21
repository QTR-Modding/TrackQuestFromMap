# Track Quest from Map

Track quests directly from Starfield's maps with the normal **Activate/Select**
control.

Supported views:

- Surface Map
- Galaxy Map
- System Map

## Requirements

- Steam Starfield 1.16.244
- SFSE 0.2.21
- Address Library for Starfield 1.16.244

## Installation

Install the DLL at:

    Data/SFSE/Plugins/TrackQuestSurfaceNativeOnly.dll

The legacy DLL filename is retained for upgrades from the Surface Map release.
The mod contains no SWF files and stores no save data.

## Building

    git clone --recursive https://github.com/QTR-Modding/TrackQuestFromMap.git
    cd TrackQuestFromMap
    xmake f -c -m release -a x64 -p windows -y
    xmake -r -y TrackQuestSurfaceNativeOnly

## License

GPL-3.0-or-later with the exceptions in [EXCEPTIONS](EXCEPTIONS).
