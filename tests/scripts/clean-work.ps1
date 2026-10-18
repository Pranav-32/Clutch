<#
.SYNOPSIS
Removes generated test folders from tests/work.
.DESCRIPTION
Locates work relative to this script, so it can be run from any directory.
Preserves the work directory itself and any loose files directly inside it.
Validates every deletion target before removing anything, and refuses symbolic
links or junctions in target trees or their ancestor paths. Close terminals and
processes using generated test directories before running cleanup.
.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File tests/scripts/clean-work.ps1 -WhatIf
Preview which generated folders would be removed.
.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File tests/scripts/clean-work.ps1
Remove generated folders so later tests can create fresh workspaces.
#>
[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
param()

$ErrorActionPreference = 'Stop'
$workDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../work'))
$comparison = if ([IO.Path]::DirectorySeparatorChar -eq '\') {
    [StringComparison]::OrdinalIgnoreCase
} else {
    [StringComparison]::Ordinal
}
$workPrefix = $workDirectory + [IO.Path]::DirectorySeparatorChar

# Reject redirected ancestor directories before enumerating any test output.
for ($ancestor = Get-Item -LiteralPath $PSScriptRoot -Force; $null -ne $ancestor; $ancestor = $ancestor.Parent) {
    if (($ancestor.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Refusing cleanup through a redirected directory: $($ancestor.FullName)"
    }
}

if (-not (Test-Path -LiteralPath $workDirectory)) {
    Write-Output 'No tests/work directory to clean.'
    return
}

$work = Get-Item -LiteralPath $workDirectory -Force
if (-not $work.PSIsContainer -or ($work.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw 'tests/work must be a regular directory, not a file or redirected path.'
}

# Reject entries outside the fixed work root and all reparse points before descent.
function Assert-WorkEntry($Entry) {
    $absolutePath = [IO.Path]::GetFullPath($Entry.FullName)
    if (-not $absolutePath.StartsWith($workPrefix, $comparison)) {
        throw "Refusing deletion outside tests/work: $absolutePath"
    }

    if (($Entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Refusing cleanup of a symbolic link or junction: $absolutePath"
    }
}

$folders = @(Get-ChildItem -LiteralPath $workDirectory -Directory -Force)
$pending = New-Object 'System.Collections.Generic.Stack[string]'
foreach ($folder in $folders) {
    Assert-WorkEntry $folder
    $pending.Push($folder.FullName)
}

# Walk explicitly instead of recursing through links. Preflight every tree so
# an invalid later target cannot leave earlier targets partially cleaned.
while ($pending.Count -gt 0) {
    $directory = Get-Item -LiteralPath $pending.Pop() -Force
    Assert-WorkEntry $directory
    foreach ($entry in Get-ChildItem -LiteralPath $directory.FullName -Force) {
        Assert-WorkEntry $entry
        if ($entry.PSIsContainer) {
            $pending.Push($entry.FullName)
        }
    }
}

# Deletion starts only after the whole forest passes preflight; ShouldProcess
# implements PowerShell's -WhatIf preview without removing the work root itself.
$removed = 0
foreach ($folder in $folders) {
    if ($PSCmdlet.ShouldProcess($folder.FullName, 'Remove generated test folder')) {
        Remove-Item -LiteralPath $folder.FullName -Recurse -Force
        ++$removed
    }
}

Write-Output "Removed $removed test work folder(s)."
