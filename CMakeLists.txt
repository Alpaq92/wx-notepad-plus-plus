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
project(wxNote VERSION 0.8.0 LANGUAGES CXX C)

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
    add_executable(wxnote WIN32 src/main.cpp src/scintillua_engine.cpp "${CMAKE_BINARY_DIR}/app.rc")
else()
    add_executable(wxnote src/main.cpp src/scintillua_engine.cpp)
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
endif()

target_include_directories(wxnote PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/scintilla/include"     # Scintilla.h, ILexer.h (SCI_* + SCNotification)
    "${CMAKE_SOURCE_DIR}/third_party/lexilla/include"       # Lexilla.h (CreateLexer), SciLexer.h (SCE_*)
    "${CMAKE_SOURCE_DIR}/include/nib"                       # nib.h - our own permissive, cross-platform plugin API
    "${CMAKE_SOURCE_DIR}/src")                              # command_ids.h, app_icon_svg.h, menu_*.h - the core includes nothing from include/npp-compat (that ABI belongs to packages/npp-bridge alone)

target_link_libraries(wxnote PRIVATE
    wx::core wx::aui wx::base wx::xml wx::stc
    lexilla lua_lpeg)

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
    target_compile_options(wxnote PRIVATE /utf-8)
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
