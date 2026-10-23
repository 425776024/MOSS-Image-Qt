; Compile with ISCC /DPayloadDir=... /DArtifactsDir=... windows-installer.iss
#ifndef PayloadDir
  #error PayloadDir is required
#endif
#ifndef ArtifactsDir
  #error ArtifactsDir is required
#endif
#define AppVersion "0.1.0"

[Setup]
AppId={{750AB42B-DBE6-44D0-8CB8-A3A291E92374}
AppName={cm:ProductName}
AppVersion={#AppVersion}
AppPublisher=ffclip
AppPublisherURL=https://ffclip.com
DefaultDirName={localappdata}\Programs\XAvatar
DefaultGroupName={cm:ProductName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.18362
OutputDir={#ArtifactsDir}
OutputBaseFilename=XAvatar-{#AppVersion}-Windows-x64-Setup
SetupIconFile=..\assets\app-icon\XAvatar.ico
UninstallDisplayIcon={app}\XAvatar.exe
VersionInfoDescription=新身数字人 · XAvatar Setup
VersionInfoProductName=XAvatar
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
UninstallDisplayName={cm:ProductName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimplified"; MessagesFile: "languages\ChineseSimplified.isl"

[CustomMessages]
english.ProductName=XAvatar
english.LaunchApp=Launch XAvatar
english.DesktopIcon=Create a desktop shortcut
chinesesimplified.ProductName=新身数字人
chinesesimplified.LaunchApp=启动新身数字人
chinesesimplified.DesktopIcon=创建桌面快捷方式

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopIcon}"

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{cm:ProductName}"; Filename: "{app}\XAvatar.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\{cm:ProductName}"; Filename: "{app}\XAvatar.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\XAvatar.exe"; Description: "{cm:LaunchApp}"; Flags: nowait postinstall skipifsilent

; Deliberately no UninstallDelete: models, settings and projects are user data.
