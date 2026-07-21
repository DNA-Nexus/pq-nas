window.PQNAS_FILEMGR = window.PQNAS_FILEMGR || {};

(() => {
  "use strict";

  const FM = window.PQNAS_FILEMGR;
  const LEASE_SECONDS = 60;
  const REFRESH_INTERVAL_MS = 20000;

  let refreshTimer = 0;
  let activePath = "";

  function api() {
    return FM && FM.api ? FM.api : null;
  }

  function isWorkspaceScope() {
    return !!(
      FM &&
      typeof FM.isWorkspaceScope === "function" &&
      FM.isWorkspaceScope()
    );
  }

  function sessionId() {
    return (
      FM &&
      typeof FM.getWorkspaceEditorSessionId === "function"
    )
      ? String(FM.getWorkspaceEditorSessionId() || "").trim()
      : "";
  }

  function errorDetails(error) {
    if (!error || typeof error !== "object") return null;

    return (
      error.response ||
      error.details ||
      null
    );
  }

  function errorCode(error) {
    const details = errorDetails(error);

    return String(
      (details && details.error) ||
      (error && error.code) ||
      (error && error.kind) ||
      ""
    );
  }

  function describeError(error, translateFn) {
    const translate =
      typeof translateFn === "function"
        ? translateFn
        : ((key, vars, fallback) => fallback || key);

    const details = errorDetails(error);
    const lease =
      details && details.lease && typeof details.lease === "object"
        ? details.lease
        : null;

    const code = errorCode(error);

    if (code === "edit_lock_missing") {
      return translate(
        "filemgr.textedit.readonly_open",
        null,
        "This file is currently open in read-only mode. Reload to try acquiring edit access again."
      );
    }

    if (!lease) {
      return translate(
        "filemgr.textedit.readonly_generic",
        null,
        "This file can only be opened in read-only mode right now."
      );
    }

    const holder = lease.holder_fp
      ? translate(
          "filemgr.textedit.locked_by_fp",
          { fp: String(lease.holder_fp).slice(0, 12) },
          ` by ${String(lease.holder_fp).slice(0, 12)}…`
        )
      : translate(
          "filemgr.textedit.locked_by_session",
          null,
          " by another session"
        );

    const until = lease.expires_at
      ? translate(
          "filemgr.textedit.editable_after",
          { time: lease.expires_at },
          ` It should become editable again after ${lease.expires_at}.`
        )
      : "";

    return translate(
      "filemgr.textedit.locked_readonly",
      { holder, until },
      `This file is currently being edited${holder}. Opened in read-only mode.${until}`
    );
  }

  async function acquire(path) {
    if (!isWorkspaceScope()) {
      return { ok: true, workspace: false };
    }

    const leaseApi = api();
    const rel = String(path || "");
    const sid = sessionId();

    if (
      !leaseApi ||
      typeof leaseApi.acquireEditLease !== "function" ||
      !rel ||
      !sid
    ) {
      throw new Error("workspace edit lease API is not ready");
    }

    const result = await leaseApi.acquireEditLease(
      rel,
      LEASE_SECONDS
    );

    activePath = rel;
    return result;
  }

  async function refresh(path = activePath) {
    if (!isWorkspaceScope()) {
      return { ok: true, workspace: false };
    }

    const leaseApi = api();
    const rel = String(path || activePath || "");

    if (
      !leaseApi ||
      typeof leaseApi.refreshEditLease !== "function" ||
      !rel
    ) {
      throw new Error("workspace edit lease refresh API is not ready");
    }

    return await leaseApi.refreshEditLease(
      rel,
      LEASE_SECONDS
    );
  }

  function stop() {
    if (refreshTimer) {
      clearInterval(refreshTimer);
      refreshTimer = 0;
    }
  }

  function start(path, onLost) {
    stop();

    activePath = String(path || "");
    if (!activePath) return;

    refreshTimer = window.setInterval(async () => {
      try {
        await refresh(activePath);
      } catch (error) {
        stop();

        if (typeof onLost === "function") {
          onLost(error);
        }
      }
    }, REFRESH_INTERVAL_MS);
  }

  async function release(path = activePath) {
    stop();

    const rel = String(path || activePath || "");
    activePath = "";

    if (!rel) return null;

    const leaseApi = api();
    if (
      !leaseApi ||
      typeof leaseApi.releaseEditLease !== "function"
    ) {
      return null;
    }

    try {
      return await leaseApi.releaseEditLease(rel);
    } catch (error) {
      console.warn(
        "[spreadsheet] workspace edit lease release failed:",
        error
      );
      return null;
    }
  }

  function saveOptions() {
    if (!isWorkspaceScope()) return {};

    const sid = sessionId();
    if (!sid) {
      throw new Error("missing workspace editor session");
    }

    return {
      workspaceEditSessionId: sid
    };
  }

  FM.spreadsheetWorkspaceLease = Object.freeze({
    acquire,
    refresh,
    release,
    start,
    stop,
    saveOptions,
    describeError,
    errorCode,
    isWorkspaceScope
  });
})();
