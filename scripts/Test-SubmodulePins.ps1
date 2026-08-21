[CmdletBinding()]
param(
    [string] $RepositoryRoot = (Join-Path $PSScriptRoot '..')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-GitText {
    param(
        [Parameter(Mandatory)]
        [string] $WorkingDirectory,

        [Parameter(Mandatory)]
        [string[]] $Arguments
    )

    $output = & git -C $WorkingDirectory @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "git $($Arguments -join ' ') failed in '$WorkingDirectory':`n$($output -join [Environment]::NewLine)"
    }

    return @($output)
}

function Get-Gitlinks {
    param(
        [Parameter(Mandatory)]
        [string] $WorkingDirectory
    )

    $records = [System.Collections.Generic.List[object]]::new()
    foreach ($line in (Invoke-GitText -WorkingDirectory $WorkingDirectory -Arguments @(
        '-c', 'core.quotePath=false', 'ls-tree', '-r', '--full-tree', 'HEAD'
    ))) {
        if ($line -notmatch '^(?<mode>[0-9]{6}) (?<type>[^ ]+) (?<oid>[0-9a-fA-F]{40,64})\t(?<path>.+)$') {
            throw "Unexpected git ls-tree output in '$WorkingDirectory': $line"
        }

        if ($Matches.mode -eq '160000') {
            if ($Matches.type -ne 'commit') {
                throw "Gitlink '$($Matches.path)' does not point to a commit."
            }

            $records.Add([pscustomobject]@{
                ObjectId = $Matches.oid.ToLowerInvariant()
                Path = $Matches.path
            })
        }
    }

    return $records
}

function Resolve-ContainedPath {
    param(
        [Parameter(Mandatory)]
        [string] $Parent,

        [Parameter(Mandatory)]
        [string] $Child
    )

    if ([IO.Path]::IsPathRooted($Child) -or $Child.IndexOf([char]0) -ge 0) {
        throw "Unsafe submodule path '$Child'."
    }

    $parentFull = [IO.Path]::GetFullPath($Parent).TrimEnd('\', '/')
    $childFull = [IO.Path]::GetFullPath((Join-Path $parentFull $Child))
    $prefix = $parentFull + [IO.Path]::DirectorySeparatorChar
    if (-not $childFull.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Submodule path '$Child' escapes '$Parent'."
    }

    return $childFull
}

function Test-RepositoryLevel {
    param(
        [Parameter(Mandatory)]
        [string] $WorkingDirectory,

        [Parameter(Mandatory)]
        [int] $Depth
    )

    if ($Depth -gt 16) {
        throw "Submodule nesting exceeds the supported depth of 16 at '$WorkingDirectory'."
    }

    $staged = @(Invoke-GitText -WorkingDirectory $WorkingDirectory -Arguments @(
        'diff', '--cached', '--name-only', 'HEAD', '--'
    ))
    if ($staged.Count -ne 0) {
        throw "The index differs from HEAD in '$WorkingDirectory':`n$($staged -join [Environment]::NewLine)"
    }

    $unstagedGitmodules = @(Invoke-GitText -WorkingDirectory $WorkingDirectory -Arguments @(
        'diff', '--name-only', '--', '.gitmodules'
    ))
    if ($unstagedGitmodules.Count -ne 0) {
        throw "The tracked .gitmodules file is modified in '$WorkingDirectory'."
    }

    foreach ($gitlink in (Get-Gitlinks -WorkingDirectory $WorkingDirectory)) {
        $submodulePath = Resolve-ContainedPath -Parent $WorkingDirectory -Child $gitlink.Path
        if (-not (Test-Path -LiteralPath $submodulePath -PathType Container)) {
            throw "Submodule '$($gitlink.Path)' is not initialized."
        }

        $headLines = @(Invoke-GitText -WorkingDirectory $submodulePath -Arguments @(
            'rev-parse', '--verify', 'HEAD'
        ))
        $actualHead = $headLines[0].Trim().ToLowerInvariant()
        if ($actualHead -cne $gitlink.ObjectId) {
            throw "Submodule '$($gitlink.Path)' is at $actualHead; HEAD pins $($gitlink.ObjectId)."
        }

        $status = @(Invoke-GitText -WorkingDirectory $submodulePath -Arguments @(
            'status', '--porcelain=v1', '--untracked-files=all', '--ignore-submodules=none'
        ))
        if ($status.Count -ne 0) {
            throw "Submodule '$($gitlink.Path)' has tracked or untracked changes:`n$($status -join [Environment]::NewLine)"
        }

        Test-RepositoryLevel -WorkingDirectory $submodulePath -Depth ($Depth + 1)
    }
}

$root = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$insideWorkTreeLines = @(Invoke-GitText -WorkingDirectory $root -Arguments @(
    'rev-parse', '--is-inside-work-tree'
))
$insideWorkTree = $insideWorkTreeLines[0].Trim()
if ($insideWorkTree -cne 'true') {
    throw "'$root' is not a Git working tree."
}

Test-RepositoryLevel -WorkingDirectory $root -Depth 0
Write-Host 'PASS: every recursive submodule is initialized, clean, and checked out at its HEAD gitlink.'
