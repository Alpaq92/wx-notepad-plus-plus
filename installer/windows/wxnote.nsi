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
!include "Sections.nsh"      ; SF_SELECTED - so an UNTICKED "Add to PATH" can undo a previous install's entry

; Read straight from the top-level CMakeLists.txt's project(... VERSION ...) so this can't drift
; out of sync with it again (every packaging script independently hardcoded its own version string
; and 0.4.0 shipped labeled 0.3.0 everywhere as a result).
!searchparse /file "..\..\CMakeLists.txt" "project(wxNote VERSION " APP_VERSION " LANGUAGES"

!define APP_NAME    "wxNote"
!define APP_URL     "https://github.com/Alpaq92/wx-notepad-plus-plus"
!define APP_EXE     "wxnote.exe"
!define ARP_KEY     "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"
; File-association identity. PROGID is the registry name for "a file wxNote opens" - Windows will not
; offer an application as a handler at all until one exists. CAP_KEY is the Capabilities subkey that
; lists wxNote in Settings > Default apps; it deliberately sits UNDER Software\wxNote, the app's own
; settings key, so every removal path below deletes that SUBKEY alone and leaves the settings beside it.
!define PROGID      "wxNote.Document"
!define CAP_KEY     "Software\wxNote\Capabilities"
!define DOC_ICON    "wxnote-doc.ico"

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
; ---- file associations, Open-with and the Explorer context menu -------------------------------
; ONE list of extensions, walked by whichever per-extension operation is passed in. Registration and
; removal MUST agree exactly: a drifted list would leave wxNote advertised for types the uninstaller
; no longer knows to clean up, and Windows would go on offering a handler whose exe is gone.
;
; The list is deliberately long rather than exhaustive. Being offered for *any* file at all - including
; extensions nobody has heard of, and files with no extension - is handled by the SupportedTypes and
; OpenWithList wildcards in AssocRegister below. THIS list is the narrower question of which types
; wxNote offers to be the DEFAULT for in Settings > Default apps, where Windows requires them named.
!macro ForEachExt _OP
  ; plain text, notes and documentation
  !insertmacro ${_OP} ".txt"
  !insertmacro ${_OP} ".log"
  !insertmacro ${_OP} ".md"
  !insertmacro ${_OP} ".markdown"
  !insertmacro ${_OP} ".mdown"
  !insertmacro ${_OP} ".mkd"
  !insertmacro ${_OP} ".rst"
  !insertmacro ${_OP} ".adoc"
  !insertmacro ${_OP} ".asciidoc"
  !insertmacro ${_OP} ".text"
  !insertmacro ${_OP} ".me"
  !insertmacro ${_OP} ".nfo"
  !insertmacro ${_OP} ".rtf"
  !insertmacro ${_OP} ".tex"
  !insertmacro ${_OP} ".bib"
  !insertmacro ${_OP} ".srt"
  !insertmacro ${_OP} ".vtt"
  ; configuration and dotfiles Explorer treats as extensions
  !insertmacro ${_OP} ".ini"
  !insertmacro ${_OP} ".cfg"
  !insertmacro ${_OP} ".conf"
  !insertmacro ${_OP} ".config"
  !insertmacro ${_OP} ".properties"
  !insertmacro ${_OP} ".toml"
  !insertmacro ${_OP} ".yaml"
  !insertmacro ${_OP} ".yml"
  !insertmacro ${_OP} ".env"
  !insertmacro ${_OP} ".editorconfig"
  !insertmacro ${_OP} ".gitignore"
  !insertmacro ${_OP} ".gitattributes"
  !insertmacro ${_OP} ".gitmodules"
  !insertmacro ${_OP} ".dockerignore"
  !insertmacro ${_OP} ".npmrc"
  !insertmacro ${_OP} ".babelrc"
  !insertmacro ${_OP} ".eslintrc"
  !insertmacro ${_OP} ".prettierrc"
  !insertmacro ${_OP} ".htaccess"
  !insertmacro ${_OP} ".reg"
  !insertmacro ${_OP} ".inf"
  !insertmacro ${_OP} ".desktop"
  ; structured data and interchange
  !insertmacro ${_OP} ".json"
  !insertmacro ${_OP} ".json5"
  !insertmacro ${_OP} ".jsonc"
  !insertmacro ${_OP} ".jsonl"
  !insertmacro ${_OP} ".ndjson"
  !insertmacro ${_OP} ".xml"
  !insertmacro ${_OP} ".xsd"
  !insertmacro ${_OP} ".xsl"
  !insertmacro ${_OP} ".xslt"
  !insertmacro ${_OP} ".dtd"
  !insertmacro ${_OP} ".csv"
  !insertmacro ${_OP} ".tsv"
  !insertmacro ${_OP} ".plist"
  !insertmacro ${_OP} ".proto"
  !insertmacro ${_OP} ".thrift"
  !insertmacro ${_OP} ".avsc"
  !insertmacro ${_OP} ".graphql"
  !insertmacro ${_OP} ".gql"
  !insertmacro ${_OP} ".cue"
  !insertmacro ${_OP} ".hcl"
  !insertmacro ${_OP} ".tf"
  !insertmacro ${_OP} ".tfvars"
  !insertmacro ${_OP} ".nix"
  ; web
  !insertmacro ${_OP} ".html"
  !insertmacro ${_OP} ".htm"
  !insertmacro ${_OP} ".xhtml"
  !insertmacro ${_OP} ".shtml"
  !insertmacro ${_OP} ".css"
  !insertmacro ${_OP} ".scss"
  !insertmacro ${_OP} ".sass"
  !insertmacro ${_OP} ".less"
  !insertmacro ${_OP} ".styl"
  !insertmacro ${_OP} ".js"
  !insertmacro ${_OP} ".mjs"
  !insertmacro ${_OP} ".cjs"
  !insertmacro ${_OP} ".jsx"
  !insertmacro ${_OP} ".ts"
  !insertmacro ${_OP} ".tsx"
  !insertmacro ${_OP} ".mts"
  !insertmacro ${_OP} ".cts"
  !insertmacro ${_OP} ".vue"
  !insertmacro ${_OP} ".svelte"
  !insertmacro ${_OP} ".astro"
  !insertmacro ${_OP} ".php"
  !insertmacro ${_OP} ".phtml"
  !insertmacro ${_OP} ".asp"
  !insertmacro ${_OP} ".aspx"
  !insertmacro ${_OP} ".jsp"
  !insertmacro ${_OP} ".ejs"
  !insertmacro ${_OP} ".hbs"
  !insertmacro ${_OP} ".njk"
  !insertmacro ${_OP} ".pug"
  !insertmacro ${_OP} ".twig"
  !insertmacro ${_OP} ".liquid"
  !insertmacro ${_OP} ".svg"
  ; systems and application languages
  !insertmacro ${_OP} ".c"
  !insertmacro ${_OP} ".h"
  !insertmacro ${_OP} ".i"
  !insertmacro ${_OP} ".cpp"
  !insertmacro ${_OP} ".cxx"
  !insertmacro ${_OP} ".cc"
  !insertmacro ${_OP} ".c++"
  !insertmacro ${_OP} ".hpp"
  !insertmacro ${_OP} ".hxx"
  !insertmacro ${_OP} ".hh"
  !insertmacro ${_OP} ".h++"
  !insertmacro ${_OP} ".ipp"
  !insertmacro ${_OP} ".inl"
  !insertmacro ${_OP} ".m"
  !insertmacro ${_OP} ".mm"
  !insertmacro ${_OP} ".cs"
  !insertmacro ${_OP} ".java"
  !insertmacro ${_OP} ".kt"
  !insertmacro ${_OP} ".kts"
  !insertmacro ${_OP} ".go"
  !insertmacro ${_OP} ".rs"
  !insertmacro ${_OP} ".swift"
  !insertmacro ${_OP} ".d"
  !insertmacro ${_OP} ".zig"
  !insertmacro ${_OP} ".nim"
  !insertmacro ${_OP} ".v"
  !insertmacro ${_OP} ".vala"
  !insertmacro ${_OP} ".pas"
  !insertmacro ${_OP} ".pp"
  !insertmacro ${_OP} ".f"
  !insertmacro ${_OP} ".f90"
  !insertmacro ${_OP} ".f95"
  !insertmacro ${_OP} ".for"
  !insertmacro ${_OP} ".ada"
  !insertmacro ${_OP} ".adb"
  !insertmacro ${_OP} ".ads"
  ; scripting and dynamic languages
  !insertmacro ${_OP} ".py"
  !insertmacro ${_OP} ".pyw"
  !insertmacro ${_OP} ".pyi"
  !insertmacro ${_OP} ".rb"
  !insertmacro ${_OP} ".rbw"
  !insertmacro ${_OP} ".erb"
  !insertmacro ${_OP} ".pl"
  !insertmacro ${_OP} ".pm"
  !insertmacro ${_OP} ".pod"
  !insertmacro ${_OP} ".t"
  !insertmacro ${_OP} ".lua"
  !insertmacro ${_OP} ".tcl"
  !insertmacro ${_OP} ".php3"
  !insertmacro ${_OP} ".r"
  !insertmacro ${_OP} ".rmd"
  !insertmacro ${_OP} ".jl"
  !insertmacro ${_OP} ".dart"
  !insertmacro ${_OP} ".groovy"
  !insertmacro ${_OP} ".scala"
  !insertmacro ${_OP} ".sc"
  !insertmacro ${_OP} ".clj"
  !insertmacro ${_OP} ".cljs"
  !insertmacro ${_OP} ".cljc"
  !insertmacro ${_OP} ".edn"
  !insertmacro ${_OP} ".ex"
  !insertmacro ${_OP} ".exs"
  !insertmacro ${_OP} ".erl"
  !insertmacro ${_OP} ".hrl"
  !insertmacro ${_OP} ".hs"
  !insertmacro ${_OP} ".lhs"
  !insertmacro ${_OP} ".ml"
  !insertmacro ${_OP} ".mli"
  !insertmacro ${_OP} ".fs"
  !insertmacro ${_OP} ".fsi"
  !insertmacro ${_OP} ".fsx"
  !insertmacro ${_OP} ".elm"
  !insertmacro ${_OP} ".purs"
  !insertmacro ${_OP} ".re"
  !insertmacro ${_OP} ".res"
  ; shells, batch and build
  !insertmacro ${_OP} ".sh"
  !insertmacro ${_OP} ".bash"
  !insertmacro ${_OP} ".zsh"
  !insertmacro ${_OP} ".fish"
  !insertmacro ${_OP} ".ksh"
  !insertmacro ${_OP} ".csh"
  !insertmacro ${_OP} ".ps1"
  !insertmacro ${_OP} ".psm1"
  !insertmacro ${_OP} ".psd1"
  !insertmacro ${_OP} ".bat"
  !insertmacro ${_OP} ".cmd"
  !insertmacro ${_OP} ".awk"
  !insertmacro ${_OP} ".sed"
  !insertmacro ${_OP} ".vim"
  !insertmacro ${_OP} ".el"
  !insertmacro ${_OP} ".mk"
  !insertmacro ${_OP} ".mak"
  !insertmacro ${_OP} ".makefile"
  !insertmacro ${_OP} ".cmake"
  !insertmacro ${_OP} ".gradle"
  !insertmacro ${_OP} ".sbt"
  !insertmacro ${_OP} ".ninja"
  !insertmacro ${_OP} ".bazel"
  !insertmacro ${_OP} ".bzl"
  !insertmacro ${_OP} ".bp"
  !insertmacro ${_OP} ".pro"
  !insertmacro ${_OP} ".pri"
  !insertmacro ${_OP} ".am"
  !insertmacro ${_OP} ".ac"
  !insertmacro ${_OP} ".spec"
  !insertmacro ${_OP} ".rules"
  ; query, diff and misc developer output
  !insertmacro ${_OP} ".sql"
  !insertmacro ${_OP} ".psql"
  !insertmacro ${_OP} ".ddl"
  !insertmacro ${_OP} ".diff"
  !insertmacro ${_OP} ".patch"
  !insertmacro ${_OP} ".po"
  !insertmacro ${_OP} ".pot"
  !insertmacro ${_OP} ".pgsql"
  !insertmacro ${_OP} ".asm"
  !insertmacro ${_OP} ".s"
  !insertmacro ${_OP} ".lst"
  !insertmacro ${_OP} ".map"
  !insertmacro ${_OP} ".sym"
  !insertmacro ${_OP} ".dump"
  !insertmacro ${_OP} ".trace"
  !insertmacro ${_OP} ".out"
  !insertmacro ${_OP} ".err"
!macroend

!macro AssocRegisterExt EXT
  ; Register the ProgId as a CHOICE for this extension, never as its default. OpenWithProgids is
  ; additive and is what "Open with" and the Default apps UI read; writing the extension key's own
  ; default value instead would silently seize the type from whatever already owns it, which is
  ; exactly the behaviour that makes installers infamous.
  WriteRegStr HKCU "${CAP_KEY}\FileAssociations" "${EXT}" "${PROGID}"
  WriteRegStr HKCU "Software\Classes\${EXT}\OpenWithProgids" "${PROGID}" ""
!macroend

!macro AssocUnregisterExt EXT
  ; Delete only OUR value under a key we share. DeleteRegKey here would take every other application's
  ; registration for that extension with it.
  DeleteRegValue HKCU "Software\Classes\${EXT}\OpenWithProgids" "${PROGID}"
  DeleteRegValue HKCU "${CAP_KEY}\FileAssociations" "${EXT}"
!macroend

; Everything the association option writes. Nothing here makes wxNote the default handler for anything:
; since Windows 8 an installer cannot set a default, and attempting it through the undocumented
; UserChoice hash is what gets an installer flagged as malware. All of this only makes wxNote
; SELECTABLE - the user still picks it, in Open with or in Settings > Default apps.
!macro AssocRegister
  ; 1. The ProgId: what a "wxNote document" is, and the icon Explorer draws for one.
  WriteRegStr HKCU "Software\Classes\${PROGID}" "" "wxNote Document"
  WriteRegStr HKCU "Software\Classes\${PROGID}" "FriendlyTypeName" "wxNote Document"
  WriteRegStr HKCU "Software\Classes\${PROGID}\DefaultIcon" "" "$INSTDIR\${DOC_ICON},0"
  WriteRegStr HKCU "Software\Classes\${PROGID}\shell\open\command" "" '"$INSTDIR\${APP_EXE}" "%1"'

  ; 2. The application. A SupportedTypes value of "*" is the documented way to say "this app opens ANY
  ; file", and it is what puts wxNote in "Open with > Choose another app" for an extension it has never
  ; been told about - the "all possible files" half of the requirement.
  WriteRegStr HKCU "Software\Classes\Applications\${APP_EXE}" "FriendlyAppName" "${APP_NAME}"
  WriteRegStr HKCU "Software\Classes\Applications\${APP_EXE}\DefaultIcon" "" "$INSTDIR\${APP_EXE},0"
  WriteRegStr HKCU "Software\Classes\Applications\${APP_EXE}\shell\open\command" "" '"$INSTDIR\${APP_EXE}" "%1"'
  WriteRegStr HKCU "Software\Classes\Applications\${APP_EXE}\SupportedTypes" "*" ""

  ; 3. ...and offer it in the Open-with list of every file type, extension or not.
  WriteRegStr HKCU "Software\Classes\*\OpenWithList\${APP_EXE}" "" ""

  ; 4. Explorer context menu: on any file, on a folder, and on the background of an open folder. The
  ; last one takes %V, not %1 - %1 is empty for a background click, %V is the folder clicked into.
  ; wxNote accepts a directory as well as a file (see its `file-or-folder` command-line parameter),
  ; which is what makes the two folder entries meaningful rather than an error message.
  ; On Windows 11 these land under "Show more options": the top-level menu only accepts a packaged
  ; IExplorerCommand handler, which a plain per-user NSIS install has no way to register.
  WriteRegStr HKCU "Software\Classes\*\shell\${APP_NAME}" "" "Edit with ${APP_NAME}"
  WriteRegStr HKCU "Software\Classes\*\shell\${APP_NAME}" "Icon" "$INSTDIR\${APP_EXE},0"
  WriteRegStr HKCU "Software\Classes\*\shell\${APP_NAME}\command" "" '"$INSTDIR\${APP_EXE}" "%1"'
  WriteRegStr HKCU "Software\Classes\Directory\shell\${APP_NAME}" "" "Open folder with ${APP_NAME}"
  WriteRegStr HKCU "Software\Classes\Directory\shell\${APP_NAME}" "Icon" "$INSTDIR\${APP_EXE},0"
  WriteRegStr HKCU "Software\Classes\Directory\shell\${APP_NAME}\command" "" '"$INSTDIR\${APP_EXE}" "%1"'
  WriteRegStr HKCU "Software\Classes\Directory\Background\shell\${APP_NAME}" "" "Open folder with ${APP_NAME}"
  WriteRegStr HKCU "Software\Classes\Directory\Background\shell\${APP_NAME}" "Icon" "$INSTDIR\${APP_EXE},0"
  WriteRegStr HKCU "Software\Classes\Directory\Background\shell\${APP_NAME}\command" "" '"$INSTDIR\${APP_EXE}" "%V"'

  ; 5. Settings > Default apps. RegisteredApplications -> Capabilities is what lists wxNote there at
  ; all; the FileAssociations written per extension below are the types it offers to be default for.
  WriteRegStr HKCU "${CAP_KEY}" "ApplicationName" "${APP_NAME}"
  WriteRegStr HKCU "${CAP_KEY}" "ApplicationDescription" "Open-source cross-platform text and code editor."
  WriteRegStr HKCU "${CAP_KEY}" "ApplicationIcon" "$INSTDIR\${APP_EXE},0"
  WriteRegStr HKCU "Software\RegisteredApplications" "${APP_NAME}" "${CAP_KEY}"
  !insertmacro ForEachExt AssocRegisterExt
!macroend

; The exact inverse of AssocRegister. Safe to run when nothing was ever registered.
!macro AssocUnregister
  !insertmacro ForEachExt AssocUnregisterExt
  DeleteRegKey   HKCU "Software\Classes\${PROGID}"
  DeleteRegKey   HKCU "Software\Classes\Applications\${APP_EXE}"
  DeleteRegKey   HKCU "Software\Classes\*\OpenWithList\${APP_EXE}"
  DeleteRegKey   HKCU "Software\Classes\*\shell\${APP_NAME}"
  DeleteRegKey   HKCU "Software\Classes\Directory\shell\${APP_NAME}"
  DeleteRegKey   HKCU "Software\Classes\Directory\Background\shell\${APP_NAME}"
  DeleteRegValue HKCU "Software\RegisteredApplications" "${APP_NAME}"
  ; The Capabilities SUBKEY only. Software\wxNote beside it is the user's settings and must survive,
  ; exactly as the uninstaller's closing comment promises.
  DeleteRegKey   HKCU "${CAP_KEY}"
!macroend

; Explorer caches association data; without this the new entries do not appear until it restarts.
!macro AssocNotifyShell
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
!macroend

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
  ; The icon Explorer draws for files handled by wxNote (see SecAssoc). Installed unconditionally even
  ; though the association section is optional: a DefaultIcon pointing at a file that is not there
  ; renders as a blank sheet, and that would outlive any later re-tick of the section.
  File "..\..\resources\${DOC_ICON}"
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

; ON by default. Registering does NOT make wxNote the default for anything - see AssocRegister - it
; makes wxNote selectable: present in "Open with" for every file type, listed in Settings > Default
; apps, and offered in the Explorer right-click menu for files and folders.
Section "File associations and context menu" SecAssoc
  !insertmacro AssocRegister
  ; Ownership marker, mirroring AddedToPath: it records that THIS installer created the registration,
  ; so the undo section below and the uninstaller only ever remove entries they actually put there.
  WriteRegDWORD HKCU "Software\wxNote-Installer" "RegisteredAssoc" 1
  !insertmacro AssocNotifyShell
SectionEnd

; Unticking the option on a re-install has to UNDO what an earlier install wrote - an unselected NSIS
; section simply does not run, so without this the entries would survive every install that turned the
; option off. Same shape, and the same reasoning, as "-UndoPathIfUnticked" below.
Section "-UndoAssocIfUnticked"
  SectionGetFlags ${SecAssoc} $0
  IntOp $0 $0 & ${SF_SELECTED}
  ${If} $0 == 0
    ReadRegDWORD $1 HKCU "Software\wxNote-Installer" "RegisteredAssoc"
    ${If} $1 == 1
      !insertmacro AssocUnregister
      DeleteRegValue HKCU "Software\wxNote-Installer" "RegisteredAssoc"
      !insertmacro AssocNotifyShell
      DetailPrint "Removed the wxNote file associations, as that option was unticked."
    ${EndIf}
  ${EndIf}
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
    ; Record WHICH directory went on PATH, not just that one did. An upgrade can be installed somewhere
    ; else, and then "$INSTDIR" no longer names the entry a previous install created - removing that
    ; would miss the stale one and, worse, could strike a directory we never added.
    WriteRegStr HKCU "Software\wxNote-Installer" "PathDir" "$INSTDIR"
    ; Tell already-running shells and Explorer to re-read the environment. Without this the new PATH
    ; only reaches processes started after the next sign-in.
    SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=5000
  ${ElseIf} $0 == 2
    DetailPrint "wxNote is already on PATH ($1) - leaving it, and leaving it alone on uninstall."
  ${Else}
    DetailPrint "Could not add wxNote to PATH: $1"
  ${EndIf}
SectionEnd

; Unticking "Add to PATH" has to UNDO what an earlier install did. An unselected NSIS section simply
; does not run - there is no "else" branch to a section - so without this hidden one the entry from a
; previous install would survive every subsequent install that turned the option off, and only ever be
; removable by uninstalling. The leading "-" keeps it off the components page and makes it always run.
;
; Gated on AddedToPath for the same reason SecPath refuses to claim an entry it found: if this
; installer never created it, the directory is on PATH because the USER put it there, and silently
; deleting it would break whatever else they keep in it. Removal targets the recorded PathDir, so an
; upgrade installed into a different folder still cleans up the old entry rather than the new one.
Section "-UndoPathIfUnticked"
  SectionGetFlags ${SecPath} $0
  IntOp $0 $0 & ${SF_SELECTED}
  ${If} $0 == 0
    ReadRegDWORD $1 HKCU "Software\wxNote-Installer" "AddedToPath"
    ${If} $1 == 1
      ReadRegStr $2 HKCU "Software\wxNote-Installer" "PathDir"
      ${If} $2 == ""
        StrCpy $2 "$INSTDIR"       ; installs predating PathDir recorded only the flag
      ${EndIf}
      InitPluginsDir               ; see SecPath - $PLUGINSDIR is empty until this runs
      File "/oname=$PLUGINSDIR\wxnote-path.ps1" "wxnote-path.ps1"
      nsExec::ExecToStack 'powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$PLUGINSDIR\wxnote-path.ps1" -Action Remove -Directory "$2"'
      Pop $3
      Pop $4
      ; Drop the ownership marker whatever the helper reported. Exit 2 means the entry was already
      ; gone, so we no longer own anything either way; leaving the flag set would make a later
      ; uninstall try to remove an entry that is not ours.
      DeleteRegValue HKCU "Software\wxNote-Installer" "AddedToPath"
      DeleteRegValue HKCU "Software\wxNote-Installer" "PathDir"
      ${If} $3 == 0
        DetailPrint "Removed wxNote from PATH ($2), as Add to PATH was unticked."
        SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=5000
      ${ElseIf} $3 == 2
        DetailPrint "wxNote was not on PATH ($2) - nothing to remove."
      ${Else}
        DetailPrint "Could not remove wxNote from PATH: $4"
      ${EndIf}
    ${EndIf}
  ${EndIf}
SectionEnd

Section "Uninstall"
  ; Remove only what the installer put there (user-created files - e.g. userDefineLangs\ - survive,
  ; and $INSTDIR itself is only removed if that leaves it empty).
  Delete "$INSTDIR\${APP_EXE}"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\NOTICE"
  Delete "$INSTDIR\${DOC_ICON}"
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
  ; PathDir is the directory that actually went on PATH, which is not necessarily $INSTDIR: an upgrade
  ; may have moved the install while the old entry stayed behind.
  ReadRegDWORD $0 HKCU "Software\wxNote-Installer" "AddedToPath"
  ${If} $0 == 1
    ReadRegStr $2 HKCU "Software\wxNote-Installer" "PathDir"
    ${If} $2 == ""
      StrCpy $2 "$INSTDIR"         ; installs predating PathDir recorded only the flag
    ${EndIf}
    InitPluginsDir                 ; see SecPath - $PLUGINSDIR is empty until this runs
    File "/oname=$PLUGINSDIR\wxnote-path.ps1" "wxnote-path.ps1"
    nsExec::ExecToStack 'powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$PLUGINSDIR\wxnote-path.ps1" -Action Remove -Directory "$2"'
    Pop $0
    Pop $1
    SetOutPath "$TEMP"   ; don't hold $INSTDIR open, or RMDir below fails
    SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=5000
  ${EndIf}

  ; File associations, Open-with entries, context-menu items and the Default-apps registration.
  ; Unconditional: this is the exact inverse of AssocRegister and is harmless if it never ran.
  !insertmacro AssocUnregister
  !insertmacro AssocNotifyShell

  DeleteRegKey HKCU "${ARP_KEY}"
  DeleteRegKey HKCU "Software\wxNote-Installer"   ; installer state only - the user's settings under Software\wxNote survive uninstall
SectionEnd
