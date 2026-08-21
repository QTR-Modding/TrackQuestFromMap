[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $OutputPath,

    [string] $RepositoryRoot = (Join-Path $PSScriptRoot '..'),

    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')]
    [string] $ArchiveRoot = 'TrackQuestFromMap-source'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function New-GitProcess {
    param(
        [Parameter(Mandatory)]
        [string] $WorkingDirectory,

        [Parameter(Mandatory)]
        [string[]] $Arguments
    )

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = 'git'
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardInput = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in $Arguments) {
        $startInfo.ArgumentList.Add($argument)
    }

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw "Failed to start git in '$WorkingDirectory'."
    }

    return $process
}

function Invoke-GitBytes {
    param(
        [Parameter(Mandatory)]
        [string] $WorkingDirectory,

        [Parameter(Mandatory)]
        [string[]] $Arguments
    )

    $process = New-GitProcess -WorkingDirectory $WorkingDirectory -Arguments $Arguments
    $memory = [IO.MemoryStream]::new()
    try {
        $copyTask = $process.StandardOutput.BaseStream.CopyToAsync($memory)
        $errorTask = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        [void] $copyTask.GetAwaiter().GetResult()
        $errorText = $errorTask.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0) {
            throw "git $($Arguments -join ' ') failed in '$WorkingDirectory':`n$errorText"
        }

        return ,$memory.ToArray()
    }
    finally {
        $process.Dispose()
        $memory.Dispose()
    }
}

function Split-ZeroTerminatedUtf8 {
    param(
        [Parameter(Mandatory)]
        [byte[]] $Bytes
    )

    $decoder = [Text.UTF8Encoding]::new($false, $true)
    $values = [System.Collections.Generic.List[string]]::new()
    $start = 0
    for ($index = 0; $index -lt $Bytes.Length; ++$index) {
        if ($Bytes[$index] -ne 0) {
            continue
        }

        if ($index -gt $start) {
            $values.Add($decoder.GetString($Bytes, $start, $index - $start))
        }
        $start = $index + 1
    }

    if ($start -ne $Bytes.Length) {
        throw 'Git emitted a non-terminated -z record.'
    }

    return $values
}

function Resolve-ContainedPath {
    param(
        [Parameter(Mandatory)]
        [string] $Parent,

        [Parameter(Mandatory)]
        [string] $Child
    )

    if ([IO.Path]::IsPathRooted($Child) -or $Child.IndexOf([char]0) -ge 0) {
        throw "Unsafe repository path '$Child'."
    }

    $parentFull = [IO.Path]::GetFullPath($Parent).TrimEnd('\', '/')
    $childFull = [IO.Path]::GetFullPath((Join-Path $parentFull $Child))
    $prefix = $parentFull + [IO.Path]::DirectorySeparatorChar
    if (-not $childFull.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Repository path '$Child' escapes '$Parent'."
    }

    return $childFull
}

function Add-RepositoryTree {
    param(
        [Parameter(Mandatory)]
        [string] $WorkingDirectory,

        [Parameter(Mandatory)]
        [string] $ArchivePrefix,

        [Parameter(Mandatory)]
        [System.Collections.Generic.SortedDictionary[string, object]] $Entries,

        [Parameter(Mandatory)]
        [int] $Depth
    )

    if ($Depth -gt 16) {
        throw "Submodule nesting exceeds the supported depth of 16 at '$WorkingDirectory'."
    }

    $treeBytes = Invoke-GitBytes -WorkingDirectory $WorkingDirectory -Arguments @(
        '-c', 'core.quotePath=false', 'ls-tree', '-r', '-z', '--full-tree', 'HEAD'
    )
    foreach ($record in (Split-ZeroTerminatedUtf8 -Bytes $treeBytes)) {
        if ($record -notmatch '^(?<mode>[0-9]{6}) (?<type>[^ ]+) (?<oid>[0-9a-fA-F]{40,64})\t(?<path>.+)$') {
            throw "Unexpected git ls-tree record in '$WorkingDirectory': $record"
        }

        $mode = $Matches.mode
        $type = $Matches.type
        $objectId = $Matches.oid.ToLowerInvariant()
        $gitPath = $Matches.path
        if ($gitPath.Contains('\') -or $gitPath.StartsWith('/') -or ($gitPath.Split('/') -contains '..')) {
            throw "Unsafe Git path '$gitPath'."
        }

        $archivePath = "$ArchivePrefix/$gitPath"
        if ($mode -eq '160000') {
            if ($type -ne 'commit') {
                throw "Gitlink '$gitPath' does not point to a commit."
            }

            $submodulePath = Resolve-ContainedPath -Parent $WorkingDirectory -Child $gitPath
            Add-RepositoryTree `
                -WorkingDirectory $submodulePath `
                -ArchivePrefix $archivePath `
                -Entries $Entries `
                -Depth ($Depth + 1)
            continue
        }

        if ($type -ne 'blob' -or $mode -notin @('100644', '100755', '120000')) {
            throw "Unsupported tree entry '$mode $type $gitPath'."
        }
        if ($Entries.ContainsKey($archivePath)) {
            throw "Duplicate archive path '$archivePath'."
        }

        $Entries.Add($archivePath, [pscustomobject]@{
            ArchivePath = $archivePath
            Mode = $mode
            ObjectId = $objectId
            WorkingDirectory = $WorkingDirectory
        })
    }
}

function Read-GitBlobBatch {
    param(
        [Parameter(Mandatory)]
        [string] $WorkingDirectory,

        [Parameter(Mandatory)]
        [string[]] $ObjectIds
    )

    $uniqueIds = @($ObjectIds | Sort-Object -Unique)
    $process = New-GitProcess -WorkingDirectory $WorkingDirectory -Arguments @('cat-file', '--batch')
    $memory = [IO.MemoryStream]::new()
    try {
        $copyTask = $process.StandardOutput.BaseStream.CopyToAsync($memory)
        $errorTask = $process.StandardError.ReadToEndAsync()
        foreach ($objectId in $uniqueIds) {
            $process.StandardInput.WriteLine($objectId)
        }
        $process.StandardInput.Close()
        $process.WaitForExit()
        [void] $copyTask.GetAwaiter().GetResult()
        $errorText = $errorTask.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0) {
            throw "git cat-file --batch failed in '$WorkingDirectory':`n$errorText"
        }

        $bytes = $memory.ToArray()
    }
    finally {
        $process.Dispose()
        $memory.Dispose()
    }

    $result = [System.Collections.Generic.Dictionary[string, byte[]]]::new([StringComparer]::Ordinal)
    $offset = 0
    foreach ($expectedId in $uniqueIds) {
        $lineEnd = [Array]::IndexOf($bytes, [byte]10, $offset)
        if ($lineEnd -lt 0) {
            throw "Truncated git cat-file header for $expectedId."
        }

        $header = [Text.Encoding]::ASCII.GetString($bytes, $offset, $lineEnd - $offset)
        if ($header -notmatch '^(?<oid>[0-9a-fA-F]{40,64}) blob (?<size>[0-9]+)$') {
            throw "Unexpected git cat-file header '$header'."
        }
        if ($Matches.oid.ToLowerInvariant() -cne $expectedId) {
            throw "git cat-file returned $($Matches.oid) while $expectedId was requested."
        }

        $size = [int64]::Parse($Matches.size, [Globalization.CultureInfo]::InvariantCulture)
        if ($size -gt [int]::MaxValue) {
            throw "Blob $expectedId exceeds the supported per-file size."
        }
        $offset = $lineEnd + 1
        if ($offset + $size -ge $bytes.Length) {
            throw "Truncated git blob $expectedId."
        }

        $content = [byte[]]::new([int]$size)
        [Array]::Copy($bytes, $offset, $content, 0, [int]$size)
        $offset += [int]$size
        if ($bytes[$offset] -ne 10) {
            throw "Missing git cat-file separator after $expectedId."
        }
        ++$offset
        $result.Add($expectedId, $content)
    }

    if ($offset -ne $bytes.Length) {
        throw 'git cat-file emitted trailing bytes.'
    }

    return ,$result
}

$root = (Resolve-Path -LiteralPath $RepositoryRoot).Path
& (Join-Path $PSScriptRoot 'Test-SubmodulePins.ps1') -RepositoryRoot $root

$entries = [System.Collections.Generic.SortedDictionary[string, object]]::new([StringComparer]::Ordinal)
Add-RepositoryTree -WorkingDirectory $root -ArchivePrefix $ArchiveRoot -Entries $entries -Depth 0
if ($entries.Count -eq 0) {
    throw 'The recursive source tree is empty.'
}

$blobSets = [System.Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($group in ($entries.Values | Group-Object WorkingDirectory)) {
    $objectIds = @($group.Group | ForEach-Object { $_.ObjectId })
    $blobSets.Add($group.Name, (Read-GitBlobBatch -WorkingDirectory $group.Name -ObjectIds $objectIds))
}

$fullOutputPath = [IO.Path]::GetFullPath($OutputPath)
if (Test-Path -LiteralPath $fullOutputPath) {
    throw "Output already exists: '$fullOutputPath'."
}
$outputDirectory = [IO.Path]::GetDirectoryName($fullOutputPath)
if (-not $outputDirectory) {
    throw "Output path has no parent directory: '$fullOutputPath'."
}
[IO.Directory]::CreateDirectory($outputDirectory) | Out-Null

$temporaryPath = Join-Path $outputDirectory ('.' + [IO.Path]::GetFileName($fullOutputPath) + '.' + [Guid]::NewGuid().ToString('N') + '.tmp')
try {
    $fileStream = [IO.File]::Open($temporaryPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    try {
        $archive = [IO.Compression.ZipArchive]::new($fileStream, [IO.Compression.ZipArchiveMode]::Create, $true, [Text.Encoding]::UTF8)
        try {
            $fixedTimestamp = [DateTimeOffset]::new(1980, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
            foreach ($source in $entries.Values) {
                $entry = $archive.CreateEntry($source.ArchivePath, [IO.Compression.CompressionLevel]::NoCompression)
                $entry.LastWriteTime = $fixedTimestamp

                $unixMode = switch ($source.Mode) {
                    '100755' { 33261 }
                    '120000' { 41471 }
                    default { 33188 }
                }
                $attributes = [uint32]$unixMode -shl 16
                $entry.ExternalAttributes = [BitConverter]::ToInt32([BitConverter]::GetBytes($attributes), 0)

                $entryStream = $entry.Open()
                try {
                    $content = $blobSets[$source.WorkingDirectory][$source.ObjectId]
                    $entryStream.Write($content, 0, $content.Length)
                }
                finally {
                    $entryStream.Dispose()
                }
            }
        }
        finally {
            $archive.Dispose()
        }
    }
    finally {
        $fileStream.Dispose()
    }

    [IO.File]::Move($temporaryPath, $fullOutputPath)
}
finally {
    if (Test-Path -LiteralPath $temporaryPath) {
        Remove-Item -LiteralPath $temporaryPath -Force
    }
}

$rootCommit = (& git -C $root rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) {
    throw 'Failed to resolve the root commit after writing the archive.'
}
$hash = (Get-FileHash -LiteralPath $fullOutputPath -Algorithm SHA256).Hash
Write-Host "PASS: exported $($entries.Count) Git blobs from root commit $rootCommit"
Write-Host "SHA-256: $hash"
Write-Host "Archive: $fullOutputPath"
