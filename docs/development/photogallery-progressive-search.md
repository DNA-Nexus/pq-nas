# Progressive Photo Gallery search

Photo Gallery 1.1.7 shows matching grid thumbnails while its recursive search is
still running. Text, rating and EXIF filters share the same candidate scan.
Changing a filter immediately applies it to the candidates already received;
changing folders or explicitly refreshing cancels the old request.

Unchanged tiles and their thumbnail elements are retained between batches.
Selection, keyboard focus, scroll position, folder grouping and RAW/JPEG burst
grouping remain available while new results arrive. Progress includes the
current match count. A failed or truncated search retains its available matches
and is labelled incomplete; Refresh starts a new attempt.

## Server transport

`GET /api/v4/gallery/search_stream?path=<relative-folder>&force=1` returns
`application/x-ndjson`. It uses the same user authentication and allocated-storage
requirements as `/api/v4/gallery/search`. `path` and `force` are optional.

Each line is one JSON object:

- `batch`: `items` uses the existing gallery image schema; `stats` contains
  `dirs`, `files` and `file_bytes`. Batches contain at most 128 images.
- `progress`: updated `stats`, including an initial empty progress event.
- `done`: final `stats` and `truncated: false`.
- `error`: terminal `error` code and `message`; no `done` follows.

The browser requires an explicit `done` event. EOF or a malformed stream is an
incomplete search. Failed writes stop server traversal. Responses disable
caching and request that reverse proxies avoid buffering.

The collector loads one scoped logical-index snapshot, then validates and emits
images progressively, merging legacy physical files without duplicates.
Logical metadata retains precedence. The collector and its helper graph are captured by value throughout stored
callbacks so they remain valid after the role-gated registration scope exits.
The HTTP regression test issues requests after leaving that temporary scope.
The first image can be emitted before
physical traversal and the rest of the metadata/EXIF joins finish. The initial
SQLite snapshot still precedes image delivery.

Logical prefix lookups use indexed, literal, case-sensitive ranges. They no
longer scan every user file for every folder or interpret `%` and `_` as SQL
wildcards. Empty-root subtree queries return the complete user namespace.

## Compatibility and installation

Deploy the rebuilt server and install
`apps/bundled/photogallery/photogallery-1.1.7.zip` through Admin → Apps. The source
under `apps/bundled/photogallery/src` is not the installed runtime copy.
The server stays at version 1.2.8. New grid messages are translated into all
12 supported languages.

An older server returning 404 for the stream endpoint uses the existing JSON
search, then the browser folder-walk fallback if necessary. The JSON route
retains its existing 25,000-image cap, which the new frontend labels incomplete.
The streaming route has no such cap. Incremental rendering applies to the grid;
the existing Photo Gallery 1.1.6 map scanner remains independent. Switching to
the map cancels a running grid scan. Map scope, GPS zero-coordinate handling
and the existing map language support are preserved.

## Validation

Run the focused regression suite from the repository root:

```bash
bash tools/dev/test_photogallery_search.sh
```

It needs Node.js, a C++17 compiler, SQLite development files and the nlohmann JSON
headers. It reruns the 63 existing Photo Gallery tests, checks the current callback
lifetime and Storage Node route guards, and tests stream parsing, 30,000-row
index scoping, and the actual shared
collector and HTTP route with real SQLite indexes and deterministic auth/EXIF
fixtures. The HTTP tests verify early delivery, more than 25,000 images,
cancellation, validation and terminal errors.

The browser suite runs the real gallery assets with controlled HTTP responses.
It requires the Playwright Node package and Chromium:

```bash
bash tools/dev/test_photogallery_search.sh --browser
```

Set `CHROME_EXECUTABLE_PATH` to use an existing Chrome/Chromium executable.
The browser tests cover 10,001 candidates, first results before completion,
filter reuse, tile/thumbnail stability, selection and scrolling, navigation and
refresh cancellation, failures, fallback, RAW/JPEG burst grouping and switching
to the independent map while a grid search is running.

Build the complete server separately:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DPQNAS_BUILD_BUNDLED_NODUS_CLI=OFF
cmake --build build --target pqnas_server -j2
```
