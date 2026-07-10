// gtk_native.cpp - GTK-only tweak wxWidgets doesn't expose: neutral-theme the app's native GtkScrollbars.
//
// The main editor is a wxStyledTextCtrl whose GTK backend (bundled Scintilla, ScintillaGTK.cxx) creates a
// REAL child GtkScrollbar for the vertical scrollbar and leaves it styled by the desktop GTK theme. Confirmed
// in the vendored source: ScintillaGTK::Initialise() does `scrollbarv = gtk_scrollbar_new(...)` and
// `gtk_widget_set_parent(PWidget(scrollbarv), PWidget(wMain))` (ScintillaGTK.cxx:592 / :599), so the CSS node
// is a plain `scrollbar` parented directly onto Scintilla's main widget - a CHILD of the STC's GtkWidget, not
// a widget we own. On Linux Mint's dark themes with a coloured system accent that native scrollbar picks up
// the accent (a green/teal gradient) and, on an EMPTY document, its thumb spans the whole track -> a
// full-height coloured strip down the window's right edge. This is the GTK analogue of the MSW
// SetWindowTheme(DarkMode_Explorer) scrollbar theming in applyTheme().
//
// WHY THE OLD APPROACH LOST. A screen-wide provider added at GTK_STYLE_PROVIDER_PRIORITY_APPLICATION (600)
// beats the desktop THEME (200) - that is exactly why third_party/wxbf/src/borderless_frame_gtk.cpp's
// APPLICATION-priority `decoration` provider visibly wins (its grey window border shows). But on Mint the
// scrollbar accent survived APPLICATION, which means it is injected ABOVE 600 - the classic culprit being a
// user ~/.config/gtk-3.0/gtk.css, loaded by GTK at GTK_STYLE_PROVIDER_PRIORITY_USER (800). GTK's cascade
// sorts by PROVIDER PRIORITY FIRST (a higher-priority provider wins regardless of selector specificity), so
// an APPLICATION(600) rule can never override a USER(800) rule. The fix is therefore two-fold and belt-and-
// suspenders:
//   (a) add the provider at G_MAXUINT - strictly above USER(800) and every other standard bucket
//       (FALLBACK 1 / THEME 200 / SETTINGS 400 / APPLICATION 600 / USER 800), so we win the priority sort
//       against anything Mint injects; and
//   (b) mark every declaration `!important`, so even in the CSS-origin edge case where a lower-priority
//       provider's important declaration could contend, our important declaration at the top priority still
//       wins.
// A screen-wide provider (not widget-scoped) is required to reach Scintilla's CHILD scrollbar, and a
// widget-scoped provider would gain NOTHING here: a per-widget provider is merged into the SAME priority
// cascade as the screen providers, so it would still lose to a USER(800) rule unless it, too, sat above 800.
// Priority - not scope - is what decides the winner, so screen-wide-at-top-priority is both sufficient and
// simplest.
//
// Both background-image AND box-shadow (and the `background` shorthand, via background-color+background-image)
// must be killed on trough AND slider: themes deliver the accent as a linear-gradient background-image OR as
// an inset box-shadow, so neutralising background-color alone can leave the gradient. We also cover the GTK
// 3.20+ sub-nodes (`contents`, `trough`, `slider`), the orientation/overlay classes, and the :hover/:active/
// :disabled/:backdrop states so nothing accent-coloured survives in any state.
//
// Compiled + GTK3-linked ONLY on UNIX-AND-NOT-APPLE (see CMakeLists.txt), exactly like src/macos_native.mm
// is Apple-only. It MUST NOT be #ifdef __WXGTK__-guarded: that macro comes from wxWidgets headers, which are
// deliberately NOT included here (only <gtk/gtk.h>), so a self-guard would delete the whole TU and leave
// main.cpp's call unresolved at link time. Gating is purely via CMake. main.cpp only calls the extern "C"
// entry below, mirroring the macOS wxnpp_HideWindowTitle shim. The provider is process-wide and reused;
// re-calling with a new `dark` reloads it live. (The identical GTK CSS-provider pattern is already used by
// third_party/wxbf/src/borderless_frame_gtk.cpp, which proves these symbols link in this build.)
#include <gtk/gtk.h>

extern "C" void wxnpp_InstallDarkScrollbarCss(void* gtkWidgetOrNull, int dark)
{
    // Disable GTK overlay scrolling app-wide. GTK Inspector confirmed the "green strip down the right edge"
    // is the editor's vertical GtkScrollbar in its "overlay-indicator" state: a thin bar GTK paints with the
    // desktop accent (Mint's green/teal), full-height on an empty document. With overlay off it becomes a
    // normal, always-visible scrollbar that the CSS below dark-themes to grey. This is the in-code equivalent
    // of launching with GTK_OVERLAY_SCROLLING=0. (GtkSettings prop, GTK 3.16+.) We keep the overlay-indicator
    // selectors below too, in case some path re-enables overlay for a given scrolled window.
    if (GtkSettings* settings = gtk_settings_get_default())
        g_object_set(settings, "gtk-overlay-scrolling", FALSE, NULL);

    static GtkCssProvider* provider = nullptr;
    static gboolean        added    = FALSE;

    // Resolve a screen from any of our widgets, else the default screen.
    GdkScreen* screen = nullptr;
    if (gtkWidgetOrNull)
        screen = gtk_widget_get_screen(GTK_WIDGET(gtkWidgetOrNull));
    if (!screen)
        screen = gdk_screen_get_default();
    if (!screen)
        return;

    if (!provider)
        provider = gtk_css_provider_new();

    // Every declaration is `!important` and the provider is installed at G_MAXUINT priority (see file header):
    // together they beat any theme/user provider, including a USER(800) ~/.config/gtk-3.0/gtk.css, that Mint
    // uses to paint the accent on the scrollbar.
    static const char* const css_dark =
        "scrollbar, scrollbar.vertical, scrollbar.horizontal,"
        "scrollbar.overlay-indicator, scrollbar.overlay-indicator.vertical,"
        "scrollbar.overlay-indicator.horizontal {"
        "  background-color: #262626 !important; background-image: none !important;"
        "  box-shadow: none !important; border: none !important; -gtk-icon-shadow: none !important; }"
        "scrollbar contents {"
        "  background-color: #262626 !important; background-image: none !important;"
        "  box-shadow: none !important; border: none !important; }"
        "scrollbar trough, scrollbar contents trough {"
        "  background-color: #262626 !important; background-image: none !important;"
        "  box-shadow: none !important; border: none !important; border-radius: 0 !important;"
        "  outline: none !important; }"
        "scrollbar slider, scrollbar contents trough slider {"
        "  background-color: #4a4a4a !important; background-image: none !important;"
        "  box-shadow: none !important; border: none !important; border-radius: 6px !important;"
        "  min-width: 8px !important; min-height: 8px !important; outline: none !important; }"
        "scrollbar slider:hover {"
        "  background-color: #5c5c5c !important; background-image: none !important; box-shadow: none !important; }"
        "scrollbar slider:active, scrollbar slider:hover:active {"
        "  background-color: #6e6e6e !important; background-image: none !important; box-shadow: none !important; }"
        "scrollbar slider:disabled {"
        "  background-color: #3a3a3a !important; background-image: none !important; box-shadow: none !important; }"
        "scrollbar slider:backdrop {"
        "  background-color: #4a4a4a !important; background-image: none !important; box-shadow: none !important; }"
        "scrollbar:backdrop, scrollbar trough:backdrop, scrollbar contents:backdrop {"
        "  background-color: #262626 !important; background-image: none !important; box-shadow: none !important; }"
        // The integrated toolbar is a native GtkToolbar whose bg Mint's theme paints; wxToolBar::SetBackgroundColour
        // is defeated by that theme (same cascade as the scrollbar). Force the `toolbar` node to the chrome colour
        // (#202020 == wxColour(32,32,32)) so the toolbar matches the wx-drawn AUI dock gap to its right - i.e. the
        // toolbar row reads as one seamless full window width instead of "icons then a different-shade gap".
        "toolbar, toolbar.horizontal, toolbar.primary-toolbar {"
        "  background-color: #202020 !important; background-image: none !important;"
        "  box-shadow: none !important; border: none !important; }";

    static const char* const css_light =
        "scrollbar, scrollbar.vertical, scrollbar.horizontal,"
        "scrollbar.overlay-indicator, scrollbar.overlay-indicator.vertical,"
        "scrollbar.overlay-indicator.horizontal {"
        "  background-color: #f0f0f0 !important; background-image: none !important;"
        "  box-shadow: none !important; border: none !important; -gtk-icon-shadow: none !important; }"
        "scrollbar contents {"
        "  background-color: #f0f0f0 !important; background-image: none !important;"
        "  box-shadow: none !important; border: none !important; }"
        "scrollbar trough, scrollbar contents trough {"
        "  background-color: #f0f0f0 !important; background-image: none !important;"
        "  box-shadow: none !important; border: none !important; border-radius: 0 !important;"
        "  outline: none !important; }"
        "scrollbar slider, scrollbar contents trough slider {"
        "  background-color: #c2c2c2 !important; background-image: none !important;"
        "  box-shadow: none !important; border: none !important; border-radius: 6px !important;"
        "  min-width: 8px !important; min-height: 8px !important; outline: none !important; }"
        "scrollbar slider:hover {"
        "  background-color: #a8a8a8 !important; background-image: none !important; box-shadow: none !important; }"
        "scrollbar slider:active, scrollbar slider:hover:active {"
        "  background-color: #909090 !important; background-image: none !important; box-shadow: none !important; }"
        "scrollbar slider:disabled {"
        "  background-color: #d8d8d8 !important; background-image: none !important; box-shadow: none !important; }"
        "scrollbar slider:backdrop {"
        "  background-color: #c2c2c2 !important; background-image: none !important; box-shadow: none !important; }"
        "scrollbar:backdrop, scrollbar trough:backdrop, scrollbar contents:backdrop {"
        "  background-color: #f0f0f0 !important; background-image: none !important; box-shadow: none !important; }"
        "toolbar, toolbar.horizontal, toolbar.primary-toolbar {"   // match the AUI dock gap (#f0f0f0 == wxColour(240,240,240)) - seamless full-width toolbar
        "  background-color: #f0f0f0 !important; background-image: none !important;"
        "  box-shadow: none !important; border: none !important; }";

    gtk_css_provider_load_from_data(provider, dark ? css_dark : css_light, -1, nullptr);

    if (!added)
    {
        // G_MAXUINT: strictly above every standard style-provider priority bucket - FALLBACK(1), THEME(200),
        // SETTINGS(400), APPLICATION(600) and, crucially, USER(800, ~/.config/gtk-3.0/gtk.css) - so our rules
        // win the priority sort no matter how Mint injects the accent. gtk_style_context_add_provider_for_
        // screen() accepts any guint priority; there is nothing above G_MAXUINT to be overridden by.
        gtk_style_context_add_provider_for_screen(
            screen, GTK_STYLE_PROVIDER(provider),
            G_MAXUINT);
        added = TRUE;
    }
}
