'use strict';

// ---------- repo config ----------
const REPO_OWNER = 'Alpaq92';
const REPO_NAME = 'wx-notepad-plus-plus';
const REPO_API_BASE = `https://api.github.com/repos/${REPO_OWNER}/${REPO_NAME}`;
const REPO_URL = `https://github.com/${REPO_OWNER}/${REPO_NAME}`;



// element toggle function
const elementToggleFunc = function (elem) { elem.classList.toggle("active"); }



// sidebar variables
const sidebar = document.querySelector("[data-sidebar]");
const sidebarBtn = document.querySelector("[data-sidebar-btn]");

// sidebar toggle functionality for mobile
sidebarBtn.addEventListener("click", function () { elementToggleFunc(sidebar); });



// custom select variables (Screenshots page category filter)
const select = document.querySelector("[data-select]");
const selectItems = document.querySelectorAll("[data-select-item]");
const selectValue = document.querySelector("[data-selecct-value]");
const filterBtn = document.querySelectorAll("[data-filter-btn]");

if (select) select.addEventListener("click", function () { elementToggleFunc(this); });

// add event in all select items
for (let i = 0; i < selectItems.length; i++) {
  selectItems[i].addEventListener("click", function () {

    let selectedValue = this.innerText.toLowerCase();
    selectValue.innerText = this.innerText;
    elementToggleFunc(select);
    filterFunc(selectedValue);

  });
}

// filter variables
const filterItems = document.querySelectorAll("[data-filter-item]");

const filterFunc = function (selectedValue) {

  for (let i = 0; i < filterItems.length; i++) {

    if (selectedValue === "all") {
      filterItems[i].classList.add("active");
    } else if (selectedValue === filterItems[i].dataset.category) {
      filterItems[i].classList.add("active");
    } else {
      filterItems[i].classList.remove("active");
    }

  }

}

// add event in all filter button items for large screen
let lastClickedBtn = filterBtn[0];

for (let i = 0; i < filterBtn.length; i++) {

  filterBtn[i].addEventListener("click", function () {

    let selectedValue = this.innerText.toLowerCase();
    selectValue.innerText = this.innerText;
    filterFunc(selectedValue);

    lastClickedBtn.classList.remove("active");
    this.classList.add("active");
    lastClickedBtn = this;

  });

}



// platform download dropdown (Download page - Linux's AppImage/.deb/.rpm/Flatpak choice)
document.querySelectorAll("[data-dropdown]").forEach((dropdown) => {
  const btn = dropdown.querySelector("[data-dropdown-btn]");

  const closeDropdown = () => {
    dropdown.classList.remove("active");
    btn.setAttribute("aria-expanded", "false");
  };

  btn.addEventListener("click", (event) => {
    event.stopPropagation();
    const isOpen = dropdown.classList.toggle("active");
    btn.setAttribute("aria-expanded", String(isOpen));
  });

  document.addEventListener("click", (event) => {
    if (!dropdown.contains(event.target)) closeDropdown();
  });
});



// page navigation variables
const navigationLinks = document.querySelectorAll("[data-nav-link]");
const pages = document.querySelectorAll("[data-page]");

// add event to all nav link
for (let i = 0; i < navigationLinks.length; i++) {
  navigationLinks[i].addEventListener("click", function () {

    for (let i = 0; i < pages.length; i++) {
      if (this.innerHTML.toLowerCase() === pages[i].dataset.page) {
        pages[i].classList.add("active");
        navigationLinks[i].classList.add("active");
        window.scrollTo(0, 0);
      } else {
        pages[i].classList.remove("active");
        navigationLinks[i].classList.remove("active");
      }
    }

  });
}



// ---------- theme (system / light / dark) ----------
// The initial paint is already themed by a small inline script in index.html's <head> (reads the
// same localStorage key, before CSS applies) - this block only wires up the toggle buttons and
// keeps things in sync afterwards, so there is no flash-of-wrong-theme on load.

const THEME_KEY = 'wxnpp-site-theme';
const root = document.documentElement;
const themeButtons = document.querySelectorAll('[data-theme-option]');
const darkMedia = window.matchMedia('(prefers-color-scheme: dark)');

const resolveTheme = (pref) => (pref === 'system' ? (darkMedia.matches ? 'dark' : 'light') : pref);

const applyTheme = (pref) => {
  root.setAttribute('data-theme', resolveTheme(pref));
  themeButtons.forEach((btn) => {
    btn.setAttribute('aria-pressed', String(btn.dataset.themeOption === pref));
  });
};

const setTheme = (pref) => {
  localStorage.setItem(THEME_KEY, pref);
  applyTheme(pref);
};

themeButtons.forEach((btn) => {
  btn.addEventListener('click', function () { setTheme(this.dataset.themeOption); });
});

darkMedia.addEventListener('change', () => {
  if ((localStorage.getItem(THEME_KEY) || 'system') === 'system') applyTheme('system');
});

applyTheme(localStorage.getItem(THEME_KEY) || 'system');



// ---------- live GitHub release data ----------
// No build step regenerates this page per release - instead it asks GitHub directly on every
// visit, so the version badge, download links and changelog are always current without needing
// a redeploy. (The GitHub Pages workflow *does* also redeploy on every published release, so the
// static HTML/CSS/JS themselves stay in sync too - see .github/workflows/pages.yml.)

const ASSET_MATCHERS = {
  windows: (name) => name.endsWith('.exe'),
  macos: (name) => name.endsWith('.dmg'),
  appimage: (name) => name.endsWith('.AppImage'),
  deb: (name) => name.endsWith('.deb'),
  rpm: (name) => name.endsWith('.rpm'),
  flatpak: (name) => name.endsWith('.flatpak'),
};

const formatDate = (iso) => {
  try {
    return new Date(iso).toLocaleDateString(undefined, { year: 'numeric', month: 'long', day: 'numeric' });
  } catch (err) {
    return '';
  }
};

async function loadLatestRelease() {
  const versionEls = document.querySelectorAll('[data-latest-version]');
  const dateEls = document.querySelectorAll('[data-latest-date]');
  const notesLinks = document.querySelectorAll('[data-latest-notes-link]');
  const downloadButtons = document.querySelectorAll('[data-download]');

  try {
    const res = await fetch(`${REPO_API_BASE}/releases/latest`);
    if (!res.ok) throw new Error(`releases/latest responded ${res.status}`);
    const release = await res.json();

    const version = release.tag_name || release.name || 'unreleased';
    const dateText = release.published_at ? `Released ${formatDate(release.published_at)}` : '';
    const notesUrl = release.html_url || `${REPO_URL}/releases`;

    versionEls.forEach((el) => { el.textContent = version; });
    dateEls.forEach((el) => { el.textContent = dateText; });
    notesLinks.forEach((el) => { el.href = notesUrl; });

    downloadButtons.forEach((btn) => {
      const matcher = ASSET_MATCHERS[btn.dataset.download];
      const asset = matcher ? (release.assets || []).find((a) => matcher(a.name)) : null;
      if (asset) {
        btn.href = asset.browser_download_url;
        btn.removeAttribute('aria-disabled');
      } else {
        btn.href = notesUrl;
        btn.setAttribute('aria-disabled', 'true');
      }
    });
  } catch (err) {
    versionEls.forEach((el) => { el.textContent = 'See GitHub'; });
    dateEls.forEach((el) => { el.textContent = ''; });
    notesLinks.forEach((el) => { el.href = `${REPO_URL}/releases`; });
    downloadButtons.forEach((btn) => {
      btn.href = `${REPO_URL}/releases`;
      btn.setAttribute('aria-disabled', 'true');
    });
  }
}

function escapeHtml(str) {
  const div = document.createElement('div');
  div.textContent = str == null ? '' : String(str);
  return div.innerHTML;
}

async function loadChangelog() {
  const list = document.querySelector('[data-changelog-list]');
  if (!list) return;

  const fallback = () => {
    list.innerHTML = `
      <li class="changelog-item">
        <div class="content-card changelog-card">
          <p class="changelog-text">Couldn't load releases from GitHub right now.</p>
          <a class="changelog-link" href="${REPO_URL}/releases" target="_blank" rel="noopener">
            View releases on GitHub <ion-icon name="arrow-forward-outline"></ion-icon>
          </a>
        </div>
      </li>`;
  };

  try {
    const res = await fetch(`${REPO_API_BASE}/releases?per_page=6`);
    if (!res.ok) throw new Error(`releases responded ${res.status}`);
    const releases = await res.json();
    if (!Array.isArray(releases) || releases.length === 0) { fallback(); return; }

    list.innerHTML = releases.map((r) => {
      const tag = escapeHtml(r.tag_name || '');
      const title = escapeHtml(r.name || r.tag_name || 'Release');
      const date = r.published_at ? escapeHtml(formatDate(r.published_at)) : '';
      const url = r.html_url || `${REPO_URL}/releases`;
      return `
        <li class="changelog-item">
          <div class="content-card changelog-card">
            <div class="changelog-meta">
              <span class="changelog-tag">${tag}</span>
              <time>${date}</time>
            </div>
            <h3 class="h3 changelog-title">${title}</h3>
            <p class="changelog-text">See what changed in this release on GitHub.</p>
            <a class="changelog-link" href="${url}" target="_blank" rel="noopener">
              Read full notes <ion-icon name="arrow-forward-outline"></ion-icon>
            </a>
          </div>
        </li>`;
    }).join('');
  } catch (err) {
    fallback();
  }
}

loadLatestRelease();
loadChangelog();
