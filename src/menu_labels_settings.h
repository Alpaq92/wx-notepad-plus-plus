#pragma once
#include <wx/intl.h>

namespace Label {
// Phase B reshape: Settings' own top-level label is retired - its items now live directly inside
// the Window menu's table (see menu_data_window.h), which keeps its own pre-existing MenuWindow().
inline const wxString SettingsPreferences()      { return _("Preferences..."); }
inline const wxString SettingsStyleConfigurator(){ return _("Style Configurator..."); }
inline const wxString SettingsShortcutMapper()   { return _("Shortcut Mapper..."); }
inline const wxString SettingsImportSubmenu()    { return _("Import"); }
inline const wxString SettingsImportPlugins()    { return _("Import plugin(s)..."); }
inline const wxString SettingsImportStyleThemes(){ return _("Import style theme(s)..."); }
inline const wxString SettingsEditContextMenu()  { return _("Edit Popup ContextMenu"); }
}
