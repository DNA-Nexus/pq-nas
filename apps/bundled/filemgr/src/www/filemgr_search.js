window.PQNAS_FILEMGR = window.PQNAS_FILEMGR || {};

(() => {
  "use strict";

  const FM = window.PQNAS_FILEMGR;
  const MAX_QUERY_CHARS = 128;
  const MIN_REMOTE_QUERY_CHARS = 2;
  const REMOTE_MAX_RESULTS = 200;
  const RELOAD_DEBOUNCE_MS = 120;
  const REMOTE_DEBOUNCE_MS = 240;

  const host = document.getElementById("fileSearch");
  const button = document.getElementById("fileSearchBtn");
  const panel = document.getElementById("fileSearchPanel");
  const label = document.getElementById("fileSearchLabel");
  const input = document.getElementById("fileSearchInput");
  const clearButton = document.getElementById("fileSearchClearBtn");
  const count = document.getElementById("fileSearchCount");
  const remoteStatus = document.getElementById("fileSearchRemoteStatus");
  const results = document.getElementById("fileSearchResults");

  if (
    !host ||
    !button ||
    !panel ||
    !label ||
    !input ||
    !clearButton ||
    !count ||
    !remoteStatus ||
    !results
  ) {
    console.warn(
      "File Manager search UI is incomplete; search module was not initialized."
    );
    return;
  }

  let query = "";
  let reloadTimer = 0;
  let remoteTimer = 0;
  let remoteController = null;
  let remoteSerial = 0;

  let remoteState = {
    kind: "idle",
    returned: 0,
    truncated: false,
    timedOut: false,
    scanCapped: false,
    depthCapped: false
  };

  function tr(key, vars = null, fallback = "") {
    try {
      if (
        window.PQNAS_I18N &&
        typeof window.PQNAS_I18N.t === "function"
      ) {
        return window.PQNAS_I18N.t(
          key,
          vars,
          fallback || key
        );
      }
    } catch (_) {}

    return fallback || key;
  }

  function boundedQuery(value) {
    return String(value == null ? "" : value)
      .slice(0, MAX_QUERY_CHARS)
      .trim();
  }

  function normalizedText(value) {
    const text = String(value == null ? "" : value);

    try {
      return text.normalize("NFKC").toLowerCase();
    } catch (_) {
      return text.toLowerCase();
    }
  }

  function searchableText(item) {
    if (!item || typeof item !== "object") return "";

    return [
      item.name,
      item.type === "link" ? item.url : ""
    ]
      .filter((value) => value != null && value !== "")
      .join("\n");
  }

  function isActive() {
    return query.length > 0;
  }

  function isWorkspaceScope() {
    return !!(
      typeof FM.isWorkspaceScope === "function" &&
      FM.isWorkspaceScope()
    );
  }

  function shouldUseRemoteSearch() {
    return (
      query.length >= MIN_REMOTE_QUERY_CHARS &&
      !isWorkspaceScope()
    );
  }

  function filterItems(items) {
    const list = Array.isArray(items) ? items : [];

    if (!isActive()) return list;

    // Security: local filtering is a bounded plain-text comparison. User input
    // is never compiled as a regular expression or rendered as HTML.
    const needle = normalizedText(query);

    return list.filter((item) => {
      return normalizedText(
        searchableText(item)
      ).includes(needle);
    });
  }

  function parentPath(relPath) {
    const rel = String(relPath || "").replace(/^\/+|\/+$/g, "");
    const slash = rel.lastIndexOf("/");
    return slash < 0 ? "" : rel.slice(0, slash);
  }

  function refreshLabels() {
    const title = tr(
      "filemgr.search.button_title",
      null,
      "Search files"
    );

    button.title = title;
    button.setAttribute("aria-label", title);

    label.textContent = tr(
      "filemgr.search.label",
      null,
      "Search all my files"
    );

    input.placeholder = tr(
      "filemgr.search.placeholder",
      null,
      "File or folder name"
    );

    clearButton.textContent = tr(
      "filemgr.search.clear",
      null,
      "Clear search"
    );
  }

  function syncActiveState() {
    const active = isActive();

    button.classList.toggle("is-active", active);
    button.dataset.searchActive = active ? "1" : "0";
    clearButton.disabled = !active;
  }

  function clearResultNodes() {
    results.replaceChildren();
    results.hidden = true;
  }

  function resetRemoteState() {
    remoteState = {
      kind: "idle",
      returned: 0,
      truncated: false,
      timedOut: false,
      scanCapped: false,
      depthCapped: false
    };
  }

  function abortRemoteRequest() {
    window.clearTimeout(remoteTimer);
    remoteTimer = 0;

    if (remoteController) {
      remoteController.abort();
      remoteController = null;
    }

    remoteSerial += 1;
  }

  function updateRemoteStatus() {
    if (!isActive()) {
      remoteStatus.textContent = "";
      count.hidden = true;
      count.textContent = "";
      return;
    }

    if (isWorkspaceScope()) {
      remoteStatus.textContent = tr(
        "filemgr.search.workspace_local_only",
        null,
        "Workspace search currently filters this folder only."
      );
      return;
    }

    if (query.length < MIN_REMOTE_QUERY_CHARS) {
      remoteStatus.textContent = tr(
        "filemgr.search.min_chars",
        { count: MIN_REMOTE_QUERY_CHARS },
        `Type at least ${MIN_REMOTE_QUERY_CHARS} characters to search all folders.`
      );
      return;
    }

    if (remoteState.kind === "loading") {
      remoteStatus.textContent = tr(
        "filemgr.search.loading",
        null,
        "Searching all folders…"
      );
      count.hidden = true;
      return;
    }

    if (remoteState.kind === "error") {
      remoteStatus.textContent = tr(
        "filemgr.search.failed",
        null,
        "Search failed."
      );
      count.hidden = true;
      return;
    }

    if (remoteState.kind === "done") {
      const limited = !!(
        remoteState.truncated ||
        remoteState.timedOut ||
        remoteState.scanCapped ||
        remoteState.depthCapped
      );

      remoteStatus.textContent = limited
        ? tr(
            "filemgr.search.truncated",
            null,
            "Some results may be omitted because the safe search limit was reached."
          )
        : remoteState.returned
            ? tr(
                "filemgr.search.global_results",
                { count: remoteState.returned },
                `${remoteState.returned} result(s) from all folders`
              )
            : tr(
                "filemgr.search.no_global_results",
                null,
                "No matches in your folders."
              );

      count.hidden = remoteState.returned === 0;
      count.textContent = remoteState.returned
        ? tr(
            "filemgr.search.count",
            {
              shown: remoteState.returned,
              total: remoteState.returned
            },
            String(remoteState.returned)
          )
        : "";

      return;
    }

    remoteStatus.textContent = "";
  }

  function afterRender(result = {}) {
    if (shouldUseRemoteSearch()) {
      updateRemoteStatus();
      return;
    }

    const shown = Number(result.shown || 0);
    const total = Number(result.total || 0);
    const active = isActive();

    count.hidden = !active;
    count.textContent = active
      ? tr(
          "filemgr.search.count",
          { shown, total },
          `${shown} / ${total}`
        )
      : "";

    updateRemoteStatus();
  }

  function requestLocalReload() {
    window.clearTimeout(reloadTimer);

    reloadTimer = window.setTimeout(() => {
      const load = typeof FM.getLoadFn === "function"
        ? FM.getLoadFn()
        : null;

      if (typeof load !== "function") return;

      Promise.resolve(load()).catch((error) => {
        console.warn(
          "File Manager search refresh failed:",
          error
        );
      });
    }, RELOAD_DEBOUNCE_MS);
  }

  function formatResultMeta(item) {
    const type = String(item && item.type || "");

    if (type === "dir") {
      return tr(
        "filemgr.search.folder",
        null,
        "Folder"
      );
    }

    const size = Number(
      item && (
        item.size_bytes ??
        item.bytes ??
        0
      )
    );

    const sizeText =
      size > 0 &&
      typeof FM.fmtSize === "function"
        ? FM.fmtSize(size)
        : "";

    return [
      tr(
        "filemgr.search.file",
        null,
        "File"
      ),
      sizeText
    ].filter(Boolean).join(" • ");
  }

  async function openSearchResult(item, resultButton) {
    const relPath = String(item && item.path || "")
      .replace(/^\/+|\/+$/g, "");

    if (!relPath) return;

    resultButton.disabled = true;

    try {
      // Clear the active filter before navigation so the destination folder
      // does not appear empty merely because the old query remains active.
      abortRemoteRequest();
      query = "";
      input.value = "";
      syncActiveState();
      resetRemoteState();
      clearResultNodes();
      count.hidden = true;
      count.textContent = "";
      remoteStatus.textContent = "";

      if (
        item.type === "dir" &&
        typeof FM.setPathAndLoad === "function"
      ) {
        await Promise.resolve(
          FM.setPathAndLoad(relPath)
        );
      } else if (
        typeof FM.openAndHighlightRelPath === "function"
      ) {
        await Promise.resolve(
          FM.openAndHighlightRelPath(relPath)
        );
      } else if (
        typeof FM.setPathAndLoad === "function"
      ) {
        await Promise.resolve(
          FM.setPathAndLoad(parentPath(relPath))
        );
      }

      closePanel();
    } catch (error) {
      remoteStatus.textContent = tr(
        "filemgr.search.open_failed",
        null,
        "Could not open the search result."
      );

      console.warn(
        "File Manager search result open failed:",
        error
      );
    } finally {
      resultButton.disabled = false;
    }
  }

  function renderRemoteResults(payload) {
    clearResultNodes();

    const items = Array.isArray(payload && payload.results)
      ? payload.results
      : [];

    for (const item of items) {
      const relPath = String(item && item.path || "")
        .replace(/^\/+|\/+$/g, "");

      if (!relPath) continue;

      const resultButton = document.createElement("button");
      resultButton.type = "button";
      resultButton.className = "fmSearchResult";
      resultButton.setAttribute("role", "listitem");

      const name = document.createElement("span");
      name.className = "fmSearchResultName";
      name.textContent =
        String(item.name || "") ||
        relPath.split("/").pop() ||
        relPath;

      const path = document.createElement("span");
      path.className = "fmSearchResultPath";
      path.textContent = "/" + (
        String(item.parent_path || "") ||
        parentPath(relPath)
      );

      const meta = document.createElement("span");
      meta.className = "fmSearchResultMeta";
      meta.textContent = formatResultMeta(item);

      resultButton.appendChild(name);
      resultButton.appendChild(path);
      resultButton.appendChild(meta);

      resultButton.addEventListener("click", () => {
        openSearchResult(item, resultButton);
      });

      results.appendChild(resultButton);
    }

    results.hidden = results.childElementCount === 0;
  }

  async function runRemoteSearch(expectedQuery) {
    if (
      expectedQuery !== query ||
      !shouldUseRemoteSearch()
    ) {
      return;
    }

    abortRemoteRequest();

    const serial = remoteSerial;
    const controller = new AbortController();
    remoteController = controller;

    remoteState = {
      kind: "loading",
      returned: 0,
      truncated: false,
      timedOut: false,
      scanCapped: false,
      depthCapped: false
    };

    clearResultNodes();
    updateRemoteStatus();

    const params = new URLSearchParams({
      q: expectedQuery,
      max: String(REMOTE_MAX_RESULTS)
    });

    try {
      const response = await fetch(
        `/api/v4/files/search?${params.toString()}`,
        {
          method: "POST",
          credentials: "include",
          cache: "no-store",
          headers: {
            "Accept": "application/json"
          },
          signal: controller.signal
        }
      );

      const payload = await response.json().catch(() => null);

      if (
        serial !== remoteSerial ||
        expectedQuery !== query
      ) {
        return;
      }

      if (
        !response.ok ||
        !payload ||
        !payload.ok
      ) {
        throw new Error(
          payload && (
            payload.message ||
            payload.error
          ) ||
          `HTTP ${response.status}`
        );
      }

      remoteState = {
        kind: "done",
        returned: Number(
          payload.returned ??
          (
            Array.isArray(payload.results)
              ? payload.results.length
              : 0
          )
        ),
        truncated: !!payload.truncated,
        timedOut: !!payload.timed_out,
        scanCapped: !!payload.scan_capped,
        depthCapped: !!payload.depth_capped
      };

      renderRemoteResults(payload);

      if (
        remoteState.truncated ||
        remoteState.timedOut ||
        remoteState.scanCapped ||
        remoteState.depthCapped
      ) {
        remoteStatus.textContent = tr(
          "filemgr.search.truncated",
          null,
          "Some results may be omitted because the safe search limit was reached."
        );
      } else {
        updateRemoteStatus();
      }
    } catch (error) {
      if (
        error &&
        error.name === "AbortError"
      ) {
        return;
      }

      if (
        serial !== remoteSerial ||
        expectedQuery !== query
      ) {
        return;
      }

      remoteState = {
        kind: "error",
        returned: 0,
        truncated: false,
        timedOut: false,
        scanCapped: false,
        depthCapped: false
      };

      clearResultNodes();
      updateRemoteStatus();

      console.warn(
        "File Manager global search failed:",
        error
      );
    } finally {
      if (remoteController === controller) {
        remoteController = null;
      }
    }
  }

  function scheduleRemoteSearch() {
    window.clearTimeout(remoteTimer);
    remoteTimer = 0;

    if (remoteController) {
      remoteController.abort();
      remoteController = null;
    }

    remoteSerial += 1;
    resetRemoteState();
    clearResultNodes();
    updateRemoteStatus();

    if (!shouldUseRemoteSearch()) return;

    const expectedQuery = query;

    remoteTimer = window.setTimeout(() => {
      runRemoteSearch(expectedQuery);
    }, REMOTE_DEBOUNCE_MS);
  }

  function setQuery(value, options = {}) {
    const nextQuery = boundedQuery(value);
    const changed = nextQuery !== query;

    query = nextQuery;

    if (input.value !== query) {
      input.value = query;
    }

    syncActiveState();

    if (
      changed &&
      typeof FM.clearSelection === "function"
    ) {
      FM.clearSelection();
    }

    if (options.reload !== false) {
      requestLocalReload();
    }

    if (changed) {
      scheduleRemoteSearch();
    } else {
      updateRemoteStatus();
    }
  }

  function openPanel() {
    panel.hidden = false;
    button.setAttribute("aria-expanded", "true");

    window.requestAnimationFrame(() => {
      input.focus();
      input.select();
    });
  }

  function closePanel(options = {}) {
    const hadQuery = isActive();

    // UX safety: closing search must remove the hidden filter. Otherwise an
    // empty file grid can incorrectly look like the user's files disappeared.
    abortRemoteRequest();
    resetRemoteState();
    clearResultNodes();

    if (hadQuery) {
      setQuery("");
    } else {
      updateRemoteStatus();
    }

    panel.hidden = true;
    button.setAttribute("aria-expanded", "false");

    if (options.focusButton) {
      button.focus();
    }
  }

  function clearSearch() {
    abortRemoteRequest();
    resetRemoteState();
    clearResultNodes();
    setQuery("");
    input.focus();
  }

  button.addEventListener("click", () => {
    if (panel.hidden) {
      openPanel();
    } else {
      closePanel({ focusButton: true });
    }
  });

  input.addEventListener("input", () => {
    setQuery(input.value);
  });

  input.addEventListener("keydown", (event) => {
    if (event.key !== "Escape") return;

    event.preventDefault();

    if (isActive()) {
      clearSearch();
    } else {
      closePanel({ focusButton: true });
    }
  });

  clearButton.addEventListener("click", clearSearch);

  document.addEventListener("pointerdown", (event) => {
    if (
      panel.hidden ||
      host.contains(event.target)
    ) {
      return;
    }

    closePanel();
  });

  FM.search = {
    filterItems,
    isActive,
    getQuery: () => query,
    afterRender,
    open: openPanel,
    clear: clearSearch
  };

  refreshLabels();
  syncActiveState();
  resetRemoteState();
  afterRender();
})();
