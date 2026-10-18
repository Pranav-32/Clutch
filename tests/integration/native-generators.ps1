<#
.SYNOPSIS
Exercises Clutch's NMake, Visual Studio and optional JOM/GNU Make generators.
.DESCRIPTION
Run after initializing an MSVC developer environment, normally through
windows-verify.cmd with --native-generators. Uses the actual Clutch CLI to
configure, persist, build and test mixed C/C++ projects in paths with spaces.
NMake and MSBuild are required; JOM is tested when available and clearly skipped
otherwise. Fixtures remain under tests/work/native-<guid> for inspection.
.PARAMETER Clutch
CMake-built clutch.exe under test.
.PARAMETER NMake
Optional explicit nmake.exe path. Otherwise resolved from the developer PATH.
.PARAMETER MSBuild
Optional MSBuild.exe path. Otherwise resolved from PATH or Visual Studio's
vswhere installation query.
.PARAMETER Jom
Optional jom.exe path. Otherwise searched on PATH; absence skips only JOM.
.PARAMETER GnuMake
Optional GNU Make path. Otherwise searched on PATH; absence skips GNU Make.
.PARAMETER Only
Run one available generator instead of the full native suite.
#>
param(
    [Parameter(Mandatory=$true)][string]$Clutch,
    [string]$NMake = '',
    [string]$MSBuild = '',
    [string]$Jom = '',
    [string]$GnuMake = '',
    [ValidateSet('', 'nmake', 'visual-studio', 'jom', 'make')][string]$Only = ''
)

$ErrorActionPreference = 'Stop'
$Clutch = (Resolve-Path -LiteralPath $Clutch).Path
$work = Join-Path (Split-Path -Parent $PSScriptRoot) ('work/native-' + [guid]::NewGuid().ToString())
New-Item -ItemType Directory -Force -Path $work | Out-Null

# Explicit paths are validated rather than silently falling back to another tool.
function Resolve-Tool($Explicit, $Name) {
    if ($Explicit) {
        return (Resolve-Path -LiteralPath $Explicit).Path
    }

    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) {
        return $command.Source
    }

    return ''
}

$NMake = Resolve-Tool $NMake 'nmake.exe'
$MSBuild = Resolve-Tool $MSBuild 'MSBuild.exe'
$Jom = Resolve-Tool $Jom 'jom.exe'
$GnuMake = Resolve-Tool $GnuMake 'make.exe'
if (-not $GnuMake) { $GnuMake = Resolve-Tool '' 'gmake.exe' }
if (-not $GnuMake) { $GnuMake = Resolve-Tool '' 'mingw32-make.exe' }

if (-not $MSBuild) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'

    if (Test-Path -LiteralPath $vswhere) {
        $installations = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe'

        if ($LASTEXITCODE -eq 0 -and $installations) {
            $MSBuild = @($installations)[0]
        }
    }
}

if (-not $NMake -or -not $MSBuild) {
    throw 'NMake and MSBuild are required. Install Visual Studio C++ build tools and initialize vcvars64, or pass explicit executable paths.'
}

if (-not (Get-Command cl.exe -CommandType Application -ErrorAction SilentlyContinue)) {
    throw 'cl.exe is missing from PATH. Run this suite from an initialized MSVC developer environment.'
}

$generators = @(
    @{ Name = 'nmake'; Tool = $NMake },
    @{ Name = 'visual-studio'; Tool = $MSBuild }
)

if ($Jom) {
    $generators += @{ Name = 'jom'; Tool = $Jom }
} else {
    Write-Host 'SKIP: JOM native generator (jom.exe is not available)'
}

if ($GnuMake) {
    $generators += @{ Name = 'make'; Tool = $GnuMake }
} else {
    Write-Host 'SKIP: GNU Make generator (GNU Make is not available)'
}

if ($Only) {
    $generators = @($generators | Where-Object { $_.Name -eq $Only })
    if (-not $generators.Count) { throw "Requested generator is unavailable: $Only" }
}

# Fixtures use BOM-free text so the compiler and manifest parser see plain UTF-8.
function Put($Path, $Text) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
    [IO.File]::WriteAllText($Path, $Text, (New-Object Text.UTF8Encoding($false)))
}

# Capture native diagnostics without letting Windows PowerShell convert stderr
# into terminating errors before the actual subprocess exit code is checked.
function Invoke-Checked($Arguments) {
    $previousPreference = $ErrorActionPreference

    try {
        $ErrorActionPreference = 'Continue'
        $output = & $Clutch @Arguments 2>&1
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }

    if ($code -ne 0) {
        throw "Clutch failed with exit ${code}: $output"
    }

    return ($output -join "`n")
}

foreach ($generator in $generators) {
    $name = $generator.Name
    $root = Join-Path $work "$name project with spaces"
    $build = Join-Path $root 'output/debug with spaces'
    $release = Join-Path $root 'output/release with spaces'
    Put "$root/clutch.toml" @"
[project]
name = "native"
version = "0.1.0"
language = "mixed"
std = { c = 17, cpp = 20 }

[build]
generator = "$name"

[profile.debug]
build_dir = "output/debug with spaces"

[profile.release]
build_dir = "output/release with spaces"

[[test]]
name = "native_check"
sources = ["test/independent.c"]
"@
    Put "$root/src/value.h" @'
#pragma once
#define EXPECTED_VALUE 42
#ifdef __cplusplus
extern "C" {
#endif
int value(void);
#ifdef __cplusplus
}
#endif
'@
    Put "$root/src/value.c" @'
#include "value.h"
int value(void) { return EXPECTED_VALUE; }
'@
    Put "$root/src/main.cpp" @'
#include "value.h"
int main() { return value() == EXPECTED_VALUE ? 0 : 1; }
'@
    Put "$root/test/independent.c" 'int main(void) { return 0; }'

    $configurationArguments = @($root, '--generator', $name, '--build-tool', $generator.Tool, '--cc', 'cl', '--cxx', 'cl')
    Invoke-Checked $configurationArguments | Write-Host

    if (Test-Path -LiteralPath "$build/native.exe") {
        throw "$name configuration compiled the application"
    }

    if (@(Get-ChildItem -LiteralPath $build -Filter '*.obj' -File -Recurse).Count) {
        throw "$name configuration compiled source units"
    }

    if (-not (Test-Path -LiteralPath "$build/configuration.toml")) {
        throw "$name saved configuration is missing"
    }

    if (-not (Test-Path -LiteralPath "$build/build-graph.dot")) {
        throw "$name graph visualization is missing"
    }

    if (Test-Path -LiteralPath "$root/compile_commands.json") {
        throw "$name compilation database leaked into the source root"
    }

    $database = Get-Content -Raw -LiteralPath "$build/compile_commands.json" | ConvertFrom-Json

    if (@($database).Count -ne 3) {
        throw "$name database omitted application or independent C test units"
    }

    foreach ($unit in $database) {
        if (-not [IO.Path]::IsPathRooted($unit.file) -or $unit.directory -ne (Resolve-Path -LiteralPath $build).Path) {
            throw "$name database does not use absolute source/build paths"
        }

        $flags = $unit.arguments -join ' '

        if ($unit.file.EndsWith('.c') -and $flags -notmatch '/std:c17') {
            throw "$name database omitted the C standard"
        }

        if ($unit.file.EndsWith('.cpp') -and $flags -notmatch '/std:c\+\+20') {
            throw "$name database omitted the C++ standard"
        }
    }

    $backendFile = if ($name -eq 'visual-studio') { 'clutch.sln' } else { 'Makefile' }

    if (-not (Test-Path -LiteralPath (Join-Path $build $backendFile))) {
        throw "$name native generator files are missing"
    }

    Invoke-Checked @('build', '--project', $root, '--jobs', '2') | Write-Host

    if (-not (Test-Path -LiteralPath "$build/native.exe")) {
        throw "$name application output is missing"
    }

    if (Test-Path -LiteralPath "$build/tests/native_check.exe") {
        throw "$name application build also built a test"
    }

    & "$build/native.exe"

    if ($LASTEXITCODE -ne 0) {
        throw "$name mixed C/C++ application failed"
    }

    $objects = @(Get-ChildItem -LiteralPath "$build/obj" -Filter '*.obj' -File)

    if ($objects.Count -ne 2) {
        throw "$name did not produce exactly two application objects"
    }

    $timestamps = @{}

    foreach ($object in $objects) {
        $timestamps[$object.FullName] = $object.LastWriteTimeUtc.Ticks
    }

    $executableTimestamp = (Get-Item -LiteralPath "$build/native.exe").LastWriteTimeUtc.Ticks
    Invoke-Checked @('build', '--project', $root) | Write-Host

    foreach ($object in $objects) {
        if ((Get-Item -LiteralPath $object.FullName).LastWriteTimeUtc.Ticks -ne $timestamps[$object.FullName]) {
            throw "$name unchanged build recompiled $($object.Name)"
        }
    }

    if ((Get-Item -LiteralPath "$build/native.exe").LastWriteTimeUtc.Ticks -ne $executableTimestamp) {
        throw "$name unchanged build relinked the application"
    }

    Start-Sleep -Milliseconds 1100
    $header = Get-Content -Raw -LiteralPath "$root/src/value.h"
    Put "$root/src/value.h" ($header + "`n// header dependency changed`n")
    Invoke-Checked @('build', '--project', $root) | Write-Host

    foreach ($object in $objects) {
        if ((Get-Item -LiteralPath $object.FullName).LastWriteTimeUtc.Ticks -le $timestamps[$object.FullName]) {
            throw "$name header change did not rebuild $($object.Name)"
        }
    }

    $tested = Invoke-Checked @('test', '--project', $root, '--filter', 'native_check')

    if ($tested -notmatch '1 passed; 0 failed' -or -not (Test-Path -LiteralPath "$build/tests/native_check.exe")) {
        throw "$name independent C test did not build and execute: $tested"
    }

    Invoke-Checked ($configurationArguments + @('--release')) | Write-Host

    if (Test-Path -LiteralPath "$release/native.exe") {
        throw "$name release configuration compiled sources"
    }

    Invoke-Checked @('build', '--project', $root, '--release') | Write-Host

    if (-not (Test-Path -LiteralPath "$release/native.exe") -or -not (Test-Path -LiteralPath "$release/configuration.toml")) {
        throw "$name release output or saved configuration is missing"
    }

    if (-not (Test-Path -LiteralPath "$release/compile_commands.json")) {
        throw "$name release compilation database is missing"
    }

    if (Test-Path -LiteralPath "$release/tests/native_check.exe") {
        throw "$name release application build also built tests"
    }

    & "$release/native.exe"

    if ($LASTEXITCODE -ne 0) {
        throw "$name release application failed"
    }

    Write-Host "PASS: $name positional configuration, saved graph, mixed C/C++, selected C test, no-op, headers and release"
}

Write-Host "Native generator integration checks passed. Fixtures retained in $work"
