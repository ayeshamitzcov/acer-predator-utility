; Open-source Inno Setup script for Predator Utility.
; Compile with: ISCC.exe installer\PredatorUtility.iss
; PrivilegesRequired=admin so it can drop files in Program Files and install PawnIO.

#define MyAppName "Predator Utility"
#define MyAppVersion "0.1.0"
#define MyAppPublisher "ayeshamitzcov"
#define MyAppURL "https://github.com/ayeshamitzcov/acer-predator-utility"
#define MyAppExeName "PredatorUtility.exe"

[Setup]
AppId={{A7C3E91B-4D2F-4B8A-9E11-8C4F2A91B077}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={autopf}\Predator Utility
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=..\dist
OutputBaseFilename=PredatorUtility-Setup-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
UninstallDisplayIcon={app}\{#MyAppExeName}
SetupIconFile=
InfoBeforeFile=welcome.txt
ChangesAssociations=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
Source: "..\build\Release\PredatorUtility.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\Release\predator-probe.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\third_party\pawnio\PawnIOLib.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\third_party\pawnio\IntelMSR.bin"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "Install-Dependencies.ps1"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Comment: "Needs Administrator"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "powershell.exe"; \
    Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\Install-Dependencies.ps1"""; \
    StatusMsg: "Installing PawnIO and the VC++ runtime (this needs admin)..."; \
    Flags: runhidden waituntilterminated
Filename: "{app}\{#MyAppExeName}"; Description: "Launch Predator Utility"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "schtasks.exe"; Parameters: "/Delete /TN PredatorUtility /F"; Flags: runhidden waituntilterminated; RunOnceId: "DelStartupTask"

[UninstallDelete]
Type: filesandordirs; Name: "{app}"

[Code]
function InitializeSetup(): Boolean;
begin
  Result := True;
end;
