#pragma once
// Embedded application icon for wxNotepad++ (original identity - not the Notepad++ logo).
// A rounded blue plate with three white "text" lines and an orange editing caret; the blue/orange
// match the toolbar icon set. Rendered to a wxIconBundle at runtime via wxBitmapBundle::FromSVG, so
// the exe is self-contained (no external icon file needed at runtime).
static const char APP_ICON_SVG[] = R"WXNPP(<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256" role="img" aria-label="wxNotepad++ app icon">
  <title>wxNotepad++ — app icon</title>

  <!-- rounded-square plate (our own blue identity, distinct from Notepad++) -->
  <rect x="16" y="16" width="224" height="224" rx="48" fill="#1971c2"/>

  <!-- notepad: three lines of "text" -->
  <rect x="58" y="80"  width="140" height="22" rx="11" fill="#ffffff"/>
  <rect x="58" y="119" width="140" height="22" rx="11" fill="#ffffff"/>
  <rect x="58" y="158" width="92"  height="22" rx="11" fill="#ffffff"/>

  <!-- editing caret (accent shared with the toolbar icon set) -->
  <rect x="162" y="149" width="18" height="40" rx="9" fill="#f08c00"/>
</svg>
)WXNPP";
