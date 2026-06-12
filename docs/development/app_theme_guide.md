# DNA-Nexus / PQ-NAS App Theme Guide

**Status:** Phase 1 guidance for new apps and future app rewrites.  
**Audience:** PQ-NAS developers and bundled app authors.

This guide explains how new apps should integrate with the shared theme system
without reintroducing hardcoded colours, duplicate button styles, or
`!important` specificity wars.

The short version:

```text
Use global tokens.
Use .pq-* shared component classes.
Create app-local tokens only as a bridge.
Avoid hardcoded colours unless there is a real reason.
Avoid !important unless it protects functional state.
Test all four shipped themes.
```

---

## 1. Theme architecture

The current shared UI foundation is:

```text
server/src/static/theme.css
server/src/static/components.css
```

`theme.css` defines global colour, field, border, radius, shadow, and theme
tokens. `components.css` defines reusable `.pq-*` component classes.

New app pages should normally load:

```html
<link rel="stylesheet" href="/static/theme.css">
<link rel="stylesheet" href="/static/components.css">
<link rel="stylesheet" href="./app.css">
```

Load app CSS last so the app can define layout and app-specific composition
while still consuming shared tokens.

---

## 2. Use `.pq-*` shared components first

Use shared components before creating app-local button/card/input/badge styles:

```html
<button class="pq-btn primary">Save</button>
<button class="pq-btn secondary">Cancel</button>
<button class="pq-btn danger">Delete</button>

<section class="pq-card">
  <input class="pq-input" placeholder="Search">
  <span class="pq-badge ok">Ready</span>
</section>
```

Common shared classes:

```text
.pq-btn
.pq-btn.primary
.pq-btn.secondary
.pq-btn.danger
.pq-card
.pq-input
.pq-select
.pq-textarea
.pq-badge
.pq-badge.ok
.pq-badge.warn
.pq-badge.err
.pq-badge.info
.pq-badge.muted
.pq-toolbar
.pq-empty-state
```

Avoid creating new button/card/input systems unless the app has a real pattern
that cannot be represented with `.pq-*`.

---

## 3. App-local token bridge

Each app may define local tokens, but they should map to global tokens. This
keeps app CSS readable without creating a second theme system.

Recommended pattern:

```css
:root {
  --myapp-bg: var(--bg);
  --myapp-fg: var(--fg);
  --myapp-muted: var(--muted);
  --myapp-panel: var(--panel);
  --myapp-card: var(--card);
  --myapp-line: var(--border);
  --myapp-line-strong: var(--border2);
  --myapp-accent: var(--accent);
  --myapp-danger: var(--danger);
}
```

Then use the app tokens:

```css
.myapp-card {
  background: var(--myapp-card);
  color: var(--myapp-fg);
  border: 1px solid var(--myapp-line);
}
```

Avoid:

```css
.myapp-card {
  background: #101827;
  color: #ffffff;
  border: 1px solid rgba(255,255,255,.14);
}
```

---

## 4. Hardcoded colour rules

Hardcoded colours are allowed only when they are intentional and explained.

Acceptable exceptions:

```text
media viewers that need neutral black/white surfaces
map library popups with third-party forced white backgrounds
one-off SVG/icon assets
chart palettes where colours are data/category identity
small decorative gradients where tokenisation would reduce readability
```

When hardcoding is necessary, add a comment explaining why.

---

## 5. !important rules

Do not use `!important` for normal visual styling.

Allowed cases:

```text
.hidden / [hidden] display enforcement
overriding JS inline display values when unavoidable
modal/overlay state locks
focus suppression when browser default focus rings break the widget
permission-gated visibility where weaker rules could expose/hide wrong UI
rare theme override cases that are documented and tested
```

Bad:

```css
.myapp-button {
  color: var(--myapp-fg) !important;
}
```

Better:

```css
.myapp .myapp-button {
  color: var(--myapp-fg);
}
```

Best:

```html
<button class="pq-btn primary">Save</button>
```

---

## 6. App layout pattern

Prefer a simple app root:

```html
<main class="myapp-shell">
  <header class="myapp-hero pq-card">...</header>
  <section class="myapp-toolbar pq-toolbar">...</section>
  <section class="myapp-content">...</section>
</main>
```

Keep layout classes app-local and component classes shared.

Good:

```html
<section class="myapp-item pq-card">
```

The app class controls layout. The `.pq-card` class controls theme-aware card
chrome.

---

## 7. Required theme QA

Every new app or major UI change must be checked in:

```text
dark
bright
cpunk_orange
win_classic
```

Minimum checklist:

```text
page loads
text contrast is readable
buttons look clickable
forms are readable
modals/overlays are readable
danger actions are visibly dangerous
empty/error/success states are readable
keyboard focus is not broken
console has no errors
```

Media-heavy apps must additionally test fullscreen/player/overlay behaviour.

---

## 8. Recommended starting point

Use:

```text
apps/templates/themed_app/
```

Copy the template, rename the app id, update the manifest, then build from
there.

Do not copy CircleStack as a starting point for new apps; it is a large legacy
surface with intentionally deferred CSS debt.

Good models for small new apps:

```text
EchoStack
SharesMgr
People
Dropzone after theme cleanup
```

Avoid using these as first-copy templates:

```text
CircleStack
theme_overrides.css
large media/player overlays unless your app is also media-heavy
```

---

## 9. Review checklist for new apps

Before merging a new app:

```text
manifest.json exists
www/index.html exists
global theme.css loaded
global components.css loaded
app.css uses local token bridge
.pq-* classes used for common UI
no unnecessary hardcoded colours
no unnecessary !important
dark / bright / cpunk_orange / win_classic tested
detached browser window tested if applicable
no JS errors
```
