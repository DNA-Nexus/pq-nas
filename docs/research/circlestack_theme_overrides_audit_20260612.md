# CircleStack theme_overrides.css Cascade Audit

**Date:** 2026-06-12
**File:** `apps/bundled/circlestack/src/www/theme_overrides.css`
**Status:** Documentation only — no runtime changes
**Branch:** `ai/ui-audit-circlestack-theme-overrides-20260612`

---

## Summary

| Metric | Count |
|---|---|
| `!important` declarations in theme_overrides.css | **489** |
| `!important` declarations in app.css | **392** |
| `!important` declarations in memory_nodes.css | **23** |
| Total lines in theme_overrides.css | 1,381 |
| Total lines in app.css | 5,011 |
| Total lines in memory_nodes.css | 940 |

theme_overrides.css loads **after** app.css, memory_nodes.css, and brand_logo.css
(stated in its own header comment, line 1-2). Source order gives it last-wins
advantage, but app.css already uses `!important` on 392 declarations — many on
the same selectors theme_overrides.css targets. This creates an `!important` arms
race that is the primary reason theme_overrides.css uses `!important` so heavily.

---

## Section Map of theme_overrides.css

| Lines | Section | !important? |
|---|---|---|
| 1–26 | Semantic token definitions (`:root[data-theme]` custom properties) | No |
| 28–62 | Bright theme modal readability (background, color, text, muted text) | Yes |
| 64–99 | Win classic modal readability (parallel to bright) | Yes |
| 101–127 | Bright/classic row styling (Find people, Known origins, My Circle) | Yes |
| 129–145 | Modal inputs (input/textarea/select in themed modals) | Yes |
| 147–176 | Modal buttons (bright/classic, including hover + win_classic emboss) | Yes |
| 178–182 | Delete button color preservation (keeps white text on danger) | Yes |
| 184–228 | Reaction popup (bright/classic `.cs-reaction-theme-menu`) | Yes |
| 230–273 | My Profile light text polish (windowbar hint, titles, muted text) | Yes |
| 275–310 | Modal polish v2 (box-sizing fix, input overflow containment) | Yes |
| 312–326 | Modal viewport containment (width, max-width, overflow-x) | Yes |
| 328–401 | Row layout grid + row refinements (grid, gap, padding, wrapping) | Yes |
| 403–407 | Known origins wider modal | Yes |
| 409–462 | Reaction/emoji wildcard selectors (`[class*="reaction"]` etc.) | Yes |
| 464–487 | FEDERATED badge readability (bright/classic) | Yes |
| 489–515 | Reaction menu compact sizing (width, height, flex) | Yes |
| 517–551 | Composer mention button (`#csAddMentionBtn`) | Yes |
| 553–585 | My Circle Forget button (danger styling in light themes) | Yes |
| 587–640 | Reaction recovery (neutralize mistagged large containers) | Yes |
| 641–689 | Profile modal centering and scrolling (backdrop, position, overflow) | Yes |
| 690–733 | Achievement rows in profile modals (bright/classic styling) | Yes |
| 735–753 | Danger button all-theme normalization (My Circle forget, all themes) | Yes |
| 755–815 | Primary modal actions (memory create, intro send — teal/blue gradient) | Yes |
| 817–853 | Destructive button red (`.cs-modal-delete`, `.cs-danger`, `data-action`) | Yes |
| 855–1019 | Federated feed polish v6 (pills, subtitles, why notes, media notes) | Yes |
| 1021–1113 | Federated feed polish v7 (why hide/show, reaction metadata, media frames) | Yes |
| 1114–1198 | Locked achievement pill + row theming (`.cs-ach-locked-pill-theme`, rows) | Yes |
| 1239–1289 | Locked achievement exact pill (`.cs-locked-achievement-lock` real DOM class) | Yes |
| 1291–1381 | Federated media meta readability (`html[data-theme]` long selector lists) | Yes |

---

## Category 1: Likely Safe to Remove Later

These `!important` declarations could be removed because specificity and/or
source order already wins, or there is no competing `!important` in app.css or
memory_nodes.css.

### 1a. `text-shadow: none !important` (defensive, no competing source)

**Estimated count:** ~50 declarations across the file

The dark theme base CSS does not set `text-shadow` on most of these selectors.
The `:root[data-theme="bright"]` prefix already provides higher specificity than
any bare `.cs-*` selector. The `!important` is purely defensive.

**Examples:**
```css
/* Line 51 */
:root[data-theme="bright"] .cs-modal:not(...) :not(img):not(svg)... {
  text-shadow: none !important;   /* no competing text-shadow in app.css base */
}

/* Lines 244-245 */
:root[data-theme="bright"] .cs-my-profile-title, ... {
  text-shadow: none !important;   /* defensive only */
}

/* Lines 867-868 */
.cs-fed-origin-pill-theme {
  text-shadow: none !important;   /* no base text-shadow on this class */
}
```

**Why safe:** No base rule sets `text-shadow` on these elements. The theme
selector specificity is already higher than any competing rule. app.css also
sets `text-shadow: none !important` on some of these same selectors (lines
4374, 4394, 4928, 4943, etc.), making theme_overrides.css's version doubly
redundant.

**Risk:** Low. Removing these would only regress if a future base CSS change
adds text-shadow to these elements, which is unlikely.

### 1b. Redundant `border-radius: 0 !important` on win_classic elements

**Estimated count:** ~12 declarations

app.css line 4172-4186 already sets `border-radius: 0 !important` and
`box-shadow: none !important` on a broad list of win_classic elements:
```css
/* app.css line 4172 */
:root[data-theme="win_classic"] .cs-post,
:root[data-theme="win_classic"] .cs-modal,
:root[data-theme="win_classic"] .cs-media-modal, ... {
  border-radius: 0 !important;
  box-shadow: none !important;
}
```

theme_overrides.css repeats this on many win_classic selectors:

**Examples:**
```css
/* Line 77 */
:root[data-theme="win_classic"] .cs-modal:not(.cs-achievement-unlock-modal) {
  border-radius: 0 !important;  /* already set by app.css */
}

/* Line 125 */
:root[data-theme="win_classic"] .cs-request-row, ... {
  border-radius: 0 !important;  /* row classes not in app.css's list though */
}
```

**Why safe:** app.css's broad win_classic flattening already covers `.cs-modal`
and `button`. Some row classes (`.cs-request-row`, etc.) are NOT covered by
app.css's list, so those `border-radius: 0` are not redundant.

**Risk:** Low for `.cs-modal`/`button` elements. Medium for row classes — need
to verify each one is covered by app.css's broad rule.

### 1c. `font-weight: 900 !important` on themed pills/badges

**Estimated count:** ~8 declarations

```css
/* Lines 475, 486 */
:root[data-theme="bright"] .cs-federated-badge {
  font-weight: 900 !important;
}

/* Lines 1126, 1253 */
.cs-ach-locked-pill-theme {
  font-weight: 900 !important;
}
```

**Why safe:** No competing `font-weight` with `!important` in app.css for these
selectors. The theme selector or class specificity is sufficient.

**Risk:** Very low.

### 1d. `opacity: 1 !important` / `filter: none !important` on federated elements

**Estimated count:** ~10 declarations

```css
/* Lines 1009-1011 */
:root[data-theme="bright"] .cs-post .cs-fed-origin-pill-theme, ... {
  opacity: 1 !important;
  filter: none !important;
}
```

**Why safe:** These override app.css line 4167's `opacity: 0.62; filter:
grayscale(1)` on locked achievements, but the federated elements are not
achievements. The selector specificity alone should prevent inheritance.

**Risk:** Low, but test visually before removing.

### 1e. Duplicate bright modal muted text colors

**Estimated count:** ~4 declarations

theme_overrides.css lines 54-62 and app.css lines 4936-4943 both set the same
muted text color on bright profile labels/descriptions:

```css
/* theme_overrides.css line 61 */
:root[data-theme="bright"] .cs-modal-text, ... {
  color: var(--cs-bright-text-muted) !important;  /* rgba(15,23,42,0.66) */
}

/* app.css line 4942 */
:root[data-theme="bright"] .cs-profile-label, ... {
  color: rgba(15,23,42,0.66) !important;  /* same value */
}
```

**Why safe:** Exact same value in both files. Source order means
theme_overrides.css wins regardless.

**Risk:** Very low. Values may drift if only one file is updated.

---

## Category 2: Requires Selector Restructuring

These `!important` declarations cannot simply be removed. They compete against
`!important` declarations in app.css. Removing them without restructuring app.css
would cause visual regressions.

### 2a. Modal background/color/border overrides (HIGHEST PRIORITY)

**Estimated count:** ~30 declarations

theme_overrides.css lines 32-42, 68-79 set themed modal appearances. These
compete with multiple app.css `!important` layers:

```css
/* theme_overrides.css line 38 */
:root[data-theme="bright"] .cs-modal:not(.cs-achievement-unlock-modal), ... {
  background: rgba(255,255,255,0.985) !important;
}
```

Competing app.css rules:
- Line 4094: `.cs-modal-backdrop { background: rgba(0,0,0,0.52) !important; }`
- Lines 4097-4110: `.cs-modal { background: var(--cs-modal-bg); }` (no !important — but CSS custom property resolution)
- Lines ~4480: bright/classic theme modal rules with `!important`

**Why restructuring needed:** app.css's broad modal surface and backdrop rules
use `!important`. To remove `!important` from theme_overrides.css, app.css
must stop using it on the same selectors, or the selectors must be restructured
so specificity alone resolves the cascade.

**Coordination plan:** Move all themed modal styling to a single file (either
app.css or theme_overrides.css) and remove `!important` from both. This is a
multi-file change.

### 2b. Row background/color/border overrides (HIGH RISK)

**Estimated count:** ~24 declarations

```css
/* theme_overrides.css line 111 */
:root[data-theme="bright"] .cs-request-row, .cs-known-origin-row, ... {
  background: rgba(248,250,252,0.92) !important;
}
```

Directly competes with app.css line 3966:
```css
/* app.css line 3950-3968 */
.cs-post, .cs-circle-row, .cs-known-origin-row, .cs-origin-row,
.cs-person-row, ... {
  background: var(--cs-surface) !important;
  color: var(--cs-text) !important;
  border-color: var(--cs-border) !important;
}
```

And app.css line 4126:
```css
.cs-find-row { background: var(--cs-surface-2) !important; }
```

**Why restructuring needed:** app.css uses `!important` on bare class selectors
for these rows. theme_overrides.css needs `!important` to win because both have
it, and it relies on source order (loading last). Removing `!important` from
theme_overrides.css while app.css keeps it would regress to dark theme colors.

**Coordination plan:** app.css should use CSS custom properties for row surfaces
that resolve differently per theme, then neither file needs `!important`.

### 2c. Button styling in themed modals

**Estimated count:** ~30 declarations

```css
/* theme_overrides.css line 152 */
:root[data-theme="bright"] .cs-modal button:not(.cs-modal-delete) {
  background: var(--cs-bright-btn-bg) !important;
}
```

Competes with app.css line 4003:
```css
button { border-color: var(--cs-border) !important; }
```

And app.css lines 4019-4021 which set `background: var(--cs-button)` (no
!important, but the broad `button { border-color !important }` on line 4003
means the more-specific theme_overrides.css button rules need !important to
override even border-color).

**Why restructuring needed:** app.css's `button { border-color !important }`
is extremely broad. Any more-specific button rule must also use `!important`
to win on border properties. This is a foundational cascade problem.

**Coordination plan:** Replace `button { border-color !important }` with
theme-variable-driven border-color that resolves per-theme, then remove
`!important` from both files' button rules.

### 2d. Modal input/textarea/select styling

**Estimated count:** ~12 declarations

```css
/* theme_overrides.css line 133 */
:root[data-theme="bright"] .cs-modal input, ... {
  background: #fff !important;
}
```

app.css sets themed input styles with !important at lines 4279-4281:
```css
:root[data-theme="win_classic"] .cs-compose textarea, ... {
  background: #fff !important;
  color: #000 !important;
  border: 1px solid #808080 !important;
}
```

**Why restructuring needed:** Both files theme the same inputs with `!important`.
Neither can drop it without risking the other file's rule winning.

### 2e. Reaction popup theming with wildcard selectors

**Estimated count:** ~18 declarations

```css
/* theme_overrides.css line 415 */
:root[data-theme="bright"] [class*="reaction"][class*="menu"], ... {
  background: rgba(255,255,255,0.98) !important;
}
```

These attribute selectors (`[class*="reaction"]`) are used because the reaction
popup class name may vary at runtime. The `!important` is needed because
app.css sets base reaction menu styles and these wildcards have lower specificity
than class selectors.

**Why restructuring needed:** The wildcard approach inherently has lower
specificity. Either the HTML needs consistent class naming (allowing direct
class selectors) or the cascade structure needs rethinking.

### 2f. Locked achievement row styling

**Estimated count:** ~20 declarations

theme_overrides.css has complex locked achievement theming at lines 1114-1237:
```css
.cs-ach-locked-pill-theme { display: none !important; visibility: hidden !important; }
```

This interacts with app.css line 4164-4168:
```css
.cs-profile-achievement-locked {
  opacity: 0.62;
  filter: grayscale(1) saturate(0);
}
```

**Why restructuring needed:** theme_overrides.css's locked rows fight both
app.css's opacity/filter rules AND the JS-applied `.cs-ach-locked-pill-theme`
class. The display/visibility toggling is tightly coupled to JS behavior.

---

## Category 3: Structurally Necessary / Leave Alone

These `!important` declarations serve structural roles: visibility toggling,
layout mode enforcement, z-index stacking, pointer-events gating, or defense
against mistagged elements. They should NOT be touched without deep behavioral
understanding.

### 3a. `display: none !important` visibility toggles

**Estimated count:** ~6 declarations

```css
/* Line 236 */
:root[data-theme="bright"] .cs-my-profile-windowbar-hint,
:root[data-theme="win_classic"] .cs-my-profile-windowbar-hint {
  display: none !important;
}

/* Line 1029 */
.cs-fed-why-note-theme[hidden], ... {
  display: none !important;
}

/* Lines 1171-1172 */
.cs-ach-locked-pill-theme {
  display: none !important;
  visibility: hidden !important;
}

/* Lines 1180-1182 */
.cs-ach-locked-row-theme::before, ... {
  content: none !important;
  display: none !important;
  visibility: hidden !important;
}
```

**Why necessary:** These enforce visibility state. Removing `!important` could
let lower-specificity `display: block` or `display: inline-flex` rules (from
the same file or app.css) leak through.

### 3b. Reaction popup size/layout defense

**Estimated count:** ~30 declarations

```css
/* Lines 605-616 */
.cs-reaction-theme-menu {
  width: max-content !important;
  max-width: 320px !important;
  display: inline-flex !important;
  ...
}

/* Lines 618-626 */
.cs-reaction-theme-menu:not(:has(> button:nth-of-type(3))) {
  display: revert !important;
  width: revert !important;
  ...
}

/* Lines 628-640 */
.cs-post.cs-reaction-theme-menu,
.cs-feed.cs-reaction-theme-menu, ... {
  display: block !important;
  width: auto !important;
  ...
}
```

**Why necessary:** This is a structural defense system. The reaction popup
class can accidentally be applied to large container elements (posts, feed,
shell) by runtime JS. Without `!important`, the compact popup styling could
make entire page sections collapse to 320px. The `revert` fallback for menus
with fewer than 3 buttons prevents non-popup elements from being styled.

**DO NOT TOUCH** without understanding the runtime class-tagging behavior.

### 3c. Profile modal position/scroll/overflow

**Estimated count:** ~20 declarations

```css
/* Lines 643-670 */
:root[data-theme="bright"] .cs-modal-backdrop:has(.cs-profile-modal) {
  display: grid !important;
  place-items: center !important;
  overflow: hidden !important;
}

:root[data-theme="bright"] .cs-profile-modal {
  position: relative !important;
  top: auto !important; right: auto !important; bottom: auto !important; left: auto !important;
  transform: none !important;
  max-height: calc(100vh - 36px) !important;
  overflow-y: auto !important;
  overflow-x: hidden !important;
  overscroll-behavior: contain !important;
}
```

**Why necessary:** The profile modal's default positioning (likely `position:
absolute` or `fixed` with `top/left/transform` centering) must be fully reset
for the grid-based centering to work. Each `auto !important` must override the
inline or base positioning. The scroll containment prevents body scroll bleed.

### 3d. Modal box-sizing wildcard

**Estimated count:** 1 declaration, applies to ~40 selectors

```css
/* Lines 279-291 */
.cs-modal, .cs-modal *, .cs-modal-backdrop,
.cs-find-people-modal, .cs-find-people-modal *, ... {
  box-sizing: border-box !important;
}
```

**Why necessary:** This is a fundamental layout fix. Without `border-box`,
inputs and rows inside modals overflow due to padding + border adding to width.
The `*` wildcard ensures no descendant reverts to `content-box`. The
`!important` is needed because browser default `box-sizing: content-box` has
the same specificity as `*`.

### 3e. Row layout `display: grid !important` and grid properties

**Estimated count:** ~16 declarations

```css
/* Lines 329-348 */
:root[data-theme="bright"] .cs-request-row, ... {
  display: grid !important;
  grid-template-columns: minmax(0, 1fr) auto auto !important;
  align-items: center !important;
  gap: 12px !important;
  width: 100% !important;
  max-width: 100% !important;
  padding: 14px !important;
  margin: 10px 0 !important;
}
```

**Why necessary:** These rows were originally flex or block. The grid layout
prevents long text (URLs, fingerprints) from pushing buttons off-screen. The
`!important` competes with app.css's `!important` on `.cs-known-origin-row`
etc., making this both structurally necessary AND a Category 2 coordination
issue.

### 3f. `pointer-events` and interaction locks (memory_nodes.css)

memory_nodes.css lines 872-893 use `!important` for show/hide animation:
```css
.cs-memory-item-reaction-actions {
  max-height: 0 !important;
  opacity: 0 !important;
  pointer-events: none !important;
}

.cs-memory-item:hover .cs-memory-item-reaction-actions {
  max-height: 32px !important;
  opacity: 1 !important;
  pointer-events: auto !important;
}
```

**Why necessary:** This is a hover-reveal interaction pattern. The `!important`
ensures no parent opacity or pointer-events inheritance leaks through.

### 3g. Primary action and destructive button styling

**Estimated count:** ~25 declarations

```css
/* Lines 759-815 */
.cs-modal-primary { font-weight: 900 !important; cursor: pointer !important; }

.cs-memory-modal .cs-modal-primary, ... {
  background: linear-gradient(135deg, #2dd4bf, #60a5fa) !important;
  color: #07111f !important;
  ...
}

/* Lines 818-853 */
.cs-modal-delete, .cs-danger, button[data-action="delete"], ... {
  background: rgba(220,38,38,0.14) !important;
  color: #ef4444 !important;
  ...
}
```

**Why necessary:** These rules prevent the generic bright/classic button
theming from overriding semantic action colors. Without `!important`, the
`:root[data-theme="bright"] .cs-modal button:not(.cs-modal-delete)` rule
(line 148) would bleed its neutral styling onto primary actions. The delete
button rules ensure danger buttons stay red in all themes. Both fight against
multiple layers of button `!important` in app.css.

---

## Bright Theme vs Win Classic Risk Notes

### Bright theme risk: LOWER

- Bright theme relies on subtle gradients, translucent backgrounds, and
  `rgba()` colors. These are visually forgiving — small specificity mistakes
  produce slightly wrong shading rather than completely broken UI.
- The text colors use `var(--cs-bright-text)` and `var(--cs-bright-text-muted)`
  tokens, making future changes centralized.
- The radial-gradient backgrounds on profile modals (line 673-676) are unique
  to theme_overrides.css and not duplicated in app.css, reducing conflict.

### Win classic risk: HIGHER

- Win classic depends on exact pixel values (`#808080`, `#f0f0f0`, `#000080`)
  and the `inset box-shadow` emboss pattern. Any `!important` removal that lets
  the wrong border-radius, box-shadow, or border through breaks the entire
  classic aesthetic.
- app.css's broad win_classic flattening (lines 4172-4186) applies
  `border-radius: 0 !important; box-shadow: none !important` — but
  theme_overrides.css re-adds specific `box-shadow: inset 1px 1px 0 #fff,
  inset -1px -1px 0 #9a9a9a !important` for embossed buttons. Removing the
  theme_overrides `!important` would lose the emboss.
- `border-radius: 999px !important` on pills in win_classic (e.g., line 880,
  915, 1146) explicitly overrides app.css's `border-radius: 0 !important`.
  This is an intentional exception where pills should stay rounded even in
  win_classic. Extremely fragile.

### Cross-theme risk matrix

| Element | bright risk | win_classic risk |
|---|---|---|
| Modal backgrounds | Medium | High (exact #f0f0f0 required) |
| Button emboss shadows | N/A | High (inset shadows are core aesthetic) |
| Row styling | Medium | High (border + flat + emboss) |
| Pills (border-radius: 999px) | Low | **Critical** (fights app.css 0 !important) |
| Text colors | Low (token-based) | Medium (hardcoded hex) |
| Reaction popup | Medium | High (emboss + flat corners) |

---

## Known Cascade Dependencies

### theme_overrides.css depends on app.css

1. **CSS custom properties from app.css `:root[data-theme]` blocks** (lines
   3830-3907): theme_overrides.css uses `var(--cs-wc-border)`, etc., defined in
   its own `:root` blocks (lines 7-26). But app.css defines `--cs-surface`,
   `--cs-border`, `--cs-text`, etc. in its own theme blocks. If app.css's custom
   properties change, theme_overrides.css's `var()` references still work
   because they use their own `--cs-bright-*` / `--cs-wc-*` namespace. This is
   GOOD isolation.

2. **app.css modal base layout** (lines 306-321): theme_overrides.css relies on
   app.css defining `.cs-modal-backdrop { position: fixed; inset: 0; display:
   grid; place-items: center; z-index: 9999; }`. The theme overrides then layer
   visual polish on top. Removing app.css's modal positioning would break
   theme_overrides.css's profile modal centering.

3. **app.css's broad `button { border-color !important }` rule** (line 4003):
   This forces every themed button override in theme_overrides.css to also use
   `!important` on border properties. This is the single biggest cascade driver.

### theme_overrides.css depends on memory_nodes.css

4. **memory_nodes.css popover `!important` block** (lines 589-614): The
   contributor popover uses `background !important`, `border !important`,
   `box-shadow !important`, `z-index: 9999 !important`. theme_overrides.css
   does NOT override these — they are left to memory_nodes.css. The separation
   is clean.

5. **memory_nodes.css `.cs-post-memory-node` `!important`** (lines 479, 483):
   Memory node post border-color and background use `!important`.
   theme_overrides.css does not fight these. Clean separation.

### app.css depends on theme_overrides.css (REVERSE dependency)

6. **app.css's detached profile window** (lines 3325-3465, 4641-4694) uses
   `!important` extensively on positioning/sizing. theme_overrides.css's profile
   modal styling (lines 641-689) targets a DIFFERENT element
   (`.cs-profile-modal` via `.cs-modal-backdrop:has(.cs-profile-modal)`) and
   does not conflict.

7. **app.css's achievement replay modal** (lines 4863-4908) and
   theme_overrides.css's locked achievement rows (lines 1114-1237) target
   different elements and do not conflict directly.

---

## Do-Not-Touch Warnings

### CRITICAL: Do not modify these sections without extensive testing

1. **Reaction popup defense (lines 587-640):** The mistagged-container
   neutralization (`cs-post.cs-reaction-theme-menu` etc.) prevents page-wide
   layout corruption. A single missed `!important` could collapse the feed.

2. **Win classic pill `border-radius: 999px` (lines 880, 915, 1146):** These
   intentionally fight app.css's `border-radius: 0 !important`. Removing the
   `!important` makes pills square, breaking visual identity.

3. **Profile modal position reset (lines 655-670):** The `position: relative
   !important; top: auto !important;` chain must fully override inline styles
   or base CSS positioning. Partial removal causes modal misplacement.

4. **Locked achievement visibility (lines 1170-1183):** The `display: none
   !important; visibility: hidden !important; content: none !important` chain
   hides JS-generated locked badges and pseudo-elements. Removing any one
   of these can cause double badges or ghost elements.

5. **Modal box-sizing wildcard (lines 279-291):** Removing this causes input
   overflow in every modal on every theme. Foundational.

---

## Proposed Next PR Plan

### PR 1 (SAFEST FIRST): Remove redundant `text-shadow: none !important`

**Scope:** ~50 declarations
**Risk:** Very low
**Files changed:** theme_overrides.css only
**Test plan:** Visual check of bright/win_classic modals, profiles, rows, pills

Remove `text-shadow: none !important` where:
- No base rule sets `text-shadow` on the element
- app.css already sets `text-shadow: none !important` on the same selector

This is purely removing redundancy. No visual change expected.

### PR 2: Remove redundant win_classic `border-radius: 0` / `box-shadow: none`

**Scope:** ~12 declarations
**Risk:** Low
**Files changed:** theme_overrides.css only
**Test plan:** Visual check of win_classic modals, buttons, cards

Only remove where app.css line 4172-4186 already covers the element. Do NOT
touch `border-radius: 999px` on pills — those are intentional overrides.

### PR 3: Remove redundant `font-weight` / `opacity` / `filter` !important

**Scope:** ~18 declarations
**Risk:** Low
**Files changed:** theme_overrides.css only
**Test plan:** Visual check of federated badges, locked pills, media elements

### PR 4: Consolidate duplicate bright/classic modal muted text colors

**Scope:** ~8 declarations
**Risk:** Low-Medium
**Files changed:** theme_overrides.css only (remove duplicates; app.css keeps its copy)
**Test plan:** Visual check of modal subtitle/label text in bright/classic

### PR 5: Restructure app.css button `border-color !important` (COORDINATION)

**Scope:** ~1 declaration in app.css, ~30 dependent in theme_overrides.css
**Risk:** Medium-High
**Files changed:** app.css AND theme_overrides.css
**Test plan:** All themes, all buttons, all modals

Replace `button { border-color: var(--cs-border) !important; }` with a
theme-variable approach. Then remove `!important` from corresponding
theme_overrides.css button rules. This is the highest-leverage single change.

### PR 6: Restructure row surface `!important` chain (COORDINATION)

**Scope:** ~24 declarations across both files
**Risk:** Medium-High
**Files changed:** app.css AND theme_overrides.css
**Test plan:** All themes, all row types (request, origin, circle, person, find)

Move row surface colors to CSS custom properties that resolve per-theme. Remove
`!important` from both files.

### PR 7: Restructure modal surface `!important` chain (COORDINATION)

**Scope:** ~30 declarations across both files
**Risk:** High
**Files changed:** app.css AND theme_overrides.css
**Test plan:** All themes, all modal types, all backdrop behaviors

This is the most complex change. Requires careful per-modal visual testing.

### Do NOT attempt in any near-term PR:
- Reaction popup defense (lines 587-640)
- Profile modal position reset (lines 641-689)
- Locked achievement visibility (lines 1170-1183)
- Modal box-sizing wildcard (lines 279-291)
- Win classic pill border-radius overrides

---

## Recommended Safest First Runtime PR

**PR 1: Remove redundant `text-shadow: none !important`**

This PR has the highest declaration count (~50), lowest risk, and touches only
theme_overrides.css. It serves as a safe proof-of-concept that !important
removal works without visual regression. If this PR goes through cleanly, it
builds confidence for PRs 2-4.

Target selectors (partial list):
- Lines 51, 88: modal descendant wildcard text-shadow
- Lines 245, 254, 263, 271: My Profile text elements
- Lines 473, 482: federated badge
- Lines 867, 902, 945, 981: federated pills/notes
- Lines 929, 933, 944, 1037, 1043, 1051, 1092, 1097, 1105, 1110: federated v7 elements
- Lines 1129, 1163, 1256: locked achievement pills

Each removal should be individually tested in the browser for the specific
theme (bright or win_classic or both) to confirm no glow/shadow leaks through.
