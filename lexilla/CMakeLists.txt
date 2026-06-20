# liblexilla.lib  --  static Lexilla lexer component for Notepad++.
#
# Mirrors lexilla/src/Lexilla.vcxproj: src/Lexilla.cxx + lexlib/*.cxx + lexers/*.cxx.

file(GLOB LEXILLA_MAIN   CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/Lexilla.cxx")
file(GLOB LEXILLA_LEXLIB CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/lexlib/*.cxx")
file(GLOB LEXILLA_LEXERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/lexers/*.cxx")

add_library(lexilla STATIC ${LEXILLA_MAIN} ${LEXILLA_LEXLIB} ${LEXILLA_LEXERS})

target_include_directories(lexilla
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/lexlib"
        "${CMAKE_SOURCE_DIR}/scintilla/include")

target_compile_definitions(lexilla PRIVATE
    WIN32
    UNICODE _UNICODE
    _CRT_SECURE_NO_DEPRECATE
    _WINDOWS
    _USRDLL)

set_target_properties(lexilla PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME liblexilla          # -> liblexilla.lib
    PREFIX "")

if(MSVC)
    target_compile_options(lexilla PRIVATE /EHsc /utf-8)
endif()
