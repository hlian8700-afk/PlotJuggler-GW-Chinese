#define AppName "BladeScope 风机录波数据分析工具"
#define AppVersion "3.17.0.35"
#ifndef StageDir
  #error StageDir must be supplied by the build workflow
#endif
[Setup]
AppId={{C716EDB0-6F30-46C7-86E4-2E2C1D4A3B60}
AppName={#AppName}
AppVersion={#AppVersion}
VersionInfoVersion={#AppVersion}
AppPublisher=BladeScope
DefaultDirName={autopf}\PlotJuggler-GW-Chinese
DefaultGroupName={#AppName}
OutputDir=.
OutputBaseFilename=BladeScope-3.17.0.35-win64-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\bin\BladeScope.exe
WizardStyle=modern
[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion
[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\bin\BladeScope.exe"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\bin\BladeScope.exe"; Tasks: desktopicon
[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"
[Run]
Filename: "{app}\bin\BladeScope.exe"; Description: "启动 BladeScope"; Flags: nowait postinstall skipifsilent
