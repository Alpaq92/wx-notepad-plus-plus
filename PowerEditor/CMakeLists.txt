# notepad++.exe  --  the Notepad++ Win32 application.
#
# Mirrors PowerEditor/visual.net/notepadPlus.vcxproj + notepadPlus.Cpp.props.
# The MSBuild project lists ~97 source files explicitly and pulls uchardet in
# through notepadPlus.uchardet.targets; together that is exactly every .cpp under
# src/ (verified), so we glob. Resources are the 26 .rc files under src/, and the
# application manifest is embedded via the ready-made gcc/manifest.rc.

# ---------------------------------------------------------------------------
# Generate NppLibsVersion.h (About-box version strings) the way the project's
# NppLibsVersionH-generator.bat does, but at configure time and into the build
# tree so the source checkout stays clean. AboutDlg.cpp picks it up through
# __has_include, so it is optional but nice to have.
# ---------------------------------------------------------------------------
function(npp_extract_quoted_define out_var file key)
    set(_value "N/A")
    if(EXISTS "${file}")
        file(STRINGS "${file}" _lines REGEX "#define[ \t]+${key}[ \t]")
        foreach(_line IN LISTS _lines)
            if(_line MATCHES "\"([^\"]+)\"")
                set(_value "${CMAKE_MATCH_1}")
            endif()
        endforeach()
    endif()
    set(${out_var} "${_value}" PARENT_SCOPE)
endfunction()

npp_extract_quoted_define(NPP_SCINTILLA_VERSION
    "${CMAKE_SOURCE_DIR}/scintilla/win32/ScintRes.rc"      "VERSION_SCINTILLA")
npp_extract_quoted_define(NPP_LEXILLA_VERSION
    "${CMAKE_SOURCE_DIR}/lexilla/src/LexillaVersion.rc"    "VERSION_LEXILLA")
npp_extract_quoted_define(NPP_BOOST_REGEX_VERSION
    "${CMAKE_SOURCE_DIR}/boostregex/boost/version.hpp"     "BOOST_LIB_VERSION")
message(STATUS "Notepad++ libs: Scintilla ${NPP_SCINTILLA_VERSION}, "
               "Lexilla ${NPP_LEXILLA_VERSION}, Boost.Regex ${NPP_BOOST_REGEX_VERSION}")

set(NPP_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file("${CMAKE_SOURCE_DIR}/cmake/NppLibsVersion.h.in"
               "${NPP_GENERATED_DIR}/NppLibsVersion.h" @ONLY)

# ---------------------------------------------------------------------------
# Sources, resources, include directories
# ---------------------------------------------------------------------------
file(GLOB_RECURSE NPP_SOURCES   CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE NPP_RESOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.rc")

# Every directory that contains a header becomes an include dir (the GCC makefile
# does the same), so the many "#include \"Foo.h\"" forms resolve without listing
# ~50 paths by hand.
file(GLOB_RECURSE _npp_headers CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp")
set(NPP_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src")
foreach(_h IN LISTS _npp_headers)
    get_filename_component(_d "${_h}" DIRECTORY)
    list(APPEND NPP_INCLUDE_DIRS "${_d}")
endforeach()
list(REMOVE_DUPLICATES NPP_INCLUDE_DIRS)

add_executable(notepadpp WIN32
    ${NPP_SOURCES}
    ${NPP_RESOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/gcc/manifest.rc")   # embeds notepad++.exe.manifest

set_target_properties(notepadpp PROPERTIES
    OUTPUT_NAME "notepad++"
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

target_include_directories(notepadpp PRIVATE
    ${NPP_INCLUDE_DIRS}
    "${NPP_GENERATED_DIR}"
    "${CMAKE_SOURCE_DIR}/scintilla/include"
    "${CMAKE_SOURCE_DIR}/lexilla/include")

# Preprocessor definitions from notepadPlus.Cpp.props (+ UNICODE from CharacterSet).
target_compile_definitions(notepadpp PRIVATE
    WIN32 _WINDOWS
    UNICODE _UNICODE
    _WIN32_WINNT=_WIN32_WINNT_WIN7
    NTDDI_VERSION=NTDDI_WIN7
    OEMRESOURCE
    NOMINMAX
    _USE_64BIT_TIME_T
    _CRT_NONSTDC_NO_DEPRECATE
    _CRT_SECURE_NO_WARNINGS
    _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES
    _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES_COUNT)

if(MSVC)
    # /EHa  : ExceptionHandling=Async (the SEH crash handler needs it)
    # /utf-8: source/exec charset    /Zc:__cplusplus: report a correct __cplusplus
    # Warnings are intentionally NOT errors here (the .vcxproj sets /WX, but a
    # newer toolset emits new warnings that would otherwise break the build).
    target_compile_options(notepadpp PRIVATE /EHa /utf-8 /Zc:__cplusplus /W3)

    target_link_options(notepadpp PRIVATE
        /ENTRY:wWinMainCRTStartup     # the entry point is wWinMain (Unicode)
        /MANIFEST:NO                  # we embed our own manifest via gcc/manifest.rc
        /DEPENDENTLOADFLAG:0x800      # load system DLLs from System32 only
        /LARGEADDRESSAWARE)
endif()

# Static Scintilla/Lexilla libs + the Win32 import libraries the app needs
# (AdditionalDependencies in notepadPlus.Cpp.props). The standard set
# (kernel32/user32/gdi32/shell32/ole32/...) is already linked by CMake.
target_link_libraries(notepadpp PRIVATE
    scintilla
    lexilla
    comctl32 shlwapi shell32 dbghelp version crypt32 wintrust
    sensapi wininet imm32 msimg32 uxtheme dwmapi)

# Drop the two model XMLs next to the exe (Release PostBuildEvent in the project)
# so Notepad++ can initialise languages/styles on first launch.
add_custom_command(TARGET notepadpp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/src/langs.model.xml"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/stylers.model.xml"
        "$<TARGET_FILE_DIR:notepadpp>"
    VERBATIM)
