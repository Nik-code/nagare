; Per-user installer: no admin prompt, and everything lands in the user's own profile.
; dev/package.ps1 passes AppVersion, PayloadDir and OutputDir on the command line.

[Setup]
AppId={{6B7C1E5A-3B7E-4B7F-9E6B-1C1E5D0A7F42}
AppName=Nagare
AppVersion={#AppVersion}
AppVerName=Nagare {#AppVersion}
AppPublisher=Nemukai
AppPublisherURL=https://github.com/Nik-code/nagare
AppSupportURL=https://github.com/Nik-code/nagare/issues
DefaultDirName={autopf}\Nagare
DefaultGroupName=Nagare
DisableProgramGroupPage=yes
DisableDirPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename=Nagare-{#AppVersion}-win-x64-setup
SetupIconFile=..\resources\nagare.ico
UninstallDisplayIcon={app}\nagare.exe
UninstallDisplayName=Nagare
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ShowLanguageDialog=no
ChangesAssociations=yes
; Restart Manager closes a running Nagare with WM_ENDSESSION, which saves first. See shell_integration.cpp.
CloseApplications=yes
RestartApplications=no

[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#PayloadDir}\nagare.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\COPYING"; DestDir: "{app}"
Source: "{#PayloadDir}\licenses\*"; DestDir: "{app}\licenses"
#ifdef SourcesDir
; Only a privately shared build sets this. A source already there is the user's, so it stays as it is.
Source: "{#SourcesDir}\*"; DestDir: "{localappdata}\Nagare\Search Sources"; Flags: onlyifdoesntexist uninsneveruninstall
#endif

[Icons]
Name: "{autoprograms}\Nagare"; Filename: "{app}\nagare.exe"
Name: "{autodesktop}\Nagare"; Filename: "{app}\nagare.exe"; Tasks: desktopicon

[Run]
; Offers Nagare to Windows straight away. It takes nothing from whatever opens torrents now.
Filename: "{app}\nagare.exe"; Parameters: "--register"; Flags: runhidden
Filename: "{app}\nagare.exe"; Description: "Open Nagare"; Flags: postinstall nowait skipifsilent

[UninstallRun]
; Closes a running Nagare, then removes every registry entry it or the Settings screen ever wrote.
Filename: "{app}\nagare.exe"; Parameters: "--unregister"; Flags: runhidden; RunOnceId: "Unregister"

[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Data: String;
begin
  if CurUninstallStep <> usPostUninstall then
    Exit;
  Data := ExpandConstant('{localappdata}\Nagare');
  if UninstallSilent or not DirExists(Data) then
    Exit;
  if MsgBox('Also remove your Nagare settings and transfer list?' + #13#10#13#10 +
            'Files you have downloaded are not affected either way.',
            mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
    DelTree(Data, True, True, True);
end;
