# Repository automation

The CI workflow performs three repository checks on Windows Server 2022:

1. every recursive submodule must be initialized, clean, and checked out at
   the commit recorded by its parent repository;
2. two recursive source exports from Git objects must have identical SHA-256
   hashes; and
3. a clean Xmake 3.0.9 Release x64 build must fit the one-DLL payload contract.

CI is compile and packaging-structure evidence only. It is not gameplay proof,
does not create a release, and does not upload artifacts.

The private repository is on QTR's GitHub Free plan, which does not enforce the
desired private-repository branch-protection rules. Changes therefore follow a
PR-only project process; the repository does not claim that GitHub currently
enforces that process.

## Local commands

```powershell
./scripts/Test-SubmodulePins.ps1
./scripts/New-RecursiveSourceArchive.ps1 -OutputPath C:\tmp\TrackQuestFromMap-source.zip

$payload = 'C:\tmp\TrackQuestFromMap-payload'
New-Item -ItemType Directory -Path "$payload\SFSE\Plugins" -Force | Out-Null
Copy-Item .\build\windows\x64\release\TrackQuestSurfaceNativeOnly.dll "$payload\SFSE\Plugins"
./scripts/Test-BinaryPayload.ps1 -PayloadRoot $payload
```

The source exporter reads tracked blobs from the root repository and every
pinned recursive submodule. It never packages working-tree files, build output,
or untracked files. Entry order, timestamps, attributes, and compression mode
are fixed so two exports of the same recursive commit graph are byte-identical.
The command refuses to overwrite an existing archive.

The binary verifier accepts exactly
`SFSE/Plugins/TrackQuestSurfaceNativeOnly.dll`, confirms that it is an AMD64 PE
DLL, and can optionally enforce an expected SHA-256 with `-ExpectedSha256`.
Passing these checks does not make a build a gameplay-tested release candidate.
