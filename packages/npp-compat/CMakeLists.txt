# npp-compat - the optional, GPL Notepad++ compatibility layer: reads Notepad++'s own files
# (shortcuts.xml, config.xml, themes, contextMenu.xml, langs.xml, sessions, workspaces) and translates them into
# wxNote's. See README.md, and docs/SETTINGS_DESIGN.md ("Notepad++") for what each file becomes.
#
# Built as part of wxNote (add_subdirectory from the top-level CMakeLists): its translations write
# wxNote's YAML through the core's own headers (src/yaml_io.h, src/theme_file.h, src/settings_schema.h,
# all Apache-2.0) and rapidyaml, so what it writes is exactly what wxNote reads.
#  * npp_compat_plugin    the runtime Nib plugin (-> <build>/bin/nib/npp_compat.*)
#  * npp_compat_selftest  the parser and translation tests (a "pure" ctest)
#  * npp2wxnote           command-line converter for every file type (on request:
#                         cmake --build build --target npp2wxnote)
#  * npp2accel            shortcuts.xml -> accelerator strings, for inspecting a file (on request)

# The parsers + translators - shared by the plugin, the tests and the tools. Wx-free, host-free.
add_library(npp_compat_core STATIC
    npp_shortcuts_parse.cpp npp_shortcuts_accel.cpp npp_xml.cpp npp_session.cpp npp_translate.cpp)
target_include_directories(npp_compat_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src"                    # yaml_io.h, theme_file.h, settings_schema.h, lang_detect.h
    "${CMAKE_SOURCE_DIR}/include/npp-compat")    # Notepad_plus_msgs.h - the LangType numbers config.xml stores
target_link_libraries(npp_compat_core PUBLIC ryml)
# Linked into the npp_compat shared MODULE below; on ELF that requires position-independent code.
set_target_properties(npp_compat_core PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(npp_compat_core PRIVATE /EHsc /utf-8 /W4)
endif()

add_library(npp_compat_plugin MODULE npp_compat_plugin.cpp)
target_link_libraries(npp_compat_plugin PRIVATE npp_compat_core)
target_include_directories(npp_compat_plugin PRIVATE "${CMAKE_SOURCE_DIR}/include/nib")
set_target_properties(npp_compat_plugin PROPERTIES
    PREFIX ""                       # -> npp_compat.dll / .so / .dylib
    OUTPUT_NAME "npp_compat"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/nib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/nib")
if(MSVC)
    target_compile_options(npp_compat_plugin PRIVATE /EHsc /utf-8)
endif()

add_executable(npp_compat_selftest selftest.cpp)
target_link_libraries(npp_compat_selftest PRIVATE npp_compat_core)
set_target_properties(npp_compat_selftest PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(MSVC)
    target_compile_options(npp_compat_selftest PRIVATE /EHsc /utf-8)
endif()

foreach(tool npp2wxnote npp2accel)
    add_executable(${tool} EXCLUDE_FROM_ALL ${tool}.cpp)
    target_link_libraries(${tool} PRIVATE npp_compat_core)
    set_target_properties(${tool} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(MSVC)
        target_compile_options(${tool} PRIVATE /EHsc /utf-8)
    endif()
endforeach()
