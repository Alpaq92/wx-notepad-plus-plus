# Cross-platform recompiled-plugin TEMPLATE + bridge PROBE (one source, one role per OS).
#
# Off-Windows: a link-only proof of the Phase 1 shim contract (wxnote depends on it there so CI
# compiles npp_shim.cpp + example_plugin.cpp). Author-facing pattern: MODULE + link libnpp_shim, no
# extra Win32 libs. Output: bin/nib/example/*.{so,dylib} - deliberately NOT where any loader scans;
# tests/bridge_selftest.cpp stages a copy under its sandboxed <user-data>/plugins/ to run it.
if(NOT WIN32)
    add_library(npp_example_plugin MODULE example_plugin.cpp)

    # The extension must match the bridge loader's kPluginExt (npp_bridge.cpp: ".dylib" on Apple, ".so"
    # elsewhere) - CMake's own MODULE default is ".so" even on macOS, so it has to be set explicitly.
    # It MUST be a plain if(), not a generator expression: SUFFIX does not support genexes, and CMake
    # bakes the unexpanded "$<IF:...>" text into the filename instead of erroring. That is exactly what
    # shipped here - the file was literally named `example_plugin$<IF:$<PLATFORM_ID:Darwin>,.dylib,.so>`,
    # so bridge_selftest's staging copy silently found nothing and the probe never loaded on POSIX.
    if(APPLE)
        set(WXN_PLUGIN_MODULE_SUFFIX ".dylib")
    else()
        set(WXN_PLUGIN_MODULE_SUFFIX ".so")
    endif()

    set_target_properties(npp_example_plugin PROPERTIES
        OUTPUT_NAME "example_plugin"
        PREFIX ""                                   # example_plugin.so (no "lib" prefix, like a real plugin)
        SUFFIX "${WXN_PLUGIN_MODULE_SUFFIX}"
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/nib/example")

    # The whole point: linking libnpp_shim is what resolves the plugin's ::SendMessage/::SendMessageW and
    # the npp_shim_bind seam. npp_shim PUBLIC-exports the npp-compat + shim include dirs, so no extra
    # target_include_directories is needed - proving the SDK is self-describing for an out-of-tree author.
    target_link_libraries(npp_example_plugin PRIVATE npp_shim)
endif()

# On Windows the same source is a REAL loadable N++ plugin DLL (the OS ::SendMessage, no shim), built
# to the exact layout the bridge's loader scans: bin/plugins/<Name>/<Name>.dll. It is the PROBE that
# tests/bridge_selftest.cpp asserts against (notification log + toolbar/allocator registrations).
if(WIN32)
    add_library(npp_probe_plugin SHARED example_plugin.cpp)

    set_target_properties(npp_probe_plugin PROPERTIES
        OUTPUT_NAME "WxnProbe"
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/plugins/WxnProbe")

    target_include_directories(npp_probe_plugin PRIVATE
        "${CMAKE_SOURCE_DIR}/include/npp-compat"             # PluginInterface.h / Notepad_plus_msgs.h (the N++ ABI)
        "${CMAKE_SOURCE_DIR}/third_party/scintilla/include") # Scintilla.h (SCI_* + SCNotification)

    if(MSVC)
        target_compile_options(npp_probe_plugin PRIVATE /utf-8 /EHsc)
    endif()
endif()
