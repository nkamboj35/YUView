; YUView-ML Inno Setup Installer Script
; Requires Inno Setup 6+ (https://jrsoftware.org/isinfo.php)
;
; Build with:
;   ISCC.exe /DSourceDir="<path-to-windeployqt-folder>" /DAppVersion="1.0.0" YUView-ML.iss
;
; SourceDir and AppVersion are expected to be passed from the build script.

#ifndef SourceDir
  #define SourceDir "..\build_test\YUViewSimpleRelease"
#endif

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{B7A3E2F1-8C4D-4E5F-9A1B-2D3C4E5F6A7B}
AppName=YUView-ML
AppVersion={#AppVersion}
AppVerName=YUView-ML {#AppVersion}
AppPublisher=YUView-ML
AppPublisherURL=https://github.com/IENT/YUView
AppSupportURL=https://github.com/IENT/YUView
DefaultDirName={autopf}\YUView-ML
DefaultGroupName=YUView-ML
OutputDir=Output
OutputBaseFilename=YUView-ML-Setup
SetupIconFile=..\YUViewApp\images\YUView.ico
UninstallDisplayIcon={app}\YUView-ML.exe
LicenseFile=..\LICENSE.GPL3
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64
WizardStyle=modern
DisableProgramGroupPage=yes
PrivilegesRequired=admin
MinVersion=10.0

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "vcredist"; Description: "Install Visual C++ Runtime (recommended if not already installed)"; GroupDescription: "Prerequisites:"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "vc_redist.x64.exe"
Source: "{#SourceDir}\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: ignoreversion deleteafterinstall; Tasks: vcredist

[Icons]
Name: "{group}\YUView-ML"; Filename: "{app}\YUView-ML.exe"; Comment: "YUV/RGB Viewer with ML Format Support"
Name: "{group}\Uninstall YUView-ML"; Filename: "{uninstallexe}"
Name: "{autodesktop}\YUView-ML"; Filename: "{app}\YUView-ML.exe"; Tasks: desktopicon; Comment: "YUV/RGB Viewer with ML Format Support"

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/quiet /norestart"; StatusMsg: "Installing Visual C++ Runtime..."; Tasks: vcredist; Flags: waituntilterminated
Filename: "{app}\YUView-ML.exe"; Description: "{cm:LaunchProgram,YUView-ML}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}"
