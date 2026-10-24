; Per-user installer, TokenViewer-windows-setup.exe. qt\scripts\package-windows.ps1 compiles it with Inno Setup 6:
;   ISCC.exe /DAppVersion=<version> /DSourceDir=<qt\dist\TokenViewer> /DIconFile=<build dir>\TokenViewer.ico TokenViewer.iss
; Silent install (install.ps1): TokenViewer-windows-setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART [/DIR=<folder>]
; Silent uninstall:             unins000.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART [/PURGE]
;   /PURGE also deletes the settings (HKCU\Software\TokenViewer) and logs (%LOCALAPPDATA%\TokenViewer);
;   an interactive uninstall asks instead. Both keep them by default.

#ifndef AppVersion
  #error Pass the app version: /DAppVersion=0.1.4
#endif
#ifndef SourceDir
  #error Pass the deployed app folder: /DSourceDir=<qt\dist\TokenViewer>
#endif
#ifndef IconFile
  #error Pass the app icon: /DIconFile=<build dir>\TokenViewer.ico
#endif

#define AppName "TokenViewer"
#define AppExeName "TokenViewer.exe"
#define AppUrl "https://github.com/jdragon96/TokenViewer"
#define SourceExe AddBackslash(SourceDir) + AppExeName
#if !FileExists(SourceExe)
  #error SourceDir has no TokenViewer.exe
#endif
; The binary version Windows shows comes from the exe, so the setup and the app always agree.
#define AppVersionNumbers GetVersionNumbersString(SourceExe)

[Setup]
; Never change this: Windows and later setups find the installed app by it (install.ps1 has it too).
AppId={{CFD0D0E6-2DEE-4F55-95EB-05DE69CA8A73}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=jy.seong
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}/issues
AppUpdatesURL={#AppUrl}/releases
AppCopyright=Copyright (c) 2026 jy.seong. MIT License.
; Per user without UAC, in the folder install.ps1 has always used (%LOCALAPPDATA%\Programs\TokenViewer).
PrivilegesRequired=lowest
DefaultDirName={autopf}\{#AppName}
; No folder page: a folder picked in the wizard could be shared with other programs (C:\Tools). Only /DIR=
; (install.ps1's TV_INSTALL_DIR) installs elsewhere, and an update stays in the folder it was installed to.
DisableDirPage=yes
AlwaysShowDirOnReadyPage=yes
DisableProgramGroupPage=yes
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#AppExeName}
; Windows 10 1903
MinVersion=10.0.18362
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
ShowLanguageDialog=no
UsePreviousLanguage=no
; [Code] closes the app first; Restart Manager only catches what is left and must never start it again.
RestartApplications=no
Compression=lzma2/ultra64
SolidCompression=yes
OutputDir=..\dist
OutputBaseFilename=TokenViewer-windows-setup
SetupIconFile={#IconFile}
VersionInfoVersion={#AppVersionNumbers}
VersionInfoTextVersion={#AppVersion}

[Languages]
; The first entry is the fallback for every UI language other than Korean.
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"

[CustomMessages]
english.AppComment=Claude usage
korean.AppComment=Claude 사용량
english.AppStillRunning=Setup could not close TokenViewer. Quit TokenViewer and run Setup again.
korean.AppStillRunning=TokenViewer 를 끝내지 못했습니다. TokenViewer 를 끝내고 설치 프로그램을 다시 실행하세요.
english.DeleteUserDataQuestion=Also delete TokenViewer's settings and logs?%n%nChoose No to keep them for a later reinstall.
korean.DeleteUserDataQuestion=TokenViewer 설정과 로그도 지울까요?%n%n다시 설치해서 쓰려면 [아니요] 를 누르세요.

; One entry per subfolder of the deployed folder (platforms, licenses), so a folder added later is covered too.
#define FindHandle
#define FindResult
#define Subfolder
#sub EmitInstallDeleteEntry
  #define Subfolder FindGetFileName(FindHandle)
  #if Subfolder != "." && Subfolder != ".." && DirExists(AddBackslash(SourceDir) + Subfolder)
Type: filesandordirs; Name: "{app}\{#Subfolder}"; Check: IsUpgradeInPlace
  #endif
#endsub
#sub EmitDirsEntry
  #define Subfolder FindGetFileName(FindHandle)
  #if Subfolder != "." && Subfolder != ".." && DirExists(AddBackslash(SourceDir) + Subfolder)
Name: "{app}\{#Subfolder}"; Flags: uninsalwaysuninstall
  #endif
#endsub

[InstallDelete]
; Qt loads every plugin it finds in these folders, so a file an older version left there must not outlive it.
#for {FindHandle = FindResult = FindFirst(AddBackslash(SourceDir) + "*", faDirectory); FindResult; FindResult = FindNext(FindHandle)} EmitInstallDeleteEntry
#if FindHandle
  #expr FindClose(FindHandle)
#endif

[Dirs]
; Removed when empty even if they existed before (a zip install from the old install.ps1 upgraded in place).
Name: "{app}"; Flags: uninsalwaysuninstall
#for {FindHandle = FindResult = FindFirst(AddBackslash(SourceDir) + "*", faDirectory); FindResult; FindResult = FindNext(FindHandle)} EmitDirsEntry
#if FindHandle
  #expr FindClose(FindHandle)
#endif

[Files]
Source: "{#AddBackslash(SourceDir)}*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; Comment: "{cm:AppComment}"

[Run]
; Never in silent mode: install.ps1 installs silently and starts the app itself.
Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[Code]
const
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';
  SettingsKey = 'Software\{#AppName}';
  QuitTimeoutMs = 5000;
  PollIntervalMs = 100;

var
  DeleteUserDataRequested: Boolean;

function GetTickCount: Cardinal;
  external 'GetTickCount@kernel32.dll stdcall';

function GetAppExePath: String;
begin
  Result := ExpandConstant('{app}\{#AppExeName}');
end;

function GetDataDir: String;
begin
  Result := ExpandConstant('{localappdata}\{#AppName}');
end;

// Counts the TokenViewer processes started from ExePath and ends them when Terminate is set.
// A copy started from anywhere else (a build folder, another install) is never touched.
function ForEachAppProcess(const ExePath: String; const Terminate: Boolean): Integer;
var
  Locator, Service, Processes, Process: Variant;
  ProcessPath: String;
  ProcessId, I: Integer;
begin
  Result := 0;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Processes := Service.ExecQuery('SELECT Handle, ProcessId, ExecutablePath FROM Win32_Process WHERE Name = ''{#AppExeName}''');
    for I := 0 to Processes.Count - 1 do
    begin
      Process := Processes.ItemIndex(I);
      // Null for other users' processes.
      if not VarIsNull(Process.ExecutablePath) then
      begin
        ProcessPath := Process.ExecutablePath;
        if SameText(ProcessPath, ExePath) then
        begin
          Result := Result + 1;
          if Terminate then
          begin
            ProcessId := Process.ProcessId;
            Log(Format('Ending process %d: %s', [ProcessId, ProcessPath]));
            Process.Terminate(1);
          end;
        end;
      end;
    end;
  except
    Log('Cannot list processes: ' + GetExceptionMessage);
  end;
end;

function WaitForAppExit(const ExePath: String; const TimeoutMs: Cardinal): Boolean;
var
  StartTicks: Cardinal;
begin
  StartTicks := GetTickCount;
  while (ForEachAppProcess(ExePath, False) > 0) and (GetTickCount - StartTicks < TimeoutMs) do
    Sleep(PollIntervalMs);
  Result := ForEachAppProcess(ExePath, False) = 0;
end;

// Closes the copy running from the install folder before its files are replaced or removed: its own --quit
// first, then by force. The --quit pipe is per user, not per folder, so it is only sent while a copy from
// the install folder runs (that copy then holds the pipe); any other TokenViewer is never asked or ended.
function StopRunningApp: Boolean;
var
  ExePath: String;
  ResultCode: Integer;
begin
  ExePath := GetAppExePath;
  Result := ForEachAppProcess(ExePath, False) = 0;
  if Result then
    Exit;
  Log('TokenViewer is running from ' + ExePath + '; asking it to quit');
  // The quitting copy also runs from the install folder and is counted until it exits.
  Exec(ExePath, '--quit', ExtractFileDir(ExePath), SW_HIDE, ewNoWait, ResultCode);
  if not WaitForAppExit(ExePath, QuitTimeoutMs) then
  begin
    Log('TokenViewer did not quit in time; ending it');
    ForEachAppProcess(ExePath, True);
    WaitForAppExit(ExePath, QuitTimeoutMs);
  end;
  Result := ForEachAppProcess(ExePath, False) = 0;
end;

function InitializeSetup: Boolean;
begin
  Log('Language: ' + ActiveLanguage);
  Result := True;
end;

// [InstallDelete] only clears folders this installer filled before, never a folder it does not own.
function IsUpgradeInPlace: Boolean;
begin
  Result := (WizardForm.PrevAppDir <> '') and
    SameText(RemoveBackslashUnlessRoot(WizardForm.PrevAppDir), RemoveBackslashUnlessRoot(ExpandConstant('{app}')));
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Log('Previous install folder: ' + WizardForm.PrevAppDir);
  if StopRunningApp then
    Result := ''
  else
    Result := CustomMessage('AppStillRunning');
end;

function HasSwitch(const Name: String): Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do
    if SameText(ParamStr(I), Name) then
      Result := True;
end;

// The app writes this value while "launch at login" is on; only a value that starts this copy is removed.
procedure RemoveLoginItem;
var
  Command: String;
begin
  if RegQueryStringValue(HKCU, RunKey, '{#AppName}', Command) and SameText(RemoveQuotes(Command), GetAppExePath) then
  begin
    Log('Removing the login item');
    RegDeleteValue(HKCU, RunKey, '{#AppName}');
  end;
end;

function HasUserData: Boolean;
begin
  Result := RegKeyExists(HKCU, SettingsKey) or DirExists(GetDataDir);
end;

procedure DeleteUserData;
begin
  Log('Deleting the settings and logs');
  RegDeleteKeyIncludingSubkeys(HKCU, SettingsKey);
  DelTree(GetDataDir, True, True, True);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    StopRunningApp;
    // Kept unless asked for: /PURGE when silent, a question (default No) otherwise.
    if UninstallSilent then
      DeleteUserDataRequested := HasSwitch('/PURGE')
    else
      DeleteUserDataRequested := HasUserData and
        (MsgBox(CustomMessage('DeleteUserDataQuestion'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES);
  end
  else if CurUninstallStep = usPostUninstall then
  begin
    RemoveLoginItem;
    if DeleteUserDataRequested then
      DeleteUserData;
  end;
end;
