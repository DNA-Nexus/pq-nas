"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

const modulePath = "server/src/static/app_frame_background_tasks.js";
const source = fs.readFileSync(modulePath, "utf8");

const listeners = new Map();

const fakeWindow = {
    location: {
        origin: "https://dna-nexus.test"
    },

    addEventListener(type, handler) {
        listeners.set(type, handler);
    },

    removeEventListener(type, handler) {
        if (listeners.get(type) === handler) {
            listeners.delete(type);
        }
    }
};

vm.runInNewContext(source, {
    window: fakeWindow,
    Object,
    Array,
    Map,
    Set,
    Number,
    RegExp
}, {
    filename: modulePath
});

const api = fakeWindow.PQNAS_APP_BACKGROUND_TASKS;

assert.ok(api);
assert.equal(typeof api.createController, "function");

function frameWrap(active) {
    return {
        hidden: !active,
        classList: {
            contains(name) {
                return name === "active" && active;
            }
        }
    };
}

const sourceA = {};
const sourceB = {};
const unknownSource = {};

const recA = {
    frame: { contentWindow: sourceA },
    frameWrap: frameWrap(false),
    lastUsed: 1,
    backgroundTasks: new Set()
};

const recB = {
    frame: { contentWindow: sourceB },
    frameWrap: frameWrap(true),
    lastUsed: 2,
    backgroundTasks: new Set()
};

const cache = new Map([
    ["filemgr@1.2.3", recA],
    ["photogallery@1.1.4", recB]
]);

const pruneCalls = [];

const controller = api.createController({
    cache,
    prune(activeKey) {
        pruneCalls.push(activeKey);
    },
    maxReasonsPerFrame: 2,
    maxPinnedFrames: 1
});

assert.ok(controller);
assert.equal(typeof controller.isPinned, "function");

const messageHandler = listeners.get("message");
assert.equal(typeof messageHandler, "function");

function send(sourceWindow, data, origin = fakeWindow.location.origin) {
    messageHandler({
        source: sourceWindow,
        origin,
        data
    });
}

const acquireUpload = {
    type: "pqnas-app-background-task",
    version: 1,
    action: "acquire",
    reason: "file-upload"
};

/*
 * Security: cross-origin messages cannot alter shell cache state.
 */
send(sourceA, acquireUpload, "https://attacker.test");
assert.equal(controller.isPinned(recA), false);

/*
 * Security: an unknown WindowProxy cannot acquire a lock.
 */
send(unknownSource, acquireUpload);
assert.equal(controller.isPinned(recA), false);

/*
 * Security: malformed protocol fields fail closed.
 */
send(sourceA, { ...acquireUpload, version: 2 });
send(sourceA, { ...acquireUpload, action: "delete" });
send(sourceA, { ...acquireUpload, reason: "FILE-UPLOAD" });
send(sourceA, { ...acquireUpload, reason: "x".repeat(65) });
assert.equal(controller.isPinned(recA), false);

/*
 * Security: app identity comes from event.source. Forged identifiers in
 * message data are ignored and do not select another cache record.
 */
send(sourceA, {
    ...acquireUpload,
    appId: "photogallery",
    appKey: "photogallery@1.1.4"
});

assert.equal(controller.isPinned(recA), true);
assert.equal(controller.isPinned(recB), false);
assert.deepEqual(Array.from(recA.backgroundTasks), ["file-upload"]);

/*
 * Duplicate acquire is idempotent.
 */
send(sourceA, acquireUpload);
assert.deepEqual(Array.from(recA.backgroundTasks), ["file-upload"]);

/*
 * Resource safety: reasons per frame are bounded.
 */
send(sourceA, {
    ...acquireUpload,
    reason: "archive-export"
});

send(sourceA, {
    ...acquireUpload,
    reason: "third-task"
});

assert.deepEqual(
    Array.from(recA.backgroundTasks),
    ["file-upload", "archive-export"]
);

/*
 * Resource safety: the number of pinned frames is bounded.
 */
send(sourceB, acquireUpload);
assert.equal(controller.isPinned(recB), false);

/*
 * Releasing an unknown reason changes nothing and does not prune.
 */
send(sourceA, {
    ...acquireUpload,
    action: "release",
    reason: "unknown-task"
});

assert.equal(pruneCalls.length, 0);

/*
 * Releasing one of several reasons keeps the frame protected.
 */
send(sourceA, {
    ...acquireUpload,
    action: "release",
    reason: "file-upload"
});

assert.equal(controller.isPinned(recA), true);
assert.equal(pruneCalls.length, 0);

/*
 * Releasing the final reason triggers normal LRU pruning. The active key is
 * derived from shell-owned frame state.
 */
send(sourceA, {
    ...acquireUpload,
    action: "release",
    reason: "archive-export"
});

assert.equal(controller.isPinned(recA), false);
assert.deepEqual(pruneCalls, ["photogallery@1.1.4"]);

/*
 * Once the previous lock is released, another frame may acquire one.
 */
send(sourceB, acquireUpload);
assert.equal(controller.isPinned(recB), true);

/*
 * Navigation/reload cleanup clears stale locks.
 */
controller.clear(recB);
assert.equal(controller.isPinned(recB), false);

/*
 * Controller teardown removes the message listener.
 */
controller.destroy();
assert.equal(listeners.has("message"), false);

console.log("OK: app frame background-task security and lifecycle tests passed");
