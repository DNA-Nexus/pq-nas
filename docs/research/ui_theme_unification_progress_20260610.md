# UI / Theme Unification — Progress Audit

**Date:** 2026-06-10 (updated 2026-06-11)
**Branch:** `ai/ui-theme-progress-audit-20260610`
**Companion document:** `ui_theme_unification_audit_20260610.md` (full technical audit)

---

## 1. Completed Work

### 1a. Shared components.css foundation

`server/src/static/components.css` (252 lines, v1) ships canonical `.pq-*`
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

### 1i. SharesMgr UI foundation

| PR | Area | Commit |
|----|------|--------|
| #29 | SharesMgr UI foundation | `aebff71` |

### 1j. NeonWave UI foundation

| PR | Area | Commit |
|----|------|--------|
| #30 | NeonWave UI foundation | `b8c3d04` |

### 1k. PhotoGallery UI foundation

| PR | Area | Commit |
|----|------|--------|
| #31 | PhotoGallery UI foundation | `4586a5f` |

### 1l. ReelStack context menu UI

| PR | Area | Commit |
|----|------|--------|
| #32 | ReelStack context menu UI | `73939c3` |

### 1m. External workspace second pass

| PR | Area | Commit |
|----|------|--------|
| #33 | External workspace theme tokens | `70e9f93` |

### 1n. People UI foundation

| PR | Area | Commit |
|----|------|--------|
| #34 | People UI foundation | `9b87275` |

### 1o. Trusted Devices UI

| PR | Area | Commit |
|----|------|--------|
| #35 | Trusted Devices UI | `5e2f8b9` |

### 1p. Shell menu theme tokens

| PR | Area | Commit |
|----|------|--------|
| #36 | Shell menu theme tokens | `b4f3cee` |

**Total merged: 28 PRs (#8–#27, #29–#36)**

---

## 2. Current Remaining Hotspots

### 2a. Hardcoded colours by major file

| File | Hex | RGB/RGBA | Total | Notes |
|------|----:|--------:|------:|-------|
| `circlestack/app.css` | 292 | 398 | 690 | Largest single source; unchanged |
| `external_workspace.css` | 91 | 241 | 332 | Second pass (#33) reduced from 385 |
| `dropzone/style.css` | 139 | 122 | 261 | Has `--dz-*` tokens but still many literals |
| `theme.css` | 47 | 193 | 240 | Expected — token definitions live here |
| `circlestack/theme_overrides.css` | 130 | 92 | 222 | Overrides for bright + win_classic |
| `reelstack/app.css` | 70 | 146 | 216 | Newly surfaced — foundation pass done but many literals remain |
| `reelstack/reelstack_context_menu.css` | 38 | 84 | 122 | Context menu pass (#32) reduced from ~144 |
| `node_profile.css` | 53 | 117 | 170 | Win_classic modal overrides |
| `echostack/style.css` | 14 | 76 | 90 | Foundation pass done; mostly tokenised rgba patterns |
| `neonwave/app.css` | 8 | 8 | 16 | Foundation pass (#30) reduced from ~80 |

Note: many `rgba(var(--fg-rgb), 0.xx)` calls are already tokenised. The
counts above include those patterns — the true "unresolved" literals are
lower, concentrated in CircleStack and the external workspace.

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
   4  photogallery/albums_view.css      ▏
   4  onboarding.css                    ▏
   3  dropzone/style.css                ▏
   2  echostack/style.css               ▏
   2  photogallery/bursts.css           ▏
   1  neonwave/app.css                  ▏
   1  photogallery/gallery_exif_search  ▏
   1  reelstack/reelstack_search.css    ▏
   1  sharesmgr/app.css                 ▏
──────────────────────────────────────────
1,559 total
```

Note: `!important` counts are essentially unchanged from the initial audit
(1,558). The foundation passes (#29–#36) focused on token adoption and
class migration rather than specificity reduction.

### 2c. Override-heavy files (top 5)

1. **circlestack/theme_overrides.css** — 489 `!important`, deeply coupled
   selectors with `:not()` chains; bright + win_classic only.
2. **circlestack/app.css** — 437 `!important`, internal specificity wars.
3. **theme.css** — 151 `!important`, necessary for cross-theme consistency
   but includes duplicate `.btn.danger` blocks and the `*` wildcard rule
   for win_classic.
4. **external_workspace.css** — 149 `!important`, hardcoded rgba reduced
   from 385 to 332 after second pass (#33); still dense.
5. **shell_menu.css** — 73 `!important`, token pass (#36) improved surface
   token wiring; bright + win_classic overrides remain.

### 2d. Bundled apps not yet fully polished

| App | CSS lines | `!important` | Status |
|-----|----------:|-------------:|--------|
| **RAIDMgr** | 0 | 0 | No app CSS — fully inherits `theme.css` tokens (done) |
| **SnapshotMgr** | 0 | 0 | No app CSS — fully inherits `theme.css` tokens (done) |

All other bundled apps have received at least one foundation pass (see
Section 3 for per-app readiness classification).

### 2e. Unmerged branches still in progress

None. All previously tracked branches have been merged:

| Branch | Merged as |
|--------|-----------|
| `ai/ui-adopt-components-static-pages-20260610` | PR #4 |
| `ai/ui-polish-admin-settings-20260610` | PR #6 |
| `ai/ui-polish-admin-settings-fields-20260610` | PR #7 |
| `ai/ui-polish-admin-audit-20260610` | PR #5 |

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
| NeonWave | 1 | Foundation pass (#30) wired `--nw-*` to shared tokens; colours reduced from ~80 to 16 |
| SharesMgr | 1 | Foundation pass (#29) adopted pq-* classes; 407 lines, clean |
| Dropzone | 3 | Own `--dz-*` layer maps to `:root` tokens |
| EchoStack | 2 | Clean after foundation pass |
| Onboarding | 4 | Small, isolated |

### Partially tokenised (mix of tokens and hardcoded values)

| App / Area | `!important` | Notes |
|------------|-------------:|-------|
| PhotoGallery | 14 | Foundation pass (#31) done; 8 CSS files, most use tokens but some hardcoded colours remain |
| File Manager | 0 | Preview files use elevated tokens well; foundation pass done |
| ReelStack | 102 | Foundation pass + context menu pass (#32) done; context menu (57) still override-heavy |
| Node profile | 43 | Win_classic modal overrides |
| People | 50 | Foundation pass (#34) done; bright/win_classic overrides remain |
| Trusted Devices | — | Polish pass (#35) done; styles in shell HTML, no separate CSS file |
| Shell menu | 73 | Token pass (#36) improved surface token wiring; bright/win_classic overrides remain |

### Still override-heavy

| App / Area | `!important` | Hardcoded colours | Notes |
|------------|-------------:|------------------:|-------|
| CircleStack | 967 | 912 | 10 polish passes done but core app.css + theme_overrides.css remain dense |
| External workspace | 149 | 332 | Two passes done (#12, #33); reduced from 385 colours but still dense (2,734 lines) |
| theme.css | 151 | 240 | Expected (token definitions) but includes fragile patterns: wildcard `*` rule, duplicate `.btn.danger` |

---

## 4. Recommended Next 5 PRs (smallest / safest first)

All five originally recommended PRs have been completed (#29–#33).
Updated recommendations follow.

### PR 1 — People specificity reduction

**Scope:** Reduce 50 `!important` in `people.css` (404 lines) by
restructuring selectors and leveraging shared `.pq-*` class specificity.
**Files:** `server/src/static/people.css`
**Risk:** Low — isolated page; foundation pass already done (#34).

### PR 2 — Node profile polish

**Scope:** Reduce 43 `!important` and 170 hardcoded colours in
`node_profile.css` (741 lines). Replace win_classic modal overrides with
token-based styling.
**Files:** `server/src/static/node_profile.css`
**Risk:** Low — isolated modal; no cross-app dependencies.

### PR 3 — ReelStack app.css token pass

**Scope:** Replace 216 hardcoded colours in `reelstack/app.css` (44
`!important`) with shared tokens. Adopt `.pq-btn` for transport controls.
**Files:** `apps/bundled/reelstack/src/www/app.css`
**Risk:** Medium — video player controls interact with overlay z-index;
requires visual testing across all four themes.

### PR 4 — Shell menu specificity reduction

**Scope:** Reduce 73 `!important` in `shell_menu.css` (640 lines).
Token wiring done (#36); remaining overrides are bright + win_classic
surface rules that can be restructured.
**Files:** `server/src/static/shell_menu.css`
**Risk:** Medium — sidebar layout states control visibility; changes
must not alter `.sidebar-collapsed`/`.sidebar-expanded` behaviour.

### PR 5 — External workspace deep pass

**Scope:** Reduce 149 `!important` and 332 hardcoded colours in
`external_workspace.css` (2,734 lines). Two passes done (#12, #33);
remaining literals are concentrated in permission-gated sections.
**Files:** `server/src/static/external_workspace.css`
**Risk:** Medium — public-facing share surface; external access token
and visibility logic must not be touched. CSS-only changes.

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
| PRs merged | 28 (#8–#27, #29–#36) |
| Unmerged branches in progress | 0 (all previously tracked branches merged) |
| Shared component classes shipped | 8 (`.pq-*` v1) |
| Apps fully token-driven | 2 (RAIDMgr, SnapshotMgr) |
| Apps theme-token ready | 5 (NeonWave, SharesMgr, Dropzone, EchoStack, Onboarding) |
| Apps partially tokenised | 7 (PhotoGallery, FileManager, ReelStack, Node profile, People, Trusted Devices, Shell menu) |
| Areas still override-heavy | 3 (CircleStack, External workspace, theme.css) |
| Total `!important` remaining | 1,559 |
| Total hardcoded colour instances | ~2,078 (including token-definition file) |
| Recommended next PRs | 5 (see Section 4) |
