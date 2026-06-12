# UI / Theme Unification — Progress Audit

**Date:** 2026-06-10 (updated 2026-06-12, final)
**Branch:** `ai/ui-theme-progress-final-audit-20260612`
**Companion documents:**
- `ui_theme_unification_audit_20260610.md` (full technical audit)
- `circlestack_theme_overrides_audit_20260612.md` (theme_overrides section map)
- `external_workspace_permission_overrides_audit_20260612.md` (permission override map)

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

### 1h. Circle Stack passes (10 PRs)

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

### 1aa. CircleStack app.css small specificity pass

| PR | Area | Commit |
|----|------|--------|
| #50 | CircleStack app.css specificity | `dd98d36` |

Reduced `!important` from 437 to 392 (−45) in `circlestack/app.css`
(5,011 lines). Targeted straightforward declarations where class
specificity alone was sufficient — button states, card backgrounds, modal
surfaces — without touching deeply coupled cascade paths.

### 1ab. CircleStack theme_overrides audit-only pass

| PR | Area | Commit |
|----|------|--------|
| #51 | CircleStack theme_overrides audit | `4c9f0b3` |

Documentation-only audit of all 489 `!important` declarations in
`theme_overrides.css` (1,380 lines). Produced a companion document
(`circlestack_theme_overrides_audit_20260612.md`) mapping every override
into categories: safe to remove, requires selector restructuring, and
structurally necessary. Identified ~50 `text-shadow: none !important`
declarations as low-risk candidates and 16 font-weight / 16 opacity
force-overrides for follow-up work.

### 1ac. CircleStack theme_overrides text-shadow cleanup

| PR | Area | Commit |
|----|------|--------|
| #52 | CircleStack theme_overrides text-shadow | `da03ec5` |

Reduced `!important` from 489 to 461 (−28) in `theme_overrides.css`
(1,380 lines) by removing defensive `text-shadow: none !important`
declarations where the win_classic wildcard rule or theme selector
specificity already ensured the correct value.

### 1ad. theme.css duplicate/fragile override cleanup

| PR | Area | Commit |
|----|------|--------|
| #53 | theme.css duplicate cleanup | `dfc47c5` |

Reduced `!important` from 151 to 145 (−6) and total lines from 1,035 to
997 (−38) in `theme.css`. Removed duplicate `.btn.danger` blocks and
consolidated redundant property declarations. The `*` wildcard rule for
win_classic text-shadow/backdrop-filter was retained as structurally
necessary but documented for future targeted replacement.

### 1ae. ReelStack app.css remaining specificity cleanup

| PR | Area | Commit |
|----|------|--------|
| #54 | ReelStack app.css specificity | `1148a15` |

Reduced `!important` from 44 to 42 (−2) and total lines from 1,230 to
1,199 (−31) in `reelstack/app.css`. Removed dead duplicate rules
identified during the specificity review. 47 tokenised `rgba(var(`
patterns unchanged.

### 1af. External Workspace permission-gated override audit

| PR | Area | Commit |
|----|------|--------|
| #55 | External workspace permission audit | `220bc31` |

Audit-only pass on the remaining 83 `!important` declarations in
`external_workspace.css` (2,748 lines). Produced a companion document
(`external_workspace_permission_overrides_audit_20260612.md`) mapping
overrides into structurally required (permission-gated visibility),
restructurable (header actions, sticky footer), and safe candidates
(dead marquee block). Identified 12 declarations in a dead marquee
override block for immediate removal.

### 1ag. External Workspace dead marquee override cleanup

| PR | Area | Commit |
|----|------|--------|
| #56 | External workspace dead marquee cleanup | `32c37fd` |

Reduced `!important` from 83 to 71 (−12) and total lines from 2,748 to
2,726 (−22) in `external_workspace.css`. Removed dead marquee selection
box override block identified during the permission audit (#55).

### 1ah. CircleStack theme_overrides font-weight specificity cleanup

| PR | Area | Commit |
|----|------|--------|
| #58 | CircleStack theme_overrides font-weight | `a1a7c8a` |

Reduced `!important` in `theme_overrides.css` by removing defensive
`font-weight: 900/950 !important` declarations where class specificity
already ensured the correct value. Targeted overrides identified as
safe in the audit document (#51).

### 1ai. CircleStack theme_overrides opacity/filter specificity cleanup

| PR | Area | Commit |
|----|------|--------|
| #59 | CircleStack theme_overrides opacity/filter | `71ea4f4` |

Reduced `!important` in `theme_overrides.css` from 461 to 446 (−15
combined with #58) by removing defensive `opacity` and `filter`
force-overrides where class specificity already won. Final cleanup pass
against the audit-identified safe candidates from #51.

**Total merged: 47 PRs (#8–#27, #29–#36, #38–#42, #44–#48, #50–#56, #58–#59)**

---

## 2. Current Remaining Hotspots

### 2a. Hardcoded colours by major file

| File | Hex | RGB/RGBA | Total | Notes |
|------|----:|--------:|------:|-------|
| `circlestack/app.css` | 292 | 398 | 690 | Largest single source; unchanged |
| `dropzone/style.css` | 139 | 111 | 250 | Colour token pass (#44) reduced from 261; `--dz-*` layer wired |
| `external_workspace.css` | ~50 | ~185 | ~235 | Dead marquee cleanup (#56) removed ~8 hardcoded; was 243 |
| `theme.css` | ~44 | ~190 | ~234 | Duplicate cleanup (#53) removed ~6 redundant; was 240 |
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
 446  circlestack/theme_overrides.css   █████████████████████████████████
 392  circlestack/app.css               █████████████████████████████
 145  theme.css                         ███████████
  71  external_workspace.css            █████        (was 83)
  58  shell_menu.css                    ████         (was 73)
  42  reelstack/app.css                 ███          (was 44)
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
1,213 total                               (was 1,386 at start; −173 overall)
```

Reductions from PRs #58–#59:
- `circlestack/theme_overrides.css`: 461 → 446 (−15, font-weight + opacity/filter cleanup)

Previous reductions (PRs #50–#56): 1,321 → 1,228 (−93)
Previous reductions (PRs #38–#48): 1,386 → 1,321 (−65)

### 2c. Override-heavy files (top 5)

1. **circlestack/theme_overrides.css** — 446 `!important` (was 489),
   deeply coupled selectors with `:not()` chains; bright + win_classic
   only. Audit document produced (#51). Three cleanup passes completed:
   text-shadow (#52, −28), font-weight (#58), opacity/filter (#59,
   −15 combined). Remaining overrides are structurally necessary or
   require deep selector restructuring — intentionally deferred.
2. **circlestack/app.css** — 392 `!important` (was 437), first small
   specificity pass done (#50); internal specificity wars in deeply
   coupled sections remain.
3. **theme.css** — 145 `!important` (was 151), duplicate `.btn.danger`
   blocks removed (#53). The `*` wildcard rule for win_classic remains
   (documented, structurally necessary pending targeted replacement).
4. **external_workspace.css** — 71 `!important` (was 83), permission
   audit (#55) and dead marquee cleanup (#56) done; remaining overrides
   in header-actions layout and sticky-footer sections.
5. **shell_menu.css** — 58 `!important` (was 73), token pass (#36)
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
| ReelStack | 57 | Foundation + context menu + token + specificity passes (#17, #32, #40, #47, #54); app.css reduced from 44 to 42; context menu from 57 to 14 |
| External workspace | 71 | Five passes done (#12, #33, #42, #55, #56); reduced from 83 after dead marquee cleanup; permission audit produced |
| Shell menu | 58 | Token + specificity passes (#36, #41); 54 tokenised rgba patterns; bright/win_classic overrides remain |
| Trusted Devices | — | Polish pass (#35) done; styles in shell HTML, no separate CSS file |

### Still override-heavy

| App / Area | `!important` | Hardcoded colours | Notes |
|------------|-------------:|------------------:|-------|
| CircleStack | 861 | 997 | 16 passes done; app.css 437→392 (#50), theme_overrides 489→446 (#52, #58, #59), memory_nodes 23 unchanged; audit doc produced (#51); remaining overrides structurally necessary or intentionally deferred |
| theme.css | 145 | ~234 | Duplicate cleanup (#53) reduced from 151; wildcard `*` rule documented, pending targeted replacement |

---

## 4. Phase 1 Complete — Next Steps

> **UI/theme unification phase 1 is complete.** Remaining CSS debt is
> known, audited, and intentionally deferred. Further reductions should
> happen only when touching the affected component for feature work or
> bug fixes.

All recommended cleanup items from previous audit revisions have been
completed (PRs #50–#56, #58–#59). The low-hanging fruit across all
bundled apps has been addressed. Remaining `!important` declarations
are concentrated in deeply coupled cascade paths (CircleStack app.css,
theme_overrides.css) or structurally necessary overrides (theme.css
wildcard, external_workspace.css permission gates).

### Guidance for future work

1. **Defer high-risk CircleStack / theme_overrides rewrites.** The
   remaining 446 overrides in `theme_overrides.css` and 392 in `app.css`
   require deep selector restructuring with full four-theme visual QA.
   These are not worth pursuing as standalone cleanup tasks — the
   risk/reward ratio is poor for changes that are purely cosmetic to
   the codebase.

2. **Clean up opportunistically.** When modifying a component for feature
   work or bug fixes, reduce `!important` and hardcoded colours in the
   code you're already touching. This is the most efficient path to
   continued improvement.

3. **Prioritise product work, bug fixes, and feature development.** The
   theme token infrastructure is in place. New features should use
   `.pq-*` classes and `:root` tokens by default. This is more impactful
   than chasing every remaining override to zero.

4. **Optional future audit: theme.css win_classic wildcard.** The
   `html[data-theme="win_classic"] * { text-shadow:none !important; ... }`
   rule in `theme.css` is the single largest source of unresolvable
   `!important` declarations. A future audit documenting its impact
   radius would be useful groundwork if a win_classic theme overhaul is
   ever planned — but this is not urgent and should not block other work.

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
Audit document produced in PR #51 maps every override by risk category.

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
can expose restricted content or hide critical UI controls. Permission
audit produced in PR #55 maps which overrides are structurally required.

---

## Summary

| Metric | Value |
|--------|-------|
| PRs merged | 47 (#8–#27, #29–#36, #38–#42, #44–#48, #50–#56, #58–#59) |
| Phase status | **Phase 1 complete** — remaining debt audited and intentionally deferred |
| Unmerged branches in progress | 0 |
| Shared component classes shipped | 8 (`.pq-*` v1) |
| Apps fully token-driven | 2 (RAIDMgr, SnapshotMgr) |
| Apps theme-token ready | 8 (People, NeonWave, Node profile, SharesMgr, EchoStack, Dropzone, PhotoGallery, Onboarding) |
| Apps partially tokenised | 5 (FileManager, ReelStack, External workspace, Shell menu, Trusted Devices) |
| Areas still override-heavy | 2 (CircleStack, theme.css) — intentionally deferred |
| Total `!important` remaining | 1,213 (was 1,386 at start; −173 overall, −12.5%) |
| Total hardcoded colour instances | ~2,320 (top-11 files; includes tokenised rgba patterns) |
| Tokenised `rgba(var(` patterns | 546 across 25 files |
| Audit documents produced | 3 (technical audit, theme_overrides map, permission overrides map) |
| Next steps | Opportunistic cleanup only — see Section 4 |
