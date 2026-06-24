# wxWidgets cross-platform editor spike (see docs/WXWIDGETS_MIGRATION_PLAN.md).
#
# Builds two ways:
#   (a) as a SUBDIRECTORY of the Notepad++ top-level CMake (Windows dev build, -DNPP_BUILD_WX_SPIKE=ON), and
#   (b) STANDALONE for CI / non-Windows:  cmake -S spike -B build
#       (no Win32 app, no RC language, builds Lexilla itself - the top-level CMake is Windows-locked).
#
# The editor is wxStyledTextCtrl (wxSTC bundles its own Scintilla); our portable Lexilla supplies the
# lexers. On Windows the Win32 plugin host / NPPM / docking / native theming compile in (#ifdef __WXMSW__).

# ---- mode detection ------------------------------------------------------------------------------
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.20)
    project(npp_wx_spike LANGUAGES CXX)
    set(NPP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")   # repo root is one level up from spike/
    set(SPIKE_STANDALONE TRUE)
else()
    set(NPP_ROOT "${CMAKE_SOURCE_DIR}")              # the Notepad++ top-level
    set(SPIKE_STANDALONE FALSE)
endif()

include(FetchContent)

# wxWidgets needs /EHsc on MSVC (the top-level strips it for the app's /EHa; this child scope is safe).
if(MSVC)
    string(APPEND CMAKE_CXX_FLAGS " /EHsc")
endif()

# ---- wxWidgets (fetched + built from source; bundles wxStyledTextCtrl + Scintilla + Lexilla) ------
set(wxBUILD_SHARED            OFF CACHE BOOL "" FORCE)
if(WIN32)
    set(wxBUILD_USE_STATIC_RUNTIME ON CACHE BOOL "" FORCE)   # /MT, matches the app on Windows
endif()
set(wxBUILD_SAMPLES          OFF CACHE BOOL "" FORCE)
set(wxBUILD_TESTS            OFF CACHE BOOL "" FORCE)
set(wxBUILD_DEMOS            OFF CACHE BOOL "" FORCE)

# 3.3.x is required for native Windows dark mode (MSWEnableDarkMode); harmless elsewhere.
message(STATUS "[spike] Fetching wxWidgets 3.3.1 (one-time; builds from source)...")
FetchContent_Declare(wxwidgets
    URL https://github.com/wxWidgets/wxWidgets/releases/download/v3.3.1/wxWidgets-3.3.1.tar.bz2
)
FetchContent_MakeAvailable(wxwidgets)

set(NPP_SRC "${NPP_ROOT}/PowerEditor/src")

# ---- Lexilla (portable C++ syntax highlighting; CreateLexer) -------------------------------------
# As a subdirectory the top-level already provides the `lexilla` target. Standalone, build it here -
# portably (no WIN32/UNICODE defines, unlike lexilla/CMakeLists.txt which targets the Windows app).
if(SPIKE_STANDALONE)
    file(GLOB SPIKE_LEXILLA_SRC CONFIGURE_DEPENDS
        "${NPP_ROOT}/lexilla/src/Lexilla.cxx"
        "${NPP_ROOT}/lexilla/lexlib/*.cxx"
        "${NPP_ROOT}/lexilla/lexers/*.cxx")
    add_library(lexilla STATIC ${SPIKE_LEXILLA_SRC})
    target_include_directories(lexilla
        PUBLIC  "${NPP_ROOT}/lexilla/include"
        PRIVATE "${NPP_ROOT}/lexilla/lexlib" "${NPP_ROOT}/scintilla/include")
    set_target_properties(lexilla PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
endif()

# ---- the spike executable ------------------------------------------------------------------------
# WIN32 = GUI subsystem app on Windows; a plain executable elsewhere (X11/Wayland/Cocoa via wx).
if(WIN32)
    add_executable(npp_wx_spike WIN32 main.cpp)
else()
    add_executable(npp_wx_spike main.cpp)
endif()

set_target_properties(npp_wx_spike PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Real Notepad++ headers used as-is: Scintilla (SCI_*/SCNotification), Lexilla, the NPPM_*/IDM_* ids.
target_include_directories(npp_wx_spike PRIVATE
    "${NPP_ROOT}/scintilla/include"     # Scintilla.h, ILexer.h
    "${NPP_ROOT}/lexilla/include"       # Lexilla.h (CreateLexer), SciLexer.h (SCE_* styles)
    "${NPP_SRC}/MISC/PluginsManager"    # Notepad_plus_msgs.h, PluginInterface.h (Windows plugin code)
    "${NPP_SRC}/WinControls/DockingWnd" # Docking.h (tTbData for plugin docking panels)
    "${NPP_SRC}")                       # menuCmdID.h

# Portable core: wxWidgets + wxStyledTextCtrl (its own bundled Scintilla) + our portable Lexilla.
# The raw native `scintilla` static lib is no longer needed - wxSTC supplies Scintilla.
target_link_libraries(npp_wx_spike PRIVATE
    wx::core wx::aui wx::base wx::xml wx::stc
    lexilla)

# Windows-only: the comctl/imm/dwm/uxtheme libs the Win32 chrome + plugin host (PluginInterface.h,
# NPPM_*, docking, DWM dark titlebar, SetWindowTheme) use - all behind #ifdef __WXMSW__ in main.cpp.
if(WIN32)
    target_link_libraries(npp_wx_spike PRIVATE
        comctl32 imm32 msimg32 ole32 oleaut32 dwmapi uxtheme)
endif()

if(MSVC)
    target_compile_options(npp_wx_spike PRIVATE /utf-8)
endif()

# Deploy Notepad++'s toolbar icon pack + themes next to the exe so they load at runtime (portable).
add_custom_command(TARGET npp_wx_spike POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/icons" "$<TARGET_FILE_DIR:npp_wx_spike>/icons"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${NPP_SRC}/stylers.model.xml" "$<TARGET_FILE_DIR:npp_wx_spike>/stylers.model.xml"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${NPP_ROOT}/PowerEditor/installer/themes" "$<TARGET_FILE_DIR:npp_wx_spike>/themes"
    VERBATIM)

# Minimal test plugin DLL (verifies the Win32 plugin loader). Windows-only: the plugin ABI is HWND-based.
if(WIN32)
    add_subdirectory(test_plugin)
    add_dependencies(npp_wx_spike npp_test_plugin)   # so building the shell also builds the test plugin
endif()
