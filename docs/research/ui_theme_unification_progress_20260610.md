# UI / Theme Unification — Progress Audit

**Date:** 2026-06-10 (updated 2026-06-12)
**Branch:** `ai/ui-theme-progress-audit-fix-next-prs-20260612`
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

### 1v. Dropzone colour token pass

| PR | Area | Commit |
|----|------|--------|
| #44 | Dropzone colour tokens | `0697582` |

Wired additional colour tokens into `dropzone/style.css` (1,597 lines).
Hardcoded colour count reduced from 261 to 250. `--dz-*` token layer
fully mapped to `:root` tokens.

### 1w. EchoStack colour token pass

| PR | Area | Commit |
|----|------|--------|
| #45 | EchoStack colour tokens | `d2a7c38` |

Extended tokenised patterns in `echostack/style.css` (1,069 lines).
Hardcoded colour count reduced from 90 to 84; 51 tokenised `rgba(var(`
patterns now in place.

### 1x. PhotoGallery colours & specificity pass

| PR | Area | Commit |
|----|------|--------|
| #46 | PhotoGallery colours & specificity | `8e7d752` |

Reduced non-vendored `!important` from 7 to 3 across PhotoGallery CSS
files (2,449 lines total). Eliminated all 4 `!important` in
`albums_view.css`.

### 1y. ReelStack context menu specificity reduction

| PR | Area | Commit |
|----|------|--------|
| #47 | ReelStack context menu specificity | `6bcfe60` |

Reduced `!important` from 57 to 14 in `reelstack_context_menu.css` (821
lines). Restructured selectors to leverage existing class specificity
while preserving 63 tokenised `rgba(var(` patterns.

### 1z. CircleStack memory_nodes specificity reduction

| PR | Area | Commit |
|----|------|--------|
| #48 | CircleStack memory_nodes specificity | `77cad8b` |

Reduced `!important` from 41 to 23 in `memory_nodes.css` (940 lines).
Restructured selectors while preserving four-theme visual compatibility
and 27 tokenised `rgba(var(` patterns.

**Total merged: 38 PRs (#8–#27, #29–#36, #38–#48)**

---

## 2. Current Remaining Hotspots

### 2a. Hardcoded colours by major file

| File | Hex | RGB/RGBA | Total | Notes |
|------|----:|--------:|------:|-------|
| `circlestack/app.css` | 292 | 398 | 690 | Largest single source; unchanged |
| `dropzone/style.css` | 139 | 111 | 250 | Colour token pass (#44) reduced from 261; `--dz-*` layer wired |
| `external_workspace.css` | 53 | 190 | 243 | Deep pass (#42) reduced from 332; 7 tokenised |
| `theme.css` | 47 | 193 | 240 | Expected — token definitions live here |
| `circlestack/theme_overrides.css` | 130 | 92 | 222 | Overrides for bright + win_classic |
| `reelstack/app.css` | 70 | 142 | 212 | Token pass (#40) added 47 tokenised patterns |
| `node_profile.css` | 53 | 117 | 170 | Polish pass (#39) reduced !important to 1; colours unchanged |
| `reelstack/reelstack_context_menu.css` | 38 | 84 | 122 | 63 tokenised patterns already adopted |
| `circlestack/memory_nodes.css` | 2 | 83 | 85 | 27 tokenised patterns |
| `echostack/style.css` | 13 | 71 | 84 | Colour token pass (#45) reduced from 90; 51 tokenised patterns |
| `neonwave/app.css` | 8 | 8 | 16 | Foundation pass (#30) reduced from ~80 |

Note: the RGB/RGBA column includes tokenised `rgba(var(--*-rgb), 0.xx)`
calls. The true "unresolved" literals are lower; 546 tokenised patterns
exist across 25 files. The largest unresolved concentrations are in
CircleStack (690) and Dropzone (250).

### 2b. `!important` counts by major file

```
 489  circlestack/theme_overrides.css   ████████████████████████████████████
 437  circlestack/app.css               ████████████████████████████████
 151  theme.css                         ████████████
  83  external_workspace.css            ██████       (was 149)
  58  shell_menu.css                    ████         (was 73)
  44  reelstack/app.css                 ███
  23  circlestack/memory_nodes.css      ██           (was 41)
  14  reelstack/reelstack_context_menu  █            (was 57)
   6  photogallery/leaflet.css          ▏  (vendored — ignore)
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
1,321 total                               (was 1,386; −65)
```

Reductions from PRs #38–#48:
- `people.css`: 50 → 0 (eliminated)
- `node_profile.css`: 43 → 1 (−42)
- `external_workspace.css`: 149 → 83 (−66)
- `shell_menu.css`: 73 → 58 (−15)
- `photogallery/albums_view.css`: 4 → 0 (eliminated)
- `reelstack_context_menu.css`: 57 → 14 (−43)
- `circlestack/memory_nodes.css`: 41 → 23 (−18)

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
| EchoStack | 2 | Colour token pass (#45) reduced colours from 90 to 84; 51 tokenised rgba patterns |
| Dropzone | 3 | Colour token pass (#44) reduced colours from 261 to 250; `--dz-*` layer wired to `:root` tokens |
| PhotoGallery | 3 | Colours & specificity pass (#46) reduced non-vendored `!important` from 7 to 3; 8 files, most use tokens |
| Onboarding | 4 | Small, isolated |

### Partially tokenised (mix of tokens and hardcoded values)

| App / Area | `!important` | Notes |
|------------|-------------:|-------|
| File Manager | 0 | Preview files use elevated tokens well; foundation pass done |
| ReelStack | 59 | Foundation + context menu + token + specificity passes (#17, #32, #40, #47); context menu reduced from 57 to 14 |
| External workspace | 83 | Three passes done (#12, #33, #42); reduced from 149 `!important` and 332 to 243 colours |
| Shell menu | 58 | Token + specificity passes (#36, #41); 54 tokenised rgba patterns; bright/win_classic overrides remain |
| Trusted Devices | — | Polish pass (#35) done; styles in shell HTML, no separate CSS file |

### Still override-heavy

| App / Area | `!important` | Hardcoded colours | Notes |
|------------|-------------:|------------------:|-------|
| CircleStack | 949 | 997 | 11 passes done but core app.css (437) + theme_overrides.css (489) + memory_nodes (23) remain dense |
| theme.css | 151 | 240 | Expected (token definitions) but includes fragile patterns: wildcard `*` rule, duplicate `.btn.danger` |

---

## 4. Recommended Next 5 PRs (smallest / safest first)

All five previously recommended PRs (#44–#48) have been completed.
Updated recommendations follow, based on current codebase state.

### PR 1 — CircleStack app.css small specificity pass

**Scope:** Target the most straightforward `!important` declarations in
`circlestack/app.css` (5,011 lines, 437 `!important`). Focus on
declarations where class specificity alone is sufficient — button states,
card backgrounds, modal surfaces — without touching deeply coupled
cascade paths.
**Files:** `apps/bundled/circlestack/src/www/app.css`
**Risk:** Medium — CircleStack is the largest CSS surface; changes must
preserve four-theme visual compatibility. Keep scope small (target 30–50
reductions).

### PR 2 — CircleStack theme_overrides.css audit-only pass

**Scope:** Document the 489 `!important` declarations in
`theme_overrides.css` (1,380 lines) into categories: safe to remove,
requires selector restructuring, and structurally necessary. This is a
preparation pass — no runtime changes, only documentation and annotation
for future targeted work.
**Files:** `apps/bundled/circlestack/src/www/theme_overrides.css` (audit
output to docs)
**Risk:** None — documentation only. The file is in the
Do-Not-Touch-Casually list; this PR maps the territory before making
changes.

### PR 3 — theme.css duplicate/fragile override cleanup

**Scope:** Address known fragile patterns in `theme.css` (1,035 lines,
151 `!important`): remove duplicate `.btn.danger` blocks, audit the `*`
wildcard rule for win_classic, and consolidate redundant property
declarations.
**Files:** `server/src/static/theme.css`
**Risk:** Medium — theme.css affects all pages. Changes are narrowly
scoped to identified duplicates and the wildcard rule. Requires
four-theme visual QA.

### PR 4 — ReelStack app.css remaining specificity cleanup

**Scope:** Reduce 44 `!important` in `reelstack/app.css` (1,230 lines).
With the context menu already cleaned up (57 → 14 in PR #47), the main
app stylesheet is the next target. 47 tokenised rgba patterns are
already in place.
**Files:** `apps/bundled/reelstack/src/www/app.css`
**Risk:** Medium — video player controls and fullscreen overlay require
careful z-index and pointer-events testing.

### PR 5 — External workspace permission-gated override audit

**Scope:** Audit the remaining 83 `!important` in
`external_workspace.css` (2,748 lines) after three prior passes (#12,
#33, #42). Map which overrides are structurally required by
permission-gated visibility logic vs. which can be safely removed or
replaced with selector restructuring.
**Files:** `server/src/static/external_workspace.css`
**Risk:** Medium — external workspace is the public-facing share surface;
visibility-class overrides control access-gated content display.

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
| PRs merged | 38 (#8–#27, #29–#36, #38–#48) |
| Unmerged branches in progress | 0 |
| Shared component classes shipped | 8 (`.pq-*` v1) |
| Apps fully token-driven | 2 (RAIDMgr, SnapshotMgr) |
| Apps theme-token ready | 8 (People, NeonWave, Node profile, SharesMgr, EchoStack, Dropzone, PhotoGallery, Onboarding) |
| Apps partially tokenised | 5 (FileManager, ReelStack, External workspace, Shell menu, Trusted Devices) |
| Areas still override-heavy | 2 (CircleStack, theme.css) |
| Total `!important` remaining | 1,321 (was 1,386; −65) |
| Total hardcoded colour instances | ~2,334 (top-11 files; includes tokenised rgba patterns) |
| Tokenised `rgba(var(` patterns | 546 across 25 files |
| Recommended next PRs | 5 (see Section 4) |
