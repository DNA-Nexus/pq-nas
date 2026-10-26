# Photo Gallery 1.1.6 — map-scope localization

Application-only update on top of the local Photo Gallery 1.1.5 map-scope patch.
Server remains 1.2.8. No auth, service, image, database, GPS classification,
map-provider, folder traversal or API changes.

## Changes

- Complete all 17 map-scope messages in the 12 languages enabled by the shared
  `server/src/static/i18n.js`: en, fi, zh, sv, uk, de, et, pl, es, fr, it, tr.
- Use `PQNAS_I18N.getLanguage()` as the selected language, with document language
  as a fallback. Normalize regional tags and underscores like the shared selector.
- Include labels, tooltips, ARIA group label, progress/error notices, zero-GPS
  warnings, partial results, scope summaries and map state badges.
- Refresh the existing central loading/error panel and its progress counters on
  language/progress changes, alongside the controls and footer. Never restart
  a scan or rebuild the Leaflet map merely to change language.
- Keep translations in the application ZIP, not root-managed server static
  files. No global language choices are added or removed.

## Tests

```
node --test tests/photogallery/test_*.cjs
```

The suite covers all 12 registered languages, all 17 message keys, matching
placeholders, language normalization, safe one-pass interpolation, language
switches during a scan, unchanged request count, error/completion updates,
versioned assets, and the existing 1.1.5 GPS/scan tests.

Tests execute the application helpers and production map-controller sections
with mocked DOM/network. They are not authenticated full-server end-to-end tests.
The patcher runs them against a copy of the complete local app source before
writing the development files and producing the ZIP.

## Deployment

Install `photogallery-1.1.6.zip` through Admin Apps beside the previous version.
Do not overwrite the installed 1.1.5 version in place. Open the 1.1.6 entry.
Try Finnish and another language both before and during a subfolder scan. Keep
old versions for fallback. No server rebuild or service restart is required.
