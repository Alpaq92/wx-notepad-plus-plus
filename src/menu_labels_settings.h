#pragma once
#include <wx/intl.h>

namespace Label {
// Settings is a top-level menu again (a post-Phase-B revision - user feedback that Preferences buried
// under "Window" was a weird home). "Se&ttings" is the original Notepad++ label, still present and
// translated in every resources/locale/*.po from the Phase A port, so restoring it needs no catalog change.
inline const wxString MenuSettings()             { return _("Se&ttings"); }
inline const wxString SettingsPreferences()      { return _("Preferences..."); }
inline const wxString SettingsStyleConfigurator(){ return _("Style Configurator..."); }
inline const wxString SettingsShortcutMapper()   { return _("Shortcut Mapper..."); }
inline const wxString SettingsImportSubmenu()    { return _("Import"); }
inline const wxString SettingsImportPlugins()    { return _("Import plugin(s)..."); }
inline const wxString SettingsImportStyleThemes(){ return _("Import style theme(s)..."); }
inline const wxString SettingsEditContextMenu()  { return _("Edit Popup ContextMenu"); }
}
