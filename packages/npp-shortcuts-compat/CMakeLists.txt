# npp-shortcuts-compat - the optional, GPL Notepad++ shortcuts.xml -> wxNote keymap compatibility layer.
#
# Built two ways:
#  * as part of the wxNote build (add_subdirectory from the top-level CMakeLists): produces the
#    runtime Nib plugin npp_shortcuts_compat (-> <build>/bin/nib/), which imports a Notepad++
#    shortcuts.xml and feeds its bindings into the core as a "Notepad++ (imported)" scheme via the
#    nib.keymap/1 capability.
#  * standalone: produces the converter CLI + the unit tests:
#       cmake -S packages/npp-shortcuts-compat -B build-nppsc && cmake --build build-nppsc
#       ctest --test-dir build-nppsc --output-on-failure
cmake_minimum_required(VERSION 3.20)
project(npp_shortcuts_compat CXX)

# The parser + accelerator-builder library - used by both the standalone tools and the runtime plugin.
# Wx-free, host-free: plain std::string in, a data model + portable accelerator strings out.
add_library(npp_shortcuts_compat_core STATIC npp_shortcuts_parse.cpp npp_shortcuts_accel.cpp)
target_include_directories(npp_shortcuts_compat_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# In-tree this static lib is linked into the npp_shortcuts_compat shared MODULE (bin/nib/...); on ELF
# that requires position-independent code, so force -fPIC (harmless on MSVC / for the standalone tools).
set_target_properties(npp_shortcuts_compat_core PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(npp_shortcuts_compat_core PRIVATE /EHsc /utf-8 /W4)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # ===== standalone: the converter CLI + the tests =====
    enable_testing()

    add_executable(npp2accel npp2accel.cpp)
    target_link_libraries(npp2accel PRIVATE npp_shortcuts_compat_core)
    set_target_properties(npp2accel PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

    add_executable(npp_shortcuts_selftest selftest.cpp)
    target_link_libraries(npp_shortcuts_selftest PRIVATE npp_shortcuts_compat_core)
    set_target_properties(npp_shortcuts_selftest PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME npp_shortcuts_selftest COMMAND npp_shortcuts_selftest)
else()
    # ===== built as part of wxNote: the runtime Nib plugin (loaded from <build>/bin/nib/) =====
    add_library(npp_shortcuts_compat_plugin MODULE npp_shortcuts_compat_plugin.cpp)
    target_link_libraries(npp_shortcuts_compat_plugin PRIVATE npp_shortcuts_compat_core)
    target_include_directories(npp_shortcuts_compat_plugin PRIVATE "${CMAKE_SOURCE_DIR}/include/nib")
    set_target_properties(npp_shortcuts_compat_plugin PROPERTIES
        PREFIX ""                       # -> npp_shortcuts_compat.dll / .so / .dylib
        OUTPUT_NAME "npp_shortcuts_compat"
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/nib"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/nib")
    if(MSVC)
        target_compile_options(npp_shortcuts_compat_plugin PRIVATE /EHsc /utf-8)
    endif()
endif()
