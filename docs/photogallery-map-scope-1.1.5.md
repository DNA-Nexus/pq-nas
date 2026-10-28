# Photo Gallery 1.1.5 — map scope and GPS classification

This is an application-only update, intended for the tested DNA-Nexus Server
1.2.8 standalone installation. It does not change server code, authentication,
Nodus services, image files, or manually rewrite the EXIF SQLite database.

## Behavior

- Map starts in **current folder only** mode. The visible **Include subfolders**
  checkbox opts into a sequential breadth-first traversal of authenticated
  `/api/v4/gallery/list` requests. Grid/text-search scope remains unchanged.
- The browser performs at most one folder request at a time. Switching off the
  checkbox, leaving the map, changing folders, or closing the page aborts the
  browser request and stops further requests. An already-running server read may
  still finish. Old responses cannot overwrite the new scope.
- Each scan is limited to 1,000 folders including the root and 25,000 images.
  Limited results are labeled **partial**. Failed subfolders produce an error,
  not a misleading “no photos with GPS” result. Requests time out after 60 s.
- The current folder's already-loaded items are reused. Completed subfolder
  results are kept only in this page's memory. Refresh invalidates them.
- Ordinary gallery-list requests can refresh the server's existing metadata
  cache. This is not a forced EXIF reindex; already-cached empty GPS values may
  still require a separate investigation.
- GPS data must have the server flag, finite numeric coordinates, and valid
  coordinate ranges. Exactly `(0,0)` is classified **suspicious**, not impossible.
  It is hidden by default; an explicit second checkbox can include it. A single
  zero coordinate, including equator or prime-meridian photos, is accepted.
- Map markers, “Show on map”, GPS text search, and the EXIF GPS filter share the
  predicate. Raw EXIF metadata is not rewritten.
- New controls and progress/warning text are bundled in Finnish and English.
- The existing Leaflet renderer and tile provider are unchanged; real GPS photos
  were confirmed to render correctly on the user's server before this change.

## Tests

```sh
node --test tests/photogallery/test_geo_scope.cjs tests/photogallery/test_map_integration.cjs
node --check apps/bundled/photogallery/src/www/app.js
node --check apps/bundled/photogallery/src/www/gallery_exif_search.js
node --check apps/bundled/photogallery/src/www/gallery_geo_scope.js
```

The integration tests execute the actual patched map-controller sections with
mocked DOM and network dependencies. They are not a full authenticated server or
real-tile end-to-end test. The development patcher checks all edited JavaScript
before creating the application ZIP.

## Manual acceptance on a test installation

1. Open a folder with one GPS photo and a nested folder containing another GPS
   photo. Map unchecked: only the first appears. Check Include subfolders: both
   appear with correct preview paths.
2. Toggle the option off during a slow scan and navigate to another folder.
   No stale markers or completion messages should appear.
3. A `(0,0)` image is excluded with a visible explanation; the override can show
   it. A `(0, nonzero)` or `(nonzero, 0)` image remains usable without an override.
4. Combine text, rating, and GPS filters with both map scopes. Return to Grid:
   existing grid search behavior remains intact.
5. Try a failed subtree and verify the failure is visible, not an empty success.
6. Confirm Finnish/English labels, keyboard-operable checkboxes, original image
   metadata, OPAQUE login, and Nodus service state remain intact.

Install the generated `photogallery-1.1.5.zip` through Admin Apps only after the
checks succeed. Keep Photo Gallery 1.1.4 installed for fallback. No merge, push,
GitHub release, or deployment is performed by the patcher.
