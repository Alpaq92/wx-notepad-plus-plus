#pragma once
// Embedded application icon for wxNote.
// Source of truth: resources/wxnote.svg (the user's own icon design).
// Rendered to a wxIconBundle at runtime via wxBitmapBundle::FromSVG, so the exe
// is self-contained (no external icon file needed at runtime).
static const char APP_ICON_SVG[] = R"NPPSVG(<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256" role="img" aria-label="wxNote app icon">
  <title>wxNote — app icon</title>

  <!-- plate (brand green, soft rounded square) -->
  <rect x="12" y="12" width="232" height="232" rx="34" fill="#37b24d"/>

  <!-- subtle drop shadow for one layer of depth -->
  <path d="M88 174 L88 82 L168 174 L168 82" transform="translate(0 5)"
        fill="none" stroke="#0b3d20" stroke-width="28" stroke-linecap="round" stroke-linejoin="round" opacity="0.16"/>

  <!-- "N" monogram (single bold mark) -->
  <path d="M88 174 L88 82 L168 174 L168 82"
        fill="none" stroke="#ffffff" stroke-width="28" stroke-linecap="round" stroke-linejoin="round"/>
</svg>
)NPPSVG";
