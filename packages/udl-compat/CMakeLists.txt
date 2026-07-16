# udl-compat - the optional, GPL Notepad++ UDL -> Scintillua compatibility layer.
#
# Built two ways:
#  * as part of the wxNote build (add_subdirectory from the top-level CMakeLists): produces the
#    runtime Nib plugin udl_compat (-> <build>/bin/nib/), which translates the user's
#    userDefineLang.xml files into Scintillua lexers and registers them via nib.langdef.
#  * standalone: produces the converter CLI + the unit tests (and, with
#    -DUDL_COMPAT_SCINTILLUA_TEST=ON, the end-to-end test against a real Lua+LPeg+Scintillua):
#       cmake -S packages/udl-compat -B build-udltest && cmake --build build-udltest
#       ctest --test-dir build-udltest --output-on-failure
cmake_minimum_required(VERSION 3.20)
project(udl_compat CXX)

# The parser + translator library - used by both the standalone tools and the runtime plugin.
add_library(udl_compat_core STATIC udl_scintillua.cpp udl_parse.cpp)
target_include_directories(udl_compat_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(udl_compat_core PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
if(MSVC)
    target_compile_options(udl_compat_core PRIVATE /EHsc /utf-8 /W4)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # ===== standalone: the converter CLI + the tests =====
    enable_testing()

    add_executable(udl2scintillua udl2scintillua.cpp)
    target_link_libraries(udl2scintillua PRIVATE udl_compat_core)
    set_target_properties(udl2scintillua PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

    add_executable(udl_selftest selftest.cpp)
    target_link_libraries(udl_selftest PRIVATE udl_compat_core)
    set_target_properties(udl_selftest PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME udl_selftest COMMAND udl_selftest)

    # ---- optional end-to-end test against a real Lua + LPeg + Scintillua runtime ----
    # OFF by default (needs network to fetch the runtime); turn on with
    #   -DUDL_COMPAT_SCINTILLUA_TEST=ON
    option(UDL_COMPAT_SCINTILLUA_TEST "Fetch Lua/LPeg/Scintillua and run the end-to-end lexing test" OFF)
    if(UDL_COMPAT_SCINTILLUA_TEST)
        enable_language(C)   # Lua + LPeg are C
        set(_dl "${CMAKE_CURRENT_BINARY_DIR}/scintillua-deps")
        file(MAKE_DIRECTORY "${_dl}")
        if(NOT EXISTS "${_dl}/lua-5.4.7/src/lua.h")
            file(DOWNLOAD "https://www.lua.org/ftp/lua-5.4.7.tar.gz" "${_dl}/lua.tar.gz" STATUS _st)
            list(GET _st 0 _stc)
            if(NOT _stc EQUAL 0)
                message(FATAL_ERROR "failed to download Lua: ${_st}")
            endif()
            file(ARCHIVE_EXTRACT INPUT "${_dl}/lua.tar.gz" DESTINATION "${_dl}")
        endif()
        if(NOT EXISTS "${_dl}/lpeg-1.1.0/lptree.c")
            file(DOWNLOAD "http://www.inf.puc-rio.br/~roberto/lpeg/lpeg-1.1.0.tar.gz" "${_dl}/lpeg.tar.gz" STATUS _st2)
            file(ARCHIVE_EXTRACT INPUT "${_dl}/lpeg.tar.gz" DESTINATION "${_dl}")
        endif()
        if(NOT EXISTS "${_dl}/lexer.lua")
            file(DOWNLOAD "https://raw.githubusercontent.com/orbitalquark/scintillua/default/lexers/lexer.lua"
                 "${_dl}/lexer.lua" STATUS _st3)
        endif()
        # Explicit source lists (file(GLOB)'s configure-time cache misses files a same-configure
        # extract just created); fixed sets of pinned Lua 5.4.7 (minus lua.c/luac.c) and LPeg 1.1.0.
        set(_lua_units lapi lauxlib lbaselib lcode lcorolib lctype ldblib ldebug ldo ldump
            lfunc lgc linit liolib llex lmathlib lmem loadlib lobject lopcodes loslib lparser
            lstate lstring lstrlib ltable ltablib ltm lundump lutf8lib lvm lzio)
        set(_lua_src "")
        foreach(_u ${_lua_units})
            list(APPEND _lua_src "${_dl}/lua-5.4.7/src/${_u}.c")
        endforeach()
        set(_lpeg_src "")
        foreach(_u lpcap lpcode lpcset lpprint lptree lpvm)
            list(APPEND _lpeg_src "${_dl}/lpeg-1.1.0/${_u}.c")
        endforeach()
        add_library(udl_lua_runtime STATIC ${_lua_src} ${_lpeg_src})
        target_include_directories(udl_lua_runtime PUBLIC "${_dl}/lua-5.4.7/src")
        if(MSVC)
            target_compile_definitions(udl_lua_runtime PRIVATE _CRT_SECURE_NO_WARNINGS)
        endif()

        add_executable(udl_scintillua_roundtrip roundtrip_test.cpp)
        target_link_libraries(udl_scintillua_roundtrip PRIVATE udl_compat_core udl_lua_runtime)
        set_target_properties(udl_scintillua_roundtrip PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
        add_test(NAME udl_scintillua_roundtrip COMMAND udl_scintillua_roundtrip "${_dl}")
    endif()
else()
    # ===== built as part of wxNote: the runtime Nib plugin (loaded from <build>/bin/nib/) =====
    add_library(udl_compat_plugin MODULE udl_compat_plugin.cpp)
    target_link_libraries(udl_compat_plugin PRIVATE udl_compat_core)
    target_include_directories(udl_compat_plugin PRIVATE "${CMAKE_SOURCE_DIR}/include/nib")
    set_target_properties(udl_compat_plugin PROPERTIES
        PREFIX ""                       # -> udl_compat.dll / .so / .dylib
        OUTPUT_NAME "udl_compat"
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/nib"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/nib")
    if(MSVC)
        target_compile_options(udl_compat_plugin PRIVATE /EHsc /utf-8)
    endif()
endif()
