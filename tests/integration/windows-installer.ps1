# Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
# SPDX-License-Identifier: BSD-3-Clause
#
# Exercise the per-user Windows installer: silent install into a disposable
# directory, version check, user PATH registration (once, also after an
# upgrade), opt-out of PATH registration, and uninstall restoring PATH.
# The original HKCU\Environment Path value and kind are always restored.
param(
    [Parameter(Mandatory)] [string] $Installer,
    [Parameter(Mandatory)] [string] $Version
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$environmentKey = 'HKCU:\Environment'
$registry = Get-Item -LiteralPath $environmentKey
$hadPath = $registry.GetValueNames() -contains 'Path'
$originalPath = if ($hadPath) { $registry.GetValue('Path', '', 'DoNotExpandEnvironmentNames') } else { $null }
$originalKind = if ($hadPath) { $registry.GetValueKind('Path') } else { $null }

$work = Join-Path ([IO.Path]::GetTempPath()) ("clutch installer " + [guid]::NewGuid())
$target = Join-Path $work 'Clutch install'
$bin = Join-Path $target 'bin'

function Get-UserPath {
    $key = Get-Item -LiteralPath $environmentKey
    if ($key.GetValueNames() -notcontains 'Path') { return '' }
    $key.GetValue('Path', '', 'DoNotExpandEnvironmentNames')
}

function Get-EntryCount([string] $Entry) {
    @((Get-UserPath) -split ';' | Where-Object {
        $_.Trim().Trim('"').TrimEnd('\') -ieq $Entry.TrimEnd('\')
    }).Count
}

function Invoke-Installer([string[]] $Extra) {
    $arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER',
                   "/DIR=`"$target`"", "/LOG=`"$(Join-Path $work 'install.log')`"") + $Extra
    $process = Start-Process -FilePath $Installer -ArgumentList $arguments -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "installer exited with $($process.ExitCode)" }
}

function Invoke-Uninstaller {
    $uninstaller = Join-Path $target 'unins000.exe'
    if (-not (Test-Path -LiteralPath $uninstaller)) { throw 'uninstaller missing' }
    $process = Start-Process -FilePath $uninstaller -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "uninstaller exited with $($process.ExitCode)" }
    # The uninstaller relaunches from a temporary copy; wait for its removal.
    $deadline = (Get-Date).AddSeconds(60)
    while ((Test-Path -LiteralPath (Join-Path $bin 'clutch.exe')) -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 200
    }
    if (Test-Path -LiteralPath (Join-Path $bin 'clutch.exe')) { throw 'uninstall left clutch.exe behind' }
}

New-Item -ItemType Directory -Path $work | Out-Null
try {
    Invoke-Installer @()
    $installed = & (Join-Path $bin 'clutch.exe') --version
    if ($installed -ne "clutch $Version") { throw "installed version mismatch: $installed" }
    foreach ($file in @('clutch.ico', 'share\licenses\clutch\LICENSE')) {
        if (-not (Test-Path -LiteralPath (Join-Path $target $file))) { throw "missing installed file: $file" }
    }
    if ((Get-EntryCount $bin) -ne 1) { throw 'install did not add exactly one PATH entry' }
    if ($hadPath -and (Get-Item -LiteralPath $environmentKey).GetValueKind('Path') -ne $originalKind) {
        throw 'install changed the PATH registry value kind'
    }
    Write-Host 'PASS: silent per-user install, version and PATH registration'

    Invoke-Installer @()
    if ((Get-EntryCount $bin) -ne 1) { throw 'reinstall duplicated the PATH entry' }
    Write-Host 'PASS: reinstall keeps a single PATH entry'

    Invoke-Uninstaller
    if ((Get-EntryCount $bin) -ne 0) { throw 'uninstall left the PATH entry' }
    if ($hadPath -and (Get-UserPath) -ne $originalPath) { throw 'uninstall changed unrelated PATH entries' }
    Write-Host 'PASS: uninstall removes exactly its PATH entry'

    Invoke-Installer @('/MERGETASKS="!addtopath"')
    if ((Get-EntryCount $bin) -ne 0) { throw 'PATH changed although the task was deselected' }
    Invoke-Uninstaller
    Write-Host 'PASS: install without PATH registration'

    # Exercise the other separator style too: a value with (or without) a
    # trailing ';' must come back byte-for-byte after install and uninstall.
    if ($hadPath) {
        $variant = if ($originalPath.EndsWith(';')) { $originalPath.TrimEnd(';') } else { "$originalPath;" }
        Set-ItemProperty -LiteralPath $environmentKey -Name 'Path' -Value $variant -Type $originalKind
        Invoke-Installer @()
        if ((Get-EntryCount $bin) -ne 1) { throw 'install did not add exactly one PATH entry (separator variant)' }
        Invoke-Uninstaller
        if ((Get-UserPath) -ne $variant) { throw 'uninstall changed the PATH separator style' }
        Write-Host 'PASS: PATH with and without a trailing separator is restored exactly'
    }
} finally {
    $uninstaller = Join-Path $target 'unins000.exe'
    if (Test-Path -LiteralPath $uninstaller) {
        Start-Process -FilePath $uninstaller -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') -Wait | Out-Null
    }
    # Restore the user's exact PATH value even after a failed assertion.
    if ($hadPath) {
        if ((Get-UserPath) -ne $originalPath -or
            (Get-Item -LiteralPath $environmentKey).GetValueKind('Path') -ne $originalKind) {
            Set-ItemProperty -LiteralPath $environmentKey -Name 'Path' -Value $originalPath -Type $originalKind
        }
    } elseif ((Get-Item -LiteralPath $environmentKey).GetValueNames() -contains 'Path') {
        Remove-ItemProperty -LiteralPath $environmentKey -Name 'Path'
    }
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
