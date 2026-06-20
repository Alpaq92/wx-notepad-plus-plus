# CMake build for Notepad++ (wx-notepad-plus-plus)
#
# This is a CMake port of the original MSBuild (Visual Studio) build.
# It produces the same three components the .sln does:
#   * scintilla  -> libscintilla.lib  (static, with Boost.Regex PCRE support)
#   * lexilla    -> liblexilla.lib    (static)
#   * notepad++  -> notepad++.exe     (Win32 GUI application)
#
# Settings (defines, include dirs, libs, runtime, language standard) mirror
# PowerEditor/visual.net/notepadPlus.{vcxproj,Cpp.props}, scintilla/win32/
# Scintilla.vcxproj and lexilla/src/Lexilla.vcxproj.
#
# Tested generator: Ninja + MSVC (x64). Configure from a VS developer shell:
#   cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -S . -B build
#   cmake --build build

cmake_minimum_required(VERSION 3.20)

# Select the MSVC runtime library statically (/MT, /MTd) for every target,
# matching <RuntimeLibrary>MultiThreaded[Debug]</RuntimeLibrary> in the projects.
# Requires CMP0091=NEW, which is the default at this cmake_minimum_required.
cmake_policy(SET CMP0091 NEW)

project(notepad-plus-plus
        VERSION 1.0
        DESCRIPTION "Notepad++ (CMake port)"
        LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "Notepad++ is a Win32 application and only builds on Windows.")
endif()
if(NOT MSVC)
    message(WARNING "This CMake port targets MSVC; other compilers are untested.")
endif()

# Default to a Release build for single-config generators (Ninja, NMake).
get_property(_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _is_multi_config AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# CMake injects /EHsc into CMAKE_CXX_FLAGS by default. The Notepad++ application
# is compiled with /EHa (ExceptionHandling=Async) for its SEH crash handler, so
# strip the default here and let each target pick its own model below.
string(REGEX REPLACE "[/-]EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

add_subdirectory(scintilla)
add_subdirectory(lexilla)
add_subdirectory(PowerEditor)
