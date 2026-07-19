# npp-bridge - the optional, GPL Notepad++ binary-plugin compatibility bridge.
# It is itself a Nib plugin (built to <build>/bin/nib/), so the core loads it like any other plugin.
#
# Phase 1 (docs/ARCHITECTURE.md): the bridge builds on EVERY OS. Its central mechanism is
# still Win32-only (SetWindowSubclass on the frame HWND) and is #ifdef _WIN32'd inside npp_bridge.cpp;
# off-Windows the bridge compiles down to its portable dispatch spine (bridge_dispatch/bridge_handleNppm)
# plus the nib.sci/1 seam, which is the router the recompiled-plugin path drives via libnpp_shim. Only
# Windows can host a real N++ plugin *binary*; building elsewhere is what compile-verifies the ABI and
# the dispatch core on CI.
add_library(npp_bridge MODULE npp_bridge.cpp)

set_target_properties(npp_bridge PROPERTIES
    PREFIX ""                                   # -> npp_bridge.dll / npp_bridge.so / npp_bridge.dylib
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/nib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/nib")

target_include_directories(npp_bridge PRIVATE
    "${CMAKE_SOURCE_DIR}/include/nib"                    # nib.h (the permissive host API + nib.sci/1)
    "${CMAKE_SOURCE_DIR}/include/npp-compat"             # PluginInterface.h / Notepad_plus_msgs.h / Docking.h (the N++ ABI)
    "${CMAKE_SOURCE_DIR}/third_party/scintilla/include"  # Scintilla.h (SCNotification, pulled in by PluginInterface.h)
    "${CMAKE_CURRENT_SOURCE_DIR}/shim")                  # npp_shim.h (NppHostBridge - the host->plugin dispatch seam)

# comctl32 is the Win32 subclass transport (SetWindowSubclass / DefSubclassProc) -> Windows only.
# Off-Windows the bridge dlopen()s plugins via wxDynamicLibrary/dlopen, which needs libdl on some libc's.
if(WIN32)
    target_link_libraries(npp_bridge PRIVATE comctl32)
else()
    target_link_libraries(npp_bridge PRIVATE ${CMAKE_DL_LIBS})
endif()

if(MSVC)
    target_compile_options(npp_bridge PRIVATE /EHsc /utf-8)
endif()

# ---- libnpp_shim : the GPL SDK artifact a PLUGIN AUTHOR links (NOT linked into wxnote or the bridge) --
# A recompiled N++ plugin links this static lib so its own ::SendMessage()/SendMessageW() calls resolve
# to the shim's forwarders (there is no Win32 to provide them off-Windows), which route into the bridge's
# bridge_dispatch via the NppHostBridge table handed over by npp_shim_bind(). On Windows the plugin uses
# the OS ::SendMessage, so npp_shim.cpp's body is #ifndef _WIN32'd out and this target links to nothing.
add_library(npp_shim STATIC shim/npp_shim.cpp)
set_target_properties(npp_shim PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)               # linked into each plugin's .so/.dylib (a shared object)
target_include_directories(npp_shim PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/shim"                   # npp_shim.h (NppHostBridge, npp_shim_bind)
    "${CMAKE_SOURCE_DIR}/include/npp-compat"             # PluginInterface.h + npp_plugin_port.h (HWND/LRESULT/... + NPP_EXPORT)
    "${CMAKE_SOURCE_DIR}/third_party/scintilla/include") # Scintilla.h (SCI_* ids + SCNotification, #included by PluginInterface.h)
if(MSVC)
    target_compile_options(npp_shim PRIVATE /EHsc /utf-8)
endif()

# ---- the example/PROBE plugin -----------------------------------------------------------------------
# One source (example/example_plugin.cpp), two per-OS roles - see example/CMakeLists.txt:
#   * off-Windows: the CI compile+link proof of the six-symbol contract + libnpp_shim SendMessage seam
#     (npp_example_plugin), unchanged from Phase 1;
#   * on Windows: a real loadable probe DLL (npp_probe_plugin -> bin/plugins/WxnProbe/WxnProbe.dll)
#     that logs every bridge notification + its toolbar/allocator registrations for
#     tests/bridge_selftest.cpp.
add_subdirectory(example)
