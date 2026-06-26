# Minimal Nib-API plugin: a cross-platform reference + smoke test for the Nib plugin API.
# Built as a shared module and emitted to <build>/bin/nib/, where the app's loadNibPlugins() finds it.
add_library(nib_test_plugin MODULE nib_test_plugin.cpp)

set_target_properties(nib_test_plugin PROPERTIES
    PREFIX ""                                   # no "lib" prefix -> nib_test_plugin.{dll,so,dylib}
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/nib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/nib")

target_include_directories(nib_test_plugin PRIVATE "${CMAKE_SOURCE_DIR}/include/nib")

if(MSVC)
    target_compile_options(nib_test_plugin PRIVATE /EHsc /utf-8)
endif()
