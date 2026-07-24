# libhunspell (Hunspell v1.7.3 subset) - the spell-check engine, built static and self-contained.
#
# wxNote uses Hunspell under the MPL-1.1 arm of its MPL-1.1 / GPL-2.0+ / LGPL-2.1+ tri-license (per-file
# weak copyleft, combines cleanly with the Apache-2.0 core - see LICENSING.md / NOTICE). We compile ONLY
# src/hunspell/ (libhunspell); src/tools/ (the CLI, which link-time depends on GPL readline) and
# src/parsers/ are deliberately NOT built, so nothing GPL is pulled in.
#
# The sources are fetched by FetchContent in the top-level CMakeLists (upstream ships no CMake), so this
# overlay just wraps the fixed .cxx set from ${hunspell_SOURCE_DIR}. The subset needs no config.h, no
# autotools step, no POSIX-only calls and no zlib (hunzip.cxx carries its own Huffman decoder), so it
# builds clean on MSVC 2022 and GCC/Clang across x64 / arm64 / riscv64.
set(HUNSPELL_SRC_DIR "${hunspell_SOURCE_DIR}/src/hunspell")

add_library(hunspell STATIC
    ${HUNSPELL_SRC_DIR}/affentry.cxx
    ${HUNSPELL_SRC_DIR}/affixmgr.cxx
    ${HUNSPELL_SRC_DIR}/csutil.cxx
    ${HUNSPELL_SRC_DIR}/filemgr.cxx
    ${HUNSPELL_SRC_DIR}/hashmgr.cxx
    ${HUNSPELL_SRC_DIR}/hunspell.cxx
    ${HUNSPELL_SRC_DIR}/hunzip.cxx
    ${HUNSPELL_SRC_DIR}/phonet.cxx
    ${HUNSPELL_SRC_DIR}/replist.cxx
    ${HUNSPELL_SRC_DIR}/suggestmgr.cxx
)

# PUBLIC so our TUs that #include <hunspell/hunspell.hxx> also collapse LIBHUNSPELL_DLL_EXPORTED to empty
# (no MSVC dllimport/dllexport mismatch against a static lib).
target_compile_definitions(hunspell PUBLIC HUNSPELL_STATIC)

# Expose the headers as <hunspell/hunspell.hxx> - matches the include in src/spell_engine.h + the self-test.
target_include_directories(hunspell PUBLIC "${hunspell_SOURCE_DIR}/src")

set_target_properties(hunspell PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)   # ends up inside the PIE wxnote exe on ELF (same as lua_lpeg / vterm)

# Third-party code we do not patch: source is UTF-8 (/utf-8 on MSVC); mute its warnings on THIS target
# only (PRIVATE) so our own targets' warning signal stays intact.
if(MSVC)
    target_compile_options(hunspell PRIVATE /utf-8 /EHsc /w)
else()
    target_compile_options(hunspell PRIVATE -w)
endif()
