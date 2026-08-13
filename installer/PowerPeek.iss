; PowerPeek installer -- Inno Setup 6.3 or newer.
;
; Build it with tools\package.ps1, which also produces the portable zip; or directly:
;   ISCC.exe installer\PowerPeek.iss
; Both expect build\release\PowerPeek.exe to exist already.

#if VER < EncodeVer(6, 3, 0)
  #error PowerPeek.iss needs Inno Setup 6.3 or newer: x64compatible arrived there.
#endif

; version.txt is the only place the version lives -- CMake reads the same file for the
; VERSIONINFO resource, so the setup and the executable it carries cannot drift apart.
; FileRead returns one line without its terminator, which is exactly the bare version.
#define VersionFile AddBackslash(SourcePath) + "..\version.txt"
#define VersionHandle FileOpen(VersionFile)
#define AppVersion FileRead(VersionHandle)
#expr FileClose(VersionHandle)
#if Len(AppVersion) < 5
  #error version.txt did not yield a version; expected a bare MAJOR.MINOR.PATCH line.
#endif

; A full path, so that /DSourceExe=... from tools\package.ps1 can point somewhere else
; entirely without the existence check below resolving against this script's directory.
#ifndef SourceExe
  #define SourceExe AddBackslash(SourcePath) + "..\build\release\PowerPeek.exe"
#endif
#if !FileExists(SourceExe)
  #error Build PowerPeek first: tools\build.bat release
#endif

; Must match peek::platform::shell::kAumid in src/platform/ShellIntegration.cpp. Windows
; refuses toasts from an unpackaged process unless a Start Menu shortcut carries the same
; AppUserModelID the process sets on itself; if the two disagree, Show() reports success
; and nothing is ever displayed. The application repairs its own shortcut at first toast,
; so a wrong id here would not break notifications forever -- it would leave a second,
; identically named entry in the Start Menu instead.
#define Aumid "Savelka.PowerPeek"

; Must match kRunKey/kRunValue in src/platform/Platform.cpp, so the "start with Windows"
; task below and the application's own autostart switch read and write one value.
#define RunKey "Software\Microsoft\Windows\CurrentVersion\Run"
#define RunValue "PowerPeek"

[Setup]
AppId={{F967FB92-E5DD-4273-B624-67633B5050D2}
AppName=PowerPeek
AppVersion={#AppVersion}
AppVerName=PowerPeek {#AppVersion}
VersionInfoVersion={#AppVersion}
AppPublisher=k0te1ch
AppPublisherURL=https://github.com/k0te1ch
AppSupportURL=https://boosty.to/k0te1ch
AppUpdatesURL=https://github.com/k0te1ch/powerpeek/releases
LicenseFile=..\LICENSE
SetupIconFile=..\resources\app.ico
UninstallDisplayIcon={app}\PowerPeek.exe
UninstallDisplayName=PowerPeek

; Per-user by default, so the common case needs no elevation at all: {autopf} resolves to
; %LOCALAPPDATA%\Programs without administrative rights and to Program Files with them.
; The dialog lets whoever wants a machine-wide install ask for one.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
DefaultDirName={autopf}\PowerPeek

; The Start Menu entry is placed at a fixed path (see [Icons]), so the group page would
; only offer a folder name nothing reads.
DisableProgramGroupPage=yes

; The tray application holds PowerPeek.exe open. Restart Manager closes it before the file
; is replaced; AppMutex is the backstop that asks the user when it could not, and it also
; guards the uninstaller. The name is kInstanceMutex from src/platform/Platform.cpp.
CloseApplications=yes
RestartApplications=no
AppMutex=Local\PowerPeek.SingleInstance

ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763

OutputDir=..\dist
OutputBaseFilename=PowerPeek-Setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"

[CustomMessages]
en.AutoStartTask=Start PowerPeek when Windows starts
ru.AutoStartTask=Запускать PowerPeek при входе в Windows
en.RemoveDataQuestion=Delete PowerPeek's settings, charge history and log as well?%n%nThey are stored in:%n%1
ru.RemoveDataQuestion=Удалить также настройки, историю заряда и журнал PowerPeek?%n%nОни хранятся здесь:%n%1

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "startup"; Description: "{cm:AutoStartTask}"

[Files]
Source: "{#SourceExe}"; DestDir: "{app}"; DestName: "PowerPeek.exe"; Flags: ignoreversion
; GPL-3.0-or-later: the licence text travels with the binary, not only with the source.
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Icons]
; {userprograms}, not {autoprograms}, and deliberately so: the application creates exactly
; this file for itself the first time it needs a toast (peek::paths::startMenuShortcut).
; Writing the same path means one shortcut instead of two identically named ones, and it
; means the uninstaller takes the application's copy with it.
Name: "{userprograms}\PowerPeek"; Filename: "{app}\PowerPeek.exe"; AppUserModelID: "{#Aumid}"
Name: "{autodesktop}\PowerPeek"; Filename: "{app}\PowerPeek.exe"; AppUserModelID: "{#Aumid}"; Tasks: desktopicon

[Registry]
; HKCU even for a machine-wide install: the application reads and writes this one value
; under HKCU, so an HKLM entry would start PowerPeek while its own setting still reported
; autostart as off. Quoted the way the application quotes it -- an unquoted path with a
; space is read as a program name plus arguments.
Root: HKCU; Subkey: "{#RunKey}"; ValueType: string; ValueName: "{#RunValue}"; ValueData: """{app}\PowerPeek.exe"""; Tasks: startup
; Leaving the task unticked is an instruction, not an absence of one: it clears an entry a
; previous install or the application itself left behind.
Root: HKCU; Subkey: "{#RunKey}"; ValueType: none; ValueName: "{#RunValue}"; Flags: deletevalue; Tasks: not startup

[Run]
; runasoriginaluser: after an elevated install the wizard runs as the administrator, and a
; tray application started from there would sit in the wrong notification area and read the
; wrong profile.
Filename: "{app}\PowerPeek.exe"; Description: "{cm:LaunchProgram,PowerPeek}"; Flags: nowait postinstall skipifsilent runasoriginaluser

[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  DataDir: String;
begin
  if CurUninstallStep <> usPostUninstall then
    Exit;

  // Unconditional rather than an uninsdeletevalue flag on the [Registry] entry above: the
  // same value is the application's own autostart setting, so it can be there even when
  // the install-time task was declined, and a Run entry pointing at a deleted executable
  // is exactly what an uninstall is supposed to prevent.
  RegDeleteValue(HKEY_CURRENT_USER, '{#RunKey}', '{#RunValue}');

  DataDir := ExpandConstant('{localappdata}\PowerPeek');
  if not DirExists(DataDir) then
    Exit;

  // A silent uninstall has nobody to ask, and settings and charge history are what a user
  // reinstalling in place expects to find again -- so keep them rather than guess.
  if UninstallSilent then
    Exit;

  if MsgBox(FmtMessage(CustomMessage('RemoveDataQuestion'), [DataDir]),
            mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
    DelTree(DataDir, True, True, True);
end;
