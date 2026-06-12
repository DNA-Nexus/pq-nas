# UI / Theme Unification — Progress Audit

**Date:** 2026-06-10 (updated 2026-06-12)
**Branch:** `ai/ui-theme-progress-audit-update-20260611`
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

### 1q. People specificity reduction

| PR | Area | Commit |
|----|------|--------|
| #38 | People specificity reduction | `7f05cbb` |

Eliminated all 50 `!important` declarations in `people.css` by
restructuring selectors and leveraging shared `.pq-*` class specificity.
File now has zero `!important` and 31 tokenised `rgba(var(` patterns.

### 1r. Node profile UI polish

| PR | Area | Commit |
|----|------|--------|
| #39 | Node profile polish | `dc8ae67` |

Reduced `!important` from 43 to 1 in `node_profile.css` (742 lines).
Replaced win_classic modal overrides with token-based styling. Added 19
tokenised `rgba(var(` patterns.

### 1s. ReelStack app.css theme token pass

| PR | Area | Commit |
|----|------|--------|
| #40 | ReelStack app.css token pass | `2de8a6c` |

Wired 47 tokenised `rgba(var(` patterns into `reelstack/app.css` (1,230
lines). Hardcoded colour count reduced from 216 to 212. Adopted `.pq-btn`
for transport controls.

### 1t. Shell menu specificity reduction

| PR | Area | Commit |
|----|------|--------|
| #41 | Shell menu specificity reduction | `65e55e8` |

Reduced `!important` from 73 to 58 in `shell_menu.css` (640 lines).
Added 56 tokenised `rgba(var(` patterns. Hardcoded hex colours reduced
from ~30 to 2.

### 1u. External workspace CSS deep pass

| PR | Area | Commit |
|----|------|--------|
| #42 | External workspace deep pass | `b98dd1a` |

Third pass on `external_workspace.css` (2,748 lines). Reduced `!important`
from 149 to 83 and hardcoded colours from 332 to 243. Added 7 tokenised
`rgba(var(` patterns. Remaining overrides concentrated in permission-gated
sections.

**Total merged: 33 PRs (#8–#27, #29–#36, #38–#42)**

---

## 2. Current Remaining Hotspots

### 2a. Hardcoded colours by major file

| File | Hex | RGB/RGBA | Total | Notes |
|------|----:|--------:|------:|-------|
| `circlestack/app.css` | 292 | 398 | 690 | Largest single source; unchanged |
| `dropzone/style.css` | 139 | 122 | 261 | Has `--dz-*` tokens but still many literals |
| `external_workspace.css` | 53 | 190 | 243 | Deep pass (#42) reduced from 332; 7 tokenised |
| `theme.css` | 47 | 193 | 240 | Expected — token definitions live here |
| `circlestack/theme_overrides.css` | 130 | 92 | 222 | Overrides for bright + win_classic |
| `reelstack/app.css` | 70 | 142 | 212 | Token pass (#40) added 47 tokenised patterns |
| `node_profile.css` | 53 | 117 | 170 | Polish pass (#39) reduced !important to 1; colours unchanged |
| `reelstack/reelstack_context_menu.css` | 38 | 84 | 122 | 63 tokenised patterns already adopted |
| `echostack/style.css` | 14 | 76 | 90 | Foundation pass done; 52 tokenised patterns |
| `circlestack/memory_nodes.css` | 2 | 83 | 85 | 30 tokenised patterns |
| `neonwave/app.css` | 8 | 8 | 16 | Foundation pass (#30) reduced from ~80 |

Note: the RGB/RGBA column includes tokenised `rgba(var(--*-rgb), 0.xx)`
calls. The true "unresolved" literals are lower; 551 tokenised patterns
exist across 25 files. The largest unresolved concentrations are in
CircleStack (690) and Dropzone (261).

### 2b. `!important` counts by major file

```
 489  circlestack/theme_overrides.css   ████████████████████████████████████
 437  circlestack/app.css               ████████████████████████████████
 151  theme.css                         ████████████
  83  external_workspace.css            ██████       (was 149)
  58  shell_menu.css                    ████         (was 73)
  57  reelstack/reelstack_context_menu  ████
  44  reelstack/app.css                 ███
  41  circlestack/memory_nodes.css      ███
   6  photogallery/leaflet.css          ▏  (vendored — ignore)
   4  photogallery/albums_view.css      ▏
   4  onboarding.css                    ▏
   3  dropzone/style.css                ▏
   2  echostack/style.css               ▏
   2  photogallery/bursts.css           ▏
   1  neonwave/app.css                  ▏
   1  node_profile.css                  ▏  (was 43)
   1  photogallery/gallery_exif_search  ▏
   1  reelstack/reelstack_search.css    ▏
   1  sharesmgr/app.css                 ▏
──────────────────────────────────────────
1,386 total                               (was 1,559; −173)
```

Reductions from PRs #38–#42:
- `people.css`: 50 → 0 (eliminated)
- `node_profile.css`: 43 → 1 (−42)
- `external_workspace.css`: 149 → 83 (−66)
- `shell_menu.css`: 73 → 58 (−15)

### 2c. Override-heavy files (top 5)

1. **circlestack/theme_overrides.css** — 489 `!important`, deeply coupled
   selectors with `:not()` chains; bright + win_classic only.
2. **circlestack/app.css** — 437 `!important`, internal specificity wars.
3. **theme.css** — 151 `!important`, necessary for cross-theme consistency
   but includes duplicate `.btn.danger` blocks and the `*` wildcard rule
   for win_classic.
4. **external_workspace.css** — 83 `!important` (down from 149),
   hardcoded colours reduced from 332 to 243 after deep pass (#42);
   remaining overrides in permission-gated sections.
5. **shell_menu.css** — 58 `!important` (down from 73), token pass (#36)
   + specificity pass (#41) improved wiring; bright + win_classic
   overrides remain.

### 2d. Bundled apps not yet fully polished

| App | CSS lines | `!important` | Status |
|-----|----------:|-------------:|--------|
| **RAIDMgr** | 0 | 0 | No app CSS — fully inherits `theme.css` tokens (done) |
| **SnapshotMgr** | 0 | 0 | No app CSS — fully inherits `theme.css` tokens (done) |

All other bundled apps have received at least one foundation pass (see
Section 3 for per-app readiness classification).

### 2e. Unmerged branches still in progress

None. All previously tracked branches have been merged.

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
| People | 0 | Specificity pass (#38) eliminated all 50 `!important`; 31 tokenised rgba patterns |
| NeonWave | 1 | Foundation pass (#30) wired `--nw-*` to shared tokens; colours reduced from ~80 to 16 |
| Node profile | 1 | Polish pass (#39) reduced from 43; 19 tokenised rgba patterns |
| SharesMgr | 1 | Foundation pass (#29) adopted pq-* classes; 407 lines, clean |
| Dropzone | 3 | Own `--dz-*` layer maps to `:root` tokens |
| EchoStack | 2 | Clean after foundation pass; 52 tokenised rgba patterns |
| Onboarding | 4 | Small, isolated |

### Partially tokenised (mix of tokens and hardcoded values)

| App / Area | `!important` | Notes |
|------------|-------------:|-------|
| PhotoGallery | 14 | Foundation pass (#31) done; 8 CSS files, most use tokens but some hardcoded colours remain |
| File Manager | 0 | Preview files use elevated tokens well; foundation pass done |
| ReelStack | 102 | Foundation + context menu + token passes done (#17, #32, #40); context menu (57) still override-heavy |
| External workspace | 83 | Three passes done (#12, #33, #42); reduced from 149 `!important` and 332 to 243 colours |
| Shell menu | 58 | Token + specificity passes (#36, #41); 56 tokenised rgba patterns; bright/win_classic overrides remain |
| Trusted Devices | — | Polish pass (#35) done; styles in shell HTML, no separate CSS file |

### Still override-heavy

| App / Area | `!important` | Hardcoded colours | Notes |
|------------|-------------:|------------------:|-------|
| CircleStack | 967 | 997 | 10 polish passes done but core app.css (437) + theme_overrides.css (489) + memory_nodes (41) remain dense |
| theme.css | 151 | 240 | Expected (token definitions) but includes fragile patterns: wildcard `*` rule, duplicate `.btn.danger` |

---

## 4. Recommended Next 5 PRs (smallest / safest first)

All five previously recommended PRs have been completed (#38–#42).
Updated recommendations follow, based on current codebase state.

### PR 1 — Dropzone colour tokenisation

**Scope:** Replace 261 hardcoded colours in `dropzone/style.css` (1,597
lines, only 3 `!important`). The `--dz-*` token layer already maps to
`:root`; remaining hex/rgba literals can be wired to existing tokens.
**Files:** `apps/bundled/dropzone/src/www/style.css`
**Risk:** Low — isolated upload surface; token layer already proven.

### PR 2 — EchoStack colour cleanup

**Scope:** Replace remaining 38 non-tokenised colour values in
`echostack/style.css` (1,054 lines, only 2 `!important`). 52 tokenised
patterns are already in place — extend the same approach to the
remaining hex (14) and non-tokenised rgba (24) values.
**Files:** `apps/bundled/echostack/src/www/style.css`
**Risk:** Low — foundation pass done; small, well-contained file.

### PR 3 — PhotoGallery colour & specificity pass

**Scope:** Reduce 14 `!important` across 8 PhotoGallery CSS files (2,423
lines total). Replace remaining hardcoded colours in `albums_view.css`
(29 total), `stats.css` (40 total), and other gallery CSS files.
**Files:** `apps/bundled/photogallery/src/www/*.css`
**Risk:** Low — isolated app; 8 small files, each independently testable.

### PR 4 — ReelStack context menu specificity reduction

**Scope:** Reduce 57 `!important` in `reelstack_context_menu.css` (821
lines). Token pass (#32) already wired 63 tokenised rgba patterns;
remaining `!important` declarations can be replaced by restructuring
selectors to leverage existing class specificity.
**Files:** `apps/bundled/reelstack/src/www/reelstack_context_menu.css`
**Risk:** Medium — context menu overlays video player; z-index stacking
and `pointer-events` toggling require visual testing.

### PR 5 — CircleStack memory_nodes specificity reduction

**Scope:** Reduce 41 `!important` and 85 hardcoded colours in
`memory_nodes.css` (952 lines). 30 tokenised patterns already in place.
This is the smallest CircleStack file and the safest entry point into
the CircleStack specificity reduction effort.
**Files:** `apps/bundled/circlestack/src/www/memory_nodes.css`
**Risk:** Medium — memory node rendering interacts with CircleStack
`app.css` cascade; changes require four-theme visual QA.

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
| PRs merged | 33 (#8–#27, #29–#36, #38–#42) |
| Unmerged branches in progress | 0 |
| Shared component classes shipped | 8 (`.pq-*` v1) |
| Apps fully token-driven | 2 (RAIDMgr, SnapshotMgr) |
| Apps theme-token ready | 7 (People, NeonWave, Node profile, SharesMgr, Dropzone, EchoStack, Onboarding) |
| Apps partially tokenised | 6 (PhotoGallery, FileManager, ReelStack, External workspace, Shell menu, Trusted Devices) |
| Areas still override-heavy | 2 (CircleStack, theme.css) |
| Total `!important` remaining | 1,386 (was 1,559; −173) |
| Total hardcoded colour instances | ~2,351 (top-11 files; includes tokenised rgba patterns) |
| Tokenised `rgba(var(` patterns | 551 across 25 files |
| Recommended next PRs | 5 (see Section 4) |
