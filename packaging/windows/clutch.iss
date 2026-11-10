; Copyright (c) 2026, Pranav Sanjeevi Kumari and contributors
; SPDX-License-Identifier: BSD-3-Clause
;
; Per-user Windows installer for Clutch (Inno Setup 6). Release automation
; compiles it from a CMake install tree:
;
;   ISCC /DAppVersion=<version> /DSourceDir=<install tree> /O<output dir>
;        /F<output base name> packaging\windows\clutch.iss
;
; The installer needs no administrator rights. It installs into the user's
; program directory, optionally appends <app>\bin to the user PATH and removes
; exactly that entry on uninstall.

#ifndef AppVersion
  #error Define AppVersion, for example /DAppVersion=1.2.3
#endif
#ifndef SourceDir
  #error Define SourceDir as the CMake install tree containing bin\clutch.exe
#endif
#ifndef OutputBaseName
  #define OutputBaseName "clutch-" + AppVersion + "-windows-x86_64-msvc-setup"
#endif

[Setup]
; Never change AppId: upgrades and uninstall entries are keyed by it.
AppId={{DC25D936-A69B-4263-819C-693884A70A63}
AppName=Clutch
AppVersion={#AppVersion}
AppVerName=Clutch {#AppVersion}
AppPublisher=Pranav Sanjeevi Kumari and contributors
AppPublisherURL=https://github.com/Pranav-32/Clutch
AppSupportURL=https://github.com/Pranav-32/Clutch/issues
AppUpdatesURL=https://github.com/Pranav-32/Clutch/releases
; {autopf} is %LOCALAPPDATA%\Programs for a per-user (lowest privilege) install.
DefaultDirName={autopf}\Clutch
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Broadcast WM_SETTINGCHANGE so newly started terminals see the PATH change.
ChangesEnvironment=yes
LicenseFile={#SourceDir}\share\licenses\clutch\LICENSE
OutputBaseFilename={#OutputBaseName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile={#SourcePath}..\..\assets\branding\clutch.ico
UninstallDisplayIcon={app}\clutch.ico
UninstallDisplayName=Clutch {#AppVersion}
VersionInfoVersion={#Copy(AppVersion, 1, Pos("-", AppVersion + "-") - 1)}

[Tasks]
Name: "addtopath"; Description: "Add Clutch to my PATH (current user)"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#SourcePath}..\..\assets\branding\clutch.ico"; DestDir: "{app}"; Flags: ignoreversion

[Code]
const
  EnvironmentKey = 'Environment';

{ Compare PATH entries case-insensitively, ignoring surrounding spaces,
  quotes and one trailing backslash. }
function NormalizeEntry(const Value: String): String;
begin
  Result := Trim(Value);
  if (Length(Result) >= 2) and (Result[1] = '"') and
     (Result[Length(Result)] = '"') then
    Result := Copy(Result, 2, Length(Result) - 2);
  Result := Lowercase(RemoveBackslashUnlessRoot(Result));
end;

{ Rebuild a PATH value without entries equal to Entry. Other entries,
  including empty ones, keep their order and spelling. }
function WithoutEntry(const Path, Entry: String; var Found: Boolean): String;
var
  Rest, Item: String;
  Separator: Integer;
  First: Boolean;
begin
  Result := '';
  Found := False;
  First := True;
  Rest := Path;
  while True do
  begin
    Separator := Pos(';', Rest);
    if Separator = 0 then
      Item := Rest
    else
      Item := Copy(Rest, 1, Separator - 1);

    if (Item <> '') and (NormalizeEntry(Item) = NormalizeEntry(Entry)) then
      Found := True
    else
    begin
      if not First then
        Result := Result + ';';
      Result := Result + Item;
      First := False;
    end;

    if Separator = 0 then
      Break;
    Delete(Rest, 1, Separator);
  end;
end;

procedure AddToUserPath(const Entry: String);
var
  Path: String;
  Found: Boolean;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, EnvironmentKey, 'Path', Path) then
    Path := '';
  WithoutEntry(Path, Entry, Found);
  if Found then
  begin
    Log('PATH already contains ' + Entry);
    Exit;
  end;

  { Keep the value's separator style: after "A;B;" append "X;" so removing X
    restores "A;B;" exactly; after "A;B" append ";X". }
  if Path = '' then
    Path := Entry
  else if Path[Length(Path)] = ';' then
    Path := Path + Entry + ';'
  else
    Path := Path + ';' + Entry;
  { Preserve REG_EXPAND_SZ so existing %VARIABLE% entries keep expanding. }
  if RegWriteExpandStringValue(HKEY_CURRENT_USER, EnvironmentKey, 'Path', Path) then
    Log('Added to user PATH: ' + Entry)
  else
    MsgBox('Clutch was installed, but the user PATH could not be updated. ' +
           'Add ' + Entry + ' to PATH manually.', mbError, MB_OK);
end;

procedure RemoveFromUserPath(const Entry: String);
var
  Path, Updated: String;
  Found: Boolean;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, EnvironmentKey, 'Path', Path) then
    Exit;
  Updated := WithoutEntry(Path, Entry, Found);
  if not Found then
    Exit;

  if RegWriteExpandStringValue(HKEY_CURRENT_USER, EnvironmentKey, 'Path', Updated) then
    Log('Removed from user PATH: ' + Entry)
  else
    Log('Could not remove from user PATH: ' + Entry);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('addtopath') then
    AddToUserPath(ExpandConstant('{app}\bin'));
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RemoveFromUserPath(ExpandConstant('{app}\bin'));
end;
