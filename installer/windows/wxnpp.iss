; Inno Setup script for wxNotepad++ (Windows installer).
; Build first (cmake --build build --target wxnpp --config Release), then compile this script:
;   ISCC.exe installer\windows\wxnpp.iss
; Output lands in build\installer\wxNotepadPlusPlus-<version>-Setup.exe
;
; Only ships what setLexerForFile/loadTheme/etc. actually read at runtime (see the POST_BUILD
; copy_directory calls in the top-level CMakeLists.txt) plus the real npp-compat plugin bridge;
; the nib_test_plugin.dll / plugins\TestPlugin\ dev-only build artifacts are deliberately excluded.

#define MyAppName "wxNotepad++"
#define MyAppVersion "0.1.0"
#define MyAppPublisher "wxNotepad++ Project"
#define MyAppURL "https://github.com/Alpaq92/wx-notepad-plus-plus"
#define MyAppExeName "wxnpp.exe"
#define RepoRoot "..\.."
#define BinDir "..\..\build\bin"

[Setup]
AppId={{2B7B0F1C-6E2C-4C7C-9C7C-6E1B7B6E1B6E}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir={#RepoRoot}\build\installer
OutputBaseFilename=wxNotepadPlusPlus-{#MyAppVersion}-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile={#RepoRoot}\resources\wxNotepad++.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
LicenseFile={#RepoRoot}\LICENSE
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#BinDir}\{#MyAppExeName}";        DestDir: "{app}";               Flags: ignoreversion
Source: "{#BinDir}\stylers.model.xml";      DestDir: "{app}";               Flags: ignoreversion
Source: "{#BinDir}\icons\*";                DestDir: "{app}\icons";         Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BinDir}\icons-solar\*";          DestDir: "{app}\icons-solar";   Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BinDir}\icons-iconpark\*";       DestDir: "{app}\icons-iconpark";Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BinDir}\themes\*";               DestDir: "{app}\themes";        Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BinDir}\locale\*";               DestDir: "{app}\locale";        Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "po2mo.py,wxnpp.pot,__pycache__\*"
Source: "{#BinDir}\nib\npp_bridge.dll";     DestDir: "{app}\nib";           Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
