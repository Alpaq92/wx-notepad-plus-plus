# wxNotepad++ - experimental cross-platform (Windows / Linux / macOS) Notepad++-faithful editor.
#
# wxWidgets UI + wxStyledTextCtrl editor (which bundles Scintilla); our portable Lexilla supplies the
# language lexers. The core exposes its own permissive cross-platform plugin API ("Nib") and contains no
# Notepad++-ABI code; real N++ binary plugins are supported on Windows via the optional GPL
# packages/npp-bridge (loaded like any plugin). Win32-only subsystems are #ifdef __WXMSW__'d out elsewhere.
#
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#   ./build/bin/wxnpp            (wxnpp.exe on Windows)

cmake_minimum_required(VERSION 3.20)
project(wxNotepadPlusPlus LANGUAGES CXX)

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

message(STATUS "[wxnpp] Fetching wxWidgets 3.3.1 (one-time; builds from source)...")
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

# ---- the wxNotepad++ executable --------------------------------------------------------------------
# WIN32 = GUI-subsystem app on Windows; a plain executable elsewhere (GTK on Linux, Cocoa on macOS).
if(WIN32)
    add_executable(wxnpp WIN32 src/main.cpp resources/app.rc)   # .rc embeds wxNotepad++.ico as the exe's shell icon
else()
    add_executable(wxnpp src/main.cpp)
endif()
set_target_properties(wxnpp PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

target_include_directories(wxnpp PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/scintilla/include"     # Scintilla.h, ILexer.h (SCI_* + SCNotification)
    "${CMAKE_SOURCE_DIR}/third_party/lexilla/include"       # Lexilla.h (CreateLexer), SciLexer.h (SCE_*)
    "${CMAKE_SOURCE_DIR}/include/npp-compat"                # menuCmdID.h only (IDM_* command ids); the rest of the N++ ABI is used solely by packages/npp-bridge
    "${CMAKE_SOURCE_DIR}/include/nib"                       # nib.h - our own permissive, cross-platform plugin API
    "${CMAKE_SOURCE_DIR}/src")                              # app_icon_svg.h, npp_menu.h

target_link_libraries(wxnpp PRIVATE
    wx::core wx::aui wx::base wx::xml wx::stc
    lexilla)

# Optional integrated/borderless title bar (Settings > Preferences): link the vendored wxBorderlessFrame
# where it exists + let the code use it (WXNPP_HAS_BORDERLESS). Off on macOS (keeps the native bar).
if(WIN32 OR (UNIX AND NOT APPLE))
    target_link_libraries(wxnpp PRIVATE wxbf)
    target_compile_definitions(wxnpp PRIVATE WXNPP_HAS_BORDERLESS=1)
endif()

# Windows-only chrome + plugin host (all behind #ifdef __WXMSW__ in src/main.cpp).
if(WIN32)
    target_link_libraries(wxnpp PRIVATE comctl32 imm32 msimg32 ole32 oleaut32 dwmapi uxtheme)
endif()
if(MSVC)
    target_compile_options(wxnpp PRIVATE /utf-8)
endif()

# ---- deploy runtime resources (toolbar icons, themes, default styler) next to the exe --------------
add_custom_command(TARGET wxnpp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources/icons"  "$<TARGET_FILE_DIR:wxnpp>/icons"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources/icons-solar" "$<TARGET_FILE_DIR:wxnpp>/icons-solar"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources/icons-iconpark" "$<TARGET_FILE_DIR:wxnpp>/icons-iconpark"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources/themes" "$<TARGET_FILE_DIR:wxnpp>/themes"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/resources/stylers.model.xml" "$<TARGET_FILE_DIR:wxnpp>/stylers.model.xml"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources/locale" "$<TARGET_FILE_DIR:wxnpp>/locale"
    VERBATIM)

# ---- Windows-only Notepad++-ABI test plugin (exercises the Win32 plugin loader) --------------------
# Lives OUTSIDE the permissive src/: it CONSUMES the N++ ABI (from include/npp-compat) like a real N++
# plugin, so it stays with the N++-compat world in packages/, keeping src/ free of N++-ABI code.
if(WIN32)
    add_subdirectory(packages/test_plugin)
    add_dependencies(wxnpp npp_test_plugin)
endif()

# ---- cross-platform Nib-API plugin (exercises our own permissive plugin API on every OS) -----------
add_subdirectory(src/plugins/nib_test_plugin)
add_dependencies(wxnpp nib_test_plugin)

# ---- optional GPL Notepad++ binary-plugin bridge (Windows-only; itself a Nib plugin) ---------------
if(WIN32)
    add_subdirectory(packages/npp-bridge)
    add_dependencies(wxnpp npp_bridge)
endif()
