# wxNote - experimental cross-platform (Windows / Linux / macOS) text editor.
#
# wxWidgets UI + wxStyledTextCtrl editor (which bundles Scintilla); our portable Lexilla supplies the
# language lexers. The core exposes its own permissive cross-platform plugin API ("Nib") and contains no
# Notepad++-ABI code; real N++ binary plugins are supported on Windows via the optional GPL
# packages/npp-bridge (loaded like any plugin). Win32-only subsystems are #ifdef __WXMSW__'d out elsewhere.
#
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#   ./build/bin/wxnote            (wxnote.exe on Windows)

cmake_minimum_required(VERSION 3.20)
# C is enabled for the vendored Lua + LPeg (Scintillua's runtime) - both are C.
project(wxNote VERSION 0.9.16 LANGUAGES CXX C)

include(FetchContent)

if(MSVC)
    string(APPEND CMAKE_CXX_FLAGS " /EHsc")          # wxWidgets requires /EHsc
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")   # /MT - match wx's static CRT (CMP0091 NEW @ cmake 3.20)
endif()

# ---- wxWidgets (fetched + built from source; provides wxStyledTextCtrl + its bundled Scintilla) ----
set(wxBUILD_SHARED            OFF CACHE BOOL "" FORCE)
if(WIN32)
    set(wxBUILD_USE_STATIC_RUNTIME ON CACHE BOOL "" FORCE)   # /MT, self-contained exe on Windows
endif()
set(wxBUILD_SAMPLES          OFF CACHE BOOL "" FORCE)
set(wxBUILD_TESTS            OFF CACHE BOOL "" FORCE)
set(wxBUILD_DEMOS            OFF CACHE BOOL "" FORCE)

message(STATUS "[wxnote] Fetching wxWidgets 3.3.1 (one-time; builds from source)...")
FetchContent_Declare(wxwidgets
    URL https://github.com/wxWidgets/wxWidgets/releases/download/v3.3.1/wxWidgets-3.3.1.tar.bz2
)
FetchContent_MakeAvailable(wxwidgets)

# ---- Lexilla (portable C++ syntax-highlighting lexers; CreateLexer) --------------------------------
file(GLOB LEXILLA_SRC CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/third_party/lexilla/src/Lexilla.cxx"
    "${CMAKE_SOURCE_DIR}/third_party/lexilla/lexlib/*.cxx"
    "${CMAKE_SOURCE_DIR}/third_party/lexilla/lexers/*.cxx")
add_library(lexilla STATIC ${LEXILLA_SRC})
target_include_directories(lexilla
    PUBLIC  "${CMAKE_SOURCE_DIR}/third_party/lexilla/include"
    PRIVATE "${CMAKE_SOURCE_DIR}/third_party/lexilla/lexlib"
            "${CMAKE_SOURCE_DIR}/third_party/scintilla/include")
set_target_properties(lexilla PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
if(MSVC)
    target_compile_options(lexilla PRIVATE /EHsc /utf-8)
endif()

# ---- Lua + LPeg + Scintillua (wxNote's native language-definition engine) ---------------------------
# Lua 5.4 + LPeg run Scintillua's Lua-LPeg lexers; src/scintillua_engine.{h,cpp} embeds them. All three
# are MIT (compatible with the Apache-2.0 core). Fetched + built from source (no upstream CMake), which
# is why the file sets are listed explicitly (a fixed set per pinned version; a glob would also miss
# files an in-configure extract just created). lua.c/luac.c are omitted - they carry their own main().
message(STATUS "[wxnote] Fetching Lua 5.4.7 + LPeg 1.1.0 + Scintillua lexer.lua...")
FetchContent_Declare(lua54 URL https://www.lua.org/ftp/lua-5.4.7.tar.gz)
FetchContent_Declare(lpeg  URL http://www.inf.puc-rio.br/~roberto/lpeg/lpeg-1.1.0.tar.gz)
FetchContent_MakeAvailable(lua54 lpeg)

set(LUA_UNITS lapi lauxlib lbaselib lcode lcorolib lctype ldblib ldebug ldo ldump lfunc lgc linit
    liolib llex lmathlib lmem loadlib lobject lopcodes loslib lparser lstate lstring lstrlib ltable
    ltablib ltm lundump lutf8lib lvm lzio)
set(LUA_LPEG_SRC "")
foreach(u ${LUA_UNITS})
    list(APPEND LUA_LPEG_SRC "${lua54_SOURCE_DIR}/src/${u}.c")
endforeach()
foreach(u lpcap lpcode lpcset lpprint lptree lpvm)
    list(APPEND LUA_LPEG_SRC "${lpeg_SOURCE_DIR}/${u}.c")
endforeach()
add_library(lua_lpeg STATIC ${LUA_LPEG_SRC})
target_include_directories(lua_lpeg PUBLIC "${lua54_SOURCE_DIR}/src")
# Linked into the wxnote executable (PIE on modern Linux) and, transitively, tools; on ELF a static
# lib pulled into a PIE/shared target must be position-independent, so force -fPIC (no-op on MSVC).
set_target_properties(lua_lpeg PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_definitions(lua_lpeg PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Scintillua's lexer.lua (the engine's Lua half); staged so the resource copy below ships it.
set(SCINTILLUA_LEXER_LUA "${CMAKE_BINARY_DIR}/scintillua/lexer.lua")
if(NOT EXISTS "${SCINTILLUA_LEXER_LUA}")
    file(DOWNLOAD "https://raw.githubusercontent.com/orbitalquark/scintillua/default/lexers/lexer.lua"
         "${SCINTILLUA_LEXER_LUA}" STATUS _sl_status)
    list(GET _sl_status 0 _sl_code)
    if(NOT _sl_code EQUAL 0)
        message(FATAL_ERROR "failed to download Scintillua lexer.lua: ${_sl_status}")
    endif()
endif()

# ---- libvterm (VT520/xterm terminal-emulation core; MIT) -------------------------------------------
# The parser/state-machine core Neovim and emacs-libvterm build on, fetched from the neovim fork
# (the maintained GitHub mirror of Paul Evans' upstream) pinned to the v0.3.3 release tag. Writing a
# correct escape-sequence parser from scratch is a known bug farm, so the integrated terminal will sit
# on this instead. No upstream CMake (only a libtool Makefile), so the static target is assembled here
# like lua_lpeg: an explicit file set per pinned version.
message(STATUS "[wxnote] Fetching libvterm 0.3.3...")
FetchContent_Declare(libvterm
    URL      https://github.com/neovim/libvterm/archive/refs/tags/v0.3.3.tar.gz
    URL_HASH SHA256=0babe3ab42c354925dadede90d352f054aa9c4ae6842ea803a20c9741e172e56
)
FetchContent_MakeAvailable(libvterm)

# encoding.c #includes two GENERATED tables (encoding/DECdrawing.inc, encoding/uk.inc) that the GitHub
# tag archive does not ship - upstream's Makefile creates them from the .tbl sources with a perl
# one-liner, and perl is not a dependency this build can assume (the MSVC CI runners configure without
# it). The pre-generated pair is vendored in third_party/libvterm-tables (provenance + regeneration
# steps in its README.md) and staged next to encoding.c so its quoted #include resolves naturally.
file(COPY "${CMAKE_SOURCE_DIR}/third_party/libvterm-tables/encoding/DECdrawing.inc"
          "${CMAKE_SOURCE_DIR}/third_party/libvterm-tables/encoding/uk.inc"
     DESTINATION "${libvterm_SOURCE_DIR}/src/encoding")

set(VTERM_SRC "")
foreach(u encoding keyboard mouse parser pen screen state unicode vterm)
    list(APPEND VTERM_SRC "${libvterm_SOURCE_DIR}/src/${u}.c")
endforeach()
add_library(vterm STATIC ${VTERM_SRC})
target_include_directories(vterm PUBLIC "${libvterm_SOURCE_DIR}/include")
# C11, not the C99 upstream advertises: the code leans on designated initializers, which MSVC's
# default (C89 + extensions) C mode rejects - /std:c11 (VS2019 16.8+) is the first MSVC mode that
# accepts them, and C11 is upward-compatible for the GCC/clang builds. PIC for the same reason as
# lua_lpeg above: this archive ends up inside the PIE wxnote executable on ELF.
set_target_properties(vterm PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)
# Third-party code we do not patch: mute its warnings on THIS target only (PRIVATE), so our own
# targets' warning signal stays intact and the global flags stay clean.
if(MSVC)
    target_compile_options(vterm PRIVATE /w)
    target_compile_definitions(vterm PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vterm PRIVATE -w)
endif()

# ---- wxBorderlessFrame (vendored; the optional integrated/borderless title bar) ---------------------
# Windows + Linux(GTK) only: the library has no macOS backend - its platform .cpp self-guard on _WIN32 /
# __WXGTK__, so on Apple they compile to nothing and wxBorderlessFrame is unusable. Built with our own
# wx targets because the upstream CMake uses find_package(wxWidgets) (a system wx), not our FetchContent.
if(WIN32 OR (UNIX AND NOT APPLE))
    file(GLOB WXBF_SRC CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/third_party/wxbf/src/*.cpp")
    add_library(wxbf STATIC ${WXBF_SRC})
    target_include_directories(wxbf PUBLIC "${CMAKE_SOURCE_DIR}/third_party/wxbf/include")
    target_link_libraries(wxbf PUBLIC wx::core wx::base)
    set_target_properties(wxbf PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    if(MSVC)
        target_compile_options(wxbf PRIVATE /EHsc /utf-8)
    endif()
    # The GTK backend (borderless_frame_gtk.cpp) includes <gdk/gdk.h> directly; wxWidgets does not
    # propagate the GTK include dirs to dependents, so add them explicitly (the upstream CMake does the
    # same in its UNIX block). GTK3 is already present - wxGTK itself was built against it.
    if(UNIX AND NOT APPLE)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
        target_include_directories(wxbf PRIVATE ${GTK3_INCLUDE_DIRS})
        target_link_directories(wxbf PUBLIC ${GTK3_LIBRARY_DIRS})
        target_link_libraries(wxbf PUBLIC ${GTK3_LIBRARIES})
    endif()
endif()

# ---- the wxNote executable --------------------------------------------------------------------
# WIN32 = GUI-subsystem app on Windows; a plain executable elsewhere (GTK on Linux, Cocoa on macOS).
if(WIN32)
    # Generate the version resource from project(... VERSION ...) so the exe's file/product version can
    # never drift from the release version (it silently shipped 0.7.0 for 0.8.0 when hand-maintained).
    # The .rc also embeds wxnote.ico as the exe's shell icon.
    configure_file("${CMAKE_SOURCE_DIR}/resources/app.rc.in" "${CMAKE_BINARY_DIR}/app.rc" @ONLY)
    add_executable(wxnote WIN32 src/main.cpp src/scintillua_engine.cpp src/term_backend.cpp src/term_view.cpp "${CMAKE_BINARY_DIR}/app.rc")
else()
    add_executable(wxnote src/main.cpp src/scintillua_engine.cpp src/term_backend.cpp src/term_view.cpp)
endif()
set_target_properties(wxnote PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Surface the single source of truth for the version (project(... VERSION)) to the code, so the
# About dialog can show it without a second hardcoded string drifting out of sync.
target_compile_definitions(wxnote PRIVATE WXN_VERSION="${PROJECT_VERSION}")

# macOS-native shim wxWidgets doesn't expose (blank the native title bar via NSWindow.titleVisibility).
# Objective-C++ (.mm) compiled only on Apple; needs AppKit (wx::core links Cocoa transitively, but be
# explicit for our own translation unit's NSWindow symbols). enable_language(OBJCXX) is
# required: since CMake 3.16 the .mm extension maps to the separate OBJCXX language, which the top-level
# project(... LANGUAGES CXX) doesn't turn on (and can't unconditionally, as Win/Linux have no ObjC++).
if(APPLE)
    enable_language(OBJCXX)
    target_sources(wxnote PRIVATE src/macos_native.mm)
    target_link_libraries(wxnote PRIVATE "-framework AppKit")
endif()

# GTK-native shim wxWidgets doesn't expose: neutral-theme the app's native GtkScrollbars (kills the Mint
# accent gradient that otherwise paints full-height down the editor's right edge). Plain C++ (no Objective-C);
# compiled + GTK3-wired ONLY here, so MSW/macOS never see it. NOTE: gtk_native.cpp has no #ifdef self-guard
# (it includes no wx header, so __WXGTK__ wouldn't be defined there) - the CMake gate below is what excludes
# it off-Linux. wxWidgets does not propagate GTK include dirs to dependents, so add them explicitly (the wxbf
# block above already discovered GTK3 via pkg_check_modules; re-invoking is idempotent).
if(UNIX AND NOT APPLE)
    target_sources(wxnote PRIVATE src/gtk_native.cpp)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    target_include_directories(wxnote PRIVATE ${GTK3_INCLUDE_DIRS})
    target_link_directories(wxnote PRIVATE ${GTK3_LIBRARY_DIRS})
    target_link_libraries(wxnote PRIVATE ${GTK3_LIBRARIES})
    # forkpty() (src/term_backend.cpp) lives in the separate libutil on glibc < 2.34 (merged into
    # libc since, with libutil kept as a stub) - linking it unconditionally is safe on every glibc
    # and required on the old ones. macOS needs nothing: forkpty is in libSystem there.
    target_link_libraries(wxnote PRIVATE util)
endif()

target_include_directories(wxnote PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/scintilla/include"     # Scintilla.h, ILexer.h (SCI_* + SCNotification)
    "${CMAKE_SOURCE_DIR}/third_party/lexilla/include"       # Lexilla.h (CreateLexer), SciLexer.h (SCE_*)
    "${CMAKE_SOURCE_DIR}/include/nib"                       # nib.h - our own permissive, cross-platform plugin API
    "${CMAKE_SOURCE_DIR}/src")                              # command_ids.h, app_icon_svg.h, menu_*.h - the core includes nothing from include/npp-compat (that ABI belongs to packages/npp-bridge alone)

target_link_libraries(wxnote PRIVATE
    wx::core wx::aui wx::base wx::xml wx::stc
    lexilla lua_lpeg
    vterm)   # terminal-emulation core behind src/term_view.cpp (its include dir propagates from the vterm target)

# Single-instance "reuse window" IPC (wxServer/wxClient/wxConnection): on Windows these map to the
# DDE classes, which live in wx::base itself (src/msw/dde.cpp) - no extra link needed there. Everywhere
# else they map to wxTCPServer/wxTCPClient/wxTCPConnection, which live in wxWidgets' separate "net"
# component (src/common/sckipc.cpp) and must be linked explicitly.
if(UNIX)
    target_link_libraries(wxnote PRIVATE wx::net)
endif()

# Optional integrated/borderless title bar (Settings > Preferences): link the vendored wxBorderlessFrame
# where it exists + let the code use it (WXN_HAS_BORDERLESS). Off on macOS (keeps the native bar).
if(WIN32 OR (UNIX AND NOT APPLE))
    target_link_libraries(wxnote PRIVATE wxbf)
    target_compile_definitions(wxnote PRIVATE WXN_HAS_BORDERLESS=1)
endif()

# Windows-only chrome + plugin host (all behind #ifdef __WXMSW__ in src/main.cpp).
if(WIN32)
    target_link_libraries(wxnote PRIVATE comctl32 imm32 msimg32 ole32 oleaut32 dwmapi uxtheme)
endif()
if(MSVC)
    # /bigobj: main.cpp emits more COFF sections than the default object format allows - the arm64
    # target crossed the limit first (fatal C1128 on the 0.9.5 CI while x64 still fit) because section
    # counts differ per target architecture.
    target_compile_options(wxnote PRIVATE /utf-8 /bigobj)
endif()

# ---- terminal_selftest: behavioural self-test for the Terminal panel ------------------------------
# Builds a real TerminalPanel and drives it through its own public API. Deliberately a SEPARATE target
# so no test-only code ships inside wxnote, and deliberately not added to add_test(): it needs a display
# (it creates a real frame and spawns a real shell), so it is a developer/CI-with-display tool rather
# than part of a headless test run.
#   cmake --build build --target terminal_selftest && build/bin/terminal_selftest
# term_backend.cpp/term_view.cpp are compiled in too: the selftest builds against the same terminal
# sources as the app, so backend/renderer regressions surface in the harness rather than only in a
# manual app run.
add_executable(terminal_selftest tests/terminal_selftest.cpp src/term_backend.cpp src/term_view.cpp)
set_target_properties(terminal_selftest PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_include_directories(terminal_selftest PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/scintilla/include"
    "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(terminal_selftest PRIVATE wx::core wx::aui wx::base wx::stc vterm)
if(UNIX AND NOT APPLE)
    target_include_directories(terminal_selftest PRIVATE ${GTK3_INCLUDE_DIRS})
    target_link_libraries(terminal_selftest PRIVATE ${GTK3_LIBRARIES})
    # forkpty() for term_backend.cpp - same libutil note as the wxnote target above.
    target_link_libraries(terminal_selftest PRIVATE util)
endif()
if(MSVC)
    target_compile_options(terminal_selftest PRIVATE /utf-8)
endif()

# ---- keymap_selftest: headless self-test for the shortcut store + conflict engine (Phase 3) --------
# No GUI (no frame, no display): seeds a KeymapStore and drives the ConflictEngine over the static
# Scintilla-default mirror, asserting the conflict classes. Links only
# wx core/base for wxAcceleratorEntry/wxString; the keymap/conflict code is header-only.
#   cmake --build build --target keymap_selftest && build/bin/keymap_selftest
add_executable(keymap_selftest tests/keymap_selftest.cpp)
set_target_properties(keymap_selftest PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_include_directories(keymap_selftest PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/scintilla/include"
    "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(keymap_selftest PRIVATE wx::core wx::base)
if(MSVC)
    target_compile_options(keymap_selftest PRIVATE /utf-8)
endif()

# ---- bridge_selftest: end-to-end self-test of the npp-bridge Phase-1 surface -----------------------
# Boots the REAL application (tests/bridge_selftest.cpp #includes src/main.cpp with wxIMPLEMENT_APP
# neutralized and supplies its own app subclass + main() -> wxEntry), so the genuine host pipeline -
# nib capability surface, plugin loader, save pipeline, wx command dispatcher - loads the real
# npp_bridge, which loads the PROBE plugin (packages/npp-bridge/example). The harness then drives
# open/edit/save/undo-to-savepoint/save-all/close through DIRECT host calls (no OS input injection)
# and asserts the Phase-1 acceptance points against the probe's notification log. User data is fully
# sandboxed (custom wxAppTraits + wxFileConfig), so it never touches a real installation's state.
# Needs a display (it creates the real frame), so - like terminal_selftest - it is a developer /
# CI-with-display tool, not part of a headless test run. Mirrors the wxnote target's build setup
# because it compiles the same sources.
#   cmake --build build --target bridge_selftest && build/bin/bridge_selftest
add_executable(bridge_selftest tests/bridge_selftest.cpp src/scintillua_engine.cpp src/term_backend.cpp src/term_view.cpp)
set_target_properties(bridge_selftest PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_compile_definitions(bridge_selftest PRIVATE WXN_VERSION="${PROJECT_VERSION}")
if(APPLE)
    target_sources(bridge_selftest PRIVATE src/macos_native.mm)
    target_link_libraries(bridge_selftest PRIVATE "-framework AppKit")
endif()
if(UNIX AND NOT APPLE)
    target_sources(bridge_selftest PRIVATE src/gtk_native.cpp)
    target_include_directories(bridge_selftest PRIVATE ${GTK3_INCLUDE_DIRS})
    target_link_directories(bridge_selftest PRIVATE ${GTK3_LIBRARY_DIRS})
    target_link_libraries(bridge_selftest PRIVATE ${GTK3_LIBRARIES})
    target_link_libraries(bridge_selftest PRIVATE util)
endif()
target_include_directories(bridge_selftest PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/scintilla/include"
    "${CMAKE_SOURCE_DIR}/third_party/lexilla/include"
    "${CMAKE_SOURCE_DIR}/include/nib"
    "${CMAKE_SOURCE_DIR}/include/npp-compat"    # Notepad_plus_msgs.h - the NPPN_*/NPPM_* codes the assertions speak
    "${CMAKE_SOURCE_DIR}/src")                  # main.cpp itself (textually included) + its headers
target_link_libraries(bridge_selftest PRIVATE
    wx::core wx::aui wx::base wx::xml wx::stc
    lexilla lua_lpeg
    vterm)
if(UNIX)
    target_link_libraries(bridge_selftest PRIVATE wx::net)
endif()
if(WIN32 OR (UNIX AND NOT APPLE))
    target_link_libraries(bridge_selftest PRIVATE wxbf)
    target_compile_definitions(bridge_selftest PRIVATE WXN_HAS_BORDERLESS=1)
endif()
if(WIN32)
    target_link_libraries(bridge_selftest PRIVATE comctl32 imm32 msimg32 ole32 oleaut32 dwmapi uxtheme)
endif()
if(MSVC)
    target_compile_options(bridge_selftest PRIVATE /utf-8 /bigobj)
endif()
# The runtime pieces the booted app + bridge load: bin resources (copied by wxnote's POST_BUILD), the
# bridge in bin/nib, the Nib test plugin, and the probe (Windows DLL / POSIX staged .so).
add_dependencies(bridge_selftest wxnote npp_bridge nib_test_plugin)
if(WIN32)
    add_dependencies(bridge_selftest npp_probe_plugin npp_test_plugin)
else()
    add_dependencies(bridge_selftest npp_example_plugin)
endif()

# ---- deploy runtime resources (toolbar icons, themes, default styler) next to the exe --------------
add_custom_command(TARGET wxnote POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources/icons"  "$<TARGET_FILE_DIR:wxnote>/icons"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources/icons-solar" "$<TARGET_FILE_DIR:wxnote>/icons-solar"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources/icons-iconpark" "$<TARGET_FILE_DIR:wxnote>/icons-iconpark"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources/icons-streamline" "$<TARGET_FILE_DIR:wxnote>/icons-streamline"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources/themes" "$<TARGET_FILE_DIR:wxnote>/themes"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources/fonts" "$<TARGET_FILE_DIR:wxnote>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/resources/stylers.model.xml" "$<TARGET_FILE_DIR:wxnote>/stylers.model.xml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/resources/contextMenu.xml" "$<TARGET_FILE_DIR:wxnote>/contextMenu.xml"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources/locale" "$<TARGET_FILE_DIR:wxnote>/locale"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SCINTILLUA_LEXER_LUA}" "$<TARGET_FILE_DIR:wxnote>/lexers/lexer.lua"
    VERBATIM)

# ---- Windows-only Notepad++-ABI test plugin (exercises the Win32 plugin loader) --------------------
# Lives OUTSIDE the permissive src/: it CONSUMES the N++ ABI (from include/npp-compat) like a real N++
# plugin, so it stays with the N++-compat world in packages/, keeping src/ free of N++-ABI code.
if(WIN32)
    add_subdirectory(packages/test_plugin)
    add_dependencies(wxnote npp_test_plugin)
endif()

# ---- cross-platform Nib-API plugin (exercises our own permissive plugin API on every OS) -----------
add_subdirectory(src/plugins/nib_test_plugin)
add_dependencies(wxnote nib_test_plugin)

# ---- optional GPL udl-compat plugin: translates legacy Notepad++ UDLs into Scintillua lexers -------
# Reproduces the N++ UDL file format, so (like npp-bridge) it stays GPL and out of the Apache core.
add_subdirectory(packages/udl-compat)
add_dependencies(wxnote udl_compat_plugin)

# ---- optional GPL npp-shortcuts-compat plugin: imports a Notepad++ shortcuts.xml keybinding file ----
# Reproduces the N++ shortcuts.xml format and feeds bindings into the core via nib.keymap/1, so (like
# udl-compat / npp-bridge) it stays GPL and out of the Apache core.
add_subdirectory(packages/npp-shortcuts-compat)
add_dependencies(wxnote npp_shortcuts_compat_plugin)

# ---- optional GPL Notepad++ binary-plugin bridge (every OS now; itself a Nib plugin) ---------------
# Phase 1 of docs/ARCHITECTURE.md: the bridge builds on Linux/macOS too (its Win32-only
# machinery is #ifdef'd internally), so abi_layout_asserts.h compile-verifies the N++ ABI layout on
# every CI runner and the portable dispatch core is exercised by the build everywhere. The bridge is
# still only a live plugin host on Windows; off-Windows it is the compile target for the shim SDK path.
add_subdirectory(packages/npp-bridge)
add_dependencies(wxnote npp_bridge)
# Off-Windows, also pull the recompiled-plugin example (which links libnpp_shim) into wxnote's build
# graph, so `cmake --build --target wxnote` on the Linux/macOS CI runners actually compiles npp_shim.cpp
# + example_plugin.cpp - the shim SDK path that Windows can't verify. (Windows uses the OS ::SendMessage.)
if(NOT WIN32)
    add_dependencies(wxnote npp_example_plugin)
endif()
