; Inno Setup script - BricoHams RT-950 Toolkit for Windows
; Built by GitHub Actions: iscc /DAppVersion=0.3.0 /DSrcDir=..\..\dist\RT950Toolkit installer.iss
; The executable and the installer are signed with the BricoHams certificate
; (signtool, see .github/workflows/build.yml).

#ifndef AppVersion
  #define AppVersion "0.3.0"
#endif
#ifndef SrcDir
  #define SrcDir "..\..\dist\RT950Toolkit"
#endif

[Setup]
AppId={{6B8E4E54-3C1B-4D4F-9C51-950B1C0A0B7E}
AppName=BricoHams RT-950 Toolkit
AppVersion={#AppVersion}
AppVerName=BricoHams RT-950 Toolkit {#AppVersion}
AppPublisher=BricoHams
AppPublisherURL=https://anatolbricoham.github.io/rt950pro-satellite-mode/
AppSupportURL=https://github.com/anatolbricoham/rt950pro-satellite-mode/issues
AppUpdatesURL=https://github.com/anatolbricoham/rt950pro-satellite-mode/releases
DefaultDirName={autopf}\BricoHams\RT-950 Toolkit
DefaultGroupName=BricoHams
DisableProgramGroupPage=yes
LicenseFile=..\..\..\LICENSE
InfoBeforeFile=disclaimer.txt
OutputDir=..\..\dist
OutputBaseFilename=BricoHams-RT950-Toolkit-Setup-{#AppVersion}
SetupIconFile=..\..\rt950_toolkit\data\brand\icon.ico
UninstallDisplayIcon={app}\RT950Toolkit.exe
WizardStyle=modern
WizardImageFile=wizard.bmp
WizardSmallImageFile=wizard_small.bmp
Compression=lzma2/max
SolidCompression=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesInstallIn64BitMode=x64compatible
VersionInfoCompany=BricoHams
VersionInfoDescription=BricoHams RT-950 Toolkit installer
VersionInfoVersion={#AppVersion}

[Languages]
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#SrcDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "BricoHams-CodeSigning.cer"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\BricoHams RT-950 Toolkit"; Filename: "{app}\RT950Toolkit.exe"
Name: "{group}\{cm:UninstallProgram,BricoHams RT-950 Toolkit}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\BricoHams RT-950 Toolkit"; Filename: "{app}\RT950Toolkit.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\RT950Toolkit.exe"; Description: "{cm:LaunchProgram,BricoHams RT-950 Toolkit}"; Flags: nowait postinstall skipifsilent
