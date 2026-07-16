# Cross-platform recompiled-plugin TEMPLATE - a link-only proof of the Phase 1 shim contract.
# Built only off-Windows (gated `if(NOT WIN32) add_subdirectory(example)` in ../CMakeLists.txt; wxnote
# depends on it there so CI compiles it). Author-facing pattern: MODULE + link libnpp_shim, no extra
# Win32 libs. Output: bin/nib/example/*.{so,dylib}.
add_library(npp_example_plugin MODULE example_plugin.cpp)

set_target_properties(npp_example_plugin PROPERTIES
    OUTPUT_NAME "example_plugin"
    PREFIX ""                                   # example_plugin.so (no "lib" prefix, like a real plugin)
    SUFFIX "$<IF:$<PLATFORM_ID:Darwin>,.dylib,.so>"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/nib/example")

# The whole point: linking libnpp_shim is what resolves the plugin's ::SendMessage/::SendMessageW and
# the npp_shim_bind seam. npp_shim PUBLIC-exports the npp-compat + shim include dirs, so no extra
# target_include_directories is needed - proving the SDK is self-describing for an out-of-tree author.
target_link_libraries(npp_example_plugin PRIVATE npp_shim)
