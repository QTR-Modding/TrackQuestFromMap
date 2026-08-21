[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $PayloadRoot,

    [string] $ExpectedSha256
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath $PayloadRoot).Path.TrimEnd('\', '/')
$expectedRelativePath = 'SFSE/Plugins/TrackQuestSurfaceNativeOnly.dll'
$expectedFullPath = [IO.Path]::GetFullPath((Join-Path $root $expectedRelativePath))

$files = @(Get-ChildItem -LiteralPath $root -Recurse -Force -File)
if ($files.Count -ne 1) {
    $found = @($files | ForEach-Object {
        [IO.Path]::GetRelativePath($root, $_.FullName).Replace('\', '/')
    })
    throw "Payload must contain exactly one file, '$expectedRelativePath'. Found: $($found -join ', ')"
}

$actualRelativePath = [IO.Path]::GetRelativePath($root, $files[0].FullName).Replace('\', '/')
if ($actualRelativePath -cne $expectedRelativePath) {
    throw "Unexpected payload path '$actualRelativePath'; expected '$expectedRelativePath'."
}
if ($files[0].FullName -cne $expectedFullPath) {
    throw "Payload path casing or normalization differs from '$expectedRelativePath'."
}
if (($files[0].Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw 'The payload DLL must be a regular file, not a reparse point.'
}
if ($files[0].Length -lt 512) {
    throw 'The payload DLL is unexpectedly small.'
}

$stream = [IO.File]::OpenRead($expectedFullPath)
$reader = $null
try {
    $reader = [IO.BinaryReader]::new($stream)
    if ($reader.ReadUInt16() -ne 0x5A4D) {
        throw 'The payload does not begin with an MZ header.'
    }

    $stream.Position = 0x3C
    $peOffset = $reader.ReadUInt32()
    if ($peOffset -gt ($stream.Length - 24)) {
        throw 'The PE header offset is outside the payload.'
    }

    $stream.Position = $peOffset
    if ($reader.ReadUInt32() -ne 0x00004550) {
        throw 'The payload does not contain a valid PE signature.'
    }
    if ($reader.ReadUInt16() -ne 0x8664) {
        throw 'The payload is not an AMD64 PE image.'
    }

    $stream.Position = $peOffset + 22
    $characteristics = $reader.ReadUInt16()
    if (($characteristics -band 0x2000) -eq 0) {
        throw 'The AMD64 PE image is not marked as a DLL.'
    }
}
finally {
    if ($null -ne $reader) {
        $reader.Dispose()
    }
    else {
        $stream.Dispose()
    }
}

$actualHash = (Get-FileHash -LiteralPath $expectedFullPath -Algorithm SHA256).Hash
if ($ExpectedSha256) {
    $normalizedExpectedHash = $ExpectedSha256.Trim().ToUpperInvariant()
    if ($normalizedExpectedHash -notmatch '^[0-9A-F]{64}$') {
        throw 'ExpectedSha256 must contain exactly 64 hexadecimal characters.'
    }
    if ($actualHash -cne $normalizedExpectedHash) {
        throw "Payload SHA-256 is $actualHash; expected $normalizedExpectedHash."
    }
}

Write-Host "PASS: exact native payload contract; SHA-256 $actualHash"
