# Themed App Template

This is a clean starting point for a new DNA-Nexus / PQ-NAS bundled app.

Copy this directory, rename the app id, and move the copied source under:

```text
apps/bundled/<your_app_id>/src/
```

Expected bundled app shape:

```text
apps/bundled/<your_app_id>/src/
  manifest.json
  www/
    index.html
    app.css
    app.js
    icon.svg
    nav_icon.svg
```

Before coding the app, update:

```text
manifest.json
www/index.html title/header copy
CSS class prefix in app.css if needed
app.js placeholder behaviour
icons
```

Theme rules:

```text
load /static/theme.css
load /static/components.css
use .pq-* shared component classes
define app-local tokens as a bridge to global tokens
avoid hardcoded colours
avoid !important except for functional state
test dark / bright / cpunk_orange / win_classic
```

See:

```text
docs/development/app_theme_guide.md
```

This template is intentionally small. It should not be installed as-is without
renaming the app id and changing the manifest metadata.
