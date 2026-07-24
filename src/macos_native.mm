// macos_native.mm - tiny Objective-C++ shim for a macOS-native window tweak that wxWidgets doesn't
// expose. Compiled only on Apple (see CMakeLists.txt: if(APPLE) target_sources ...). Kept free of
// wxWidgets types on purpose: src/main.cpp passes the frame's raw NSWindow* (obtained from
// wxTopLevelWindowMac::MacGetTopLevelWindowRef()) through as a void*, so this file only needs AppKit.
#import <AppKit/AppKit.h>

// Blank the native title bar. wx's SetTitle("") technically clears the text, but the clean look
// shouldn't hinge on that one call surviving every wx/AppKit title reassertion; hiding the title
// outright is unconditional. (User preference on macOS: no "<doc> - wxNote" - the document name
// already shows in the tab, and Cocoa apps commonly leave the title empty.)
extern "C" void wxn_HideWindowTitle(void* nsWindow)
{
    if (!nsWindow) return;
    NSWindow* w = (NSWindow*)nsWindow;
    w.titleVisibility = NSWindowTitleHidden;   // NSWindowTitleVisibility enum: Visible=0, Hidden=1
}

// Integrated top bar: reclaim the title-bar band and centre the stock traffic lights in the toolbar
// row. This is the technique Electron ships today (WindowButtonsProxy, post-PR#27489 - it explicitly
// ABANDONED the older dummy-NSToolbar "hiddenInset" trick over mouse-event bugs): keep the window
// TITLED (so the lights, resize border and rounded corners survive), add FullSizeContentView so the
// content extends under the (transparent) title bar, then resize the NSTitlebarContainerView to the
// toolbar-row height and move the three standardWindowButton views to vertically centre in it.
// wx 3.3.1 tolerates all of this: its client geometry is always read live from [[window contentView]
// frame] (never contentLayoutRect, never cached titlebar math), and the only styleMask writes in
// src/osx (legacy ShowFullScreen) save/restore the FULL mask, preserving our bit both ways.
//
// Idempotent and cheap - MUST be re-called after resize / key-status change / deminiaturize /
// fullscreen-exit, because AppKit rebuilds the titlebar layout and snaps the buttons back (Electron
// re-runs its redraw from exactly those delegate hooks; we mirror that with wx event Binds, since wx
// installs ONE shared NSWindowDelegate for all windows that must not be replaced or subclassed).
// Returns the x where toolbar content may start (right edge of the zoom button + a gap), so the
// caller can inset the first toolbar icon; 0 if the window/buttons are unavailable.
extern "C" int wxn_InlineTrafficLights(void* nsWindow, int rowHeightPx)
{
    if (!nsWindow) return 0;
    NSWindow* w = (NSWindow*)nsWindow;
    w.titlebarAppearsTransparent = YES;
    w.styleMask |= NSWindowStyleMaskFullSizeContentView;
    if (@available(macOS 11.0, *))
        w.titlebarSeparatorStyle = NSTitlebarSeparatorStyleNone;   // no hairline under the (invisible) titlebar
    NSButton* close = [w standardWindowButton:NSWindowCloseButton];
    NSButton* mini  = [w standardWindowButton:NSWindowMiniaturizeButton];
    NSButton* zoom  = [w standardWindowButton:NSWindowZoomButton];
    if (!close || !mini || !zoom) return 0;
    // In native fullscreen AppKit owns the titlebar reveal strip - never fight it (Electron guard).
    if (w.styleMask & NSWindowStyleMaskFullScreen) return (int)(NSMaxX(zoom.frame) + 8);
    // Stock 28pt band already centres the lights; only a taller toolbar row needs repositioning.
    if (rowHeightPx > 28)
    {
        NSView* container = close.superview.superview;   // NSTitlebarContainerView (Electron's exact lookup)
        if (!container) return (int)(NSMaxX(zoom.frame) + 8);   // defensive: skip repositioning rather than message a nil view, should AppKit ever change this depth
        NSRect cf = container.frame;
        cf.size.height = rowHeightPx;
        cf.origin.y = NSHeight(w.frame) - rowHeightPx;
        container.frame = cf;
        // Read the stock metrics live (button size / inter-button gap survive our own moves, so this
        // stays idempotent) - never hardcode them, they differ across macOS versions.
        const CGFloat btnW = NSWidth(close.frame), btnH = NSHeight(close.frame);
        const CGFloat gap  = NSMinX(mini.frame) - NSMaxX(close.frame);
        const CGFloat x    = 12;                                   // Electron's hiddenInset preset x
        const CGFloat y    = ((CGFloat)rowHeightPx - btnH) / 2;
        [close setFrameOrigin:NSMakePoint(x, y)];
        [mini  setFrameOrigin:NSMakePoint(x + (btnW + gap), y)];
        [zoom  setFrameOrigin:NSMakePoint(x + 2 * (btnW + gap), y)];
    }
    return (int)(NSMaxX(zoom.frame) + 8);
}

// Start a native window drag from the current mouse-down. Needed because wx views consume unhandled
// mouse events (they never reach AppKit's frame-drag machinery), so with the title bar reclaimed the
// empty toolbar-row area couldn't drag the window otherwise. Call from a wxEVT_LEFT_DOWN handler -
// [NSApp currentEvent] IS that mouse-down NSEvent, since wx dispatches synchronously (the same
// pattern as Tauri's drag regions). NSWindow API, macOS 10.11+.
extern "C" void wxn_DragWindow(void* nsWindow)
{
    if (!nsWindow) return;
    NSWindow* w = (NSWindow*)nsWindow;
    NSEvent* e = [NSApp currentEvent];
    if (e) [w performWindowDragWithEvent:e];
}

// ===== Native spell-check via NSSpellChecker (backs src/spell_engine.h's macOS Engine) =====
// The C++ side (spell_engine.h) declares these extern "C" and wraps them in a MacEngine, so the
// Scintilla squiggle/suggestion UX in main.cpp is engine-agnostic. Uses the shared system checker and
// the user's own OS dictionaries - no bundled dictionary, no third-party library.
#include <string.h>

// Map a BCP-47 tag ("en-US") to the underscore identifier NSSpellChecker uses ("en_US"); "" -> nil.
static NSString* wxnSpellLang(const char* bcp47)
{
    if (!bcp47 || !*bcp47) return nil;
    NSString* t = [NSString stringWithUTF8String:bcp47];
    return t ? [t stringByReplacingOccurrencesOfString:@"-" withString:@"_"] : nil;
}

extern "C" bool wxn_spell_available(void)
{
    return [NSSpellChecker sharedSpellChecker] != nil;
}

// true = the OS spell checker can check the requested language - exact, or a variant sharing its primary
// subtag (so "en-US" is satisfied by an installed "en" or "en_GB"). Lets spell_engine.h fall through to the
// bundled Hunspell dictionary when the OS lacks the requested language, mirroring the Windows path.
extern "C" bool wxn_spell_language_available(const char* bcp47)
{
    @autoreleasepool {
        NSSpellChecker* sc = [NSSpellChecker sharedSpellChecker];
        NSString* want = wxnSpellLang(bcp47);
        if (!sc || !want) return false;
        NSString* wantPrimary = [[want componentsSeparatedByString:@"_"] firstObject];   // "en_US" -> "en"
        for (NSString* lang in [sc availableLanguages]) {
            if ([lang caseInsensitiveCompare:want] == NSOrderedSame) return true;
            NSString* langPrimary = [[lang componentsSeparatedByString:@"_"] firstObject];
            if (wantPrimary.length && [langPrimary caseInsensitiveCompare:wantPrimary] == NSOrderedSame)
                return true;
        }
        return false;
    }
}

// true = the word is spelled correctly in `bcp47lang`. Passing the language EXPLICITLY means the result
// does not depend on the user's global spelling-language setting (the wrong-language-fallback fix).
extern "C" bool wxn_spell_check(const char* utf8word, const char* bcp47lang)
{
    @autoreleasepool {
        if (!utf8word) return true;
        NSString* s = [NSString stringWithUTF8String:utf8word];
        if (!s || s.length == 0) return true;
        NSInteger wordCount = 0;
        NSRange r = [[NSSpellChecker sharedSpellChecker] checkSpellingOfString:s
                                                                   startingAt:0
                                                                     language:wxnSpellLang(bcp47lang)
                                                                         wrap:NO
                                                       inSpellDocumentWithTag:0
                                                                    wordCount:&wordCount];
        return r.location == NSNotFound;
    }
}

// '\n'-joined UTF-8 suggestions (up to 8) in `bcp47lang`, malloc'd for the caller to free(); NULL if none.
extern "C" char* wxn_spell_suggest(const char* utf8word, const char* bcp47lang)
{
    @autoreleasepool {
        if (!utf8word) return NULL;
        NSString* s = [NSString stringWithUTF8String:utf8word];
        if (!s || s.length == 0) return NULL;
        NSSpellChecker* sc = [NSSpellChecker sharedSpellChecker];
        NSArray<NSString*>* guesses = [sc guessesForWordRange:NSMakeRange(0, s.length)
                                                     inString:s
                                                     language:wxnSpellLang(bcp47lang)
                                       inSpellDocumentWithTag:0];
        if (!guesses || guesses.count == 0) return NULL;
        NSMutableString* joined = [NSMutableString string];
        NSUInteger i = 0;
        for (NSString* g in guesses) {
            if (i >= 8) break;
            if (i) [joined appendString:@"\n"];
            [joined appendString:g];
            ++i;
        }
        const char* c = [joined UTF8String];
        return c ? strdup(c) : NULL;
    }
}

extern "C" bool wxn_spell_add(const char* utf8word)
{
    @autoreleasepool {
        if (!utf8word) return false;
        NSString* s = [NSString stringWithUTF8String:utf8word];
        if (!s || s.length == 0) return false;
        [[NSSpellChecker sharedSpellChecker] learnWord:s];
        return true;
    }
}
