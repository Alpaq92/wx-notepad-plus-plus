# wxNotepad++ project site

The GitHub Pages landing page for wxNotepad++, deployed by
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

## Where the "live" content comes from

The version badge, the three platform download buttons, and the changelog list are **not** baked in
at deploy time - `script.js` calls the GitHub REST API directly from the visitor's browser
(`GET /repos/Alpaq92/wx-notepad-plus-plus/releases/latest` and `.../releases?per_page=6`) on every
page load, so they're always current even between deploys. Download buttons are matched to release
assets by filename suffix (`.exe`, `.dmg`, `.AppImage`, `.deb`, `.rpm`, `.flatpak`) - if a packaging
script's output naming ever changes (see `installer/*/build-*.sh` and `installer/windows/wxnpp.nsi`),
update `ASSET_MATCHERS` in `script.js` to match.

## Screenshots page

Currently ships with placeholder tiles (`.project-img--placeholder`, dashed border + an icon) instead
of real screenshots. To swap one in: replace the `<figure class="project-img project-img--placeholder">
...</figure>` block for that tile with a plain `<figure class="project-img"><img src="..." loading="lazy">
</figure>` - the surrounding grid/filter CSS doesn't need to change.

## Local preview

Any static file server works, e.g. `python -m http.server 4173 --directory site`.
