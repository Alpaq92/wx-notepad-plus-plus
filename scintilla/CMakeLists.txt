# libscintilla.lib  --  static Scintilla editing component for Notepad++.
#
# Mirrors scintilla/win32/Scintilla.vcxproj: all of src/*.cxx and win32/*.cxx
# plus the Boost.Regex PCRE integration (boostregex/*.cxx) that replaces
# Scintilla's built-in std::regex engine (SCI_OWNREGEX + BOOST_REGEX_STANDALONE).

file(GLOB SCINTILLA_SRC  CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx")
file(GLOB SCINTILLA_WIN  CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/win32/*.cxx")
file(GLOB BOOSTREGEX_SRC CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/boostregex/*.cxx")

add_library(scintilla STATIC ${SCINTILLA_SRC} ${SCINTILLA_WIN} ${BOOSTREGEX_SRC})

# So consumers (lexilla, the app) and our own win32 sources resolve headers.
target_include_directories(scintilla
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/win32"
        "${CMAKE_SOURCE_DIR}/boostregex")

target_compile_definitions(scintilla PRIVATE
    UNICODE _UNICODE
    _CRT_SECURE_NO_DEPRECATE
    _USRDLL
    SCI_NAMESPACE
    SCI_OWNREGEX
    BOOST_REGEX_STANDALONE
    _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS)

set_target_properties(scintilla PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME libscintilla        # -> libscintilla.lib (matches the .vcxproj output)
    PREFIX "")

if(MSVC)
    target_compile_options(scintilla PRIVATE /EHsc /utf-8)
endif()
