window.PQNAS_FILEMGR = window.PQNAS_FILEMGR || {};

(() => {
  "use strict";

  const FM = window.PQNAS_FILEMGR;
  const XLSX_VENDOR_URL = "./vendor/xlsx.full.min.js";
  const XLSX_VENDOR_SCRIPT_ID = "pqnasXlsxVendorScriptForCreate";
  const XLSX_MIME = "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";

  let modal = null;
  let selectedTemplateId = "blank";

  function tr(key, vars = null, fallback = "") {
    try {
      if (window.PQNAS_I18N && typeof window.PQNAS_I18N.t === "function") {
        return window.PQNAS_I18N.t(key, vars, fallback || key);
      }
    } catch (_) {}
    return fallback || key;
  }

  function setGlobalStatus(text, kind = "") {
    const statusEl = FM && typeof FM.getStatusEl === "function" ? FM.getStatusEl() : null;
    if (statusEl) statusEl.textContent = String(text || "");
    if (FM && typeof FM.setBadge === "function" && kind) {
      FM.setBadge(kind === "err" ? "err" : kind === "warn" ? "warn" : "ok", kind === "err" ? "error" : "ready");
    }
  }

  function canWriteCurrentScope() {
    try {
      if (FM && typeof FM.canWriteCurrentScope === "function") return !!FM.canWriteCurrentScope();
      if (FM && typeof FM.canCurrentScopeWrite === "function") return !!FM.canCurrentScopeWrite();
    } catch (_) {}
    return true;
  }

  const templates = [
    {
      id: "blank",
      filename: "spreadsheet.xlsx",
      title: () => tr("filemgr.spreadsheet_create.template.blank.title", null, "Blank spreadsheet"),
      desc: () => tr("filemgr.spreadsheet_create.template.blank.desc", null, "Start with an empty sheet."),
      sheet: () => tr("filemgr.spreadsheet_create.sheet.sheet1", null, "Sheet1"),
      rows: () => [[]],
      cols: []
    },
    {
      id: "budget",
      filename: "budget.xlsx",
      title: () => tr("filemgr.spreadsheet_create.template.budget.title", null, "Simple budget"),
      desc: () => tr("filemgr.spreadsheet_create.template.budget.desc", null, "Budget, actuals, difference and notes."),
      sheet: () => tr("filemgr.spreadsheet_create.template.budget.sheet", null, "Budget"),
      rows: () => [
        [
          tr("filemgr.spreadsheet_create.col.category", null, "Category"),
          tr("filemgr.spreadsheet_create.col.budget", null, "Budget"),
          tr("filemgr.spreadsheet_create.col.actual", null, "Actual"),
          tr("filemgr.spreadsheet_create.col.difference", null, "Difference"),
          tr("filemgr.spreadsheet_create.col.notes", null, "Notes")
        ],
        ["", "", "", "", ""],
        ["", "", "", "", ""],
        ["", "", "", "", ""]
      ],
      cols: [{ wch: 24 }, { wch: 14 }, { wch: 14 }, { wch: 14 }, { wch: 32 }]
    },
    {
      id: "tasks",
      filename: "tasks.xlsx",
      title: () => tr("filemgr.spreadsheet_create.template.tasks.title", null, "Task list"),
      desc: () => tr("filemgr.spreadsheet_create.template.tasks.desc", null, "Track tasks, owners, status and due dates."),
      sheet: () => tr("filemgr.spreadsheet_create.template.tasks.sheet", null, "Tasks"),
      rows: () => [
        [
          tr("filemgr.spreadsheet_create.col.task", null, "Task"),
          tr("filemgr.spreadsheet_create.col.owner", null, "Owner"),
          tr("filemgr.spreadsheet_create.col.status", null, "Status"),
          tr("filemgr.spreadsheet_create.col.due_date", null, "Due date"),
          tr("filemgr.spreadsheet_create.col.notes", null, "Notes")
        ],
        ["", "", "", "", ""],
        ["", "", "", "", ""],
        ["", "", "", "", ""]
      ],
      cols: [{ wch: 32 }, { wch: 20 }, { wch: 16 }, { wch: 16 }, { wch: 36 }]
    },
    {
      id: "inventory",
      filename: "inventory.xlsx",
      title: () => tr("filemgr.spreadsheet_create.template.inventory.title", null, "Inventory list"),
      desc: () => tr("filemgr.spreadsheet_create.template.inventory.desc", null, "Track items, quantities, locations and reorder levels."),
      sheet: () => tr("filemgr.spreadsheet_create.template.inventory.sheet", null, "Inventory"),
      rows: () => [
        [
          tr("filemgr.spreadsheet_create.col.item", null, "Item"),
          tr("filemgr.spreadsheet_create.col.sku", null, "SKU"),
          tr("filemgr.spreadsheet_create.col.quantity", null, "Quantity"),
          tr("filemgr.spreadsheet_create.col.location", null, "Location"),
          tr("filemgr.spreadsheet_create.col.reorder_level", null, "Reorder level"),
          tr("filemgr.spreadsheet_create.col.notes", null, "Notes")
        ],
        ["", "", "", "", "", ""],
        ["", "", "", "", "", ""],
        ["", "", "", "", "", ""]
      ],
      cols: [{ wch: 28 }, { wch: 18 }, { wch: 12 }, { wch: 20 }, { wch: 14 }, { wch: 34 }]
    }
  ];

  function templateById(id) {
    return templates.find((t) => t.id === id) || templates[0];
  }

  function currentFolderLabel() {
    try {
      const p = FM && typeof FM.getCurPath === "function" ? String(FM.getCurPath() || "") : "";
      return p ? "/" + p : "/";
    } catch (_) {
      return "/";
    }
  }

  function ensureXlsxLibrary() {
    if (window.XLSX && typeof window.XLSX.write === "function" && window.XLSX.utils) {
      return Promise.resolve(window.XLSX);
    }

    return new Promise((resolve, reject) => {
      let script = document.getElementById(XLSX_VENDOR_SCRIPT_ID) ||
        document.querySelector('script[src*="xlsx.full.min.js"]');

      const finish = () => {
        if (window.XLSX && typeof window.XLSX.write === "function" && window.XLSX.utils) {
          resolve(window.XLSX);
        } else {
          reject(new Error(tr(
            "filemgr.spreadsheet_create.xlsx_missing",
            null,
            "Spreadsheet writer is not available. Check vendor/xlsx.full.min.js."
          )));
        }
      };

      if (script) {
        script.addEventListener("load", finish, { once: true });
        script.addEventListener("error", () => reject(new Error(tr(
          "filemgr.spreadsheet_create.xlsx_missing",
          null,
          "Spreadsheet writer is not available. Check vendor/xlsx.full.min.js."
        ))), { once: true });
        window.setTimeout(finish, 0);
        return;
      }

      script = document.createElement("script");
      script.id = XLSX_VENDOR_SCRIPT_ID;
      script.src = XLSX_VENDOR_URL;
      script.async = true;
      script.addEventListener("load", finish, { once: true });
      script.addEventListener("error", () => reject(new Error(tr(
        "filemgr.spreadsheet_create.xlsx_missing",
        null,
        "Spreadsheet writer is not available. Check vendor/xlsx.full.min.js."
      ))), { once: true });

      document.head.appendChild(script);
    });
  }

  function buildWorkbook(XLSX, tpl) {
    const wb = XLSX.utils.book_new();
    const ws = XLSX.utils.aoa_to_sheet(tpl.rows());
    if (Array.isArray(tpl.cols) && tpl.cols.length) ws["!cols"] = tpl.cols;
    XLSX.utils.book_append_sheet(wb, ws, String(tpl.sheet() || "Sheet1").slice(0, 31));
    return wb;
  }

  function normalizeFilename(raw) {
    let name = String(raw || "").trim();

    // Keep generated uploads confined to the current folder; never accept a path here.
    name = name.replace(/\\/g, "/").split("/").pop();
    name = name.replace(/[\u0000-\u001f\u007f]/g, "");
    name = name.replace(/[<>:"|?*]/g, "-");
    name = name.replace(/\s+/g, " ").trim();

    if (name && !/\.xlsx$/i.test(name)) name += ".xlsx";
    return name;
  }

  function isSafeXlsxLeafName(name) {
    if (!name) return false;
    if (name === "." || name === "..") return false;
    if (name.includes("/") || name.includes("\\")) return false;
    if (!/\.xlsx$/i.test(name)) return false;
    if (name.length > 180) return false;
    return true;
  }

  async function createXlsxFile(filename, templateId) {
    const XLSX = await ensureXlsxLibrary();
    const tpl = templateById(templateId);
    const wb = buildWorkbook(XLSX, tpl);
    const out = XLSX.write(wb, { bookType: "xlsx", type: "array" });
    const blob = new Blob([out], { type: XLSX_MIME });

    try {
      return new File([blob], filename, { type: XLSX_MIME, lastModified: Date.now() });
    } catch (_) {
      blob.name = filename;
      blob.lastModified = Date.now();
      return blob;
    }
  }

  function setDialogStatus(text, kind = "") {
    const el = modal ? modal.querySelector(".spreadsheetCreateStatus") : null;
    if (!el) return;
    el.textContent = String(text || "");
    el.dataset.kind = kind || "";
  }

  function setBusy(on) {
    if (!modal) return;
    const create = modal.querySelector("[data-spreadsheet-create-submit]");
    const cancel = modal.querySelector("[data-spreadsheet-create-cancel]");
    const input = modal.querySelector("[data-spreadsheet-create-name]");
    const cards = Array.from(modal.querySelectorAll("[data-spreadsheet-template]"));

    if (create) {
      create.disabled = !!on;
      create.textContent = on
        ? tr("filemgr.spreadsheet_create.creating", null, "Creating…")
        : tr("filemgr.spreadsheet_create.create", null, "Create spreadsheet");
    }
    if (cancel) cancel.disabled = !!on;
    if (input) input.disabled = !!on;
    for (const card of cards) card.disabled = !!on;
  }

  function renderTemplates(container) {
    container.replaceChildren();

    for (const tpl of templates) {
      const btn = document.createElement("button");
      btn.type = "button";
      btn.className = "spreadsheetCreateTemplate";
      btn.dataset.spreadsheetTemplate = tpl.id;
      btn.setAttribute("aria-pressed", tpl.id === selectedTemplateId ? "true" : "false");

      const title = document.createElement("span");
      title.className = "spreadsheetCreateTemplateTitle";
      title.textContent = tpl.title();

      const desc = document.createElement("span");
      desc.className = "spreadsheetCreateTemplateDesc";
      desc.textContent = tpl.desc();

      btn.append(title, desc);
      btn.addEventListener("click", () => {
        selectedTemplateId = tpl.id;
        for (const el of container.querySelectorAll("[data-spreadsheet-template]")) {
          el.setAttribute("aria-pressed", el.dataset.spreadsheetTemplate === selectedTemplateId ? "true" : "false");
        }

        const input = modal && modal.querySelector("[data-spreadsheet-create-name]");
        if (input && !input.dataset.userEdited) input.value = tpl.filename;
      });

      container.appendChild(btn);
    }
  }

  function ensureModal() {
    if (modal) return modal;

    modal = document.createElement("div");
    modal.className = "spreadsheetCreateBackdrop";
    modal.hidden = true;
    modal.setAttribute("aria-hidden", "true");

    const card = document.createElement("div");
    card.className = "spreadsheetCreateCard";
    card.setAttribute("role", "dialog");
    card.setAttribute("aria-modal", "true");
    card.setAttribute("aria-labelledby", "spreadsheetCreateTitle");

    const head = document.createElement("div");
    head.className = "spreadsheetCreateHead";

    const titleWrap = document.createElement("div");

    const title = document.createElement("div");
    title.id = "spreadsheetCreateTitle";
    title.className = "spreadsheetCreateTitle";
    title.textContent = tr("filemgr.spreadsheet_create.title", null, "New spreadsheet");

    const sub = document.createElement("div");
    sub.className = "spreadsheetCreateSub";
    sub.textContent = tr("filemgr.spreadsheet_create.subtitle", null, "Choose a template and create an .xlsx file in the current folder.");

    titleWrap.append(title, sub);

    const close = document.createElement("button");
    close.type = "button";
    close.className = "btn secondary";
    close.textContent = tr("filemgr.close", null, "Close");
    close.addEventListener("click", closeDialog);

    head.append(titleWrap, close);

    const body = document.createElement("div");
    body.className = "spreadsheetCreateBody";

    const templateGrid = document.createElement("div");
    templateGrid.className = "spreadsheetCreateTemplates";

    const form = document.createElement("div");
    form.className = "spreadsheetCreateForm";

    const folderLine = document.createElement("div");
    folderLine.className = "spreadsheetCreateFolder mono";

    const folderLabel = document.createElement("span");
    folderLabel.textContent = tr("filemgr.spreadsheet_create.target_folder", null, "Target folder");

    const folderValue = document.createElement("strong");
    folderValue.dataset.spreadsheetCreateFolder = "1";
    folderValue.textContent = currentFolderLabel();

    folderLine.append(folderLabel, folderValue);

    const label = document.createElement("label");
    label.className = "spreadsheetCreateField";

    const labelText = document.createElement("span");
    labelText.textContent = tr("filemgr.spreadsheet_create.filename_label", null, "File name");

    const input = document.createElement("input");
    input.type = "text";
    input.autocomplete = "off";
    input.spellcheck = false;
    input.dataset.spreadsheetCreateName = "1";
    input.value = templateById(selectedTemplateId).filename;
    input.placeholder = tr("filemgr.spreadsheet_create.filename_placeholder", null, "example.xlsx");
    input.addEventListener("input", () => {
      input.dataset.userEdited = "1";
      setDialogStatus("", "");
    });

    label.append(labelText, input);

    const status = document.createElement("div");
    status.className = "spreadsheetCreateStatus";
    status.setAttribute("aria-live", "polite");

    form.append(folderLine, label, status);

    const foot = document.createElement("div");
    foot.className = "spreadsheetCreateFoot";

    const cancel = document.createElement("button");
    cancel.type = "button";
    cancel.className = "btn secondary";
    cancel.dataset.spreadsheetCreateCancel = "1";
    cancel.textContent = tr("filemgr.cancel", null, "Cancel");
    cancel.addEventListener("click", closeDialog);

    const create = document.createElement("button");
    create.type = "button";
    create.className = "btn";
    create.dataset.spreadsheetCreateSubmit = "1";
    create.textContent = tr("filemgr.spreadsheet_create.create", null, "Create spreadsheet");
    create.addEventListener("click", submitCreate);

    foot.append(cancel, create);
    body.append(templateGrid, form);
    card.append(head, body, foot);
    modal.appendChild(card);

    modal.addEventListener("click", (ev) => {
      if (ev.target === modal) closeDialog();
    });

    document.addEventListener("keydown", (ev) => {
      if (ev.key === "Escape" && modal && !modal.hidden) closeDialog();
    });

    document.body.appendChild(modal);
    renderTemplates(templateGrid);
    return modal;
  }

  function closeDialog() {
    if (!modal) return;
    modal.hidden = true;
    modal.setAttribute("aria-hidden", "true");
  }

  function openDialog() {
    if (!canWriteCurrentScope()) {
      const msg = tr(
        "filemgr.spreadsheet_create.read_only",
        null,
        "New spreadsheet is not available because the current location is read-only."
      );
      setGlobalStatus(msg, "warn");
      return;
    }

    const dlg = ensureModal();
    const folder = dlg.querySelector("[data-spreadsheet-create-folder]");
    if (folder) folder.textContent = currentFolderLabel();

    const input = dlg.querySelector("[data-spreadsheet-create-name]");
    if (input) {
      input.dataset.userEdited = "";
      input.value = templateById(selectedTemplateId).filename;
    }

    setDialogStatus("", "");
    setBusy(false);

    dlg.hidden = false;
    dlg.setAttribute("aria-hidden", "false");

    window.setTimeout(() => {
      const first = dlg.querySelector("[data-spreadsheet-template]");
      if (first) first.focus();
    }, 0);
  }

  async function submitCreate() {
    if (!modal) return;

    if (!canWriteCurrentScope()) {
      setDialogStatus(tr(
        "filemgr.spreadsheet_create.read_only",
        null,
        "New spreadsheet is not available because the current location is read-only."
      ), "warn");
      return;
    }

    const input = modal.querySelector("[data-spreadsheet-create-name]");
    const filename = normalizeFilename(input ? input.value : "");

    if (!filename) {
      setDialogStatus(tr("filemgr.spreadsheet_create.filename_required", null, "Enter a file name."), "err");
      if (input) input.focus();
      return;
    }

    if (!isSafeXlsxLeafName(filename)) {
      setDialogStatus(tr(
        "filemgr.spreadsheet_create.filename_invalid",
        null,
        "Use a simple .xlsx file name without folders."
      ), "err");
      if (input) input.focus();
      return;
    }

    if (!FM || typeof FM.uploadGeneratedFile !== "function") {
      setDialogStatus(tr(
        "filemgr.spreadsheet_create.upload_missing",
        null,
        "File Manager upload helper is not ready."
      ), "err");
      return;
    }

    setBusy(true);
    setDialogStatus(tr("filemgr.spreadsheet_create.creating", null, "Creating…"), "warn");

    try {
      const file = await createXlsxFile(filename, selectedTemplateId);
      closeDialog();
      await FM.uploadGeneratedFile(file, filename);
      setGlobalStatus(tr("filemgr.spreadsheet_create.created", { name: filename }, `Created spreadsheet: ${filename}`), "ok");
    } catch (e) {
      const msg = String(e && e.message ? e.message : e);
      setDialogStatus(tr("filemgr.spreadsheet_create.failed", { error: msg }, `New spreadsheet failed: ${msg}`), "err");
      setGlobalStatus(tr("filemgr.spreadsheet_create.failed", { error: msg }, `New spreadsheet failed: ${msg}`), "err");
    } finally {
      setBusy(false);
    }
  }

  function closeActionsMenu() {
    const menu = document.getElementById("fmActionsMenu");
    const toggle = document.getElementById("fmActionsMenuBtn");
    const panel = document.getElementById("fmActionsMenuPanel");

    if (menu) menu.classList.remove("open");
    if (toggle) toggle.setAttribute("aria-expanded", "false");
    if (panel) panel.setAttribute("aria-hidden", "true");
  }

  function toggleActionsMenu() {
    const menu = document.getElementById("fmActionsMenu");
    const toggle = document.getElementById("fmActionsMenuBtn");
    const panel = document.getElementById("fmActionsMenuPanel");
    if (!menu || !toggle || !panel) return;

    const open = !menu.classList.contains("open");
    menu.classList.toggle("open", open);
    toggle.setAttribute("aria-expanded", open ? "true" : "false");
    panel.setAttribute("aria-hidden", open ? "false" : "true");

    if (open) {
      window.setTimeout(() => {
        const first = panel.querySelector("button");
        if (first) first.focus();
      }, 0);
    }
  }

  function initButton() {
    const menuBtn = document.getElementById("fmActionsMenuBtn");
    if (menuBtn && menuBtn.dataset.actionsMenuBound !== "1") {
      menuBtn.dataset.actionsMenuBound = "1";
      menuBtn.addEventListener("click", (ev) => {
        ev.preventDefault();
        ev.stopPropagation();
        toggleActionsMenu();
      });

      document.addEventListener("click", (ev) => {
        const menu = document.getElementById("fmActionsMenu");
        if (menu && !menu.contains(ev.target)) closeActionsMenu();
      });

      document.addEventListener("keydown", (ev) => {
        if (ev.key === "Escape") closeActionsMenu();
      });
    }

    const btn = document.getElementById("newSpreadsheetBtn");
    if (!btn || btn.dataset.spreadsheetCreateBound === "1") return;

    btn.dataset.spreadsheetCreateBound = "1";
    btn.addEventListener("click", (ev) => {
      ev.preventDefault();
      closeActionsMenu();
      openDialog();
    });
  }

  FM.spreadsheetCreate = {
    open: openDialog
  };

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", initButton, { once: true });
  } else {
    initButton();
  }

  window.addEventListener("pqnas-language-changed", () => {
    if (modal) {
      modal.remove();
      modal = null;
    }
    initButton();
  });
})();
