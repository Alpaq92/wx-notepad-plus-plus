# npp-bridge - the optional, GPL, Windows-only Notepad++ binary-plugin compatibility bridge.
# It is itself a Nib plugin (built to <build>/bin/nib/), so the core loads it like any other plugin.
add_library(npp_bridge MODULE npp_bridge.cpp)

set_target_properties(npp_bridge PROPERTIES
    PREFIX ""                                   # -> npp_bridge.dll (no "lib" prefix)
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/nib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/nib")

target_include_directories(npp_bridge PRIVATE
    "${CMAKE_SOURCE_DIR}/include/nib"                    # nib.h (the permissive host API)
    "${CMAKE_SOURCE_DIR}/include/npp-compat"             # PluginInterface.h / Notepad_plus_msgs.h / Docking.h (the N++ ABI)
    "${CMAKE_SOURCE_DIR}/third_party/scintilla/include") # Scintilla.h (SCNotification, pulled in by PluginInterface.h)

if(MSVC)
    target_compile_options(npp_bridge PRIVATE /EHsc /utf-8)
endif()
