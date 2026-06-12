# External Workspace Permission-Gated CSS Overrides Audit

**Date:** 2026-06-12
**File:** `server/src/static/external_workspace.css`
**Status:** Documentation only — no runtime changes
**Branch:** `ai/ui-audit-external-workspace-permission-overrides-20260612`

---

## Summary

| Metric | Count |
|---|---|
| Total `!important` declarations | **83** |
| Total lines in external_workspace.css | 2,748 |
| Category 1 — Structurally required / leave alone | 20 |
| Category 2 — Could be removed with selector restructuring | 42 |
| Category 3 — Likely safe candidates for a future tiny PR | 21 |

External workspace is the public-facing share surface. All CSS in this file is
scoped to the external workspace page (`external_workspace.html`). The file
loads after `theme.css` and `components.css` (linked in the HTML head), so
source order gives it natural last-wins advantage for same-specificity rules.

The 83 `!important` declarations cluster into three functional groups:
permission/access-state visibility gating, detached floating modal behavior,
and layout overrides that fight same-file earlier rules. No `!important` in
this file fights cross-file rules from `theme.css` or `components.css` — the
conflicts are all internal to `external_workspace.css` itself.

---

## Section Map of external_workspace.css

| Lines | Section | `!important` count |
|---|---|---|
| 1–26 | CSS custom property tokens (`:root`) | 0 |
| 28–41 | Box-sizing reset, body styling | 0 |
| 43–100 | Shell grid, card surfaces, typography | 0 |
| 102–146 | Top line layout, badges/pills (role indicators) | 0 |
| 148–178 | QR box, status badges | 0 |
| 180–257 | Buttons, toolbar, input fields | 0 |
| 259–315 | Breadcrumbs, upload box (inline panel) | 0 |
| 317–418 | File grid, file row cards, download link | 0 |
| 420–449 | `.signedIn` state layout (hides sidebar, expands main) | 0 |
| 451–505 | Context menu (right-click) | 0 |
| 507–636 | `win_classic` theme overrides (base elements) | 0 |
| 638–1045 | Modal dialogs (ext picker, props modal, picker theming) | 0 |
| 1046–1057 | **Detached folder picker (floating overlay)** | **2** |
| 1058–1082 | Ext picker drag/grab, pointer-events | 0 |
| 1084–1122 | File selection state, marquee block 1 (no `!important`) | 0 |
| 1124–1134 | **Focus suppression + `.hidden` utility** | **3** |
| 1136–1148 | Responsive breakpoints (≤900px, ≤560px) | 0 |
| 1150–1170 | **Marquee selection override block 2** | **12** |
| 1174–1197 | **Marquee catch-all override block 3** | **9** |
| 1199–1212 | **File surface focus suppression** | **1** |
| 1215–1325 | Text preview modal + `win_classic` theming | 0 |
| 1327–1523 | Text editor modal (base styles, `win_classic`, responsive) | 0 |
| 1525–1536 | **Text editor detached floating** | **3** |
| 1537–1563 | Text editor drag/grab, pointer-events | 0 |
| 1567–1694 | Toolbar badges, list/grid view, `win_classic` toolbar | 0 |
| 1696–1931 | Drag/drop overlay, upload progress modal, upload box hidden | 0 |
| 1934–2039 | Upload conflict dialog | 0 |
| 2040–2066 | SVG file type icons, `.downloadLink { display:none }` | 0 |
| 2068–2073 | **Toolbar buttons hidden (upload/new-folder)** | **1** |
| 2076–2135 | External workspace footer (v1 grid layout) | 0 |
| 2137–2172 | **Sticky footer layout v2 (`.signedIn` context)** | **9** |
| 2174–2203 | Footer flex layout refinements | 0 |
| 2204–2278 | **Header actions layout overrides** | **43** |
| 2282–2519 | Trash window (centered modal) | 0 |
| 2521–2748 | Trash detached modal v2 (draggable) | 0 |

---

## Permission / Access-State Model

### JS State Variables (from `external_workspace.js`)

| Variable | Type | Purpose |
|---|---|---|
| `signedIn` | boolean | Set `true` after successful QR or password auth |
| `canEdit` | boolean | Set from `j.can_edit` in API response — gates upload, delete, rename, move, copy, new folder |
| `currentRole` | string | Role label from API (`"editor"`, `"viewer"`, etc.) |
| `currentAuthMode` | string | `"qr"` or `"password"` — determines sign-in UI |

### CSS Classes Toggled by JS Permission Logic

| Class | Applied to | Trigger | Effect |
|---|---|---|---|
| `.signedIn` | `#shell` | After successful auth | Hides sidebar (QR/password panel), expands file surface to full width |
| `.hidden` | Multiple elements | Permission checks, state changes | `display:none !important` |
| `.edit` / `.readonly` | `#rolePill`, `#accessPill`, `#externalRolePill` | `canEdit` boolean | Visual role indicator styling |
| `.show` | Modal overlays | JS modal open/close | `display:flex` or `display:block` depending on context |
| `.selected` | `.fileRow` | Marquee/click selection | Selection highlight |
| `.keyboardFocus` | `.fileRow` | Keyboard navigation | Focus outline |
| `.dragging` | Modal cards | Drag-to-move in progress | Cursor change |
| `.active` | `.btn`, `.pq-btn` | View mode/dirs-first toggle | Active button styling |
| `.listView` | `.files` / `#files` | View mode toggle | Switches grid → list layout |
| `[hidden]` | `#fmUploadProgressBackdrop`, buttons | Upload progress state | Native hidden attribute |

### Permission-Gated Actions in JS

All of these check `canEdit` and/or `signedIn` before executing:

- **Upload** (file picker, drag/drop, folder upload) — requires `signedIn && canEdit`
- **New folder** — requires `canEdit`
- **Rename** — requires `canEdit`
- **Move / Copy** — requires `canEdit`
- **Move to trash** — requires `canEdit`
- **Text file save** — requires `canEdit`
- **Empty trash** — requires `canEdit`
- **Restore from trash** — requires `canEdit`

Read-only actions (browse, download, preview, properties) work for all signed-in users.

---

## Category 1: Structurally Required — Leave Alone

**20 declarations. DO NOT TOUCH without a full regression test of the external workspace.**

### 1.1 Universal visibility toggle (1 declaration)

```css
/* Line 1134 */
.hidden{ display:none !important; }
```

**Why it must stay:** This is the single most critical `!important` in the file. JS uses
`.classList.add("hidden")` / `.classList.remove("hidden")` on at least 15 different
elements: context menus (`emptyContextMenu`, `itemContextMenu`, `selectionContextMenu`),
editor tools (`#editorTools`), upload box (`#uploadBox`), marquee selection boxes,
find bar (`#textEditFindBar`), and more. Without `!important`, any element that also
has `display:flex` or `display:inline-flex` from another rule would ignore `.hidden`
and remain visible. This is a standard pattern used across the entire PQ-NAS UI.

**Risk if removed:** Menus would stay visible after closing, permission-hidden elements
would leak through to read-only users, marquee boxes would persist after drag ends.

### 1.2 Focus suppression (3 declarations)

```css
/* Lines 1130-1131 */
#files:focus, #files:focus-visible,
.files:focus, .files:focus-visible{
    outline:none !important;
    box-shadow:none !important;
}

/* Line 1211 */
.filesStage:focus, .filesSurface:focus, .fileSurface:focus,
.filesGrid:focus, .fileGrid:focus,
#filesStage:focus, #filesSurface:focus, #fileSurface:focus,
#filesGrid:focus, #fileGrid:focus{
    outline:none !important;
}
```

**Why they must stay:** The `#files` grid and `#fileSurface` are made focusable
(`tabIndex`) for keyboard navigation. Without `!important`, the browser's user-agent
`:focus-visible` outline would draw a giant rectangle around the entire file area.
Individual file rows get proper outlines via `.fileRow.keyboardFocus { outline:... }`.

**Risk if removed:** Ugly full-surface browser outline on keyboard focus. Accessibility
regression if someone later replaces with a worse solution.

### 1.3 Detached floating modal behavior (5 declarations)

```css
/* Lines 1048, 1056 — Folder picker */
.extPickerOverlay{
    background:transparent !important;    /* line 1048 */
}
.extPickerOverlay.show{
    display:block !important;             /* line 1056 */
}

/* Lines 1527-1528, 1535 — Text editor */
.textEditModal{
    background:transparent !important;    /* line 1527 */
    pointer-events:none !important;       /* line 1528 */
}
.textEditModal.show{
    display:block !important;             /* line 1535 */
}
```

**Why they must stay:** Both the folder picker and text editor use a "detached floating
window" pattern. The overlay itself is transparent and non-interactive (`pointer-events:none`),
while the card inside uses `position:fixed` with `pointer-events:auto`. The `display:block
!important` on `.show` overrides the earlier `display:flex` rule (lines 650, 1339) that
the non-detached version uses.

Without `!important`, the `.show` class would apply `display:flex` from the
earlier rule block, breaking the fixed-position floating layout.

**Risk if removed:** Modals would render as flex overlays instead of floating windows.
Pointer events would reach the transparent overlay instead of passing through to the
file grid underneath.

### 1.4 Permission-gated toolbar buttons hidden (1 declaration)

```css
/* Line 2072 */
#btnToggleUpload,
#newFolderName,
#btnNewFolder{
    display:none !important;
}
```

**Why it must stay:** These three elements are permanently hidden from the external
workspace top bar. Upload and new-folder actions are only available via the right-click
context menu (which is gated by `canEdit` in JS). The HTML keeps them in the DOM
because existing JS handlers reference them. The `!important` ensures no theme or
component CSS can accidentally re-show them.

The HTML also uses `hidden` attribute and `.hidden` class on these elements (belt and
suspenders), but the CSS `!important` is the authoritative guarantee.

**Risk if removed:** Upload/new-folder buttons could appear in the toolbar if any
other CSS sets `display:flex` or `display:inline-flex` on `.pq-btn` inside `.toolbarGroup`.

### 1.5 Sticky footer layout (9 declarations)

```css
/* Lines 2152-2154 */
.shell.signedIn #fileSurface{
    min-height:100vh !important;
    display:flex !important;
    flex-direction:column !important;
}

/* Lines 2159-2162 */
.shell.signedIn #fileSurface > .cardBody{
    flex:1 1 auto !important;
    min-height:0 !important;
    display:flex !important;
    flex-direction:column !important;
}

/* Line 2166 */
.shell.signedIn #fileSurface #files{
    flex:1 1 auto !important;
}

/* Line 2172 */
.externalWorkspaceFooter{
    margin-top:auto !important;
}
```

**Why they must stay:** This is the sticky footer system. After sign-in (`.signedIn`
class on `#shell`), the file surface must fill the viewport with the footer pinned
to the bottom. The `!important` fights earlier rules in the same file:
- `#fileSurface .cardBody` (line 443) sets `min-height:calc(100vh - 92px)`
- `#fileSurface .cardBody` (line 2121) sets `min-height:calc(100vh - 178px)`
- The grid layout from `.shell` (line 43) conflicts with the full-width signed-in layout

These are same-file conflicts where later rules need to override earlier ones at
the same specificity. The `.shell.signedIn` prefix adds specificity, but the
nested child selectors create complex interactions.

**Risk if removed:** Footer floats in the middle of the page when folder has few
files. File surface doesn't fill the viewport. Layout breaks after sign-in.

### 1.6 Docked toolbar suppression (1 declaration)

```css
/* Line 2260 */
#fileSurface .toolbar.externalWorkspaceToolbarDocked{
    display:none !important;
}
```

**Why it must stay:** When the header actions layout (Category 2 below) is active,
the old docked toolbar is hidden. The `!important` overrides the `.toolbar` display
set at line 221 (`display:flex`). Without it, both the header actions AND the docked
toolbar would be visible, creating a duplicate UI.

**Risk if removed:** Duplicate toolbar appears below the header.

---

## Category 2: Could Be Removed With Selector Restructuring

**42 declarations. These currently fight other local CSS rules in the same file.
Removal requires a coordinated small PR with visual testing.**

### 2.1 Header actions layout overrides (lines 2204-2278)

All selectors are scoped to `#fileSurface .externalWorkspace*`. The `#fileSurface`
ID prefix already provides high specificity, making `!important` theoretically
unnecessary. The `!important` was likely added during development iteration when
multiple layout approaches were being tested.

#### 2.1.1 Top line layout (5 declarations, lines 2206-2210)

```css
#fileSurface .externalWorkspaceTopLine{
    display:flex !important;
    align-items:flex-start !important;
    justify-content:space-between !important;
    gap:18px !important;
    width:100% !important;
}
```

**What it fights:** `.topLine` at line 102 sets `display:flex; align-items:center;
justify-content:space-between; gap:12px`. The `#fileSurface` ID specificity should
already win over the class-only `.topLine` rule. `!important` is redundant here.

#### 2.1.2 Title block (2 declarations, lines 2214-2215)

```css
#fileSurface .externalWorkspaceFilesTitleBlock{
    min-width:0 !important;
    flex:1 1 auto !important;
}
```

**What it fights:** Nothing directly. This element has no other rules. `!important`
is defensive/preventive.

#### 2.1.3 Files title (8 declarations, lines 2219-2226)

```css
#fileSurface .externalWorkspaceFilesTitle{
    display:inline-flex !important;
    align-items:center !important;
    justify-content:flex-start !important;
    gap:12px !important;
    margin:0 !important;
    width:auto !important;
    max-width:100% !important;
    line-height:1.2 !important;
}
```

**What it fights:** `h1 { margin:0; font-size:23px; }` at line 88. Since this is
also an `h1` element, the `#fileSurface .externalWorkspaceFilesTitle` selector
(ID + class) already has higher specificity than the bare `h1` selector. `!important`
is redundant.

#### 2.1.4 Role pill positioning (10 declarations, lines 2230-2239)

```css
#fileSurface #externalRolePill.externalRolePill{
    display:inline-flex !important;
    position:static !important;
    margin:0 !important;
    transform:none !important;
    align-self:center !important;
    flex:0 0 auto !important;
    text-transform:none !important;
    white-space:nowrap !important;
    font-size:12px !important;
    font-weight:600 !important;
}
```

**What it fights:** `.pill` at line 116 sets `display:inline-flex; padding:7px 10px;
font-size:12px`. The selector uses TWO IDs (`#fileSurface #externalRolePill`) plus
a class (`.externalRolePill`), giving extremely high specificity. `!important` is
almost certainly unnecessary here — this is the highest-specificity selector in the
entire file.

**Note:** The `position:static`, `transform:none`, and `margin:0` suggest this pill
was previously absolutely positioned or transformed, and these reset it to normal flow.

#### 2.1.5 Header actions container (6 declarations, lines 2243-2248)

```css
#fileSurface .externalWorkspaceHeaderActions{
    flex:0 0 auto !important;
    display:flex !important;
    align-items:center !important;
    justify-content:flex-end !important;
    gap:10px !important;
    max-width:60vw !important;
}
```

**What it fights:** Nothing directly. The `#externalWorkspaceHeaderActions` is
dynamically populated by `external_workspace_topbar.js`. No other rule targets
`.externalWorkspaceHeaderActions`. `!important` is defensive.

#### 2.1.6 Toolbar main in header (5 declarations, lines 2252-2256)

```css
#fileSurface .externalWorkspaceHeaderActions .fileToolbarMain{
    display:flex !important;
    align-items:center !important;
    justify-content:flex-end !important;
    gap:10px !important;
    flex-wrap:nowrap !important;
}
```

**What it fights:** `.toolbarGroup` at line 235 sets `display:flex; align-items:center;
gap:10px; flex-wrap:wrap`. The `.fileToolbarMain` class is also `.toolbarGroup`, so
`flex-wrap:nowrap` overrides `flex-wrap:wrap`. The `#fileSurface ... .fileToolbarMain`
chain has enough specificity to win without `!important`.

#### 2.1.7 Responsive media query overrides (6 declarations, lines 2263-2276)

```css
@media (max-width:900px){
    #fileSurface .externalWorkspaceTopLine{
        flex-direction:column !important;    /* line 2265 */
        align-items:stretch !important;      /* line 2266 */
    }
    #fileSurface .externalWorkspaceHeaderActions{
        max-width:100% !important;           /* line 2270 */
        justify-content:flex-start !important; /* line 2271 */
    }
    #fileSurface .externalWorkspaceHeaderActions .fileToolbarMain{
        flex-wrap:wrap !important;            /* line 2275 */
        justify-content:flex-start !important; /* line 2276 */
    }
}
```

**What they fight:** The non-responsive versions of the same selectors above.
At same specificity inside a media query, source order wins. `!important` is
redundant here since the media query rules come after the base rules.

### Why Category 2 requires visual testing before removal

1. The header layout is complex: title + role pill + toolbar buttons must wrap
   correctly across screen sizes (900px breakpoint).
2. `external_workspace_topbar.js` dynamically injects elements into
   `#externalWorkspaceHeaderActions` — the injected elements may carry inline
   styles or classes from `components.css` that could conflict.
3. The role pill (`#externalRolePill`) positioning was clearly refactored
   (static replaces what was likely absolute/transformed), and removing
   `!important` needs verification that no other rule re-positions it.

---

## Category 3: Likely Safe Candidates for a Future Tiny Runtime PR

**21 declarations. These are duplicated/dead CSS from iterative development.
Low risk, visual-only, not connected to permission/access behavior.**

### 3.1 Marquee selection block 2 — dead/overridden (12 declarations, lines 1150-1170)

```css
/* Block 2 (lines 1150-1170) — OVERRIDDEN by block 3 */
.selectionBox{
    position:fixed !important;              /* line 1152 */
    z-index:99999 !important;               /* line 1153 */
    pointer-events:none !important;          /* line 1154 */
    box-sizing:border-box !important;        /* line 1155 */
    border:1px solid rgba(0,96,192,.90) !important;  /* line 1156 */
    background:rgba(0,96,192,.12) !important;        /* line 1157 */
    box-shadow:none !important;              /* line 1158 */
}

html[data-theme="cpunk_orange"] .selectionBox{
    border-color:rgba(255,138,28,.95) !important;    /* line 1162 */
    background:rgba(255,138,28,.12) !important;      /* line 1163 */
}

html[data-theme="win_classic"] .selectionBox{
    border:1px dotted #005bbb !important;            /* line 1167 */
    background:rgba(0,91,187,.10) !important;        /* line 1168 */
    box-shadow:none !important;              /* line 1169 */
}
```

**Why this is safe to remove:** Block 3 (lines 1174-1197) targets the exact same
elements with broader selectors at the same specificity + `!important`, and appears
LATER in source order. Block 3 wins on every property. Block 2 is 100% dead CSS.

Additionally, block 3's border color (`rgba(11,78,162,.72)`) overrides block 2's
blue (`rgba(0,96,192,.90)`) and block 2's cpunk_orange theme override is also
dead since block 3 doesn't have a cpunk_orange variant (it uses the same blue
for all themes except win_classic).

**Verification needed:** Confirm block 3's catch-all selectors match all elements
that block 2 targets. Check that `resetMarqueeVisual()` in JS (line 1940) uses
the same selectors.

### 3.2 Marquee catch-all block 3 — consolidation candidate (9 declarations, lines 1174-1197)

```css
/* Block 3 (lines 1174-1197) — could lose !important after block 1+2 cleanup */
.marqueeBox, .selectionBox, .selectionRect,
#marqueeBox, #selectionBox, #selectionRect{
    border:1px solid rgba(11,78,162,.72) !important;  /* line 1180 */
    background:rgba(11,78,162,.14) !important;         /* line 1181 */
    box-shadow:none !important;               /* line 1182 */
    outline:none !important;                  /* line 1183 */
    pointer-events:none !important;           /* line 1184 */
}

html[data-theme="win_classic"] .marqueeBox,
html[data-theme="win_classic"] .selectionBox, ... {
    border:1px solid rgba(11,78,162,.78) !important;  /* line 1193 */
    background:rgba(11,78,162,.16) !important;         /* line 1194 */
    box-shadow:none !important;               /* line 1195 */
    outline:none !important;                  /* line 1196 */
}
```

**Why this is safe after block 2 removal:** Once block 2 (and block 1 at lines
1107-1122) are removed or consolidated into this single block, there is no
same-specificity competitor. The `!important` would only be needed if
`components.css` or `theme.css` targets `.selectionBox` — which they do not
(verified: these are external-workspace-only classes).

**Proposed cleanup:** Remove blocks 1 and 2 entirely. Keep block 3 as the single
authoritative marquee definition. Drop `!important` from all 9 declarations.

---

## High-Risk Sections — DO NOT TOUCH

| Lines | Section | Risk |
|---|---|---|
| 1134 | `.hidden` utility | **CRITICAL** — breaks all permission gating if removed |
| 2068-2073 | `#btnToggleUpload` etc. hidden | **HIGH** — editor-only actions would leak to viewers |
| 2152-2172 | Sticky footer `.signedIn` | **HIGH** — layout breaks after authentication |
| 1046-1057, 1525-1536 | Detached modal floating | **MEDIUM** — modals revert to overlay mode |
| 1124-1134, 1199-1212 | Focus suppression | **MEDIUM** — ugly browser outlines, accessibility concern |

---

## Low-Risk Future PR Candidates

| Lines | Declarations | Risk | PR Type |
|---|---|---|---|
| 1150-1170 | 12 | **Very low** | Delete dead CSS (block 2 overridden by block 3) |
| 1107-1122 + 1174-1197 | 9 (after block 2 removal) | **Low** | Consolidate marquee blocks, drop `!important` |
| 2206-2278 | 42 | **Medium** | Remove `!important` from header layout (needs visual testing) |

---

## Recommended Safest First Runtime PR

### PR 1: Remove dead marquee selection CSS (block 2)

**Scope:** Delete lines 1150-1170 (12 `!important` declarations).

**Why it's safe:**
- Block 3 (lines 1174-1197) already overrides every property in block 2
- Same specificity, later source order, same `!important` — block 2 is dead code
- No JS references block 2's specific selectors differently from block 3
- `resetMarqueeVisual()` in JS uses `.marqueeBox, .selectionBox, .selectionRect,
  #marqueeBox, #selectionBox, #selectionRect` which matches block 3, not block 2

**Testing:**
- Open external workspace, sign in
- Select files via click, Ctrl+click, Shift+click
- Drag-select (marquee) across files
- Verify selection box renders with correct blue border
- Test in `win_classic` and `cpunk_orange` themes
- Verify marquee clears on mouse release

**Result:** 83 → 71 `!important` declarations.

---

## Proposed Next PR Plan (After PR 1)

### PR 2: Consolidate marquee to single block

**Scope:** Remove block 1 (lines 1107-1122, no `!important`), remove `!important`
from block 3 (lines 1174-1197). Net: −9 `!important`.

**Requires:** Same visual testing as PR 1 plus keyboard selection testing.

**Result:** 71 → 62 `!important` declarations.

### PR 3: Header actions layout — drop `!important`

**Scope:** Remove `!important` from all 42 declarations in lines 2206-2278.
The `#fileSurface` ID prefix provides sufficient specificity.

**Requires:** Visual testing at multiple screen widths (>900px, ≤900px, ≤560px).
Verify role pill positioning, toolbar button wrapping, title truncation.

**Risk factors:**
- `external_workspace_topbar.js` injects elements dynamically
- Role pill was previously repositioned (static overrides transform/position)
- Responsive breakpoint behavior must be verified

**Result:** 62 → 20 `!important` declarations.

### PR 4 (final): Audit remaining 20

The remaining 20 are all structurally required (Category 1). They should stay
unless the entire external workspace CSS is refactored from scratch. Document
them as intentional and close the audit.

---

## Known JS Class/State Dependencies

### Authentication state flow

```
startSession()
  → signedIn = false
  → shell.classList.remove("signedIn")
  → canEdit = false

pollStatus() → state === "approved"
  → applyAccessInfo(j)
    → signedIn = true
    → canEdit = !!j.can_edit
    → currentRole = j.role
    → rolePill.classList.add(canEdit ? "edit" : "readonly")
    → editorTools.classList.toggle("hidden", !canEdit)
  → showSignedInState()
    → shell.classList.add("signedIn")

passwordLogin()
  → applyAccessInfo(j)
  → showSignedInState()
```

### Modal state flow

```
showPickerOverlay(true)
  → overlay.classList.toggle("show", true)
  → display becomes "block" via !important (detached mode)

hideContextMenus()
  → menu.classList.add("hidden")
  → display:none !important via .hidden class

resetMarqueeVisual()
  → box.classList.add("hidden")
  → box.style.display = "none"  (inline style + .hidden class, belt-and-suspenders)
```

### Upload permission gate

```
All upload paths:
  if (!signedIn || !canEdit) {
      setStatus("Upload requires editor access.", "bad");
      return;
  }
```

---

## Do-Not-Touch Warnings

1. **`.hidden { display:none !important }`** — This is load-bearing infrastructure.
   Every visibility toggle in the external workspace depends on it.

2. **`#btnToggleUpload, #newFolderName, #btnNewFolder { display:none !important }`** —
   These elements exist in the HTML DOM with `hidden` attribute AND `.hidden` class AND
   this CSS rule. The triple-layer defense is intentional because this is a public-facing
   security surface. Do not simplify to a single mechanism.

3. **Sticky footer `.signedIn` rules** — These override earlier same-file rules that
   set different `min-height` values. The override chain is:
   - Line 443: `min-height:calc(100vh - 92px)` (base)
   - Line 2121: `min-height:calc(100vh - 178px)` (footer v1)
   - Line 2152: `min-height:100vh !important` (signed-in override)
   Removing the `!important` without also cleaning up lines 443 and 2121 will cause
   layout regression.

4. **Detached modal `!important`** — The folder picker and text editor both use
   `background:transparent !important` and `display:block !important` to override
   their own earlier `display:flex` base rules. These are same-selector conflicts
   within the same file. Removing `!important` requires deleting or rewriting the
   earlier base rules.

5. **Focus suppression** — Browser user-agent `:focus-visible` outlines have high
   specificity. The `!important` is needed to reliably suppress them across all
   browsers. Do not remove without providing an alternative focus indicator.
