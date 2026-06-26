# Minimal Notepad++ plugin DLL to verify the wx shell's plugin loader (Windows only).
# Output: build-spike/bin/plugins/TestPlugin/TestPlugin.dll  -- the layout the loader scans.
add_library(npp_test_plugin SHARED TestPlugin.cpp)

set_target_properties(npp_test_plugin PROPERTIES
    OUTPUT_NAME "TestPlugin"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/plugins/TestPlugin")

target_include_directories(npp_test_plugin PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/scintilla/include"      # Scintilla.h (SCI_* ids)
    "${CMAKE_SOURCE_DIR}/include/npp-compat")                # our Apache-2.0 clean-room plugin ABI (PluginInterface.h, Notepad_plus_msgs.h, Docking.h)

if(MSVC)
    target_compile_options(npp_test_plugin PRIVATE /utf-8 /EHsc)
endif()
