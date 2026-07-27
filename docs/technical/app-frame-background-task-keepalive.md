# App frame background-task keepalive

## Purpose

DNA-Nexus keeps recently used embedded apps alive in an iframe cache. The
normal cache limit is three app frames.

Some browser-side operations depend on state that exists only inside an app
iframe. File Manager uploads are one example:

- selected File objects are held by the browser
- the remaining upload queue lives in JavaScript memory
- the active XMLHttpRequest lives inside the iframe
- upload progress and conflict state live inside the iframe

Removing the iframe during an upload destroys the remaining client-side queue
and may abort the current request.

The background-task keepalive protocol lets an embedded app temporarily protect
its iframe from normal least-recently-used cache eviction.

## Scope

The protocol protects long-running work while the user switches between
embedded DNA-Nexus apps in the same browser tab.

It does not convert browser uploads into server-owned background jobs. It
cannot protect against:

- closing the browser or tab
- browser or operating-system crashes
- network loss
- logout
- explicit shell cache clearing
- server restart
- client device shutdown

Files that have completed remain stored normally. Files that have not started
remain available only while the browser iframe and its File objects survive.

## Protocol

An embedded app sends a same-origin postMessage request to the shell.

Acquire message:

    {
      type: "pqnas-app-background-task",
      version: 1,
      action: "acquire",
      reason: "file-upload"
    }

Release message:

    {
      type: "pqnas-app-background-task",
      version: 1,
      action: "release",
      reason: "file-upload"
    }

The target origin must be window.location.origin. A wildcard target origin is
not allowed.

The reason identifies one logical background operation. Each iframe record uses
a Set so one application can later hold independent locks for different tasks.

## Shell behavior

The generic protocol implementation is located in:

    server/src/static/app_frame_background_tasks.js

The shell cache integration remains in:

    server/src/static/app.js

This keeps the main shell file limited to cache ownership and lifecycle calls.

The server does not expose the complete static directory directly. The module
therefore also requires an explicit backend route:

    GET /static/app_frame_background_tasks.js

The route is registered in:

    server/src/routes/routes_core_ui_shell.cpp

Its fixed filesystem path is created with `static_path()` in `server/src/main.cpp`
and passed through `CoreUiShellRoutesContext`. Request parameters never select
or modify the served filesystem path.

A public request returning HTTP 404 means the module is not active and the
shell will use its diagnostic no-op fallback. Runtime validation must therefore
confirm that this route returns JavaScript before testing iframe retention.

Each cached iframe record contains:

    frameWrap
    frame
    lastUsed
    backgroundTasks: Set<string>

Normal frames remain subject to the three-frame LRU cache limit.

A frame with one or more background-task reasons is excluded from normal LRU
eviction. The cache may therefore temporarily exceed three frames.

When the final reason is released, the shell immediately runs normal cache
pruning again.

Reloading or navigating an iframe clears its reasons because the JavaScript task
that acquired them no longer exists.

Explicit cache clearing, logout and shell teardown may still remove protected
frames.

## Security invariants

The protocol changes only client-side iframe retention. It grants no file,
workspace, user or API permissions.

The shell enforces these rules:

1. event.origin must equal window.location.origin.
2. The sending application is identified from event.source.
3. event.source must equal the contentWindow of a cached iframe.
4. App identifiers, versions and cache keys in message data are not trusted.
5. The message type and protocol version must match exactly.
6. The action must be exactly acquire or release.
7. The reason must match a strict lowercase allowlist and length limit.
8. Reasons per iframe are bounded.
9. The number of simultaneously pinned frames is bounded.
10. Invalid messages fail closed without altering cache state.
11. Message values are not inserted into HTML.
12. Message values do not select URLs, routes or API operations.
13. Server authentication and authorization remain the real security boundary.
14. Workspace membership and upload-path validation remain server-enforced.

These requirements follow docs/security/secure_coding_baseline.md. In
particular, all cross-window input is treated as untrusted, resource use is
bounded and malformed input fails closed.

## File Manager integration

File Manager acquires the file-upload background task for the complete upload
batch.

The lock covers:

- folder creation required by folder uploads
- ordinary XMLHttpRequest uploads
- chunked uploads
- existing-file conflict handling
- quota and upload-limit failures
- final quota and directory refreshes

A public uploadRelFiles wrapper owns the background-task lifetime. The existing
upload implementation runs in uploadRelFilesImpl.

The wrapper releases the lock from a finally block. Release therefore happens
after:

- successful completion
- user cancellation
- network failure
- quota failure
- gateway rejection
- unexpected JavaScript exceptions

Overlapping upload batches are rejected because File Manager currently uses
shared XHR, conflict and cancellation state.

File Manager also registers a beforeunload handler during an active batch.
Browser behavior for leave-page warnings varies, but supported browsers may
warn before refresh, navigation or tab closure.

The pagehide handler releases the shell lock. The pageshow handler reacquires it
when an active page is restored from the browser back-forward cache.

## Resource limits

Current limits:

    normal iframe cache size: 3
    background reasons per iframe: 8
    simultaneously pinned frames: 8
    reason length: 1 to 64 characters

A valid lock has no short timeout because large uploads may legitimately take
hours. Fixed reason and frame limits prevent unlimited cache growth.

## Manual regression tests

### Upload survives cache pressure

1. Start a large multi-file upload in File Manager.
2. Open at least four other embedded apps without returning to File Manager.
3. Return to File Manager.
4. Confirm that the same upload modal and queue are still present.
5. Confirm that upload progress continued.
6. Confirm that the batch completes normally.

### Cache returns to normal

1. Complete or cancel the upload.
2. Open several other applications.
3. Confirm that File Manager can again be removed from the iframe cache.
4. Confirm that reopening File Manager creates a normal fresh frame.

### Cancellation cleanup

1. Start a multi-file upload.
2. Open several other apps.
3. Return to File Manager and cancel the upload.
4. Open additional apps.
5. Confirm that File Manager is no longer protected from normal eviction.

### Error cleanup

Repeat the test with:

- simulated network failure
- quota failure
- upload-size failure
- an existing-file conflict
- an unexpected rejected promise

Confirm that the background-task lock is released in every case.

### Navigation warning

1. Start an upload.
2. Refresh or close the browser tab.
3. Check whether the browser displays its standard leave-page warning.
4. Complete or cancel the upload.
5. Confirm that the warning is no longer requested.

### Security checks

Verify that the shell ignores:

- a message with a different origin
- a message from an unknown event.source
- a wrong protocol version
- an unknown action
- an uppercase reason
- an oversized reason
- a malformed reason
- a forged app id
- a forged cache key
- more than the configured reason limit
- more than the configured pinned-frame limit

## Automated regression test

Run:

    node tools/dev/test_app_frame_background_tasks.js

The test verifies:

- same-origin enforcement
- sender identification from event.source
- rejection of unknown senders
- strict protocol version and action validation
- strict reason validation
- forged app identifiers being ignored
- idempotent acquire behavior
- bounded reasons per iframe
- bounded simultaneously pinned frames
- final-release cache pruning
- reload cleanup
- listener teardown

## Version history

- 2026-07-27: Protocol version 1 and File Manager upload integration.
