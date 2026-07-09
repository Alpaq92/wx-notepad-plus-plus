# Site credits

The HTML structure, CSS component system, and interaction JavaScript in this directory are adapted
from **vCard - Personal Portfolio**, © 2022 codewithsadee, MIT License:
<https://github.com/codewithsadee/vcard-personal-portfolio>

What changed from the original template:
- All content: rewritten from a personal-portfolio bio into a project landing page (About / Features
  / Screenshots / Download / Changelog instead of About / Resume / Portfolio / Blog / Contact).
- Color system: every color token now has a light-theme counterpart (`[data-theme="light"]` in
  `assets/css/style.css`), plus a system/light/dark toggle (`assets/js/script.js`) - the original
  template is dark-only. The accent color was changed from the template's yellow to wxNotepad++'s own
  brand green.
- Sections with no project equivalent (testimonials, client logos, the skills progress bars, the
  contact form and map) were removed; their CSS was removed with them rather than left dead.
- New sections with no template equivalent (release/download cards, changelog cards fed from the
  GitHub API, screenshot placeholders) were added following the template's existing `.content-card`
  visual language.

Full license text:

```
MIT License

Copyright (c) 2022 codewithsadee

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

The app icon (`assets/images/logo.svg`) is wxNotepad++'s own, copied from `src/app_icon_svg.h` - see
the root [`LICENSING.md`](../LICENSING.md) for its provenance.

[Ionicons](https://ionic.io/ionicons) (MIT) and [Poppins](https://fonts.google.com/specimen/Poppins)
(SIL OFL 1.1) are loaded from their respective CDNs (unpkg, Google Fonts) and are not vendored here.
