; NSIS script for wxNote (Windows installer). NSIS is zlib-licensed (fully open source) and
; ships preinstalled on GitHub's windows-latest runners as `makensis`.
;
; Build first (cmake --build build --target wxnote --config Release), create build\installer\, then:
;   makensis installer\windows\wxnote.nsi
; Output: build\installer\wxNote-<version>-Setup.exe
;
; Relative paths below are resolved against this script's directory (makensis cd's here by default).
; Only ships what the app actually reads at runtime (see the POST_BUILD copy_directory calls in the
; top-level CMakeLists.txt) plus the real npp-compat plugin bridge; the nib_test_plugin.dll /
; plugins\TestPlugin\ dev-only build artifacts are deliberately excluded, as are the locale catalog's
; source-side files (.po/.pot/tooling - the app only reads the compiled .mo files).

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "x64.nsh"

; Read straight from the top-level CMakeLists.txt's project(... VERSION ...) so this can't drift
; out of sync with it again (every packaging script independently hardcoded its own version string
; and 0.4.0 shipped labeled 0.3.0 everywhere as a result).
!searchparse /file "..\..\CMakeLists.txt" "project(wxNote VERSION " APP_VERSION " LANGUAGES"

!define APP_NAME    "wxNote"
!define APP_URL     "https://github.com/Alpaq92/wx-notepad-plus-plus"
!define APP_EXE     "wxnote.exe"
!define ARP_KEY     "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"

; TARGET_ARM64 (makensis /DTARGET_ARM64): the windows-arm64 CI leg builds an ARM64 wxnote.exe and
; passes this define so the installer is named apart and guards for the right CPU at .onInit.
; Default (undefined) = the x64 build, whose asset name stays exactly as before.
!ifdef TARGET_ARM64
  !define ARCH_SUFFIX "-arm64"
!else
  !define ARCH_SUFFIX ""
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
VIAddVersionKey /LANG=1033 "FileDescription" "${APP_NAME} installer"
VIAddVersionKey /LANG=1033 "CompanyName"     "wxNote Project"
VIAddVersionKey /LANG=1033 "LegalCopyright"  "Apache-2.0 - see LICENSE"

; The payload exe is single-arch (see CMakeLists.txt) - NSIS itself has no "ArchitecturesAllowed"
; concept the way Inno Setup does, so the CPU check has to be enforced by hand or the 32-bit
; installer stub would happily lay an exe down on a machine that can't run it. The ARM64 build
; requires a native ARM64 Windows (x64 Windows can't run ARM64 binaries); the x64 build keeps the
; broader RunningX64 check - it deliberately still installs on ARM64 Windows 11, where x64 apps
; run fine under the OS's built-in emulation.
Function .onInit
!ifdef TARGET_ARM64
  ${IfNot} ${IsNativeARM64}
    MessageBox MB_OK|MB_ICONSTOP "This ${APP_NAME} installer is for ARM64 Windows. Please download the x64 installer instead."
    Quit
  ${EndIf}
!else
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "${APP_NAME} requires 64-bit Windows."
    Quit
  ${EndIf}
!endif
FunctionEnd

Section "${APP_NAME} (required)" SecCore
  SectionIn RO
  SetOutPath "$INSTDIR"
  File "..\..\build\bin\${APP_EXE}"
  File "..\..\build\bin\stylers.model.xml"
  File /r "..\..\build\bin\icons"
  File /r "..\..\build\bin\icons-solar"
  File /r "..\..\build\bin\icons-iconpark"
  File /r "..\..\build\bin\icons-streamline"
  File /r "..\..\build\bin\themes"
  File /r /x "po2mo.py" /x "wxn.pot" /x "*.po" /x "__pycache__" "..\..\build\bin\locale"
  SetOutPath "$INSTDIR\nib"
  File "..\..\build\bin\nib\npp_bridge.dll"
  SetOutPath "$INSTDIR"

  WriteUninstaller "$INSTDIR\uninstall.exe"
  WriteRegStr HKCU "Software\wxNote-Installer" "InstallDir" "$INSTDIR"

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

Section "Uninstall"
  ; Remove only what the installer put there (user-created files - e.g. userDefineLangs\ - survive,
  ; and $INSTDIR itself is only removed if that leaves it empty).
  Delete "$INSTDIR\${APP_EXE}"
  Delete "$INSTDIR\stylers.model.xml"
  RMDir /r "$INSTDIR\icons"
  RMDir /r "$INSTDIR\icons-solar"
  RMDir /r "$INSTDIR\icons-iconpark"
  RMDir /r "$INSTDIR\icons-streamline"
  RMDir /r "$INSTDIR\themes"
  RMDir /r "$INSTDIR\locale"
  Delete "$INSTDIR\nib\npp_bridge.dll"
  RMDir "$INSTDIR\nib"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"

  Delete "$DESKTOP\${APP_NAME}.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Uninstall ${APP_NAME}.lnk"
  RMDir "$SMPROGRAMS\${APP_NAME}"

  DeleteRegKey HKCU "${ARP_KEY}"
  DeleteRegKey HKCU "Software\wxNote-Installer"   ; installer state only - the user's settings under Software\wxNote survive uninstall
SectionEnd
