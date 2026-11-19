#define AppName "PlotJuggler Goldwind Chinese"
#define AppVersion "3.17.0"
#ifndef StageDir
  #error StageDir must be supplied by the build workflow
#endif
[Setup]
AppId={{C716EDB0-6F30-46C7-86E4-2E2C1D4A3B60}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Goldwind PlotJuggler Custom Build
DefaultDirName={autopf}\PlotJuggler-GW-Chinese
DefaultGroupName={#AppName}
OutputDir=.
OutputBaseFilename=PlotJuggler-GW-Chinese-3.17.0-win64-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\bin\plotjuggler.exe
WizardStyle=modern
[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion
[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\bin\plotjuggler.exe"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\bin\plotjuggler.exe"; Tasks: desktopicon
[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"
[Run]
Filename: "{app}\bin\plotjuggler.exe"; Description: "Launch PlotJuggler"; Flags: nowait postinstall skipifsilent
