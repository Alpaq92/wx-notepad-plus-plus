// gtk_native.cpp - GTK-only tweak wxWidgets doesn't expose: neutral-theme the app's native GtkScrollbars
// (and their GtkScrolledWindow decoration nodes) so a coloured desktop theme accent stops painting down the
// editor's right edge.
//
// WHAT THE STRIP ACTUALLY IS. The main editor is a wxStyledTextCtrl. Crucially, wxSTC does NOT use the native
// ScintillaGTK backend - it uses wxWidgets' own Scintilla platform layer (src/stc/ScintillaWX.cpp), whose
// ScintillaWX::ModifyScrollBars() calls stc->SetScrollbar(wxVERTICAL, ...) - i.e. the *wxWindow built-in*
// scrollbar. On wxGTK that built-in scrollbar is a real GtkScrollbar living inside the wxWindow's GtkScrolled-
// Window wrapper (wxPizza-in-GtkScrolledWindow). So the editor IS inside a GtkScrolledWindow (this is exactly
// what the user's GTK Inspector reported), and on Linux Mint's dark themes with a coloured system accent that
// scrollbar - plus the scrolled window's overshoot/undershoot/junction decoration nodes - picks up the accent.
// On an EMPTY document the slider spans the whole track -> a full-height coloured strip / "tint" down the right
// edge. (An earlier version of this file wrongly attributed the strip to a raw ScintillaGTK child scrollbar;
// wxSTC never instantiates ScintillaGTK, so that rationale was incorrect - the widget is wx's own GtkScrollbar.)
//
// TWO THINGS ARE NEEDED, because the accent can arrive on more than one node:
//   1. the GtkScrollbar's slider/trough (the classic scrollbar), and
//   2. the GtkScrolledWindow's `overshoot`/`undershoot` edge-glow and `junction` nodes, which many themes paint
//      as a semi-transparent accent gradient - this is the "blue TINT" (a gradient, not a solid bar) the
//      scrollbar-only rules of the previous version left untouched.
//
// WINNING THE CASCADE. GTK's style cascade sorts by PROVIDER PRIORITY FIRST (a higher-priority provider wins
// regardless of selector specificity). Mint injects the accent somewhere at/above APPLICATION(600) - typically
// a USER(800) ~/.config/gtk-3.0/gtk.css. We therefore:
//   (a) install a screen-wide provider at G_MAXUINT - strictly above every standard bucket (FALLBACK 1 /
//       THEME 200 / SETTINGS 400 / APPLICATION 600 / USER 800) - so we win the priority sort; and
//   (b) ALSO add the same provider DIRECTLY to every GtkScrollbar widget's own style context (walking the
//       toplevel's widget tree, internal children included). A widget-scoped provider rooted at the scrollbar
//       matches the `scrollbar {...}` rules against the scrollbar itself, so it cannot miss the node via a
//       selector-shape mismatch - a guarantee the screen-wide selector alone doesn't give. Both are at
//       G_MAXUINT.
// Belt (screen-wide, catches nodes/widgets created later) and suspenders (per-widget, guarantees today's
// scrollbars are hit).
//
// WARNING - never add `!important` to this CSS. GTK3's CSS parser does NOT implement it (provider priority
// is GTK's substitute for it): any declaration carrying `!important` is rejected wholesale as "Junk at end
// of value" and silently discarded. The 0.5.9 shim had it on EVERY declaration, so the whole stylesheet
// loaded empty and the "fix" was a no-op - that, not the cascade, is why the accent strip survived two
// releases. (The rejects do show up as "Theme parsing error" g_warnings on stderr, ~one per declaration.)
//
// Compiled + GTK3-linked ONLY on UNIX-AND-NOT-APPLE (see CMakeLists.txt), exactly like src/macos_native.mm
// is Apple-only. It MUST NOT be #ifdef __WXGTK__-guarded: that macro comes from wxWidgets headers, which are
// deliberately NOT included here (only <gtk/gtk.h>), so a self-guard would delete the whole TU and leave
// main.cpp's call unresolved at link time. Gating is purely via CMake. main.cpp only calls the extern "C"
// entry below, mirroring the macOS wxn_HideWindowTitle shim. The provider is process-wide and reused;
// re-calling with a new `dark` reloads it live. (The identical GTK CSS-provider pattern is already used by
// third_party/wxbf/src/borderless_frame_gtk.cpp, which proves these symbols link in this build.)
#include <gtk/gtk.h>

// Recursively add `provider` (at G_MAXUINT) to every GtkScrollbar's own style context under `w`. Uses
// gtk_container_forall (not _foreach) so a GtkScrolledWindow's scrollbars - which are INTERNAL children and
// invisible to _foreach - are reached. Re-adding the same provider to a context that already has it is guarded
// by a remove-first, so repeated calls (e.g. every dark/light toggle) never stack duplicate references.
static void wxn_restyle_scrollbars(GtkWidget* w, gpointer data)
{
    if (!w) return;
    GtkStyleProvider* provider = GTK_STYLE_PROVIDER(data);
    if (GTK_IS_SCROLLBAR(w))
    {
        GtkStyleContext* ctx = gtk_widget_get_style_context(w);
        gtk_style_context_remove_provider(ctx, provider);
        gtk_style_context_add_provider(ctx, provider, G_MAXUINT);
    }
    if (GTK_IS_CONTAINER(w))
        gtk_container_forall(GTK_CONTAINER(w), wxn_restyle_scrollbars, data);
}

extern "C" void wxn_InstallDarkScrollbarCss(void* gtkWidgetOrNull, int dark)
{
    // Disable GTK overlay scrolling app-wide so the editor's GtkScrolledWindow scrollbar becomes a normal,
    // always-visible scrollbar that the CSS below dark-themes to grey (rather than the thin accent-coloured
    // "overlay-indicator" bar). GtkSettings prop, GTK 3.16+; the in-code equivalent of GTK_OVERLAY_SCROLLING=0.
    // The overlay-indicator selectors below are kept too, in case a path re-enables overlay for some window.
    if (GtkSettings* settings = gtk_settings_get_default())
        g_object_set(settings, "gtk-overlay-scrolling", FALSE, NULL);

    // Structural (colour-independent) toolbar compaction, appended to BOTH palettes below. The inter-icon
    // spacing on GTK comes entirely from the desktop theme's button metrics (Mint-Y: `button { min-width:
    // 20px; padding: 5px 8px; }`, `.image-button { min-width: 24px; }`, `toolbar { padding: 4px; }` plus
    // 1px margins), which makes the toolbar row far airier than the Windows/macOS toolbars. wx adds no
    // spacing of its own (wxToolBar/GTK is a plain GtkToolbar with GtkToolButton items), so theme-level CSS
    // is the right lever - and at G_MAXUINT these plain declarations win the cascade. Node tree:
    // toolbar > toolbutton > button - the theme's padding/min-width sit on the INNER `button` node;
    // separators are `separator` nodes. border/background are deliberately untouched so the theme's flat
    // hover/pressed feedback still paints.
#define WXN_TOOLBAR_COMPACT_CSS \
    "toolbar { padding: 2px; }" \
    "toolbar toolbutton > button, toolbar toolitem button {" \
    "  padding: 3px; margin: 0; min-width: 0; min-height: 0; }" \
    "toolbar.horizontal separator { margin: 0 3px; }" \
    "toolbar.vertical separator { margin: 3px 0; }"

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

    // The provider sits at G_MAXUINT priority (see file header), which alone beats any theme/user provider -
    // including a USER(800) ~/.config/gtk-3.0/gtk.css - that Mint uses to paint the accent; GTK sorts the
    // cascade by provider priority before selector specificity, and GTK3 CSS has no `!important` (see the
    // WARNING in the file header - adding it would void the whole stylesheet). Both background-image AND
    // box-shadow (and the `background` shorthand) are killed on trough AND slider, because themes deliver
    // the accent as a linear-gradient background-image OR an inset box-shadow. `overshoot`/`undershoot`/
    // `junction` cover the GtkScrolledWindow decoration nodes that carry the semi-transparent accent "tint".
    static const char* const css_dark =
        "scrollbar, scrollbar.vertical, scrollbar.horizontal,"
        "scrollbar.overlay-indicator, scrollbar.overlay-indicator.vertical,"
        "scrollbar.overlay-indicator.horizontal {"
        "  background-color: #262626; background-image: none;"
        "  box-shadow: none; border: none; -gtk-icon-shadow: none; }"
        "scrollbar contents {"
        "  background-color: #262626; background-image: none;"
        "  box-shadow: none; border: none; }"
        "scrollbar trough, scrollbar contents trough {"
        "  background-color: #262626; background-image: none;"
        "  box-shadow: none; border: none; border-radius: 0;"
        "  outline: none; }"
        "scrollbar slider, scrollbar contents trough slider {"
        "  background-color: #4a4a4a; background-image: none;"
        "  box-shadow: none; border: none; border-radius: 6px;"
        "  min-width: 8px; min-height: 8px; outline: none; }"
        "scrollbar slider:hover {"
        "  background-color: #5c5c5c; background-image: none; box-shadow: none; }"
        "scrollbar slider:active, scrollbar slider:hover:active {"
        "  background-color: #6e6e6e; background-image: none; box-shadow: none; }"
        "scrollbar slider:disabled {"
        "  background-color: #3a3a3a; background-image: none; box-shadow: none; }"
        "scrollbar slider:backdrop {"
        "  background-color: #4a4a4a; background-image: none; box-shadow: none; }"
        "scrollbar:backdrop, scrollbar trough:backdrop, scrollbar contents:backdrop {"
        "  background-color: #262626; background-image: none; box-shadow: none; }"
        // GtkScrolledWindow decoration nodes: the accent frequently arrives here as a semi-transparent radial/
        // linear gradient (the "tint"), NOT on the scrollbar at all. Kill the gradient + shadow on all of them.
        "overshoot, overshoot.top, overshoot.bottom, overshoot.left, overshoot.right,"
        "undershoot, undershoot.top, undershoot.bottom, undershoot.left, undershoot.right {"
        "  background: none; background-image: none; background-color: transparent;"
        "  box-shadow: none; border: none; }"
        "junction {"
        "  background-color: #262626; background-image: none;"
        "  box-shadow: none; border: none; }"
        // The integrated toolbar is a native GtkToolbar whose bg Mint's theme paints; wxToolBar::SetBackgroundColour
        // is defeated by that theme (same cascade as the scrollbar). Force the `toolbar` node to the chrome colour
        // (#202020 == wxColour(32,32,32)) so the toolbar matches the wx-drawn AUI dock gap to its right - i.e. the
        // toolbar row reads as one seamless full window width instead of "icons then a different-shade gap".
        "toolbar, toolbar.horizontal, toolbar.primary-toolbar {"
        "  background-color: #202020; background-image: none;"
        "  box-shadow: none; border: none; }"
        // Toolbar/control tooltips (e.g. hovering "Word Wrap") are a native GtkTooltip popup - a
        // separate top-level window, not a descendant of any wx-managed widget, so nothing else in
        // this file reaches it; only this screen-wide G_MAXUINT provider can. Left unstyled it renders
        // in GTK's stock light/blue-accented box regardless of the app's own dark chrome. Match the
        // same (45,45,45)/(220,220,220) pair themeDialog() already uses for every wxNote dialog.
        "tooltip, tooltip.background, tooltip decoration {"
        "  background-color: #2d2d2d; background-image: none;"
        "  color: #dcdcdc; border: 1px solid #4a4a4a; border-radius: 4px;"
        "  box-shadow: none; padding: 4px 8px; }"
        "tooltip label { color: #dcdcdc; }"
        WXN_TOOLBAR_COMPACT_CSS;

    static const char* const css_light =
        "scrollbar, scrollbar.vertical, scrollbar.horizontal,"
        "scrollbar.overlay-indicator, scrollbar.overlay-indicator.vertical,"
        "scrollbar.overlay-indicator.horizontal {"
        "  background-color: #f0f0f0; background-image: none;"
        "  box-shadow: none; border: none; -gtk-icon-shadow: none; }"
        "scrollbar contents {"
        "  background-color: #f0f0f0; background-image: none;"
        "  box-shadow: none; border: none; }"
        "scrollbar trough, scrollbar contents trough {"
        "  background-color: #f0f0f0; background-image: none;"
        "  box-shadow: none; border: none; border-radius: 0;"
        "  outline: none; }"
        "scrollbar slider, scrollbar contents trough slider {"
        "  background-color: #c2c2c2; background-image: none;"
        "  box-shadow: none; border: none; border-radius: 6px;"
        "  min-width: 8px; min-height: 8px; outline: none; }"
        "scrollbar slider:hover {"
        "  background-color: #a8a8a8; background-image: none; box-shadow: none; }"
        "scrollbar slider:active, scrollbar slider:hover:active {"
        "  background-color: #909090; background-image: none; box-shadow: none; }"
        "scrollbar slider:disabled {"
        "  background-color: #d8d8d8; background-image: none; box-shadow: none; }"
        "scrollbar slider:backdrop {"
        "  background-color: #c2c2c2; background-image: none; box-shadow: none; }"
        "scrollbar:backdrop, scrollbar trough:backdrop, scrollbar contents:backdrop {"
        "  background-color: #f0f0f0; background-image: none; box-shadow: none; }"
        "overshoot, overshoot.top, overshoot.bottom, overshoot.left, overshoot.right,"
        "undershoot, undershoot.top, undershoot.bottom, undershoot.left, undershoot.right {"
        "  background: none; background-image: none; background-color: transparent;"
        "  box-shadow: none; border: none; }"
        "junction {"
        "  background-color: #f0f0f0; background-image: none;"
        "  box-shadow: none; border: none; }"
        "toolbar, toolbar.horizontal, toolbar.primary-toolbar {"   // match the AUI dock gap (#f0f0f0 == wxColour(240,240,240)) - seamless full-width toolbar
        "  background-color: #f0f0f0; background-image: none;"
        "  box-shadow: none; border: none; }"
        // Light-mode tooltip: a clean neutral box instead of the stock theme's blue-accented one, so it
        // reads as intentional/branded chrome rather than a leftover default. See the dark palette above
        // for why a screen-wide provider is the only way to reach this native GtkTooltip popup at all.
        "tooltip, tooltip.background, tooltip decoration {"
        "  background-color: #fafafa; background-image: none;"
        "  color: #202020; border: 1px solid #c8c8c8; border-radius: 4px;"
        "  box-shadow: none; padding: 4px 8px; }"
        "tooltip label { color: #202020; }"
        WXN_TOOLBAR_COMPACT_CSS;

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

    // Suspenders: attach the same provider directly to every GtkScrollbar already in the tree, so the
    // scrollbar rules are guaranteed to be rooted at (and thus match) each actual scrollbar node - not merely
    // reachable via a screen-wide `scrollbar` selector. Walk from the toplevel so panel scrollbars are covered
    // too, not just the editor's. Safe pre-realization: GtkWidgets exist as objects (and forall traverses
    // them) before they're shown.
    if (gtkWidgetOrNull)
    {
        GtkWidget* top = gtk_widget_get_toplevel(GTK_WIDGET(gtkWidgetOrNull));
        wxn_restyle_scrollbars(top ? top : GTK_WIDGET(gtkWidgetOrNull), provider);
    }
}
