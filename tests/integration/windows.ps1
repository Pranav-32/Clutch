<#
.SYNOPSIS
Exercises positional configuration and persisted build/development workflows.
.DESCRIPTION
Run from any directory with absolute or resolvable Clutch and Ninja paths. The
compiler environment must already be initialized (for example with vcvars64).
Checks throw on failure, producing a nonzero PowerShell process status. Fixtures
are retained in tests/work/<guid> for inspection; no existing project is modified.
.PARAMETER Clutch
Executable under test, usually build/Release/clutch.exe from the CMake bootstrap.
.PARAMETER Ninja
Ninja executable used to execute the generated backend rules.
.PARAMETER CC
C compiler command or path. Defaults to cl in an initialized MSVC environment.
.PARAMETER CXX
C++ compiler command or path. Pass clang++ to exercise the Clang toolchain.
#>
param(
    [Parameter(Mandatory=$true)][string]$Clutch,
    [Parameter(Mandatory=$true)][string]$Ninja,
    [string]$CC = 'cl',
    [string]$CXX = 'cl'
)

# Resolve executable inputs once and give every test run its own sandbox.
$ErrorActionPreference = 'Stop'
$Clutch = (Resolve-Path -LiteralPath $Clutch).Path
$Ninja = (Resolve-Path -LiteralPath $Ninja).Path
$work = Join-Path (Split-Path -Parent $PSScriptRoot) ('work/' + [guid]::NewGuid().ToString())
New-Item -ItemType Directory -Force $work | Out-Null

# Write BOM-free UTF-8 fixtures and create parent directories on demand.
function Put($Path, $Text) {
    New-Item -ItemType Directory -Force (Split-Path -Parent $Path) | Out-Null
    [IO.File]::WriteAllText($Path, $Text, (New-Object Text.UTF8Encoding($false)))
}

# Configure fixtures with explicit host tools using Clutch's positional project
# command. Build/run/test only consume saved configuration and accept no compiler
# or backend overrides. "configure" below is an internal helper operation.
function Invoke-Clutch($Command, $Root, $Extra = @(), $ExpectedFailure = $false) {
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        if ($Command -eq 'configure') {
            $output = & $Clutch $Root --build-tool $Ninja --cc $CC --cxx $CXX @Extra 2>&1
        } else {
            $output = & $Clutch $Command --project $Root @Extra 2>&1
        }
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
    if (($code -eq 0) -eq $ExpectedFailure) { throw "Unexpected exit $code : $output" }
    return ($output -join "`n")
}

# Stage 1: C, C++, and mixed fixtures use paths with spaces. Verify configuration
# is compile-free, databases describe each language, and Ninja tracks headers.
foreach ($kind in @('c', 'cpp', 'mixed')) {
    $root = Join-Path $work "$kind project with spaces"
    $standard = if ($kind -eq 'mixed') { '{ c = 11, cpp = 20 }' } elseif ($kind -eq 'c') { '11' } else { '20' }
    Put "$root/clutch.toml" "[project]`nname = 'hello'`nversion = '0.1.0'`nlanguage = '$kind'`nstd = $standard`n`n[build]`ngenerator = 'ninja'`n"
    Put "$root/include/value.h" "#pragma once`n#define VALUE 42`n"
    if ($kind -eq 'c') {
        Put "$root/src/main.c" ('#include "value.h"'+"`n"+'int main(void) { return VALUE == 42 ? 0 : 1; }')
    } elseif ($kind -eq 'cpp') {
        Put "$root/src/main.cpp" ('#include "value.h"'+"`n"+'int main() { return VALUE == 42 ? 0 : 1; }')
    } else {
        Put "$root/src/value.c" ('#include "value.h"'+"`n"+'int value(void) { return VALUE; }')
        Put "$root/src/main.cpp" 'extern "C" int value(void); int main() { return value() == 42 ? 0 : 1; }'
    }
    Invoke-Clutch configure $root | Write-Host
    if (Test-Path "$root/Clutch-build-debug/hello.exe") { throw 'configure compiled sources' }
    if (-not (Test-Path "$root/Clutch-build-debug/configuration.toml")) { throw 'saved configuration missing' }
    if (-not (Test-Path "$root/Clutch-build-debug/build-graph.dot")) { throw 'build graph visualization missing' }
    if (Test-Path "$root/compile_commands.json") { throw 'database leaked into project root' }
    $db = Get-Content -Raw "$root/Clutch-build-debug/compile_commands.json" | ConvertFrom-Json
    if (@($db).Count -ne $(if ($kind -eq 'mixed') {2} else {1})) { throw 'incorrect database size' }
    foreach ($unit in $db) {
        if (-not [IO.Path]::IsPathRooted($unit.directory) -or -not [IO.Path]::IsPathRooted($unit.file)) { throw 'relative database path' }
        if (-not $unit.arguments) { throw 'missing compiler arguments' }
        if ($unit.directory -ne (Resolve-Path "$root/Clutch-build-debug").Path) { throw 'database directory is not the build directory' }
        $flags = $unit.arguments -join ' '
        if ($unit.file.EndsWith('.c') -and $flags -notmatch '(-std=c11|/std:c11)') { throw "incorrect C standard: $flags" }
        if (-not $unit.file.EndsWith('.c') -and $flags -notmatch '(-std=c\+\+20|/std:c\+\+20)') { throw "incorrect C++ standard: $flags" }
    }
    Invoke-Clutch build $root | Write-Host
    & "$root/Clutch-build-debug/hello.exe"
    if ($LASTEXITCODE -ne 0) { throw 'built executable failed' }
    $second = Invoke-Clutch build $root
    if ($second -notmatch 'no work to do') { throw "unchanged build recompiled: $second" }
    Start-Sleep -Milliseconds 1100
    Put "$root/include/value.h" "#pragma once`n#define VALUE 42`n// changed header`n"
    $changed = Invoke-Clutch build $root
    if ($changed -notmatch 'Compiling') { throw 'header change did not recompile' }
    Invoke-Clutch configure $root @('--release') | Write-Host
    Invoke-Clutch build $root @('--release') | Write-Host
    if (-not (Test-Path "$root/Clutch-build-release/hello.exe")) { throw 'release missing' }
    if (-not (Test-Path "$root/Clutch-build-debug/compile_commands.json")) { throw 'release overwrote debug database' }
    if (-not (Test-Path "$root/Clutch-build-release/compile_commands.json")) { throw 'release database missing' }
    Write-Host "PASS: $kind configure, database, debug, release, no-op and header dependency"
}

# Stage 2: reject unsafe manifests and propagate compiler failures to the caller.
$bad = Join-Path $work 'invalid'
Put "$bad/clutch.toml" "[project]`nname = '../escape'`nversion = '0.1.0'"
Invoke-Clutch configure $bad @() $true | Write-Host
Put "$bad/clutch.toml" "[project]`nname = 'broken'`nversion = '0.1.0'"
Put "$bad/src/main.cpp" 'this will not compile'
Invoke-Clutch configure $bad | Out-Null
Invoke-Clutch build $bad @() $true | Write-Host
Write-Host 'PASS: invalid manifest and compiler failure propagate'

# Stage 3: scaffold a new project and exercise program argument/exit forwarding.
$scaffold = Join-Path $work 'new project'
& $Clutch new $scaffold --name example
if ($LASTEXITCODE -ne 0) { throw 'new failed' }
if (-not (Test-Path "$scaffold/src/main.cpp")) { throw 'new did not scaffold C++' }
Put "$scaffold/src/main.cpp" @'
#include <cstring>
#include <iostream>
int main(int argc, char** argv) {
    if (argc != 3 || std::strcmp(argv[1], "two words") || std::strcmp(argv[2], "--flag")) return 23;
    std::cout << "arguments preserved\n";
}
'@
Invoke-Clutch configure $scaffold | Out-Null
$ran = Invoke-Clutch run $scaffold @('--', 'two words', '--flag')
if ($ran -notmatch 'arguments preserved') { throw 'run lost program arguments' }
Invoke-Clutch run $scaffold @('--', 'wrong') $true | Write-Host
# Persisted tool selection must work without repeating compiler/backend options.
& $Clutch $scaffold
if ($LASTEXITCODE -ne 0) { throw 'cached toolchain not reused' }
$toolchain = Get-Content -Raw "$scaffold/Clutch-build-debug/toolchain.toml"
if ($toolchain -notmatch 'cxx') { throw 'compiler toolchain cache missing' }
$backend = Get-Content -Raw "$scaffold/Clutch-build-debug/backend.toml"
if ($backend -notmatch 'ninja' -or $backend -notmatch 'executable') { throw 'backend cache missing' }

# Stage 4: saved selections must survive an environment change; process variables
# are restored in finally so later checks and the parent shell remain predictable.
# Verify backend persistence without a backend-specific environment variable,
# then check the generic environment and explicit executable override priorities.
$savedToolEnvironment = @{}
foreach ($variable in @('NINJA', 'CLUTCH_BUILD_TOOL', 'CC', 'CXX')) {
    $savedToolEnvironment[$variable] = [Environment]::GetEnvironmentVariable($variable, 'Process')
    [Environment]::SetEnvironmentVariable($variable, $null, 'Process')
}
try {
    & $Clutch $scaffold
    if ($LASTEXITCODE -ne 0) { throw 'cached backend executable not reused' }

    $env:CLUTCH_BUILD_TOOL = $Ninja
    $env:NINJA = 'missing-backend-executable'
    & $Clutch $scaffold
    if ($LASTEXITCODE -ne 0) { throw 'generic backend environment override failed' }

    $env:CLUTCH_BUILD_TOOL = 'missing-backend-executable'
    Invoke-Clutch configure $scaffold | Out-Null
} finally {
    foreach ($variable in $savedToolEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($variable, $savedToolEnvironment[$variable], 'Process')
    }
}

# Stage 5: discover manifest-declared tests, filter names, aggregate failures, and
# keep test compilation units visible in the same profile's IDE database.
Put "$scaffold/test/pass.cpp" 'int main() { return 0; }'
Put "$scaffold/test/fail.c" 'int main(void) { return 7; }'
$manifest = Get-Content -Raw "$scaffold/clutch.toml"
Put "$scaffold/clutch.toml" ($manifest + @'

[[test]]
name = "passing"
sources = ["test/pass.cpp"]

[[test]]
name = "failing"
sources = ["test/fail.c"]
'@)
Invoke-Clutch configure $scaffold | Out-Null
$passed = Invoke-Clutch test $scaffold @('--filter', 'passing')
if ($passed -notmatch '1 passed; 0 failed') { throw "test execution failed: $passed" }
$failed = Invoke-Clutch test $scaffold @() $true
if ($failed -notmatch '1 passed; 1 failed') { throw "test failures not aggregated: $failed" }
Invoke-Clutch test $scaffold @('--filter', 'nonexistent') $true | Write-Host
$db = Get-Content -Raw "$scaffold/Clutch-build-debug/compile_commands.json" | ConvertFrom-Json
if (@($db).Count -ne 3) { throw 'test compilation commands missing' }

# Clean only the selected, Clutch-owned profile; keep sources and release output.
Invoke-Clutch configure $scaffold @('--release') | Out-Null
Invoke-Clutch build $scaffold @('--release') | Write-Host
& $Clutch clean --project $scaffold
if ($LASTEXITCODE -ne 0) { throw 'clean failed' }
if (Test-Path "$scaffold/Clutch-build-debug") { throw 'clean left debug artifacts' }
if (-not (Test-Path "$scaffold/Clutch-build-release/example.exe")) { throw 'clean removed other profile' }
if (-not (Test-Path "$scaffold/src/main.cpp")) { throw 'clean removed sources' }

# Stage 6: initialize existing C sources without overwriting them or a manifest.
$existing = Join-Path $work 'existing'
Put "$existing/src/main.c" 'int main(void) { return 0; }'
$before = Get-Content -Raw "$existing/src/main.c"
& $Clutch init $existing --language c
if ($LASTEXITCODE -ne 0) { throw 'init failed' }
if ((Get-Content -Raw "$existing/src/main.c") -ne $before) { throw 'init overwrote source' }
Invoke-Clutch configure $existing | Out-Null
Invoke-Clutch build $existing | Write-Host
# Reinitialization must fail without changing the manifest.
$previousPreference = $ErrorActionPreference
try {
    $ErrorActionPreference = 'Continue'
    $ignored = & $Clutch init $existing 2>&1
    if ($LASTEXITCODE -eq 0) { throw 'init accepted an existing manifest' }
} finally { $ErrorActionPreference = $previousPreference }
Write-Host 'PASS: new/init, run argument forwarding and failures, cached tools, tests/filter/failures, profile-safe clean'

# Stage 7: profile-specific paths (including spaces) control all artifacts. Invoke
# Ninja directly after a manifest edit to verify automatic Clutch regeneration.
$custom = Join-Path $work 'custom profiles'
Put "$custom/clutch.toml" @'
[project]
name = "custom"
version = "0.1.0"
language = "c++"
std = "17"

[build]
generator = "Ninja"

[profile.debug]
build_dir = "out/debug with spaces/"

[profile.release]
build_dir = "out/release with spaces/"
'@
Put "$custom/src/main.cpp" 'int main() { return 0; }'
Invoke-Clutch configure $custom | Out-Null
Invoke-Clutch build $custom | Write-Host
Invoke-Clutch configure $custom @('--release') | Out-Null
Invoke-Clutch build $custom @('--release') | Write-Host
foreach ($profile in @('debug', 'release')) {
    $dir = "$custom/out/$profile with spaces"
    if (-not (Test-Path "$dir/build.ninja") -or -not (Test-Path "$dir/custom.exe")) { throw 'custom profile output missing' }
    $db = Get-Content -Raw "$dir/compile_commands.json" | ConvertFrom-Json
    if ($db[0].directory -ne (Resolve-Path $dir).Path) { throw 'custom database directory incorrect' }
}
if (Test-Path "$custom/compile_commands.json") { throw 'custom database leaked into root' }
if (Test-Path "$custom/Clutch-build-debug") { throw 'custom build ignored profile directory' }
# The generated regeneration command must keep using the manifest's directory.
Start-Sleep -Milliseconds 1100
Add-Content "$custom/clutch.toml" "`n# trigger Ninja regeneration"
$regenerated = & $Ninja -C "$custom/out/debug with spaces" 2>&1
if ($LASTEXITCODE -ne 0) { throw "backend regeneration failed: $regenerated" }
if (($regenerated -join "`n") -notmatch 'Configuring custom') { throw 'backend did not regenerate' }
& $Clutch clean --project $custom
if ($LASTEXITCODE -ne 0) { throw 'custom profile clean failed' }
if (Test-Path "$custom/out/debug with spaces") { throw 'custom clean left artifacts' }
if (-not (Test-Path "$custom/out/release with spaces/custom.exe")) { throw 'custom clean removed release profile' }
Invoke-Clutch configure $custom @('--build-dir', 'not-allowed') $true | Write-Host

# Stage 8: invalid language, standard, backend, and directory declarations must
# fail early. Unowned directories must never be repurposed or cleaned by Clutch.
$validation = Join-Path $work 'validation'
Put "$validation/src/main.cpp" 'int main() { return 0; }'
foreach ($entry in @(
    @{ Name = 'legacy edition'; Project = "edition = '17'"; Build = ''; Profile = '' },
    @{ Name = 'invalid std'; Project = 'std = 42'; Build = ''; Profile = '' },
    @{ Name = 'floating std'; Project = 'std = 17.0'; Build = ''; Profile = '' },
    @{ Name = 'language mismatch'; Project = "language = 'c'`nstd = 11"; Build = ''; Profile = '' },
    @{ Name = 'unknown generator'; Project = 'std = 17'; Build = "[build]`ngenerator = 'not-a-backend'"; Profile = '' },
    @{ Name = 'empty directory'; Project = 'std = 17'; Build = ''; Profile = "[profile.debug]`nbuild_dir = ''" },
    @{ Name = 'source directory'; Project = 'std = 17'; Build = ''; Profile = "[profile.debug]`nbuild_dir = 'src'" },
    @{ Name = 'dependency workspace'; Project = 'std = 17'; Build = ''; Profile = "[profile.debug]`nbuild_dir = '.clutch/cache'" },
    @{ Name = 'root directory'; Project = 'std = 17'; Build = ''; Profile = "[profile.debug]`nbuild_dir = '.'" }
)) {
    Put "$validation/clutch.toml" ("[project]`nname = 'validation'`nversion = '0.1.0'`n" + $entry.Project + "`n" + $entry.Build + "`n" + $entry.Profile)
    Invoke-Clutch configure $validation @() $true | Out-Null
    Write-Host "PASS: rejected $($entry.Name)"
}
Put "$validation/user-data/keep.txt" 'preserve this file'
Put "$validation/clutch.toml" "[project]`nname = 'validation'`nversion = '0.1.0'`nstd = 17`n[profile.debug]`nbuild_dir = 'user-data'"
Invoke-Clutch configure $validation @() $true | Out-Null
$previousPreference = $ErrorActionPreference
try {
    $ErrorActionPreference = 'Continue'
    $ignored = & $Clutch clean --project $validation 2>&1
    if ($LASTEXITCODE -eq 0) { throw 'clean accepted an unowned build directory' }
} finally { $ErrorActionPreference = $previousPreference }
if ((Get-Content -Raw "$validation/user-data/keep.txt") -ne 'preserve this file') { throw 'unowned directory was changed' }
Write-Host 'PASS: profile directories, build-local databases, backend regeneration, manifest validation and clean ownership'

# Stage 9: exercise a C++ layout with headers beside implementations, nested
# modules, and no separate include directory or extra include-path settings.
$colocated = Join-Path $work 'colocated modules'
Put "$colocated/clutch.toml" "[project]`nname = 'colocated'`nversion = '0.1.0'`nlanguage = 'cpp'`nstd = 17"
Put "$colocated/src/math/value.hpp" "#pragma once`nint value();`n"
Put "$colocated/src/math/value.cpp" ('#include "value.hpp"' + "`n" + 'int value() { return 42; }')
Put "$colocated/src/main.cpp" ('#include "math/value.hpp"' + "`n" + 'int main() { return value() == 42 ? 0 : 1; }')
Invoke-Clutch configure $colocated | Out-Null
Invoke-Clutch run $colocated | Write-Host
if (Test-Path "$colocated/include") { throw 'co-located layout unexpectedly required an include directory' }
$colocatedDatabase = Get-Content -Raw "$colocated/Clutch-build-debug/compile_commands.json" | ConvertFrom-Json
if (@($colocatedDatabase).Count -ne 2) { throw 'nested source discovery omitted a module' }
Write-Host 'PASS: co-located C++ headers/sources and recursive module discovery'

# Stage 10: building consumes saved state. It neither creates missing profiles
# nor silently accepts a manifest that changed after configuration.
$state = Join-Path $work 'configuration lifecycle'
Put "$state/clutch.toml" "[project]`nname = 'state'`nversion = '0.1.0'`nstd = 17"
Put "$state/src/main.cpp" 'int main() { return 0; }'
$missing = Invoke-Clutch build $state @() $true
if ($missing -notmatch 'not configured') { throw "missing configuration diagnosis unclear: $missing" }
if (Test-Path "$state/Clutch-build-debug/state.exe") { throw 'unconfigured build compiled sources' }
Invoke-Clutch configure $state | Out-Null
Invoke-Clutch build $state | Out-Null
$missingRelease = Invoke-Clutch build $state @('--release') $true
if ($missingRelease -notmatch 'not configured') { throw "missing release diagnosis unclear: $missingRelease" }
Add-Content "$state/clutch.toml" "`n# changed after configuration"
$stale = Invoke-Clutch build $state @() $true
if ($stale -notmatch 'changed since configuration') { throw "stale configuration diagnosis unclear: $stale" }
Invoke-Clutch configure $state | Out-Null
Invoke-Clutch build $state | Out-Null
Write-Host 'PASS: missing profile configuration and stale manifest rejection'

# Stage 11: cmd.exe discovers bare executable names in the current directory.
# Configuration must identify that running copy even when its directory is not
# on PATH, and generated regeneration commands must continue to use that copy.
$localExecutable = Join-Path $work 'local executable with spaces'
Put "$localExecutable/clutch.toml" "[project]`nname = 'local'`nversion = '0.1.0'`nstd = 17"
Put "$localExecutable/src/main.cpp" 'int main() { return 0; }'
Copy-Item -LiteralPath $Clutch -Destination "$localExecutable/clutch.exe"
Push-Location -LiteralPath $localExecutable
try {
    foreach ($invocation in @('clutch', 'clutch.exe', '.\clutch.exe')) {
        $command = $invocation + ' . --build-tool "' + $Ninja + '" --cc "' + $CC + '" --cxx "' + $CXX + '"'
        $configured = & $env:ComSpec /d /c $command 2>&1
        if ($LASTEXITCODE -ne 0) { throw "Local invocation failed: $invocation : $configured" }
        if (($configured -join "`n") -notmatch 'Configured local') { throw 'local configuration output missing' }
    }

    Add-Content './clutch.toml' "`n# exercise regeneration with the resolved executable path"
    $regenerated = & $Ninja -C './Clutch-build-debug' 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Local executable regeneration failed: $regenerated" }
    if (($regenerated -join "`n") -notmatch 'Configuring local') { throw 'local executable did not regenerate' }
    & $env:ComSpec /d /c 'clutch run'
    if ($LASTEXITCODE -ne 0) { throw 'local executable run failed' }
} finally {
    Pop-Location
}
Write-Host 'PASS: bare, extension-qualified and relative local invocations, regeneration and execution'

# Stage 12: fresh profile directories discover bundled Ninja without a developer
# terminal. Fake installation roots keep this independent of installed IDEs.
$resolvedCC = (Get-Command $CC -CommandType Application -ErrorAction Stop).Source
$resolvedCXX = (Get-Command $CXX -CommandType Application -ErrorAction Stop).Source
$discoveryRunner = Join-Path $work 'discovery runner'
New-Item -ItemType Directory -Force $discoveryRunner | Out-Null
$discoveryClutch = Join-Path $discoveryRunner 'clutch.exe'
Copy-Item -LiteralPath $Clutch -Destination $discoveryClutch
# Development MinGW binaries may need runtime DLLs. Place them beside the copy
# so restricting PATH tests tool discovery rather than Windows DLL loading.
$runtimeDirectories = @((Split-Path -Parent $Clutch), (Split-Path -Parent $resolvedCXX)) + ($env:PATH -split ';')
foreach ($pattern in @('libgcc_s_*.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll')) {
    foreach ($directory in $runtimeDirectories) {
        if (-not $directory) { continue }
        $libraries = @(Get-ChildItem -LiteralPath $directory -Filter $pattern -File -ErrorAction SilentlyContinue)
        if ($libraries.Count -gt 0) {
            foreach ($library in $libraries) {
                Copy-Item -LiteralPath $library.FullName -Destination $discoveryRunner
            }
            break
        }
    }
}

$savedDiscoveryEnvironment = @{}
foreach ($variable in @('PATH', 'ProgramFiles', 'ProgramFiles(x86)', 'ProgramW6432', 'VSINSTALLDIR', 'NINJA', 'CLUTCH_BUILD_TOOL')) {
    $savedDiscoveryEnvironment[$variable] = [Environment]::GetEnvironmentVariable($variable, 'Process')
}
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    foreach ($variable in @('VSINSTALLDIR', 'NINJA', 'CLUTCH_BUILD_TOOL')) {
        [Environment]::SetEnvironmentVariable($variable, $null, 'Process')
    }

    foreach ($bundle in @(
        @{ Name = 'bundled VS'; Relative = 'Microsoft Visual Studio/2099/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe' },
        @{ Name = 'bundled CLion'; Relative = 'JetBrains/CLion 2099.1/bin/ninja/win/x64/ninja.exe' }
    )) {
        $bundleRoot = Join-Path $work $bundle.Name
        $bundledExecutable = Join-Path $bundleRoot $bundle.Relative
        New-Item -ItemType Directory -Force (Split-Path -Parent $bundledExecutable) | Out-Null
        Copy-Item -LiteralPath $Ninja -Destination $bundledExecutable
        [Environment]::SetEnvironmentVariable('ProgramFiles', $bundleRoot, 'Process')
        # Windows derives a 64-bit child's ProgramFiles from ProgramW6432.
        [Environment]::SetEnvironmentVariable('ProgramW6432', $bundleRoot, 'Process')
        [Environment]::SetEnvironmentVariable('ProgramFiles(x86)', "$bundleRoot/unused", 'Process')
        $discovered = Join-Path $work ($bundle.Name + ' project')
        $discoveryManifest = "[project]`nname = 'discovered'`nversion = '0.1.0'`nstd = 17`n[build]`ngenerator = 'ninja'`n"
        Put "$discovered/src/main.cpp" 'int main() { return 0; }'

        foreach ($directory in @('out/debug', 'out/changed-debug')) {
            Put "$discovered/clutch.toml" ($discoveryManifest + "[profile.debug]`nbuild_dir = '$directory'`n")
            foreach ($attempt in @(1, 2)) {
                $configured = & $discoveryClutch $discovered --cc $resolvedCC --cxx $resolvedCXX 2>&1
                if ($LASTEXITCODE -ne 0) { throw "Bundled discovery/reconfiguration failed: $configured" }
                $cache = Get-Content -Raw "$discovered/$directory/backend.toml"
                if ($cache -notmatch [regex]::Escape($bundle.Name) -or $cache -notmatch 'ninja.exe') {
                    throw "Bundled tool selection was not persisted: $cache"
                }
            }
        }

        # A broken explicit selection must be diagnosed rather than replaced.
        $previousPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            $rejected = & $discoveryClutch $discovered --build-tool "$bundleRoot/missing-ninja.exe" --cc $resolvedCC --cxx $resolvedCXX 2>&1
            if ($LASTEXITCODE -eq 0 -or ($rejected -join "`n") -notmatch 'cannot find build generator executable') {
                throw 'bundled discovery replaced an invalid explicit tool selection'
            }
        } finally { $ErrorActionPreference = $previousPreference }
    }
} finally {
    foreach ($variable in $savedDiscoveryEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($variable, $savedDiscoveryEnvironment[$variable], 'Process')
    }
}
Write-Host 'PASS: VS/CLion Ninja discovery without PATH, repeated configuration, changed profile directories and explicit tool precedence'

# Stage 13: compile real C++17 code while checking the selected standard and
# warning policy in every application/test IDE entry. Deprecation is enabled by
# default in these compilers, so omitting -Wall alone cannot satisfy "none".
$policy = Join-Path $work 'language and warning policy'
Put "$policy/src/main.cpp" @'
#include <optional>

#ifdef _MSVC_LANG
static_assert(_MSVC_LANG == 201703L, "Clutch must select C++17");
#else
static_assert(__cplusplus == 201703L, "Clutch must select C++17");
#endif

[[deprecated("CLUTCH_PROFILE_WARNING")]] int legacy() { return 42; }

int main() {
    const std::optional value{legacy()};
    return *value == 42 ? 0 : 1;
}
'@
foreach ($warnings in @('default', 'none', 'extra')) {
    Put "$policy/clutch.toml" @"
[project]
name = "policy"
version = "0.1.0"
language = "cpp"
std = 17
entry = "src/main.cpp"

[profile.debug]
warnings = "$warnings"

[profile.release]
warnings = "none"

[[test]]
name = "policy_test"
sources = ["src/main.cpp"]
"@
    Invoke-Clutch configure $policy | Out-Null
    $database = Get-Content -Raw "$policy/Clutch-build-debug/compile_commands.json" | ConvertFrom-Json
    if (@($database).Count -ne 2) { throw 'policy database omitted the application or test' }
    foreach ($unit in $database) {
        if (-not ($unit.arguments -contains '-std=c++17' -or $unit.arguments -contains '/std:c++17')) {
            throw 'C++17 was not propagated to the compilation database'
        }
        $suppressed = $unit.arguments -contains '-w' -or $unit.arguments -contains '/W0'
        if ($suppressed -ne ($warnings -eq 'none')) { throw 'warning suppression does not match the profile' }
    }

    $compiled = (Invoke-Clutch build $policy) + (Invoke-Clutch test $policy)
    if (($compiled -match 'CLUTCH_PROFILE_WARNING') -ne ($warnings -ne 'none')) {
        throw "Compiler output did not respect warnings = $warnings : $compiled"
    }
}
Invoke-Clutch configure $policy @('--release') | Out-Null
$releaseOutput = (Invoke-Clutch build $policy @('--release')) + (Invoke-Clutch test $policy @('--release'))
if ($releaseOutput -match 'CLUTCH_PROFILE_WARNING') { throw 'release warnings = none still emitted warnings' }

$legacyManifest = Join-Path $work 'legacy manifest table'
Put "$legacyManifest/src/main.cpp" 'int main() { return 0; }'
Put "$legacyManifest/clutch.toml" "[package]`nname = 'legacy'`nversion = '0.1.0'`nstd = 17"
$migration = Invoke-Clutch configure $legacyManifest @() $true
if ($migration -notmatch 'rename \[package\] to \[project\]') { throw 'legacy manifest migration instruction missing' }
Write-Host 'PASS: C++17 compiler mode, warning defaults/suppression, application/test databases, release policy and project table migration'

Write-Host "All integration checks passed. Fixtures retained in $work"
