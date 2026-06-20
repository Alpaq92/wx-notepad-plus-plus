# Experimental wxWidgets Phase-0 spike (see docs/WXWIDGETS_MIGRATION_PLAN.md).
#
# Fetches wxWidgets and links it against our OWN native scintilla static lib to
# prove: native-Scintilla-in-wxFrame, the HWND/message ABI shim, and a wx leaf
# dialog. This subdirectory is only added when -DNPP_BUILD_WX_SPIKE=ON, so the
# normal Notepad++ build never fetches or builds wxWidgets.

include(FetchContent)

# The top-level CMakeLists strips /EHsc from CMAKE_CXX_FLAGS so the Notepad++ app
# can use /EHa. wxWidgets needs /EHsc, though: without it MSVC leaves _CPPUNWIND
# undefined and wxWidgets forces wxUSE_EXCEPTIONS=0, which omits wxApp's exception
# methods (wxApp::OnExceptionInMainLoop, wxAppConsoleBase::StoreCurrentException, ...)
# and breaks linking. Restore /EHsc for this subtree (wxWidgets + the spike only;
# this is a child scope, so the app's /EHa is unaffected).
if(MSVC)
    string(APPEND CMAKE_CXX_FLAGS " /EHsc")
endif()

# Build wxWidgets statically with the static CRT (/MT), matching scintilla + the app.
set(wxBUILD_SHARED            OFF CACHE BOOL "" FORCE)
set(wxBUILD_USE_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
set(wxBUILD_SAMPLES          OFF CACHE BOOL "" FORCE)
set(wxBUILD_TESTS            OFF CACHE BOOL "" FORCE)
set(wxBUILD_DEMOS            OFF CACHE BOOL "" FORCE)

message(STATUS "[spike] Fetching wxWidgets 3.2.6 (one-time; builds from source)...")
FetchContent_Declare(wxwidgets
    URL https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.6/wxWidgets-3.2.6.tar.bz2
)
FetchContent_MakeAvailable(wxwidgets)

add_executable(npp_wx_spike WIN32 main.cpp)

set_target_properties(npp_wx_spike PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

target_include_directories(npp_wx_spike PRIVATE "${CMAKE_SOURCE_DIR}/scintilla/include")

# wx::core/wx::base = wxWidgets; scintilla = our native editor lib; the rest are
# the Win32 libs the native Scintilla needs (same set the app links).
target_link_libraries(npp_wx_spike PRIVATE
    wx::core wx::base
    scintilla
    comctl32 imm32 msimg32 ole32 oleaut32)

if(MSVC)
    # /EHsc is already in CMAKE_CXX_FLAGS for this subtree (restored above).
    target_compile_options(npp_wx_spike PRIVATE /utf-8)
endif()
