# Reel Stack 1.0.3 — context-menu localization

Application-only update for both right-click menus. Server binaries, global
language dictionaries, videos, indexes, Photo Gallery, OPAQUE, and Nodus are not
modified. The app's existing minimum server version remains 1.1.3; the target
installation for this change is the user's tested standalone server 1.2.8.

## Changes

- Ship 20 context-menu strings in all 12 languages from the pinned server
  registry: en, fi, zh, sv, uk, de, et, pl, es, fr, it, tr.
- Video menu: play, metadata, rename, share link, download, delete, and keyboard
  hints. Background menu: upload, refresh, and all seven view modes.
- Resolve these keys from the app bundle using the shell's selected language.
  An outdated/missing server translation cannot force a known menu label back
  to English. Other translation keys continue using the original global helper.
- Refresh an open menu on the language event, document language change, and
  initial i18n readiness. Preserve its file path, keyboard focus, and anchor;
  rendering does not call playback/upload/delete/index-refresh handlers.
- Keep the original styles, API endpoints, callbacks, confirmation dialogs,
  video selection, and upload implementation. Product name and filenames are
  not translated. This is not an audit of all Reel Stack dialogs or date formats.

## Tests

```sh
node --test tests/reelstack/test_context_i18n.cjs
node --check apps/bundled/reelstack/src/www/reelstack_context_i18n.js
node --check apps/bundled/reelstack/src/www/reelstack_context_menu.js
```

The tests execute the actual menu-rendering/controller sections extracted from
source with mocked DOM and API boundaries. They cover translation parity,
placeholder parity, regional tags, delayed i18n readiness, language changes,
path/focus preservation, and original action dispatch including multi-delete.
They deliberately never upload/delete/play real media. They are not a full
server end-to-end test. The dev patcher tests a staged copy of the complete app
before applying files, and creates an installable ZIP from that tested copy.

## Manual acceptance

Install reelstack-1.0.3.zip via Admin Apps next to 1.0.2, then open the 1.0.3 row.
Check both right-click menus in Finnish, one other Latin-script language, and
Chinese/Ukrainian. Change the shell language with a menu open. The file title
and focused menu row must remain correct. Confirm Play opens the selected video
and the background view choices still work. No need to delete media or rebuild
the media index for this language check. Keep 1.0.2 for fallback.

The patcher performs no Git commit/push/merge, public release, service restart,
or production install. Existing uncommitted Photo Gallery and generated WASM
files are left untouched.
