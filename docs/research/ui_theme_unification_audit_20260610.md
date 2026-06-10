# UI / Theme Unification Audit

**Date:** 2026-06-10
**Branch:** `ai/ui-theme-unification-audit-20260610`
**Scope:** All CSS across `server/src/static/` and `apps/bundled/*/src/www/`

---

## 1. Current UI / Theme Architecture

### Theme switching

| Component | File | Role |
|-----------|------|------|
| Token definitions | `server/src/static/theme.css` | `:root` tokens + per-theme overrides via `html[data-theme]` |
| Runtime switcher | `server/src/static/theme.js` | Sets `data-theme` on `<html>`, syncs via localStorage + server API |

`theme.js` recognises four values: **dark** (default), **bright**, **cpunk_orange**, **win_classic**.

The attribute `html[data-theme="<value>"]` is the single selector all theme-aware CSS relies on.
There is no SCSS, PostCSS, or build-time preprocessing -- everything is plain CSS with custom properties.

### Loading order

1. `theme.css` is loaded first (linked in every HTML page's `<head>`).
2. `theme.js` runs immediately, applying localStorage value, then fetches server setting as authoritative override.
3. Shell CSS (`shell_menu.css`, `external_workspace.css`, etc.) is loaded per-page.
4. Bundled app CSS (`app.css`, `theme_overrides.css`, etc.) is loaded inside each app's HTML.

### How apps consume the theme

Bundled apps run **in the same document** (not iframes), so they inherit
`:root` custom properties directly. Some apps (Dropzone, NeonWave) define
a local token layer (`--dz-*`, `--nw-*`) that maps to global tokens with
fallback defaults for standalone dev use.

---

## 2. Main CSS / Theme Files and What They Control

### Server shell (`server/src/static/`)

| File | Lines | Purpose |
|------|------:|---------|
| `theme.css` | 1,035 | Global token definitions, all four theme variants, elevated-surface tokens, danger-button styling, file-manager modal win_classic overrides, folder-picker component |
| `shell_menu.css` | 636 | Sidebar, nav buttons, topbar, app frame dock, mobile breakpoints |
| `external_workspace.css` | 2,722 | External/shared workspace UI (breadcrumbs, sections, icons, file lists, previews, admin features) |
| `node_profile.css` | 741 | Node profile modal with bright + win_classic overrides |
| `people.css` | 404 | People/contacts page |
| `onboarding.css` | 274 | First-run onboarding flow |
| `admin_audit.css` | 0 | Empty (audit-log page styling lives in theme.css) |
| `nexus-presentation/assets/site.css` | 332 | Static marketing/presentation page |

### Global token categories (defined in `:root`)

| Category | Tokens |
|----------|--------|
| **Core palette** | `--bg`, `--fg`, `--fg-dim`, `--ui_is_dark` |
| **Surfaces** | `--panel`, `--panel2` |
| **Borders** | `--border`, `--border2` |
| **Effects** | `--shadow`, `--radius`, `--radius2` |
| **Status** | `--ok`, `--warn`, `--err` / `--fail`, `--info` |
| **RGB helpers** | `--fg-rgb`, `--warn-rgb`, `--fail-rgb`, `--err-rgb`, `--info-rgb` |
| **Form fields** | `--field-bg`, `--field-bg2`, `--field-fg`, `--field-ph`, `--field-border`, `--field-border2`, `--field-focus-ring` |
| **Charts** | `--chart_rx`, `--chart_tx`, `--chart_grid` |
| **Toast / popups** | `--toast-bg`, `--toast-border`, `--toast-fg`, `--toast-shadow` |
| **Elevated surfaces** | `--overlay-bg`, `--elevated-bg`, `--elevated-bg2`, `--elevated-border`, `--elevated-border2`, `--elevated-shadow`, `--elevated-soft`, `--elevated-soft-2` |
| **Menus** | `--menu-bg`, `--menu-bg2`, `--menu-border`, `--menu-shadow` |
| **File manager** | `--fm_modal_overlay` |
| **Typography** | `--sans`, `--mono` |

**Total core tokens:** ~47, each redefined in all four theme blocks.

---

## 3. App-Specific CSS Files and Their Responsibilities

### Bundled apps (`apps/bundled/*/src/www/`)

| App | File(s) | Lines | Notes |
|-----|---------|------:|-------|
| **CircleStack** | `app.css` | 4,854 | Social-network UI; largest single app CSS |
| | `theme_overrides.css` | 1,356 | Bright + win_classic fixes; 489 `!important` |
| | `memory_nodes.css` | 918 | Memory-node cards |
| | `brand_logo.css` | 30 | Logo |
| **Dropzone** | `style.css` | 1,587 | Upload UI; own `--dz-*` token layer |
| **EchoStack** | `style.css` | 1,015 | Search app |
| | `fulltext_search.css` | 113 | Full-text search panel |
| | `background.css` | 10 | Decorative background |
| **File Manager** | `file_versions_ui.css` | 184 | Version comparison modal |
| | `pdf_preview.css` | 120 | PDF viewer |
| | `video_preview.css` | 130 | Video viewer |
| | `audio_preview.css` | 171 | Audio player |
| | `office_preview.css` | 135 | Office document viewer |
| | `filemgr_storage_insights.css` | 54 | Storage stats |
| **NeonWave** | `app.css` | 843 | Music player; own `--nw-*` token layer with `color-mix()` |
| **Photo Gallery** | `albums_view.css` | 559 | Album grid |
| | `albums_picker.css` | 238 | Album selection modal |
| | `compare_view.css` | 168 | Image comparison |
| | `bursts.css` | 207 | Burst photos |
| | `stats.css` | 336 | Photo statistics |
| | `gallery_exif_search.css` | 90 | EXIF search |
| | `gallery_map.css` | 198 | Map view |
| | `leaflet.css` | 661 | Leaflet map library (vendored) |
| **ReelStack** | `app.css` | 1,193 | Video app |
| | `reelstack_context_menu.css` | 789 | Context menu (57 `!important`) |
| | `reelstack_search.css` | 175 | Search overlay |
| **SharesMgr** | `app.css` | 406 | Shares management |

**Total CSS:** 34 files, ~22,350 lines.

---

## 4. Repeated or Conflicting Styles

### 4a. Button patterns duplicated across files

The `.btn` base pattern (padding, border-radius, border, background, cursor, transition) is independently defined in:

- `shell_menu.css` (shell navigation buttons)
- `sharesmgr/app.css` (`.btn`, `.btnPrimary`, `.btnDanger`, `.btnGhost`)
- `reelstack/app.css`
- `dropzone/style.css`
- `circlestack/app.css`
- `theme.css` (danger button, folder-picker buttons)

Values are close but not identical (e.g., `border-radius` varies between 10px, 12px, 14px; padding varies between `10px 12px` and `10px 13px`).

### 4b. Card / modal / panel surfaces

The glass-card pattern (border-radius 16-24px, 1px border, linear-gradient + rgba background, box-shadow) appears in:

- `theme.css` (`.modalCard`, `.card`, `.uiDialog`, `.raidDialogInnerCard`, `.fmMoveCard`)
- `shell_menu.css` (`.card`, `.statusCard`)
- `external_workspace.css` (`.card`, sections)
- `circlestack/app.css` (`.cs-modal`, custom cards)
- `sharesmgr/app.css` (cards)
- `node_profile.css` (modal windows)

### 4c. Input / form field styling

Form field base style (background, border, color, padding, border-radius, focus ring) is redefined in:

- `theme.css` (field tokens + win_classic overrides)
- `dropzone/style.css` (via `--dz-field-*` tokens)
- `circlestack/app.css` (inline field styling)
- `external_workspace.css` (search inputs)

### 4d. Badge / pill patterns

The pill/badge pattern (border-radius 999px, padding, border, background) appears in:

- `shell_menu.css`
- `sharesmgr/app.css`
- `external_workspace.css`
- `theme.css` (win_classic pill overrides)

### 4e. Sidebar / navigation

Sidebar layout (flex column, background, nav button styling with active/hover states) is defined in `shell_menu.css` and partially re-declared in `external_workspace.css` and `node_profile.css`.

### 4f. `color-scheme` declarations

Multiple files independently declare `color-scheme`:

- `neonwave/app.css`
- `reelstack/app.css`
- `dropzone/style.css`
- `echostack/style.css` (implied by body styles)

These should be handled by `theme.css` alone.

---

## 5. List of Risky Overrides

### 5a. `!important` usage by file

| File | Count | Risk |
|------|------:|------|
| `circlestack/theme_overrides.css` | 489 | **HIGH** -- largest override file; tightly coupled to app.css class names |
| `circlestack/app.css` | 437 | **HIGH** -- app-internal importance wars |
| `theme.css` | 151 | **MEDIUM** -- necessary for cross-theme consistency but fragile |
| `external_workspace.css` | 149 | **MEDIUM** -- hardcoded rgba values alongside token usage |
| `shell_menu.css` | 73 | **MEDIUM** -- bright + win_classic surface overrides |
| `reelstack/reelstack_context_menu.css` | 57 | **MEDIUM** -- context menu theme fixes |
| `people.css` | 50 | **LOW** -- isolated page |
| `node_profile.css` | 43 | **LOW** -- win_classic modal overrides |
| `reelstack/app.css` | 44 | **LOW** |
| `circlestack/memory_nodes.css` | 41 | **LOW** |
| All other files | <10 each | **LOW** |
| **Total** | **~1,558** | |

### 5b. Hardcoded colour values (not using tokens)

| File | Hardcoded hex values |
|------|---------------------:|
| `circlestack/app.css` | ~333 |
| `circlestack/theme_overrides.css` | ~176 |
| `external_workspace.css` | ~81 |

These are the biggest risks: any future theme addition or palette change
will not propagate to these values automatically.

### 5c. Win Classic wildcard kill rule

```css
/* theme.css:284-286 */
html[data-theme="win_classic"] * {
    text-shadow: none !important;
    backdrop-filter: none !important;
}
```

This `*` selector fires on every element and overrides any future intentional use of `text-shadow` or `backdrop-filter` in win_classic mode. It works today but is a maintenance trap.

### 5d. Deep coupling in CircleStack theme_overrides.css

CircleStack's override file uses deeply coupled selectors like:

```css
:root[data-theme="bright"] .cs-modal:not(.cs-achievement-unlock-modal)
    :not(img):not(svg):not(path):not(circle):not(rect) { ... }
```

Any refactoring of CircleStack's HTML structure will silently break these rules.

### 5e. Duplicate danger-button rules

`.btn.danger` is defined twice in `theme.css` (lines 439-471 and 799-828) because the second block needs higher specificity (`html[data-theme]`) to beat win_classic button overrides. This is fragile and confusing.

---

## 6. Suggested Design-Token / CSS Variable Structure

### Goals

1. Every colour in every file should resolve through a token -- zero hardcoded hex in app CSS.
2. Apps should never need `!important` to match the active theme.
3. Adding a fifth theme should require editing only `theme.css`.

### Proposed token tiers

```
Tier 1 -- Semantic primitives  (defined in :root, overridden per theme)
    --color-bg
    --color-fg
    --color-fg-muted
    --color-fg-rgb            (for rgba mixing)

    --color-surface-1         (replaces --panel)
    --color-surface-2         (replaces --panel2)
    --color-surface-elevated  (replaces --elevated-bg)
    --color-surface-overlay   (replaces --overlay-bg)

    --color-border
    --color-border-muted
    --color-divider           (new: 1px separators, replaces ad-hoc rgba)

    --color-status-ok
    --color-status-warn
    --color-status-error
    --color-status-info
    --color-status-ok-rgb     (etc.)

    --color-accent            (primary interactive colour)
    --color-accent-rgb

Tier 2 -- Component tokens  (resolve to Tier 1; defined once in theme.css)
    --btn-bg
    --btn-fg
    --btn-border
    --btn-radius
    --btn-danger-bg
    --btn-danger-fg

    --card-bg
    --card-border
    --card-radius
    --card-shadow

    --input-bg
    --input-fg
    --input-border
    --input-focus-ring

    --modal-overlay
    --modal-bg
    --modal-border
    --modal-shadow

    --menu-bg
    --menu-border
    --menu-shadow

    --sidebar-bg
    --nav-btn-radius
    --nav-btn-active-bg

    --pill-bg
    --pill-border
    --pill-radius

    --toast-bg
    --toast-border
    --toast-fg

Tier 3 -- App-scoped tokens  (only where truly needed)
    --nw-accent      (NeonWave accent, maps to --color-accent)
    --dz-accent      (Dropzone, maps to --color-accent)
```

### Migration principle

Keep the **current token names** as aliases during migration:

```css
:root {
    /* New canonical name */
    --color-bg: #050712;
    /* Old name kept as alias (remove after all consumers migrate) */
    --bg: var(--color-bg);
}
```

This avoids a big-bang rename and lets files migrate incrementally.

---

## 7. Safe Staged Migration Plan

### Stage 0 -- Audit (this document)

Delivered. No code changes.

### Stage 1 -- Extract shared component classes

Create a new file `server/src/static/components.css`, loaded after `theme.css`,
containing canonical `.btn`, `.card`, `.pill`, `.input-field`, `.modal-overlay`,
and `.modal-card` classes.

- Source the patterns from the current `theme.css` + `shell_menu.css`.
- Each class uses only existing `:root` tokens.
- **No app CSS is changed yet.** Shell pages are updated to use the new classes.

**Risk:** Very low. New file, additive only.

### Stage 2 -- Introduce Tier 2 component tokens

Add component-level tokens (e.g., `--btn-bg`, `--card-bg`) to `theme.css`, defined
per theme block. Update `components.css` to consume them.

**Risk:** Low. Tokens are additive; nothing breaks if unused.

### Stage 3 -- Migrate shell CSS to component tokens

Replace hardcoded rgba / hex values in `shell_menu.css`, `external_workspace.css`,
`node_profile.css`, `people.css`, `onboarding.css` with Tier 1/2 tokens. Remove
corresponding `!important` where specificity is no longer needed.

**Risk:** Medium. Requires visual regression testing across all four themes.

### Stage 4 -- Migrate bundled apps (one at a time)

For each app (starting with the simplest):

1. **SharesMgr** -- smallest, fewest overrides.
2. **EchoStack** -- small, few hardcoded colours.
3. **File Manager** -- already uses elevated tokens well.
4. **NeonWave** -- has its own token layer; rewire `--nw-*` to Tier 1.
5. **Dropzone** -- has its own token layer; rewire `--dz-*` to Tier 1.
6. **Photo Gallery** -- moderate size, few overrides.
7. **ReelStack** -- context menu has 57 `!important`; needs care.
8. **CircleStack** -- last, largest, most overrides (489+437 `!important`).

For each app: replace hardcoded colours with tokens, adopt `components.css`
classes where applicable, delete app-local button/card/input re-declarations,
remove `!important` where possible.

**Risk per app:** Medium, but isolated -- each app can be a separate PR.

### Stage 5 -- Consolidate theme overrides

After all apps consume tokens:

- Delete `circlestack/theme_overrides.css` (its rules should be unnecessary).
- Remove duplicate `color-scheme` declarations from app CSS files.
- Remove the `html[data-theme="win_classic"] *` wildcard rule in favour of
  targeted no-effect rules only where needed.
- Remove the duplicate `.btn.danger` block in `theme.css`.

**Risk:** Medium. Requires full visual QA across all themes and all apps.

### Stage 6 -- Rename tokens to final names (optional)

If the team wants cleaner names (`--color-bg` instead of `--bg`), do the
alias-based rename described in Section 6. Old names can be removed in a
follow-up once grep confirms zero consumers.

---

## 8. Smallest Low-Risk First Patch

**Goal:** Extract a shared `components.css` with canonical `.pq-btn` classes.

### What the patch does

1. Create `server/src/static/components.css` (~60 lines) defining:
   - `.pq-btn` -- base button (uses `--field-bg`, `--border`, `--fg`, `--radius`)
   - `.pq-btn--primary` -- filled accent button
   - `.pq-btn--danger` -- red danger button (consolidates the two duplicate blocks from `theme.css`)
   - `.pq-btn--ghost` -- transparent button

2. Add a `<link>` to `components.css` in the shell's HTML `<head>`, after `theme.css`.

3. **No existing classes are modified.** Old `.btn` continues to work. The new
   `.pq-btn` classes are additive and can be adopted file-by-file.

### Why this is safe

- Pure addition: no existing CSS is edited or removed.
- No `!important` needed: the new classes are designed with low specificity
  that works with the token system.
- Easily revertible: delete the file and the `<link>` tag.
- Gives immediate value: any new feature work can use `.pq-btn` instead of
  re-inventing button styles.

### What to test

- Visually confirm the new `.pq-btn` variants look correct in all four themes.
- Confirm no existing page appearance changes (since no old classes are touched).

---

## Appendix A -- Full CSS File Inventory

```
server/src/static/
    theme.css                             1,035 lines
    theme.js                                 54 lines
    shell_menu.css                          636 lines
    external_workspace.css                2,722 lines
    node_profile.css                        741 lines
    people.css                              404 lines
    onboarding.css                          274 lines
    admin_audit.css                           0 lines  (empty)
    nexus-presentation/assets/site.css      332 lines

apps/bundled/circlestack/src/www/
    app.css                               4,854 lines
    theme_overrides.css                   1,356 lines
    memory_nodes.css                        918 lines
    brand_logo.css                           30 lines

apps/bundled/dropzone/src/www/
    style.css                             1,587 lines

apps/bundled/echostack/src/www/
    style.css                             1,015 lines
    fulltext_search.css                     113 lines
    background.css                           10 lines

apps/bundled/filemgr/src/www/
    file_versions_ui.css                    184 lines
    pdf_preview.css                         120 lines
    video_preview.css                       130 lines
    audio_preview.css                       171 lines
    office_preview.css                      135 lines
    filemgr_storage_insights.css             54 lines

apps/bundled/neonwave/src/www/
    app.css                                 843 lines

apps/bundled/photogallery/src/www/
    albums_view.css                         559 lines
    albums_picker.css                       238 lines
    compare_view.css                        168 lines
    bursts.css                              207 lines
    stats.css                               336 lines
    gallery_exif_search.css                  90 lines
    gallery_map.css                         198 lines
    leaflet.css                             661 lines  (vendored)

apps/bundled/reelstack/src/www/
    app.css                               1,193 lines
    reelstack_context_menu.css              789 lines
    reelstack_search.css                    175 lines

apps/bundled/sharesmgr/src/www/
    app.css                                 406 lines

TOTAL                                    ~22,350 lines across 34 files
```

## Appendix B -- `!important` Heat Map

```
 489  circlestack/theme_overrides.css     ████████████████████████████████████
 437  circlestack/app.css                 ████████████████████████████████
 151  theme.css                           ████████████
 149  external_workspace.css              ████████████
  73  shell_menu.css                      ██████
  57  reelstack/reelstack_context_menu.css████
  50  people.css                          ████
  44  reelstack/app.css                   ███
  43  node_profile.css                    ███
  41  circlestack/memory_nodes.css        ███
   6  photogallery/leaflet.css            ▏
   4  onboarding.css                      ▏
   4  photogallery/albums_view.css        ▏
   3  dropzone/style.css                  ▏
   2  echostack/style.css                 ▏
   2  photogallery/bursts.css             ▏
   1  neonwave/app.css                    ▏
   1  photogallery/gallery_exif_search.css▏
   1  reelstack/reelstack_search.css      ▏
   1  sharesmgr/app.css                   ▏
────────────────────────────────────────────
1,558 total
```
