# wxNote project site

The GitHub Pages landing page for wxNote, deployed by
[`.github/workflows/pages.yml`](../.github/workflows/pages.yml) on every published release (and on
any push to `master` that touches this directory).

## Stack

Plain HTML/CSS/JS, no build step, no dependencies to install - adapted from
[codewithsadee/vcard-personal-portfolio](https://github.com/codewithsadee/vcard-personal-portfolio)
(MIT-licensed vCard template), reworked from a personal-portfolio layout into a project page:

- `index.html` - the whole site (About / Features / Screenshots / Download / Changelog, single-page
  tab navigation, no routing).
- `assets/css/style.css` - all styling. Colors are CSS custom properties defined twice - once under
  `:root` (dark, the default) and once under `[data-theme="light"]` - so light/dark/system theming
  needs no per-component overrides; only the token *values* differ.
- `assets/js/script.js` - sidebar/nav/filter interactions kept from the template, plus the theme
  toggle and the live GitHub API calls described below.
- `assets/images/logo.svg` - the app icon (`src/app_icon_svg.h`'s SVG, copied verbatim), used as
  favicon and sidebar avatar.
- `assets/vendor/img-previewer/` - [img-previewer](https://github.com/yue1123/img-previewer) (MIT),
  vendored rather than CDN-linked (see its own `CREDITS.md`), powers the Screenshots page's
  click-to-zoom lightbox.

## Where the "live" content comes from

The version badge, the platform download buttons, and the changelog list are **not** baked in
at deploy time - `script.js` calls the GitHub REST API directly from the visitor's browser
(`GET /repos/Alpaq92/wx-notepad-plus-plus/releases/latest` and `.../releases?per_page=6`) on every
page load, so they're always current even between deploys. Download buttons are matched to release
assets by filename suffix and, for the two macOS builds, an `arm64`/`x86_64` substring
(`.exe`, `arm64...dmg`, `x86_64...dmg`, `.AppImage`, `.deb`, `.rpm`, `.flatpak`) - if a packaging
script's output naming ever changes (see `installer/*/build-*.sh` and `installer/windows/wxnote.nsi`),
update `ASSET_MATCHERS` in `script.js` to match.

## Screenshots page

Real screenshots live in `assets/images/screenshots/`, captured from an actual running build (English
UI, Tabler icons unless the tile is specifically about a different icon pack/theme). To retake one:
replace the PNG in place and it picks up automatically - each tile is a plain `<figure
class="project-img"><img src="..." loading="lazy"></figure>`, no other markup to change. Every tile's
`<img>` is click-to-zoom via img-previewer (see Stack above) - a single `new ImgPreviewer(".project-list")`
call in `script.js` covers the whole list, including filtered-out (hidden, not removed) tiles, so no
re-init is needed when the category filter changes. The `.project-img--placeholder` CSS variant
(dashed border + icon) still exists in
`assets/css/style.css` for any future tile that doesn't have a screenshot yet - swap a `<figure
class="project-img">...<img></figure>` back to `<figure class="project-img project-img--placeholder">
<ion-icon name="image-outline"></ion-icon><span>Screenshot coming soon</span></figure>` to use it.

## Local preview

Any static file server works, e.g. `python -m http.server 4173 --directory site`.
