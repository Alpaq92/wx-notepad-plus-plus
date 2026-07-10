// macos_native.mm - tiny Objective-C++ shim for a macOS-native window tweak that wxWidgets doesn't
// expose. Compiled only on Apple (see CMakeLists.txt: if(APPLE) target_sources ...). Kept free of
// wxWidgets types on purpose: src/main.cpp passes the frame's raw NSWindow* (obtained from
// wxTopLevelWindowMac::MacGetTopLevelWindowRef()) through as a void*, so this file only needs AppKit.
#import <AppKit/AppKit.h>

// Blank the native title bar. wx's SetTitle("") technically clears the text, but the clean look
// shouldn't hinge on that one call surviving every wx/AppKit title reassertion; hiding the title
// outright is unconditional. (User preference on macOS: no "<doc> - wxNotepad++" - the document name
// already shows in the tab, and Cocoa apps commonly leave the title empty.)
extern "C" void wxnpp_HideWindowTitle(void* nsWindow)
{
    if (!nsWindow) return;
    NSWindow* w = (NSWindow*)nsWindow;
    w.titleVisibility = NSWindowTitleHidden;   // NSWindowTitleVisibility enum: Visible=0, Hidden=1
}
