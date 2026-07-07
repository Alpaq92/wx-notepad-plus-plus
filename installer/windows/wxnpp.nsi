; NSIS script for wxNotepad++ (Windows installer). NSIS is zlib-licensed (fully open source) and
; ships preinstalled on GitHub's windows-latest runners as `makensis`.
;
; Build first (cmake --build build --target wxnpp --config Release), create build\installer\, then:
;   makensis installer\windows\wxnpp.nsi
; Output: build\installer\wxNotepadPlusPlus-<version>-Setup.exe
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
!searchparse /file "..\..\CMakeLists.txt" "project(wxNotepadPlusPlus VERSION " APP_VERSION " LANGUAGES"

!define APP_NAME    "wxNotepad++"
!define APP_URL     "https://github.com/Alpaq92/wx-notepad-plus-plus"
!define APP_EXE     "wxnpp.exe"
!define ARP_KEY     "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"

Name "${APP_NAME}"
OutFile "..\..\build\installer\wxNotepadPlusPlus-${APP_VERSION}-Setup.exe"
Unicode true
SetCompressor /SOLID lzma
ManifestDPIAware true

; Per-user install (no UAC), mirroring the previous installer's lowest-privilege default.
RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\Programs\${APP_NAME}"
InstallDirRegKey HKCU "Software\wxNotepadPlusPlus\Installer" "InstallDir"

!define MUI_ICON "..\..\resources\wxNotepad++.ico"
!define MUI_UNICON "..\..\resources\wxNotepad++.ico"

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
VIAddVersionKey /LANG=1033 "CompanyName"     "wxNotepad++ Project"
VIAddVersionKey /LANG=1033 "LegalCopyright"  "GPL v3 - see LICENSE"

; wxnpp.exe is built x64-only (see CMakeLists.txt) - NSIS itself has no "ArchitecturesAllowed" concept
; the way Inno Setup does, so the 64-bit-ness has to be enforced by hand or the 32-bit installer stub
; would happily lay a 64-bit exe down on a 32-bit system, where it can't run at all.
Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "${APP_NAME} requires 64-bit Windows."
    Quit
  ${EndIf}
FunctionEnd

Section "${APP_NAME} (required)" SecCore
  SectionIn RO
  SetOutPath "$INSTDIR"
  File "..\..\build\bin\${APP_EXE}"
  File "..\..\build\bin\stylers.model.xml"
  File /r "..\..\build\bin\icons"
  File /r "..\..\build\bin\icons-solar"
  File /r "..\..\build\bin\icons-iconpark"
  File /r "..\..\build\bin\themes"
  File /r /x "po2mo.py" /x "wxnpp.pot" /x "*.po" /x "__pycache__" "..\..\build\bin\locale"
  SetOutPath "$INSTDIR\nib"
  File "..\..\build\bin\nib\npp_bridge.dll"
  SetOutPath "$INSTDIR"

  WriteUninstaller "$INSTDIR\uninstall.exe"
  WriteRegStr HKCU "Software\wxNotepadPlusPlus\Installer" "InstallDir" "$INSTDIR"

  ; Add/Remove Programs entry (per-user hive, matching the per-user install).
  WriteRegStr   HKCU "${ARP_KEY}" "DisplayName"          "${APP_NAME}"
  WriteRegStr   HKCU "${ARP_KEY}" "DisplayVersion"       "${APP_VERSION}"
  WriteRegStr   HKCU "${ARP_KEY}" "Publisher"            "wxNotepad++ Project"
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
  DeleteRegKey HKCU "Software\wxNotepadPlusPlus"   ; only ever held the Installer\InstallDir value written above
SectionEnd
