// gtk_native.cpp - GTK-only tweak wxWidgets doesn't expose: neutral-theme the app's native GtkScrollbars.
//
// The main editor is a wxStyledTextCtrl whose GTK backend (bundled Scintilla, ScintillaGTK.cxx) creates a
// REAL child GtkScrollbar for the vertical scrollbar and leaves it styled by the desktop GTK theme. On Linux
// Mint's dark themes with a coloured system accent that native scrollbar picks up the accent (a green/teal
// gradient) and, on an EMPTY document, its thumb spans the whole track -> a full-height coloured strip down
// the window's right edge (unchanged when the window is moved over a dark background, because it is an
// app-owned native widget, not the compositor shadow). This is the GTK analogue of the MSW
// SetWindowTheme(DarkMode_Explorer) scrollbar theming in applyTheme().
//
// We override ALL of the app's native scrollbars with an APPLICATION-priority GtkCssProvider (a screen-wide
// provider is the only way to reach Scintilla's CHILD scrollbar, which a widget-scoped provider can't
// target). Both background-image AND box-shadow must be killed: themes deliver the accent as a linear-
// gradient background-image OR as an inset box-shadow on the trough/slider, so neutralising background-color
// alone can leave the gradient.
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

    static const char* const css_dark =
        "scrollbar, scrollbar.vertical, scrollbar.horizontal {"
        "  background-color: #262626; background-image: none; box-shadow: none; border: none; }"
        "scrollbar trough {"
        "  background-color: #262626; background-image: none; box-shadow: none; border: none; border-radius: 0; }"
        "scrollbar slider {"
        "  background-color: #4a4a4a; background-image: none; box-shadow: none; border: none;"
        "  border-radius: 6px; min-width: 8px; min-height: 8px; }"
        "scrollbar slider:hover    { background-color: #5c5c5c; background-image: none; box-shadow: none; }"
        "scrollbar slider:active   { background-color: #6e6e6e; background-image: none; box-shadow: none; }"
        "scrollbar slider:disabled { background-color: #3a3a3a; background-image: none; box-shadow: none; }";

    static const char* const css_light =
        "scrollbar, scrollbar.vertical, scrollbar.horizontal {"
        "  background-color: #f0f0f0; background-image: none; box-shadow: none; border: none; }"
        "scrollbar trough {"
        "  background-color: #f0f0f0; background-image: none; box-shadow: none; border: none; border-radius: 0; }"
        "scrollbar slider {"
        "  background-color: #c2c2c2; background-image: none; box-shadow: none; border: none;"
        "  border-radius: 6px; min-width: 8px; min-height: 8px; }"
        "scrollbar slider:hover    { background-color: #a8a8a8; background-image: none; box-shadow: none; }"
        "scrollbar slider:active   { background-color: #909090; background-image: none; box-shadow: none; }"
        "scrollbar slider:disabled { background-color: #d8d8d8; background-image: none; box-shadow: none; }";

    gtk_css_provider_load_from_data(provider, dark ? css_dark : css_light, -1, nullptr);

    if (!added)
    {
        gtk_style_context_add_provider_for_screen(
            screen, GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        added = TRUE;
    }
}
