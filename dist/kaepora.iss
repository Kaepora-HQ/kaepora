; The Windows installer, which Inno Setup's compiler makes from the package folder named by Package.

#define Version GetFileProductVersionString(Package + "\kaepora.exe")

[Setup]
; A newer installer with the same id installs over the older version, in its folder.
AppId=Kaepora
AppName=Kaepora
AppVersion={#Version}
VersionInfoVersion={#Version}
UninstallDisplayName=Kaepora
UninstallDisplayIcon={app}\kaepora.exe
SetupIconFile=org.kaepora.Kaepora.ico
WizardStyle=modern dynamic
PrivilegesRequired=lowest
SetupArchitecture=x64
DefaultDirName={autopf}\Kaepora
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableFinishedPage=yes

[Messages]
ConfirmUninstall=Remove %1 from this computer? The seeds you made and your settings stay.

[Files]
Source: "{#Package}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{autoprograms}\Kaepora"; Filename: "{app}\kaepora.exe"

[Run]
Filename: "{app}\kaepora.exe"; Flags: nowait skipifsilent
