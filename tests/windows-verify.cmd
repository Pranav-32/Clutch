@echo off
rem Run integration checks from the repository root against MSVC and Clang.
rem Arguments: vcvars64.bat path, ninja.exe path, optional Clutch executable path,
rem optional --native-generators to also exercise NMake, MSBuild and available JOM.
rem Returns 2 for missing arguments, the failing command's status, or 0 on success.
setlocal

if "%~2"=="" (
  echo Usage: windows-verify.cmd "path\to\vcvars64.bat" "path\to\ninja.exe" ["path\to\clutch.exe"] [--native-generators]
  exit /b 2
)

rem Prepare the SDK, compiler, and linker environment only for this script.
call "%~1"
if errorlevel 1 exit /b %errorlevel%

set "NINJA=%~2"
set "CLUTCH_TEST_BINARY=%~3"
if "%CLUTCH_TEST_BINARY%"=="" set "CLUTCH_TEST_BINARY=build\Release\clutch.exe"

rem The first pass uses cl for both languages; the second explicitly selects Clang.
powershell -NoProfile -ExecutionPolicy Bypass -File tests\integration.ps1 -Clutch "%CLUTCH_TEST_BINARY%" -Ninja "%NINJA%"
if errorlevel 1 exit /b %errorlevel%

powershell -NoProfile -ExecutionPolicy Bypass -File tests\integration.ps1 -Clutch "%CLUTCH_TEST_BINARY%" -Ninja "%NINJA%" -CC clang -CXX clang++
if errorlevel 1 exit /b %errorlevel%

rem Native Windows generators reuse the initialized compiler and SDK environment.
rem Opt in explicitly so machines with only Ninja preserve the original workflow.
if not "%~4"=="--native-generators" exit /b 0

powershell -NoProfile -ExecutionPolicy Bypass -File tests\native-generators.ps1 -Clutch "%CLUTCH_TEST_BINARY%"
exit /b %errorlevel%
