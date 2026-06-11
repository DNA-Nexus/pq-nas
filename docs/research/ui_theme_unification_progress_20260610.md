# UI / Theme Unification — Progress Audit

**Date:** 2026-06-10
**Branch:** `ai/ui-theme-progress-audit-20260610`
**Companion document:** `ui_theme_unification_audit_20260610.md` (full technical audit)

---

## 1. Completed Work

### 1a. Shared components.css foundation

`server/src/static/components.css` (253 lines, v1) ships canonical `.pq-*`
classes that consume only `:root` tokens and require zero `!important`:

| Class | Purpose |
|-------|---------|
| `.pq-btn` (+ `.primary`, `.secondary`, `.danger`) | Theme-aware buttons |
| `.pq-card` | Card surface |
| `.pq-input`, `.pq-select`, `.pq-textarea` | Form controls |
| `.pq-badge` (+ `.ok`, `.warn`, `.err`, `.info`, `.muted`) | Status badges |
| `.pq-toolbar` | Button toolbar |
| `.pq-empty-state` | Placeholder UI |

### 1b. Admin & static pages migrated

| PR | Area | Commit |
|----|------|--------|
| #8 | Admin users & stats | `cc939ac` |
| #9 | Admin apps, updates, approvals | `183e328` |
| #10 | Admin system overview | `f399dd5` |
| #11 | Admin shared badges | `dbcb98c` |
| #13 | Login & wait-approval pages | `68f8c8e` |

### 1c. External workspace

| PR | Area | Commit |
|----|------|--------|
| #12 | External workspace | `0d2a820` |

### 1d. Dropzone

| PR | Area | Commit |
|----|------|--------|
| #14 | Dropzone upload UI | `33f5b4b` |

### 1e. File Manager foundation

| PR | Area | Commit |
|----|------|--------|
| #15 | File Manager foundation | `be9a7b1` |

### 1f. Echo Stack foundation

| PR | Area | Commit |
|----|------|--------|
| #16 | Echo Stack foundation | `a84b742` |

### 1g. Reel Stack foundation

| PR | Area | Commit |
|----|------|--------|
| #17 | Reel Stack foundation | `186efb2` |

### 1h. Circle Stack passes (8 PRs)

| PR | Area | Commit |
|----|------|--------|
| #18 | Circle Stack foundation | `f3f614e` |
| #19 | Composer | `0c67254` |
| #20 | Post cards | `7a82917` |
| #21 | Comments | `b5d86aa` |
| #22 | Reactions | `232221b` |
| #23 | Media preview | `8752a54` |
| #24 | Profile modal | `84f05d3` |
| #25 | Federation UI | `f051cea` |
| #26 | Memory nodes | `61adf9b` |
| #27 | Theme overrides foundation | `970f3f1` |

**Total merged: 20 PRs (#8–#27)**

---

## 2. Current Remaining Hotspots

### 2a. Hardcoded colours by major file

| File | Hex | RGB/RGBA | Total | Notes |
|------|----:|--------:|----- :|-------|
| `circlestack/app.css` | 292 | 398 | 690 | Largest single source |
| `external_workspace.css` | 91 | 294 | 385 | Many rgba() with literal values |
| `dropzone/style.css` | 139 | 122 | 261 | Has `--dz-*` tokens but still many literals |
| `theme.css` | 47 | 193 | 240 | Expected — token definitions live here |
| `circlestack/theme_overrides.css` | 130 | 92 | 222 | Overrides for bright + win_classic |
| `reelstack/reelstack_context_menu.css` | — | ~144 | 144 | Context menu theme fixes |
| `neonwave/app.css` | — | ~80 | 80 | Has `--nw-*` layer, some literals remain |

Note: many `rgba(var(--fg-rgb), 0.xx)` calls are already tokenised. The
counts above include those patterns — the true "unresolved" literals are
lower, concentrated in CircleStack and the context menu.

### 2b. `!important` counts by major file

```
 489  circlestack/theme_overrides.css   ████████████████████████████████████
 437  circlestack/app.css               ████████████████████████████████
 151  theme.css                         ████████████
 149  external_workspace.css            ████████████
  73  shell_menu.css                    ██████
  57  reelstack/reelstack_context_menu  ████
  50  people.css                        ████
  44  reelstack/app.css                 ███
  43  node_profile.css                  ███
  41  circlestack/memory_nodes.css      ███
   6  photogallery/leaflet.css          ▏  (vendored — ignore)
   4  onboarding.css                    ▏
   4  photogallery/albums_view.css      ▏
   3  dropzone/style.css                ▏
   2  echostack/style.css               ▏
   2  photogallery/bursts.css           ▏
   1  neonwave/app.css                  ▏
   1  photogallery/gallery_exif_search  ▏
   1  reelstack/reelstack_search.css    ▏
   1  sharesmgr/app.css                 ▏
──────────────────────────────────────────
1,558 total
```

### 2c. Override-heavy files (top 5)

1. **circlestack/theme_overrides.css** — 489 `!important`, deeply coupled
   selectors with `:not()` chains; bright + win_classic only.
2. **circlestack/app.css** — 437 `!important`, internal specificity wars.
3. **theme.css** — 151 `!important`, necessary for cross-theme consistency
   but includes duplicate `.btn.danger` blocks and the `*` wildcard rule
   for win_classic.
4. **external_workspace.css** — 149 `!important`, many hardcoded rgba.
5. **shell_menu.css** — 73 `!important`, bright + win_classic surface overrides.

### 2d. Bundled apps not yet polished

| App | CSS lines | `!important` | Status |
|-----|----------:|-------------:|--------|
| **NeonWave** | 843 | 1 | Has own `--nw-*` token layer with `color-mix()`, not yet migrated to shared tokens |
| **PhotoGallery** | 2,457 | 14 | 8 CSS files, vendored leaflet.css; no polish pass yet |
| **SharesMgr** | 406 | 1 | Smallest app; low override count but button/card patterns still local |
| **RAIDMgr** | 0 | 0 | No app CSS — fully inherits `theme.css` tokens (done) |
| **SnapshotMgr** | 0 | 0 | No app CSS — fully inherits `theme.css` tokens (done) |

### 2e. Unmerged branches still in progress

| Branch | Area |
|--------|------|
| `ai/ui-adopt-components-static-pages-20260610` | Adopting `.pq-*` classes into static/admin pages |
| `ai/ui-polish-admin-settings-20260610` | Admin settings page |
| `ai/ui-polish-admin-settings-fields-20260610` | Admin settings form fields |
| `ai/ui-polish-admin-audit-20260610` (remote) | Admin audit log page |

---

## 3. Theme Readiness Classification

### Fully using shared tokens (no app CSS)

| App / Area | Notes |
|------------|-------|
| RAIDMgr | Zero CSS files; 100% theme-token driven |
| SnapshotMgr | Zero CSS files; 100% theme-token driven |

### Theme-token ready (uses tokens throughout, minimal overrides)

| App / Area | `!important` | Notes |
|------------|-------------:|-------|
| SharesMgr | 1 | 406 lines, clean; adopts pq-* classes next |
| Dropzone | 3 | Own `--dz-*` layer maps to `:root` tokens |
| EchoStack | 2 | Clean after foundation pass |
| Onboarding | 4 | Small, isolated |

### Partially tokenised (mix of tokens and hardcoded values)

| App / Area | `!important` | Notes |
|------------|-------------:|-------|
| NeonWave | 1 | `--nw-*` token layer uses `color-mix()`; not yet wired to shared tokens |
| PhotoGallery | 14 | 8 CSS files; most use tokens but some hardcoded colours remain |
| File Manager | 0 | Preview files use elevated tokens well; foundation pass done |
| ReelStack | 102 | Foundation pass done; context menu (57) still override-heavy |
| Node profile | 43 | Win_classic modal overrides |
| People | 50 | Isolated page; bright/win_classic overrides |

### Still override-heavy

| App / Area | `!important` | Hardcoded colours | Notes |
|------------|-------------:|------------------:|-------|
| CircleStack | 967 | 912 | 10 polish passes done but core app.css + theme_overrides.css remain dense |
| External workspace | 149 | 385 | Large file (2,722 lines); foundation pass done but many literals remain |
| Shell menu | 73 | — | Bright + win_classic surface overrides |
| theme.css | 151 | 240 | Expected (token definitions) but includes fragile patterns: wildcard `*` rule, duplicate `.btn.danger` |

---

## 4. Recommended Next 5 PRs (smallest / safest first)

### PR 1 — Adopt `.pq-*` component classes on static/admin pages

**Scope:** Update admin HTML pages to use `.pq-btn`, `.pq-card`, `.pq-badge`
instead of ad-hoc button/card/badge styles.
**Files:** `admin.html`, `admin_users.html`, `admin_apps.html`,
`admin_settings.html`, `admin_stats.html`, `admin_approvals.html`,
`admin_updates.html`, `admin_audit.html`
**Risk:** Low — additive class adoption; existing styles remain as fallback.
**Note:** Branch `ai/ui-adopt-components-static-pages-20260610` may already
cover this.

### PR 2 — SharesMgr token migration

**Scope:** Replace local button/card/input patterns in `sharesmgr/app.css`
(406 lines) with `.pq-*` classes and shared tokens.
**Files:** `apps/bundled/sharesmgr/src/www/app.css`, `index.html`
**Risk:** Low — smallest app, 1 `!important`, isolated.

### PR 3 — NeonWave shared-token wiring

**Scope:** Rewire `--nw-*` tokens to resolve to `:root` Tier 1 tokens
instead of standalone fallback values. Remove redundant `color-scheme`
declaration.
**Files:** `apps/bundled/neonwave/src/www/app.css`
**Risk:** Low — already well-tokenised; mostly alias changes.

### PR 4 — PhotoGallery token pass

**Scope:** Replace hardcoded colours in `albums_view.css`, `stats.css`,
`compare_view.css`, `bursts.css` with shared tokens. Adopt `.pq-btn` for
action buttons.
**Files:** 7 CSS files under `apps/bundled/photogallery/src/www/`
(exclude vendored `leaflet.css`)
**Risk:** Low-medium — 14 `!important` total; mostly straightforward
substitutions.

### PR 5 — ReelStack context menu cleanup

**Scope:** Reduce `!important` count in `reelstack_context_menu.css` (57)
by restructuring selectors and adopting shared menu/elevated-surface tokens.
**Files:** `apps/bundled/reelstack/src/www/reelstack_context_menu.css`
**Risk:** Medium — context menu interacts with player overlays; requires
visual testing across all four themes.

---

## 5. Do-Not-Touch-Casually List

The following areas have non-obvious coupling and should not be modified as
part of routine UI polish without careful review and testing.

### Auth logic

- `server/src/static/login.html` — login form submission, token handling
- `server/src/static/wait_approval.html` — approval polling logic
- Any `fetch()` calls to `/api/auth/*` endpoints

**Why:** Breaking auth locks users out. CSS-only changes to these pages are
fine; do not touch `<script>` blocks or form `action` attributes.

### File operations

- `apps/bundled/filemgr/` — upload, move, copy, delete, rename, lock logic
- `server/src/static/external_workspace.css` inline JS handlers
- File version comparison logic in `file_versions_ui.css` companion JS

**Why:** File operations are destructive. UI changes must not alter event
handlers, form data, or API call parameters.

### External workspace token/access logic

- `external_workspace.html` — share-token validation, guest access flow
- Visibility checks, expiry logic, password-gate UI

**Why:** External workspace is the public-facing share surface. Broken
access logic exposes files or locks out legitimate share recipients.

### Circle Stack theme_overrides cascade

- `circlestack/theme_overrides.css` — loaded last, after `app.css`,
  `memory_nodes.css`, and `brand_logo.css`
- Uses deeply coupled `:not()` chains targeting specific class hierarchies
- Example: `:root[data-theme="bright"] .cs-modal:not(.cs-achievement-unlock-modal) :not(img):not(svg)...`

**Why:** The load order and selector specificity in this file are tightly
coupled. Reordering CSS loads, renaming classes in `app.css`, or adding
new elements inside targeted containers can silently break bright and
win_classic themes. Changes here require full four-theme visual QA.

### Media / player overlays

- `reelstack/app.css` — video player controls, fullscreen overlay
- `reelstack/reelstack_context_menu.css` — right-click menu over video
- `neonwave/app.css` — audio player transport, now-playing overlay
- `photogallery/compare_view.css` — image comparison slider

**Why:** Player overlays use `z-index` stacking, `position: fixed/absolute`,
and `pointer-events` toggling that is sensitive to layout changes. A
misplaced `overflow: hidden` or `z-index` change can make controls
unreachable or invisible.

### Visibility selector cascade

- `external_workspace.css` — `.visibility-*` classes control show/hide
  of sections based on share permissions
- `circlestack/app.css` — `.cs-visibility-*` classes for feed mode switching
- `shell_menu.css` — `.sidebar-collapsed` / `.sidebar-expanded` layout states

**Why:** These selectors control functional visibility, not decorative
styling. Overriding `display`, `visibility`, or `opacity` on these classes
can expose restricted content or hide critical UI controls.

---

## Summary

| Metric | Value |
|--------|-------|
| PRs merged | 20 (#8–#27) |
| Unmerged branches in progress | 4 |
| Shared component classes shipped | 8 (`.pq-*` v1) |
| Apps fully token-driven | 2 (RAIDMgr, SnapshotMgr) |
| Apps theme-token ready | 4 (SharesMgr, Dropzone, EchoStack, Onboarding) |
| Apps partially tokenised | 5 (NeonWave, PhotoGallery, FileManager, ReelStack, Node profile) |
| Areas still override-heavy | 4 (CircleStack, External workspace, Shell menu, theme.css) |
| Total `!important` remaining | 1,558 |
| Total hardcoded colour instances | ~2,078 (including token-definition file) |
| Recommended next PRs | 5 (see Section 4) |
