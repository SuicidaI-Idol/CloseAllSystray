#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

[Setup]
AppId={{A66AB19D-BBC2-4B0F-A837-6B0266EC8994}
AppName=CloseAllSystray
AppVersion={#AppVersion}
DefaultDirName={autopf}\CloseAllSystray
UsePreviousAppDir=no
DefaultGroupName=CloseAllSystray
DisableProgramGroupPage=yes
PrivilegesRequired=admin
SetupIconFile=..\src\assets\app.ico
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir=..\dist
OutputBaseFilename=CloseAllSystray-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\CloseAllSystray.exe
AppMutex=Local\CloseAllSystray.SingleInstance
CloseApplications=no
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\build\Release\CloseAllSystray.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\CloseAllSystray"; Filename: "{app}\CloseAllSystray.exe"
Name: "{autodesktop}\CloseAllSystray"; Filename: "{app}\CloseAllSystray.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\CloseAllSystray.exe"; Description: "{cm:LaunchProgram,CloseAllSystray}"; Flags: nowait postinstall skipifsilent
