; NSIS script for wxNote (Windows installer). NSIS is zlib-licensed (fully open source) and
; ships preinstalled on GitHub's windows-latest runners as `makensis`.
;
; Build first (cmake --build build --target wxnote --config Release), create build\installer\, then:
;   makensis installer\windows\wxnote.nsi
; Output: build\installer\wxNote-<version>-Setup.exe
;
; Relative paths below are resolved against this script's directory (makensis cd's here by default).
; Ships what the app actually reads at runtime (see the POST_BUILD copy commands in the top-level
; CMakeLists.txt) plus the shipped nib bridge plugins; the nib_test_plugin.dll / plugins\TestPlugin\
; dev-only build artifacts are deliberately excluded, as are the locale catalog's source-side files
; (.po/.pot/tooling - the app only reads the compiled .mo files).
;
; THIS LIST IS HAND-MAINTAINED AND HAS DRIFTED BEFORE. Everything CMake stages into build/bin must be
; either shipped here or consciously excluded above: fonts/, lexers/, contextMenu.xml and two of the
; three nib plugins were silently absent from every release up to and including 0.14.1, which meant
; installed builds had no Scintillua highlighting (lexer.lua is a hard requirement), no bundled default
; font, and no UDL support. The same list is duplicated in .github/workflows/build.yml's zip step and
; in installer/linux/io.github.Alpaq92.WxNote.yml - change all three together.

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "x64.nsh"
!include "WinMessages.nsh"   ; WM_WININICHANGE / HWND_BROADCAST for the PATH change broadcast

; Read straight from the top-level CMakeLists.txt's project(... VERSION ...) so this can't drift
; out of sync with it again (every packaging script independently hardcoded its own version string
; and 0.4.0 shipped labeled 0.3.0 everywhere as a result).
!searchparse /file "..\..\CMakeLists.txt" "project(wxNote VERSION " APP_VERSION " LANGUAGES"

!define APP_NAME    "wxNote"
!define APP_URL     "https://github.com/Alpaq92/wx-notepad-plus-plus"
!define APP_EXE     "wxnote.exe"
!define ARP_KEY     "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"

; TARGET_ARM64 / TARGET_X86 (makensis /DTARGET_ARM64, /DTARGET_X86): the windows-arm64 and windows-x86
; CI legs build an ARM64 / 32-bit x86 wxnote.exe and pass their define so the installer is named apart
; and applies the right (or no) CPU guard at .onInit.
; Default (neither defined) = the x64 build, whose asset name stays exactly as before.
; These suffixes MUST stay in step with matrix.arch_suffix in .github/workflows/build.yml, which names
; the matching .zip - the site's ASSET_MATCHERS read both, and a divergence silently mismatches them.
!ifdef TARGET_ARM64
  !define ARCH_SUFFIX "-arm64"
  !define ARCH_NAME   "ARM64"
!else ifdef TARGET_X86
  !define ARCH_SUFFIX "-x86"
  !define ARCH_NAME   "32-bit x86"
!else
  !define ARCH_SUFFIX ""
  !define ARCH_NAME   "x64"
!endif

Name "${APP_NAME}"
OutFile "..\..\build\installer\wxNote-${APP_VERSION}${ARCH_SUFFIX}-Setup.exe"
Unicode true
SetCompressor /SOLID lzma
ManifestDPIAware true

; Per-user install (no UAC), mirroring the previous installer's lowest-privilege default.
; NOTE: the installer's own registry state deliberately lives OUTSIDE "Software\wxNote" - that key
; is wxConfig's root for the app's user settings, and an installer-created subkey there would (a)
; defeat the app's first-launch "new settings tree is still empty" legacy-migration gate and (b)
; make uninstall's cleanup of installer state delete the user's settings with it.
RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\Programs\${APP_NAME}"
InstallDirRegKey HKCU "Software\wxNote-Installer" "InstallDir"

!define MUI_ICON "..\..\resources\wxnote.ico"
!define MUI_UNICON "..\..\resources\wxnote.ico"

!insertmacro MUI_PAGE_LICENSE "..\..\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey /LANG=1033 "ProductName"     "${APP_NAME}"
VIAddVersionKey /LANG=1033 "ProductVersion"  "${APP_VERSION}.0"
VIAddVersionKey /LANG=1033 "FileVersion"     "${APP_VERSION}.0"
VIAddVersionKey /LANG=1033 "FileDescription" "${APP_NAME} ${APP_VERSION} Setup"
VIAddVersionKey /LANG=1033 "CompanyName"     "wxNote Project"
VIAddVersionKey /LANG=1033 "LegalCopyright"  "Copyright (C) 2026 The wxNote Authors. Licensed under Apache-2.0."
; A complete StringFileInfo block is what a legitimate installer carries; the three fields below were
; simply absent, leaving Explorer's Details tab (and anything else reading the resource) with blanks.
VIAddVersionKey /LANG=1033 "OriginalFilename" "wxNote-${APP_VERSION}${ARCH_SUFFIX}-Setup.exe"
VIAddVersionKey /LANG=1033 "InternalName"     "wxNote-Setup"
VIAddVersionKey /LANG=1033 "Comments"         "Open-source text editor. Source: ${APP_URL}"

; The payload exe is single-arch (see CMakeLists.txt) - NSIS itself has no "ArchitecturesAllowed"
; concept the way Inno Setup does, so the CPU check has to be enforced by hand or the 32-bit
; installer stub would happily lay an exe down on a machine that can't run it. The ARM64 build
; requires a native ARM64 Windows (x64 Windows can't run ARM64 binaries); the x64 build keeps the
; broader RunningX64 check - it deliberately still installs on ARM64 Windows 11, where x64 apps
; run fine under the OS's built-in emulation.
Function .onInit
  ; All three Windows builds install to the same per-user directory and share one Add/Remove Programs
  ; entry, so installing a different architecture over an existing one silently replaces it. That was
  ; impossible while every build had a CPU guard that partitioned the machines; the x86 build has none
  ; (deliberately - it runs everywhere), so an x64 machine can now reach this. Warn rather than block:
  ; swapping architectures on purpose is legitimate, being surprised by it is not.
  ; /SD is not optional on any MessageBox reachable from .onInit: under /S the dialog is never shown,
  ; and without a silent default the installer blocks forever on an invisible modal. winget drives
  ; this installer with /S, so that is a permanently stuck package install, not just a slow one.
  ReadRegStr $0 HKCU "Software\wxNote-Installer" "Arch"
  ${If} $0 != ""
  ${AndIf} $0 != "${ARCH_NAME}"
    MessageBox MB_OKCANCEL|MB_ICONEXCLAMATION "The $0 build of ${APP_NAME} is already installed here.$\r$\n$\r$\nThis installer will replace it with the ${ARCH_NAME} build.$\r$\n$\r$\nContinue?" /SD IDOK IDOK +2
    Quit
  ${EndIf}
!ifdef TARGET_ARM64
  ${IfNot} ${IsNativeARM64}
    MessageBox MB_OK|MB_ICONSTOP "This ${APP_NAME} installer is for ARM64 Windows. Please download the x64 installer instead." /SD IDOK
    Quit
  ${EndIf}
!else ifdef TARGET_X86
  ; Deliberately empty, and it must stay that way. A 32-bit payload runs everywhere Windows runs:
  ; natively on 32-bit Windows, on x64 through WOW64, and on ARM64 through the OS's x86 emulation.
  ; There is no machine to exclude. In particular the ${RunningX64} check from the x64 branch below
  ; must NOT be copied here - it is FALSE on exactly the 32-bit machines this build exists for, so it
  ; would turn away every one of its intended users.
!else
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "${APP_NAME} requires 64-bit Windows. On 32-bit Windows, download the x86 installer instead." /SD IDOK
    Quit
  ${EndIf}
!endif
FunctionEnd

Section "${APP_NAME} (required)" SecCore
  SectionIn RO
  SetOutPath "$INSTDIR"
  File "..\..\build\bin\${APP_EXE}"
  ; Apache-2.0 section 4(a) wants a copy of the licence to travel WITH the distribution. Displaying it
  ; on a wizard page is not that, and a silent install (/S) skips that page entirely - so install the
  ; files as well. NOTICE carries the third-party attributions (bundled fonts, the GPL bridge plugins).
  File "..\..\LICENSE"
  File "..\..\NOTICE"
  File "..\..\build\bin\stylers.model.xml"
  File "..\..\build\bin\contextMenu.xml"
  File /r "..\..\build\bin\icons"
  File /r "..\..\build\bin\icons-solar"
  File /r "..\..\build\bin\icons-iconpark"
  File /r "..\..\build\bin\icons-streamline"
  File /r "..\..\build\bin\themes"
  File /r "..\..\build\bin\dictionaries"
  ; The bundled code fonts (JetBrains Mono is the DEFAULT editor font) are loaded from <exeDir>/fonts
  ; via wxFont::AddPrivateFont - without them the default font silently falls back to a system face.
  File /r "..\..\build\bin\fonts"
  ; Scintillua's lexer.lua. src/scintillua_engine.cpp hard-fails `require('lexer')` if it is absent and
  ; leaves the engine not-ready, which disables ALL Scintillua syntax highlighting - so this is not
  ; optional, despite being one small file.
  File /r "..\..\build\bin\lexers"
  File /r /x "wxn.pot" /x "*.po" "..\..\build\bin\locale"
  SetOutPath "$INSTDIR\nib"
  File "..\..\build\bin\nib\npp_bridge.dll"
  ; The other two shipped bridge plugins: udl_compat provides User-Defined Language support (it is
  ; where UDL moved when it left the core) and npp_shortcuts_compat maps Notepad++ keyboard shortcuts.
  ; nib_test_plugin.dll is deliberately NOT shipped - it is a dev-only loader test.
  File "..\..\build\bin\nib\udl_compat.dll"
  File "..\..\build\bin\nib\npp_shortcuts_compat.dll"
  SetOutPath "$INSTDIR"

  WriteUninstaller "$INSTDIR\uninstall.exe"
  WriteRegStr HKCU "Software\wxNote-Installer" "InstallDir" "$INSTDIR"
  ; Which build owns this directory. All three architectures install to the SAME $INSTDIR and share
  ; one ARP key, so .onInit needs this to notice an architecture swap - see the guard there.
  WriteRegStr HKCU "Software\wxNote-Installer" "Arch" "${ARCH_NAME}"

  ; Add/Remove Programs entry (per-user hive, matching the per-user install).
  WriteRegStr   HKCU "${ARP_KEY}" "DisplayName"          "${APP_NAME}"
  WriteRegStr   HKCU "${ARP_KEY}" "DisplayVersion"       "${APP_VERSION}"
  WriteRegStr   HKCU "${ARP_KEY}" "Publisher"            "wxNote Project"
  WriteRegStr   HKCU "${ARP_KEY}" "URLInfoAbout"         "${APP_URL}"
  WriteRegStr   HKCU "${ARP_KEY}" "DisplayIcon"          "$INSTDIR\${APP_EXE}"
  WriteRegStr   HKCU "${ARP_KEY}" "UninstallString"      '"$INSTDIR\uninstall.exe"'
  WriteRegStr   HKCU "${ARP_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKCU "${ARP_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${ARP_KEY}" "NoRepair" 1
  ; Fields a well-behaved installer is expected to write; their absence left the ARP entry sparser than
  ; a normal application's. All are facts about this install; none change behaviour.
  WriteRegStr   HKCU "${ARP_KEY}" "InstallLocation"      "$INSTDIR"
  WriteRegStr   HKCU "${ARP_KEY}" "HelpLink"             "${APP_URL}"
  WriteRegStr   HKCU "${ARP_KEY}" "URLUpdateInfo"        "${APP_URL}/releases"
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegDWORD HKCU "${ARP_KEY}" "EstimatedSize" $0

  CreateDirectory "$SMPROGRAMS\${APP_NAME}"
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\Uninstall ${APP_NAME}.lnk" "$INSTDIR\uninstall.exe"
SectionEnd

; Unchecked by default, matching the previous installer's optional desktop-icon task.
Section /o "Desktop shortcut" SecDesktop
  CreateShortcut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
SectionEnd

; Optional, and ON by default: put $INSTDIR on the user's PATH so `wxnote` works from any shell.
; Done in PowerShell, not here - see installer/windows/wxnote-path.ps1 for why (this NSIS build has
; NSIS_MAX_STRLEN=1024, so ReadRegStr would TRUNCATE any PATH longer than that and writing it back
; would destroy every entry past the cut; the helper also preserves the REG_EXPAND_SZ value kind).
; A failure here is reported and ignored: a PATH entry is a convenience, not part of a working wxNote.
Section "Add to PATH" SecPath
  ; InitPluginsDir is REQUIRED before any File into $PLUGINSDIR. NSIS only creates that directory on
  ; demand - on an explicit InitPluginsDir, or implicitly before the first plugin call - and until then
  ; $PLUGINSDIR expands to the EMPTY STRING. Without this the extraction target becomes "\wxnote-path.ps1"
  ; at the drive root and NSIS shows "Error opening file for writing". nsExec below is a plugin, but it
  ; runs AFTER the File, which is too late to help.
  InitPluginsDir
  File "/oname=$PLUGINSDIR\wxnote-path.ps1" "wxnote-path.ps1"
  nsExec::ExecToStack 'powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$PLUGINSDIR\wxnote-path.ps1" -Action Add -Directory "$INSTDIR"'
  Pop $0   ; exit code
  Pop $1   ; output
  ; ONLY exit code 0 means "this installer added the entry". 2 means it was already on PATH, which
  ; happens whenever the user installs into a directory they already had there - claiming ownership of
  ; that would make uninstall delete it and break whatever else lives in it.
  ${If} $0 == 0
    WriteRegDWORD HKCU "Software\wxNote-Installer" "AddedToPath" 1
    ; Tell already-running shells and Explorer to re-read the environment. Without this the new PATH
    ; only reaches processes started after the next sign-in.
    SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=5000
  ${ElseIf} $0 == 2
    DetailPrint "wxNote is already on PATH ($1) - leaving it, and leaving it alone on uninstall."
  ${Else}
    DetailPrint "Could not add wxNote to PATH: $1"
  ${EndIf}
SectionEnd

Section "Uninstall"
  ; Remove only what the installer put there (user-created files - e.g. userDefineLangs\ - survive,
  ; and $INSTDIR itself is only removed if that leaves it empty).
  Delete "$INSTDIR\${APP_EXE}"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\NOTICE"
  Delete "$INSTDIR\stylers.model.xml"
  Delete "$INSTDIR\contextMenu.xml"
  RMDir /r "$INSTDIR\icons"
  RMDir /r "$INSTDIR\icons-solar"
  RMDir /r "$INSTDIR\icons-iconpark"
  RMDir /r "$INSTDIR\icons-streamline"
  RMDir /r "$INSTDIR\themes"
  RMDir /r "$INSTDIR\dictionaries"
  RMDir /r "$INSTDIR\fonts"
  RMDir /r "$INSTDIR\lexers"
  RMDir /r "$INSTDIR\locale"
  Delete "$INSTDIR\nib\npp_bridge.dll"
  Delete "$INSTDIR\nib\udl_compat.dll"
  Delete "$INSTDIR\nib\npp_shortcuts_compat.dll"
  RMDir "$INSTDIR\nib"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"

  Delete "$DESKTOP\${APP_NAME}.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Uninstall ${APP_NAME}.lnk"
  RMDir "$SMPROGRAMS\${APP_NAME}"

  ; Remove the PATH entry only if this installer added it, and only ours - never rewrite the rest.
  ReadRegDWORD $0 HKCU "Software\wxNote-Installer" "AddedToPath"
  ${If} $0 == 1
    InitPluginsDir                 ; see SecPath - $PLUGINSDIR is empty until this runs
    File "/oname=$PLUGINSDIR\wxnote-path.ps1" "wxnote-path.ps1"
    nsExec::ExecToStack 'powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$PLUGINSDIR\wxnote-path.ps1" -Action Remove -Directory "$INSTDIR"'
    Pop $0
    Pop $1
    SetOutPath "$TEMP"   ; don't hold $INSTDIR open, or RMDir below fails
    SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=5000
  ${EndIf}

  DeleteRegKey HKCU "${ARP_KEY}"
  DeleteRegKey HKCU "Software\wxNote-Installer"   ; installer state only - the user's settings under Software\wxNote survive uninstall
SectionEnd
