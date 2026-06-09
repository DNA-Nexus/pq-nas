(() => {
  "use strict";

  const el = (id) => document.getElementById(id);

  const statusLine = el("statusLine");
  const zonesList = el("zonesList");
  const refreshBtn = el("refreshBtn");
  const createBtn = el("createBtn");

  const createModal = el("createModal");
  const modalCloseBtn = el("modalCloseBtn");
  const cancelCreateBtn = el("cancelCreateBtn");
  const createForm = el("createForm");
  const createResult = el("createResult");
  const submitCreateBtn = el("submitCreateBtn");

  const zoneNameInput = el("zoneNameInput");
  const destInput = el("destInput");
  const expiryInput = el("expiryInput");
  const passwordInput = el("passwordInput");
  const maxFileInput = el("maxFileInput");
  const maxTotalInput = el("maxTotalInput");

  const brandEnabledInput = el("brandEnabledInput");
  const brandCompanyInput = el("brandCompanyInput");
  const brandLogoUrlInput = el("brandLogoUrlInput");
  const brandLogoFileInput = el("brandLogoFileInput");
  const brandLogoPickBtn = el("brandLogoPickBtn");
  const brandLogoClearBtn = el("brandLogoClearBtn");
  const brandLogoPreview = el("brandLogoPreview");
  const brandLogoPreviewEmpty = el("brandLogoPreviewEmpty");
  const brandLogoStatus = el("brandLogoStatus");
  const brandTitleInput = el("brandTitleInput");
  const brandDescriptionInput = el("brandDescriptionInput");
  const brandPrimaryColorInput = el("brandPrimaryColorInput");
  const brandBackgroundColorInput = el("brandBackgroundColorInput");
  const brandButtonTextInput = el("brandButtonTextInput");
  const brandFooterTextInput = el("brandFooterTextInput");

  const brandTemplateSelect = el("brandTemplateSelect");
  const brandTemplateApplyBtn = el("brandTemplateApplyBtn");
  const brandTemplateSaveBtn = el("brandTemplateSaveBtn");
  const brandTemplateDeleteBtn = el("brandTemplateDeleteBtn");


  const BRAND_TEMPLATE_STORAGE_KEY = "pqnas.dropzone.brandTemplates.v1";

  const BUILTIN_BRAND_TEMPLATES = [
    {
      id: "builtin_secure_documents",
      name: "Secure documents",
      branding: {
        title: "Send documents securely",
        description: "Upload contracts, forms and other documents directly to our secure DNA-Nexus server.",
        primary_color: "#ff9f1c",
        background_color: "#080a0f",
        button_text: "Upload documents",
        footer_text: "Secured by DNA-Nexus"
      }
    },
    {
      id: "builtin_accounting_receipts",
      name: "Accounting receipts",
      branding: {
        title: "Send receipts and payroll files",
        description: "Upload receipts, invoices, payroll material and accounting documents securely.",
        primary_color: "#2f80ed",
        background_color: "#07111f",
        button_text: "Upload accounting files",
        footer_text: "Secure file intake powered by DNA-Nexus"
      }
    },
    {
      id: "builtin_media_delivery",
      name: "Photo / video delivery",
      branding: {
        title: "Send photos and videos",
        description: "Upload large media files directly to our secure storage. No email attachment limits.",
        primary_color: "#9b51e0",
        background_color: "#10091a",
        button_text: "Upload media files",
        footer_text: "Large file delivery secured by DNA-Nexus"
      }
    },
    {
      id: "builtin_support_logs",
      name: "Support logs",
      branding: {
        title: "Send support files",
        description: "Upload logs, screenshots and diagnostic files so our support team can help you faster.",
        primary_color: "#27ae60",
        background_color: "#06140d",
        button_text: "Upload support files",
        footer_text: "Support file upload secured by DNA-Nexus"
      }
    },
    {
      id: "builtin_job_applications",
      name: "Job applications",
      branding: {
        title: "Send your application securely",
        description: "Upload your CV, application letter and attachments through this secure upload page.",
        primary_color: "#f2994a",
        background_color: "#111014",
        button_text: "Upload application",
        footer_text: "Recruitment file intake secured by DNA-Nexus"
      }
    }
  ];

  function dzString(v) {
    return String(v == null ? "" : v).trim();
  }

  function currentBrandingFromForm() {
    return {
      company_name: dzString(brandCompanyInput?.value),
      logo_url: dzString(brandLogoUrlInput?.value),
      title: dzString(brandTitleInput?.value),
      description: dzString(brandDescriptionInput?.value),
      primary_color: dzString(brandPrimaryColorInput?.value),
      background_color: dzString(brandBackgroundColorInput?.value),
      button_text: dzString(brandButtonTextInput?.value),
      footer_text: dzString(brandFooterTextInput?.value)
    };
  }

  function applyBrandingToForm(branding, opts = {}) {
    const b = branding && typeof branding === "object" ? branding : {};
    const preserveIdentity = !!opts.preserveIdentity;

    if (brandEnabledInput) brandEnabledInput.checked = true;

    if (!preserveIdentity || Object.prototype.hasOwnProperty.call(b, "company_name")) {
      if (brandCompanyInput && b.company_name != null) brandCompanyInput.value = String(b.company_name || "");
    }

    if (!preserveIdentity || Object.prototype.hasOwnProperty.call(b, "logo_url")) {
      if (brandLogoUrlInput && b.logo_url != null) brandLogoUrlInput.value = String(b.logo_url || "");
    }

    updateBrandLogoPreview();

    if (brandTitleInput && b.title != null) brandTitleInput.value = String(b.title || "");
    if (brandDescriptionInput && b.description != null) brandDescriptionInput.value = String(b.description || "");
    if (brandPrimaryColorInput && b.primary_color) brandPrimaryColorInput.value = String(b.primary_color);
    if (brandBackgroundColorInput && b.background_color) brandBackgroundColorInput.value = String(b.background_color);
    if (brandButtonTextInput && b.button_text != null) brandButtonTextInput.value = String(b.button_text || "");
    if (brandFooterTextInput && b.footer_text != null) brandFooterTextInput.value = String(b.footer_text || "");
  }

  function loadCustomBrandTemplates() {
    try {
      const raw = localStorage.getItem(BRAND_TEMPLATE_STORAGE_KEY);
      const parsed = raw ? JSON.parse(raw) : [];
      if (!Array.isArray(parsed)) return [];

      return parsed
          .filter((t) => t && typeof t === "object")
          .filter((t) => typeof t.id === "string" && typeof t.name === "string")
          .filter((t) => t.branding && typeof t.branding === "object")
          .slice(0, 50);
    } catch (_) {
      return [];
    }
  }

  function saveCustomBrandTemplates(templates) {
    try {
      localStorage.setItem(BRAND_TEMPLATE_STORAGE_KEY, JSON.stringify((templates || []).slice(0, 50)));
      return true;
    } catch (_) {
      return false;
    }
  }

  function allBrandTemplates() {
    const builtins = BUILTIN_BRAND_TEMPLATES.map((t) => ({ ...t, builtin: true }));
    const custom = loadCustomBrandTemplates().map((t) => ({ ...t, builtin: false }));
    return { builtins, custom, all: [...builtins, ...custom] };
  }

  function refreshBrandTemplateSelect(selectedId = "") {
    if (!brandTemplateSelect) return;

    const { builtins, custom } = allBrandTemplates();

    brandTemplateSelect.innerHTML = "";

    const empty = document.createElement("option");
    empty.value = "";
    empty.textContent = "Choose template…";
    brandTemplateSelect.appendChild(empty);

    const addGroup = (label, rows) => {
      if (!rows.length) return;

      const group = document.createElement("optgroup");
      group.label = label;

      for (const row of rows) {
        const opt = document.createElement("option");
        opt.value = row.id;
        opt.textContent = row.name;
        group.appendChild(opt);
      }

      brandTemplateSelect.appendChild(group);
    };

    addGroup("Built-in", builtins);
    addGroup("Saved", custom);

    if (selectedId) {
      brandTemplateSelect.value = selectedId;
    }
  }

  function selectedBrandTemplate() {
    const id = dzString(brandTemplateSelect?.value);
    if (!id) return null;

    const { all } = allBrandTemplates();
    return all.find((t) => t.id === id) || null;
  }

  function applySelectedBrandTemplate() {
    const t = selectedBrandTemplate();
    if (!t) return;

    applyBrandingToForm(t.branding, {
      preserveIdentity: !!t.builtin
    });
  }

  function saveCurrentBrandTemplate() {
    if (brandEnabledInput) brandEnabledInput.checked = true;

    const current = selectedBrandTemplate();
    const currentCustom = current && !current.builtin ? current : null;

    const branding = currentBrandingFromForm();
    const fallbackName =
        branding.company_name ||
        branding.title ||
        "Branded upload page";

    const name = dzString(window.prompt("Template name", currentCustom ? currentCustom.name : fallbackName));
    if (!name) return;

    const custom = loadCustomBrandTemplates();
    const id = currentCustom
        ? currentCustom.id
        : `custom_${Date.now().toString(36)}_${Math.random().toString(36).slice(2, 8)}`;

    const next = custom.filter((t) => t.id !== id);
    next.unshift({
      id,
      name: name.slice(0, 120),
      branding
    });

    if (!saveCustomBrandTemplates(next)) {
      window.alert("Could not save template. Browser storage may be full or disabled.");
      return;
    }

    refreshBrandTemplateSelect(id);
  }

  function deleteSelectedBrandTemplate() {
    const t = selectedBrandTemplate();
    if (!t) return;

    if (t.builtin) {
      window.alert("Built-in templates cannot be deleted.");
      return;
    }

    const ok = window.confirm(`Delete template "${t.name}"?`);
    if (!ok) return;

    const next = loadCustomBrandTemplates().filter((row) => row.id !== t.id);
    saveCustomBrandTemplates(next);
    refreshBrandTemplateSelect("");
  }

  async function getAppVersion() {
    const m = location.pathname.match(/^\/apps\/([^/]+)\/([^/]+)\//);
    if (m && m[2]) return decodeURIComponent(m[2]);

    for (const url of ["../manifest.json", "./manifest.json"]) {
      try {
        const r = await fetch(url, {
          cache: "no-store",
          headers: { "Accept": "application/json" }
        });
        if (!r.ok) continue;
        const j = await r.json();
        const ver = j && typeof j.version === "string" ? j.version.trim() : "";
        if (ver) return ver;
      } catch (_) {}
    }

    return "";
  }

  async function initAppVersion() {
    const versionEl = el("appVersion");
    if (!versionEl) return;

    const ver = await getAppVersion();
    if (!ver) {
      versionEl.hidden = true;
      return;
    }

    versionEl.textContent = `v${ver}`;
    versionEl.title = `Drop Zone ${ver}`;
    versionEl.hidden = false;
  }

  initAppVersion();

  function escapeHtml(s) {
    return String(s == null ? "" : s)
        .replaceAll("&", "&amp;")
        .replaceAll("<", "&lt;")
        .replaceAll(">", "&gt;")
        .replaceAll("\"", "&quot;")
        .replaceAll("'", "&#39;");
  }

  function tr(key, vars = null, fallback = "") {
    try {
      if (window.PQNAS_I18N && typeof window.PQNAS_I18N.t === "function") {
        return window.PQNAS_I18N.t(key, vars, fallback || key);
      }
    } catch (_) {}
    return fallback || key;
  }

  function ensureDropZoneConfirmCss() {
    if (document.getElementById("dropZoneConfirmCss")) return;

    const style = document.createElement("style");
    style.id = "dropZoneConfirmCss";
    style.textContent = `
.dzConfirmBackdrop{
  position:fixed;
  inset:0;
  z-index:100000;
  display:flex;
  align-items:center;
  justify-content:center;
  padding:18px;
  background:rgba(0,0,0,.55);
  backdrop-filter:blur(6px);
  -webkit-backdrop-filter:blur(6px);
}
.dzConfirmCard{
  width:min(560px, calc(100vw - 24px));
  border:1px solid rgba(255,255,255,.18);
  border-radius:18px;
  overflow:hidden;
  background:var(--panel,#181818);
  color:var(--fg,#f5f5f5);
  box-shadow:0 18px 70px rgba(0,0,0,.45);
}
.dzConfirmHead{
  padding:14px 16px;
  border-bottom:1px solid rgba(255,255,255,.12);
  background:rgba(255,255,255,.06);
}
.dzConfirmTitle{
  font-weight:950;
  letter-spacing:.2px;
}
.dzConfirmBody{
  padding:16px;
}
.dzConfirmMessage{
  padding:10px 12px;
  border-radius:14px;
  border:1px solid rgba(255,190,90,.35);
  background:rgba(255,190,90,.10);
  white-space:pre-wrap;
  line-height:1.45;
}
.dzConfirmFoot{
  display:flex;
  justify-content:flex-end;
  gap:10px;
  padding:12px 16px;
  border-top:1px solid rgba(255,255,255,.12);
  background:rgba(255,255,255,.06);
}
.dzConfirmBtn{
  appearance:none;
  border:1px solid rgba(255,255,255,.22);
  border-radius:12px;
  padding:9px 13px;
  font-weight:850;
  cursor:pointer;
  background:rgba(255,255,255,.08);
  color:inherit;
}
.dzConfirmBtn.danger{
  border-color:rgba(180,40,40,.55);
  background:rgba(180,40,40,.18);
}
`;
    document.head.appendChild(style);
  }

  function openDropZoneConfirmModal(opts = {}) {
    ensureDropZoneConfirmCss();

    return new Promise((resolve) => {
      const options = opts || {};

      const modal = document.createElement("div");
      modal.className = "dzConfirmBackdrop";
      modal.setAttribute("role", "dialog");
      modal.setAttribute("aria-modal", "true");

      const card = document.createElement("div");
      card.className = "dzConfirmCard";

      const head = document.createElement("div");
      head.className = "dzConfirmHead";

      const title = document.createElement("div");
      title.className = "dzConfirmTitle";
      title.textContent = options.title || tr("dropzone.confirm.title", null, "Confirm");

      const body = document.createElement("div");
      body.className = "dzConfirmBody";

      const message = document.createElement("div");
      message.className = "dzConfirmMessage";
      message.textContent = options.message || "";

      const foot = document.createElement("div");
      foot.className = "dzConfirmFoot";

      const cancelBtn = document.createElement("button");
      cancelBtn.type = "button";
      cancelBtn.className = "dzConfirmBtn";
      cancelBtn.textContent = options.cancelText || tr("dropzone.cancel", null, "Cancel");

      const okBtn = document.createElement("button");
      okBtn.type = "button";
      okBtn.className = options.danger ? "dzConfirmBtn danger" : "dzConfirmBtn";
      okBtn.textContent = options.confirmText || tr("dropzone.ok", null, "OK");

      head.appendChild(title);
      body.appendChild(message);
      foot.appendChild(cancelBtn);
      foot.appendChild(okBtn);

      card.appendChild(head);
      card.appendChild(body);
      card.appendChild(foot);
      modal.appendChild(card);
      document.body.appendChild(modal);

      const finish = (value) => {
        document.removeEventListener("keydown", onKey, true);
        modal.remove();
        resolve(!!value);
      };

      const onKey = (ev) => {
        if (ev.key === "Escape") {
          ev.preventDefault();
          ev.stopPropagation();
          finish(false);
          return;
        }
        if (ev.key === "Enter") {
          ev.preventDefault();
          ev.stopPropagation();
          finish(true);
        }
      };

      document.addEventListener("keydown", onKey, true);
      modal.addEventListener("click", (ev) => {
        if (ev.target === modal) finish(false);
      });
      cancelBtn.addEventListener("click", () => finish(false));
      okBtn.addEventListener("click", () => finish(true));

      setTimeout(() => cancelBtn.focus(), 0);
    });
  }

  function setStatus(text) {
    if (statusLine) statusLine.textContent = text || "";
  }

  function fmtBytes(n) {
    n = Number(n || 0);
    if (!Number.isFinite(n) || n <= 0) return tr("dropzone.no_limit", null, "No limit");

    const units = ["B", "KiB", "MiB", "GiB", "TiB"];
    let i = 0;

    while (n >= 1024 && i < units.length - 1) {
      n /= 1024;
      i++;
    }

    return `${n.toFixed(i === 0 ? 0 : 1)} ${units[i]}`;
  }

  function fmtEpoch(epoch) {
    const n = Number(epoch || 0);
    if (!Number.isFinite(n) || n <= 0) return tr("dropzone.never", null, "Never");

    try {
      return new Date(n * 1000).toLocaleString();
    } catch (_) {
      return String(epoch);
    }
  }

  function nowEpoch() {
    return Math.floor(Date.now() / 1000);
  }

  function dropZoneStatus(z) {
    const raw = String(z?.status || "").trim().toLowerCase();
    const expiresEpoch = Number(z?.expires_epoch || 0);
    const expired = raw === "expired" ||
        !!z?.expired ||
        (Number.isFinite(expiresEpoch) && expiresEpoch > 0 && expiresEpoch <= nowEpoch());

    if (!!z?.disabled || raw === "disabled") {
      return {
        key: "disabled",
        label: tr("dropzone.disabled", null, "Disabled"),
        className: "bad"
      };
    }

    if (expired) {
      return {
        key: "expired",
        label: tr("dropzone.expired", null, "Expired"),
        className: "warn"
      };
    }

    return {
      key: "active",
      label: tr("dropzone.active", null, "Active"),
      className: "ok"
    };
  }

  const DROPZONE_LINK_STORAGE_KEY = "pqnas.dropzone.ownerLinks.v1";

  let dropZoneListCache = [];
  let dropZoneSearchText = "";
  let dropZoneStatusFilter = "all";
  const expandedDropZoneIds = new Set();

  function readSavedDropZoneLinks() {
    try {
      const raw = localStorage.getItem(DROPZONE_LINK_STORAGE_KEY);
      const parsed = raw ? JSON.parse(raw) : {};
      return parsed && typeof parsed === "object" ? parsed : {};
    } catch (_) {
      return {};
    }
  }

  function saveDropZoneLink(id, url) {
    const zoneId = String(id || "").trim();
    const fullUrl = String(url || "").trim();
    if (!zoneId || !fullUrl) return;

    try {
      const links = readSavedDropZoneLinks();
      links[zoneId] = {
        url: fullUrl,
        savedAt: Date.now()
      };
      localStorage.setItem(DROPZONE_LINK_STORAGE_KEY, JSON.stringify(links));
    } catch (_) {}
  }

  function dropZonePublicUrl(z) {
    const id = String(z?.id || "").trim();
    const saved = id ? readSavedDropZoneLinks()[id] : null;

    const candidates = [
      z?.full_url,
      z?.public_url,
      z?.url,
      z?.public_path,
      saved && saved.url
    ];

    for (const value of candidates) {
      const link = String(value || "").trim();
      if (!link) continue;
      if (link.startsWith("https://") || link.startsWith("http://")) return link;
      if (link.startsWith("/") && !link.startsWith("//")) return `${window.location.origin}${link}`;
    }

    return "";
  }

  function setBusy(on) {
    if (refreshBtn) refreshBtn.disabled = !!on;
    if (createBtn) createBtn.disabled = !!on;
  }

  function setCreateBusy(on) {
    if (submitCreateBtn) submitCreateBtn.disabled = !!on;
    if (cancelCreateBtn) cancelCreateBtn.disabled = !!on;
    if (modalCloseBtn) modalCloseBtn.disabled = !!on;
  }

  function renderEmpty(message) {
    if (!zonesList) return;
    zonesList.innerHTML = `<div class="dzEmpty">${escapeHtml(message)}</div>`;
  }

  function openCreateModal() {
    if (!createModal) return;

    if (createForm) createForm.reset();
    if (createResult) {
      createResult.classList.add("hidden");
      createResult.innerHTML = "";
    }

    if (zoneNameInput) zoneNameInput.value = tr("dropzone.default_name", null, "Drop Zone");
    if (destInput) destInput.value = tr("dropzone.default_destination", null, "Incoming/Drop Zones/Drop Zone");

    if (brandEnabledInput) brandEnabledInput.checked = false;
    if (brandCompanyInput) brandCompanyInput.value = "";
    if (brandLogoUrlInput) brandLogoUrlInput.value = "";
    clearBrandLogoPreviewStatus();
    updateBrandLogoPreview();
    if (brandTitleInput) brandTitleInput.value = "Send files securely";
    if (brandDescriptionInput) brandDescriptionInput.value = "Upload files directly to our secure DNA-Nexus server.";
    if (brandPrimaryColorInput) brandPrimaryColorInput.value = "#ff9f1c";
    if (brandBackgroundColorInput) brandBackgroundColorInput.value = "#080a0f";
    if (brandButtonTextInput) brandButtonTextInput.value = "Upload files";
    if (brandFooterTextInput) brandFooterTextInput.value = "Secured by DNA-Nexus";

    refreshBrandTemplateSelect();

    createModal.classList.remove("hidden");
    createModal.setAttribute("aria-hidden", "false");

    setTimeout(() => zoneNameInput?.focus(), 0);
  }

  function closeCreateModal() {
    if (!createModal) return;

    createModal.classList.add("hidden");
    createModal.setAttribute("aria-hidden", "true");
  }

  function showCreateResult(kind, html) {
    if (!createResult) return;

    createResult.classList.remove("hidden", "ok", "fail");
    createResult.classList.add(kind === "ok" ? "ok" : "fail");
    createResult.innerHTML = html;
  }

  async function copyText(text) {
    const s = String(text || "");
    if (!s) return false;

    try {
      await navigator.clipboard.writeText(s);
      return true;
    } catch (_) {
      const ta = document.createElement("textarea");
      ta.value = s;
      ta.setAttribute("readonly", "readonly");
      ta.style.position = "fixed";
      ta.style.left = "-9999px";
      document.body.appendChild(ta);
      ta.select();

      let ok = false;
      try {
        ok = document.execCommand("copy");
      } catch (_) {
        ok = false;
      }

      ta.remove();
      return ok;
    }
  }

  async function apiJson(url, opts) {
    const res = await fetch(url, {
      cache: "no-store",
      credentials: "include",
      headers: {
        "Accept": "application/json",
        ...(opts && opts.headers ? opts.headers : {})
      },
      ...(opts || {})
    });

    const text = await res.text().catch(() => "");
    let json = null;

    try {
      json = text ? JSON.parse(text) : null;
    } catch (_) {
      json = null;
    }

    if (!res.ok || !json || json.ok === false) {
      const msg =
          json && (json.message || json.error)
              ? (json.message || json.error)
              : (text ? text.replace(/\s+/g, " ").slice(0, 240) : `HTTP ${res.status}`);

      const err = new Error(msg);
      err.status = res.status;
      err.json = json;
      throw err;
    }

    return json;
  }

  function dropZoneSearchHaystack(z) {
    const branding = z && z.branding && typeof z.branding === "object" ? z.branding : {};

    return [
      z && z.id,
      z && z.name,
      z && z.destination_path,
      z && z.status,
      branding.company_name,
      branding.title,
      branding.description,
      branding.footer_text,
      branding.button_text
    ].map((v) => String(v || "").toLowerCase()).join(" ");
  }

  function zoneMatchesCurrentFilters(z) {
    const status = dropZoneStatus(z).key;

    if (dropZoneStatusFilter !== "all" && status !== dropZoneStatusFilter) {
      return false;
    }

    const q = String(dropZoneSearchText || "").trim().toLowerCase();
    if (!q) return true;

    return dropZoneSearchHaystack(z).includes(q);
  }

  function ensureDropZoneListToolbar() {
    if (!zonesList) return;

    const existing = document.getElementById("dzListToolbar");
    if (existing) return;

    const toolbar = document.createElement("div");
    toolbar.id = "dzListToolbar";
    toolbar.className = "dzListToolbar";
    toolbar.innerHTML = `
      <div class="dzSearchWrap">
        <input
          class="dzSearchInput"
          type="search"
          autocomplete="off"
          placeholder="${escapeHtml(tr("dropzone.search_placeholder", null, "Search Drop Zones…"))}"
        >
      </div>

      <div class="dzFilterChips" role="group" aria-label="${escapeHtml(tr("dropzone.status_filter", null, "Status filter"))}">
        <button class="dzFilterChip active" type="button" data-dz-filter="all">
          ${escapeHtml(tr("dropzone.filter_all", null, "All"))}
          <span class="dzFilterCount" data-dz-count="all">0</span>
        </button>
        <button class="dzFilterChip" type="button" data-dz-filter="active">
          ${escapeHtml(tr("dropzone.filter_active", null, "Active"))}
          <span class="dzFilterCount" data-dz-count="active">0</span>
        </button>
        <button class="dzFilterChip" type="button" data-dz-filter="expired">
          ${escapeHtml(tr("dropzone.filter_expired", null, "Expired"))}
          <span class="dzFilterCount" data-dz-count="expired">0</span>
        </button>
        <button class="dzFilterChip" type="button" data-dz-filter="disabled">
          ${escapeHtml(tr("dropzone.filter_disabled", null, "Disabled"))}
          <span class="dzFilterCount" data-dz-count="disabled">0</span>
        </button>
      </div>
    `;

    zonesList.parentNode.insertBefore(toolbar, zonesList);

    const input = toolbar.querySelector(".dzSearchInput");
    if (input) {
      input.value = dropZoneSearchText;
      input.addEventListener("input", () => {
        dropZoneSearchText = input.value || "";
        renderZones(dropZoneListCache);
      });
    }

    toolbar.addEventListener("click", (ev) => {
      const btn = ev.target && ev.target.closest ? ev.target.closest(".dzFilterChip") : null;
      if (!btn) return;

      dropZoneStatusFilter = btn.getAttribute("data-dz-filter") || "all";
      renderZones(dropZoneListCache);
    });
  }

  function updateDropZoneListToolbar(zones) {
    const toolbar = document.getElementById("dzListToolbar");
    if (!toolbar) return;

    const allZones = Array.isArray(zones) ? zones : [];
    const counts = { all: allZones.length, active: 0, expired: 0, disabled: 0 };

    for (const z of allZones) {
      const key = dropZoneStatus(z).key;
      counts[key] = (counts[key] || 0) + 1;
    }

    toolbar.querySelectorAll("[data-dz-count]").forEach((el) => {
      const key = el.getAttribute("data-dz-count") || "all";
      el.textContent = String(counts[key] || 0);
    });

    toolbar.querySelectorAll(".dzFilterChip").forEach((btn) => {
      const key = btn.getAttribute("data-dz-filter") || "all";
      btn.classList.toggle("active", key === dropZoneStatusFilter);
    });

    const input = toolbar.querySelector(".dzSearchInput");
    if (input && document.activeElement !== input && input.value !== dropZoneSearchText) {
      input.value = dropZoneSearchText;
    }
  }

  function renderZones(zones) {
    if (!zonesList) return;

    const allZones = Array.isArray(zones) ? zones : [];
    dropZoneListCache = allZones;

    ensureDropZoneListToolbar();
    updateDropZoneListToolbar(allZones);

    if (allZones.length === 0) {
      renderEmpty(tr("dropzone.empty", null, "No Drop Zones yet. Create one when you need an outsider upload link."));
      return;
    }

    const visibleZones = allZones.filter(zoneMatchesCurrentFilters);

    if (visibleZones.length === 0) {
      renderEmpty(tr("dropzone.no_matches", null, "No Drop Zones match the current search or filter."));
      return;
    }

    zonesList.innerHTML = visibleZones.map((z, index) => {
      const id = z.id || "";
      const isExpanded = id ? expandedDropZoneIds.has(String(id)) : false;
      const safeId = String(id || index || "zone").replace(/[^a-zA-Z0-9_-]/g, "_");
      const detailsId = `dzDetails_${safeId}_${index}`;

      const name = z.name || id || "Drop Zone";
      const dest = z.destination_path || "—";
      const uploads = Number(z.upload_count || 0);
      const bytes = Number(z.bytes_uploaded || 0);
      const expires = fmtEpoch(z.expires_epoch);
      const maxFile = fmtBytes(z.max_file_bytes || 0);
      const maxTotal = fmtBytes(z.max_total_bytes || 0);
      const branding = z.branding && typeof z.branding === "object" ? z.branding : {};
      const branded = Object.keys(branding).length > 0;
      const brandName = branding.company_name || branding.title || "";
      const status = dropZoneStatus(z);
      const publicUrl = dropZonePublicUrl(z);

      const canDisable = status.key === "active";
      const canReenable = status.key === "disabled";
      const canRenew = status.key === "expired";
      const canDelete = status.key === "disabled" || status.key === "expired";

      return `
        <article class="dzCard dzCardCompact${isExpanded ? " dzCardOpen" : ""}" data-zone-id="${escapeHtml(id)}">
          <div class="dzCompactRow">
            <button class="dzCompactMain dzDetailsToggleBtn" type="button" data-dz-details-target="${escapeHtml(detailsId)}" aria-expanded="${isExpanded ? "true" : "false"}">
              <span class="dzCompactTitleLine">
                <span class="dzCompactTitle">${escapeHtml(name)}</span>
                <span class="dzBadge ${escapeHtml(status.className)}">${escapeHtml(status.label)}</span>
              </span>

              <span class="dzCompactMeta">
                ${branded ? `<span>${escapeHtml(tr("dropzone.branded_page", null, "Branded page"))}: ${escapeHtml(brandName || tr("common.enabled", null, "Enabled"))}</span>` : ""}
                <span>${escapeHtml(tr("dropzone.destination", null, "Destination"))}: ${escapeHtml(dest)}</span>
                <span>${escapeHtml(tr("dropzone.expires", null, "Expires"))}: ${escapeHtml(expires)}</span>
                <span>${escapeHtml(tr("dropzone.uploads", null, "Uploads"))}: ${uploads}</span>
              </span>

              <span class="dzDetailsToggleText">${escapeHtml(isExpanded ? tr("dropzone.hide_details", null, "Hide details") : tr("dropzone.details", null, "Details"))}</span>
            </button>

            <div class="dzCompactActions">
              ${publicUrl ? `<button class="dzGhost dzCopyLinkBtn" type="button" data-zone-url="${escapeHtml(publicUrl)}">${escapeHtml(tr("dropzone.copy_link", null, "Copy link"))}</button>` : ""}
              ${publicUrl ? `<button class="dzGhost dzPreviewBtn" type="button" data-zone-url="${escapeHtml(publicUrl)}">${escapeHtml(tr("dropzone.preview", null, "Preview"))}</button>` : ""}
              ${canDisable ? `<button class="dzGhost dzDisableBtn" type="button" data-zone-id="${escapeHtml(id)}">${escapeHtml(tr("dropzone.disable", null, "Disable"))}</button>` : ""}
              ${canReenable ? `<button class="dzGhost dzEnableBtn" type="button" data-zone-id="${escapeHtml(id)}">${escapeHtml(tr("dropzone.reenable", null, "Re-enable"))}</button>` : ""}
              ${canRenew ? `<button class="dzGhost dzRenewBtn" type="button" data-zone-id="${escapeHtml(id)}" data-days="7">${escapeHtml(tr("dropzone.renew_7d", null, "Renew 7 days"))}</button>` : ""}
              ${canRenew ? `<button class="dzGhost dzRenewBtn" type="button" data-zone-id="${escapeHtml(id)}" data-days="30">${escapeHtml(tr("dropzone.renew_30d", null, "Renew 30 days"))}</button>` : ""}
              ${canDelete ? `<button class="dzGhost dzDeleteBtn danger" type="button" data-zone-id="${escapeHtml(id)}">${escapeHtml(tr("dropzone.delete", null, "Delete"))}</button>` : ""}
            </div>
          </div>

          <div class="dzDetailsPanel${isExpanded ? "" : " hidden"}" id="${escapeHtml(detailsId)}">
            <div class="dzStats dzStatsCompact">
              <div><span>${escapeHtml(tr("dropzone.uploads", null, "Uploads"))}</span><strong>${uploads}</strong></div>
              <div><span>${escapeHtml(tr("dropzone.uploaded", null, "Uploaded"))}</span><strong>${escapeHtml(fmtBytes(bytes))}</strong></div>
              <div><span>${escapeHtml(tr("dropzone.expires", null, "Expires"))}</span><strong>${escapeHtml(expires)}</strong></div>
            </div>

            <div class="dzDetailGrid">
              <div>
                <span>${escapeHtml(tr("dropzone.destination", null, "Destination"))}</span>
                <strong>${escapeHtml(dest)}</strong>
              </div>
              <div>
                <span>${escapeHtml(tr("dropzone.max_file", null, "Max file"))}</span>
                <strong>${escapeHtml(maxFile)}</strong>
              </div>
              <div>
                <span>${escapeHtml(tr("dropzone.total_limit", null, "Total limit"))}</span>
                <strong>${escapeHtml(maxTotal)}</strong>
              </div>
              ${branded ? `<div><span>${escapeHtml(tr("dropzone.branded_page", null, "Branded page"))}</span><strong>${escapeHtml(brandName || tr("common.enabled", null, "Enabled"))}</strong></div>` : ""}
              ${id ? `<div><span>${escapeHtml(tr("dropzone.internal_id", null, "Internal ID"))}</span><strong>${escapeHtml(id)}</strong></div>` : ""}
            </div>

            ${publicUrl ? `<div class="dzDetailsLink">${escapeHtml(publicUrl)}</div>` : `<div class="dzHint dzNoLinkHint">${escapeHtml(tr("dropzone.link_not_available", null, "Link is not available for this older Drop Zone."))}</div>`}
          </div>
        </article>
      `;
    }).join("");
  }

  async function loadZones() {
    setStatus(tr("common.loading", null, "Loading…"));
    setBusy(true);

    try {
      const json = await apiJson("/api/v4/dropzones/list");

      const zones = Array.isArray(json.drop_zones)
          ? json.drop_zones
          : (Array.isArray(json.zones) ? json.zones : []);

      const counts = zones.reduce((acc, z) => {
        const key = dropZoneStatus(z).key;
        acc[key] = (acc[key] || 0) + 1;
        return acc;
      }, { active: 0, expired: 0, disabled: 0 });

      setStatus(`${zones.length} Drop Zone${zones.length === 1 ? "" : "s"} · ${counts.active || 0} active · ${counts.expired || 0} expired · ${counts.disabled || 0} disabled`);
      renderZones(zones);
    } catch (e) {
      if (e && e.status === 404) {
        setStatus(tr("dropzone.backend_not_wired", null, "Backend not wired yet."));
        renderEmpty(tr("dropzone.routes_missing", null, "Drop Zone UI is installed, but /api/v4/dropzones routes are missing."));
      } else {
        setStatus(tr("dropzone.load_failed", null, "Could not load Drop Zones."));
        renderEmpty(String(e && e.message ? e.message : e));
      }
    } finally {
      setBusy(false);
    }
  }

  function formStringValue(input) {
    return String(input?.value || "").trim();
  }


  const BRAND_LOGO_MAX_BYTES = 256 * 1024;
  const BRAND_LOGO_ALLOWED_TYPES = new Set([
    "image/png",
    "image/jpeg",
    "image/webp",
    "image/gif"
  ]);

  function setBrandLogoStatus(text, kind = "") {
    if (!brandLogoStatus) return;
    brandLogoStatus.textContent = text || "";
    brandLogoStatus.className = `dzLogoStatus ${kind || ""}`.trim();
  }

  function clearBrandLogoPreviewStatus() {
    setBrandLogoStatus("");
  }

  function isDisplayableLogoSrc(src) {
    const s = String(src || "").trim();
    return (
      s.startsWith("https://") ||
      (s.startsWith("/") && !s.startsWith("//")) ||
      s.startsWith("data:image/png;base64,") ||
      s.startsWith("data:image/jpeg;base64,") ||
      s.startsWith("data:image/webp;base64,") ||
      s.startsWith("data:image/gif;base64,")
    );
  }

  function updateBrandLogoPreview() {
    const src = String(brandLogoUrlInput?.value || "").trim();
    const ok = isDisplayableLogoSrc(src);

    if (brandLogoPreview) {
      if (ok) {
        brandLogoPreview.src = src;
        brandLogoPreview.style.display = "";
      } else {
        brandLogoPreview.removeAttribute("src");
        brandLogoPreview.style.display = "none";
      }
    }

    if (brandLogoPreviewEmpty) {
      brandLogoPreviewEmpty.style.display = ok ? "none" : "";
    }
  }

  function readFileAsDataUrl(file) {
    return new Promise((resolve, reject) => {
      const reader = new FileReader();
      reader.onerror = () => reject(new Error("Could not read logo file."));
      reader.onload = () => resolve(String(reader.result || ""));
      reader.readAsDataURL(file);
    });
  }

  async function handleBrandLogoFileSelected() {
    const file = brandLogoFileInput && brandLogoFileInput.files
        ? brandLogoFileInput.files[0]
        : null;

    if (!file) return;

    if (!BRAND_LOGO_ALLOWED_TYPES.has(file.type)) {
      setBrandLogoStatus("Use PNG, JPG, WebP or GIF.", "fail");
      if (brandLogoFileInput) brandLogoFileInput.value = "";
      return;
    }

    if (file.size > BRAND_LOGO_MAX_BYTES) {
      setBrandLogoStatus("Logo is too large. Maximum size is 256 KB.", "fail");
      if (brandLogoFileInput) brandLogoFileInput.value = "";
      return;
    }

    try {
      const dataUrl = await readFileAsDataUrl(file);

      if (!isDisplayableLogoSrc(dataUrl)) {
        setBrandLogoStatus("Unsupported logo format.", "fail");
        return;
      }

      if (brandLogoUrlInput) brandLogoUrlInput.value = dataUrl;
      if (brandEnabledInput) brandEnabledInput.checked = true;

      updateBrandLogoPreview();
      setBrandLogoStatus(`Uploaded: ${file.name} (${Math.ceil(file.size / 1024)} KB)`, "ok");
    } catch (e) {
      setBrandLogoStatus(e && e.message ? e.message : "Could not load logo.", "fail");
    } finally {
      if (brandLogoFileInput) brandLogoFileInput.value = "";
    }
  }

  function clearBrandLogo() {
    if (brandLogoUrlInput) brandLogoUrlInput.value = "";
    if (brandLogoFileInput) brandLogoFileInput.value = "";
    updateBrandLogoPreview();
    setBrandLogoStatus("Logo cleared.");
  }


  function collectBrandingPayload() {
    return {
      company_name: formStringValue(brandCompanyInput),
      logo_url: formStringValue(brandLogoUrlInput),
      title: formStringValue(brandTitleInput),
      description: formStringValue(brandDescriptionInput),
      primary_color: formStringValue(brandPrimaryColorInput),
      background_color: formStringValue(brandBackgroundColorInput),
      button_text: formStringValue(brandButtonTextInput),
      footer_text: formStringValue(brandFooterTextInput)
    };
  }

  function brandingPayloadHasVisibleContent(branding) {
    if (!branding || typeof branding !== "object") return false;

    return [
      branding.company_name,
      branding.logo_url,
      branding.title,
      branding.description,
      branding.button_text,
      branding.footer_text
    ].some((v) => String(v || "").trim().length > 0);
  }

  function shouldSubmitBrandingPayload(branding) {
    return !!(brandEnabledInput && brandEnabledInput.checked) ||
        brandingPayloadHasVisibleContent(branding);
  }

  async function createDropZone(ev) {
    ev?.preventDefault?.();

    const name = String(zoneNameInput?.value || "").trim() || tr("dropzone.default_name", null, "Drop Zone");
    const destinationPath = String(destInput?.value || "").trim();
    const expiresInSeconds = Number(expiryInput?.value || 86400);
    const maxFileBytes = Number(maxFileInput?.value || 0);
    const maxTotalBytes = Number(maxTotalInput?.value || 0);
    const password = String(passwordInput?.value || "");

    if (createResult) {
      createResult.classList.add("hidden");
      createResult.innerHTML = "";
    }

    setCreateBusy(true);

    try {
      const body = {
        name,
        destination_path: destinationPath,
        expires_in_seconds: Number.isFinite(expiresInSeconds) ? expiresInSeconds : 86400,
        max_file_bytes: Number.isFinite(maxFileBytes) ? maxFileBytes : 0,
        max_total_bytes: Number.isFinite(maxTotalBytes) ? maxTotalBytes : 0
      };

      if (password) body.password = password;

      const brandingPayload = collectBrandingPayload();
      if (shouldSubmitBrandingPayload(brandingPayload)) {
        body.branding = brandingPayload;
      }

      const json = await apiJson("/api/v4/dropzones/create", {
        method: "POST",
        headers: {
          "Content-Type": "application/json"
        },
        body: JSON.stringify(body)
      });

      const fullUrl = json.full_url || (json.url ? `${window.location.origin}${json.url}` : "");
      saveDropZoneLink(json.id || "", fullUrl);

      showCreateResult("ok", `
        <div class="dzResultTitle">${escapeHtml(tr("dropzone.created_title", null, "Drop Zone created"))}</div>
        <div class="dzResultUrl">${escapeHtml(fullUrl || tr("dropzone.no_url_returned", null, "Link was created, but no URL was returned."))}</div>
        <div class="dzResultActions">
          ${fullUrl ? `<button id="copyCreatedLinkBtn" class="dzGhost" type="button">${escapeHtml(tr("dropzone.copy_link", null, "Copy link"))}</button>` : ""}
          ${fullUrl ? `<button id="openCreatedLinkBtn" class="dzGhost" type="button">${escapeHtml(tr("dropzone.open_page", null, "Open page"))}</button>` : ""}
        </div>
        <div class="dzHint">
          ${escapeHtml(tr("dropzone.save_link_hint", null, "Save this link now. For security, the raw token is only shown when the Drop Zone is created."))}
        </div>
      `);

      el("copyCreatedLinkBtn")?.addEventListener("click", async () => {
        const ok = await copyText(fullUrl);
        setStatus(ok ? tr("dropzone.link_copied", null, "Link copied.") : tr("dropzone.copy_failed", null, "Could not copy link."));
      });

      el("openCreatedLinkBtn")?.addEventListener("click", () => {
        if (fullUrl) window.open(fullUrl, "_blank", "noopener,noreferrer");
      });

      await loadZones();
    } catch (e) {
      showCreateResult("fail", `
        <div class="dzResultTitle">${escapeHtml(tr("dropzone.create_failed", null, "Could not create Drop Zone"))}</div>
        <div>${escapeHtml(e && e.message ? e.message : e)}</div>
      `);
    } finally {
      setCreateBusy(false);
    }
  }

  async function disableZone(id) {
    if (!id) return;

    const ok = await openDropZoneConfirmModal({
      title: tr("dropzone.disable.title", null, "Disable this Drop Zone?"),
      message: tr("dropzone.disable.message", null, "Existing public link will stop accepting uploads."),
      confirmText: tr("dropzone.disable.confirm", null, "Disable"),
      cancelText: tr("dropzone.cancel", null, "Cancel"),
      danger: true
    });
    if (!ok) return;

    setStatus(tr("dropzone.disabling", null, "Disabling Drop Zone…"));
    setBusy(true);

    try {
      await apiJson("/api/v4/dropzones/disable", {
        method: "POST",
        headers: {
          "Content-Type": "application/json"
        },
        body: JSON.stringify({
          id,
          disabled: true
        })
      });

      setStatus(tr("dropzone.disabled_status", null, "Drop Zone disabled."));
      await loadZones();
    } catch (e) {
      setStatus(tr("dropzone.disable_failed", { error: String(e && e.message ? e.message : e) }, `Could not disable Drop Zone: ${e && e.message ? e.message : e}`));
    } finally {
      setBusy(false);
    }
  }

  async function enableZone(id) {
    if (!id) return;

    setStatus(tr("dropzone.enabling", null, "Re-enabling Drop Zone…"));
    setBusy(true);

    try {
      await apiJson("/api/v4/dropzones/disable", {
        method: "POST",
        headers: {
          "Content-Type": "application/json"
        },
        body: JSON.stringify({
          id,
          disabled: false
        })
      });

      setStatus(tr("dropzone.enabled_status", null, "Drop Zone re-enabled."));
      await loadZones();
    } catch (e) {
      setStatus(tr("dropzone.enable_failed", { error: String(e && e.message ? e.message : e) }, `Could not re-enable Drop Zone: ${e && e.message ? e.message : e}`));
    } finally {
      setBusy(false);
    }
  }

  async function renewZone(id, days) {
    if (!id) return;

    const safeDays = Number(days || 7);
    const expiresInSeconds = Math.max(1, safeDays) * 24 * 60 * 60;

    setStatus(tr("dropzone.renewing", null, "Renewing Drop Zone…"));
    setBusy(true);

    try {
      await apiJson("/api/v4/dropzones/renew", {
        method: "POST",
        headers: {
          "Content-Type": "application/json"
        },
        body: JSON.stringify({
          id,
          expires_in_seconds: expiresInSeconds
        })
      });

      setStatus(`Drop Zone renewed for ${safeDays} day${safeDays === 1 ? "" : "s"}.`);
      await loadZones();
    } catch (e) {
      setStatus(tr("dropzone.renew_failed", { error: String(e && e.message ? e.message : e) }, `Could not renew Drop Zone: ${e && e.message ? e.message : e}`));
    } finally {
      setBusy(false);
    }
  }

  async function deleteZone(id) {
    if (!id) return;

    const ok = await openDropZoneConfirmModal({
      title: tr("dropzone.delete.title", null, "Delete this Drop Zone?"),
      message: tr("dropzone.delete.message", null, "This removes the Drop Zone from management. Uploaded files already stored in the destination folder are not deleted."),
      confirmText: tr("dropzone.delete.confirm", null, "Delete"),
      cancelText: tr("dropzone.cancel", null, "Cancel"),
      danger: true
    });
    if (!ok) return;

    setStatus(tr("dropzone.deleting", null, "Deleting Drop Zone…"));
    setBusy(true);

    try {
      await apiJson("/api/v4/dropzones/delete", {
        method: "POST",
        headers: {
          "Content-Type": "application/json"
        },
        body: JSON.stringify({ id })
      });

      expandedDropZoneIds.delete(String(id));
      setStatus(tr("dropzone.deleted_status", null, "Drop Zone deleted."));
      await loadZones();
    } catch (e) {
      setStatus(tr("dropzone.delete_failed", { error: String(e && e.message ? e.message : e) }, `Could not delete Drop Zone: ${e && e.message ? e.message : e}`));
    } finally {
      setBusy(false);
    }
  }

  function ensureDropZonePreviewModal() {
    let modal = document.getElementById("dzPreviewModal");
    if (modal) return modal;

    modal = document.createElement("div");
    modal.id = "dzPreviewModal";
    modal.className = "dzPreviewModal hidden";
    modal.innerHTML = `
      <div class="dzPreviewWindow" role="dialog" aria-modal="false" aria-label="${escapeHtml(tr("dropzone.preview", null, "Preview"))}">
        <div class="dzPreviewHeader">
          <div class="dzPreviewTitle">
            <span>${escapeHtml(tr("dropzone.preview", null, "Preview"))}</span>
            <span class="dzPreviewScale">50%</span>
          </div>

          <div class="dzPreviewHeaderActions">
            <a class="dzGhost dzPreviewOpenFull" href="#" target="_blank" rel="noopener noreferrer">
              ${escapeHtml(tr("dropzone.open_full_size", null, "Open full size"))}
            </a>
            <button class="dzGhost dzPreviewCloseBtn" type="button">
              ${escapeHtml(tr("dropzone.close", null, "Close"))}
            </button>
          </div>
        </div>

        <div class="dzPreviewBody">
          <div class="dzPreviewScaledFrame">
            <iframe class="dzPreviewFrame" title="${escapeHtml(tr("dropzone.preview", null, "Preview"))}" loading="lazy"></iframe>
          </div>
        </div>
      </div>
    `;

    document.body.appendChild(modal);

    const closeBtn = modal.querySelector(".dzPreviewCloseBtn");
    const frame = modal.querySelector(".dzPreviewFrame");
    const win = modal.querySelector(".dzPreviewWindow");
    const header = modal.querySelector(".dzPreviewHeader");

    const close = () => {
      modal.classList.add("hidden");
      if (frame) frame.src = "about:blank";
    };

    closeBtn?.addEventListener("click", close);

    modal.addEventListener("keydown", (ev) => {
      if (ev.key === "Escape") close();
    });

    // Small detached-window drag behavior.
    let drag = null;

    header?.addEventListener("mousedown", (ev) => {
      if (ev.target && ev.target.closest && ev.target.closest("button,a")) return;
      if (!win) return;

      const rect = win.getBoundingClientRect();
      drag = {
        dx: ev.clientX - rect.left,
        dy: ev.clientY - rect.top
      };

      win.style.left = `${rect.left}px`;
      win.style.top = `${rect.top}px`;
      win.style.transform = "none";
      win.classList.add("dragging");

      ev.preventDefault();
    });

    document.addEventListener("mousemove", (ev) => {
      if (!drag || !win) return;

      const margin = 12;
      const maxLeft = Math.max(margin, window.innerWidth - win.offsetWidth - margin);
      const maxTop = Math.max(margin, window.innerHeight - win.offsetHeight - margin);

      const left = Math.min(maxLeft, Math.max(margin, ev.clientX - drag.dx));
      const top = Math.min(maxTop, Math.max(margin, ev.clientY - drag.dy));

      win.style.left = `${left}px`;
      win.style.top = `${top}px`;
    });

    document.addEventListener("mouseup", () => {
      if (!drag || !win) return;
      drag = null;
      win.classList.remove("dragging");
    });

    return modal;
  }

  function openDropZonePreviewModal(url) {
    const previewUrl = String(url || "").trim();
    if (!previewUrl) return;

    const modal = ensureDropZonePreviewModal();
    const frame = modal.querySelector(".dzPreviewFrame");
    const openFull = modal.querySelector(".dzPreviewOpenFull");
    const win = modal.querySelector(".dzPreviewWindow");

    if (openFull) openFull.href = previewUrl;
    if (frame) frame.src = previewUrl;

    // Reset position each time, so a previously dragged modal does not disappear
    // after window size changes.
    if (win) {
      win.style.left = "50%";
      win.style.top = "7vh";
      win.style.transform = "translateX(-50%)";
    }

    modal.classList.remove("hidden");
    modal.focus?.();
  }

  zonesList?.addEventListener("click", async (ev) => {
    const target = ev.target && ev.target.closest ? ev.target : null;
    if (!target) return;

    const detailsBtn = target.closest(".dzDetailsToggleBtn");
    if (detailsBtn) {
      const targetId = detailsBtn.getAttribute("data-dz-details-target") || "";
      const panel = targetId ? document.getElementById(targetId) : null;
      if (!panel) return;

      const isOpen = !panel.classList.contains("hidden");
      const card = detailsBtn.closest(".dzCardCompact");

      panel.classList.toggle("hidden", isOpen);
      detailsBtn.setAttribute("aria-expanded", isOpen ? "false" : "true");

      if (card) {
        card.classList.toggle("dzCardOpen", !isOpen);

        const zoneId = card.getAttribute("data-zone-id") || "";
        if (zoneId) {
          if (isOpen) {
            expandedDropZoneIds.delete(zoneId);
          } else {
            expandedDropZoneIds.add(zoneId);
          }
        }
      }

      const label = detailsBtn.querySelector(".dzDetailsToggleText");
      if (label) {
        label.textContent = isOpen
            ? tr("dropzone.details", null, "Details")
            : tr("dropzone.hide_details", null, "Hide details");
      }

      return;
    }

    const copyBtn = target.closest(".dzCopyLinkBtn");
    if (copyBtn) {
      const url = copyBtn.getAttribute("data-zone-url") || "";
      const ok = await copyText(url);
      setStatus(ok ? tr("dropzone.link_copied", null, "Link copied.") : tr("dropzone.copy_failed", null, "Could not copy link."));
      return;
    }

    const previewBtn = target.closest(".dzPreviewBtn");
    if (previewBtn) {
      const url = previewBtn.getAttribute("data-zone-url") || "";
      if (url) openDropZonePreviewModal(url);
      return;
    }

    const disableBtn = target.closest(".dzDisableBtn");
    if (disableBtn) {
      disableZone(disableBtn.getAttribute("data-zone-id") || "");
      return;
    }

    const enableBtn = target.closest(".dzEnableBtn");
    if (enableBtn) {
      enableZone(enableBtn.getAttribute("data-zone-id") || "");
      return;
    }

    const renewBtn = target.closest(".dzRenewBtn");
    if (renewBtn) {
      renewZone(renewBtn.getAttribute("data-zone-id") || "", Number(renewBtn.getAttribute("data-days") || 7));
      return;
    }

    const deleteBtn = target.closest(".dzDeleteBtn");
    if (deleteBtn) {
      deleteZone(deleteBtn.getAttribute("data-zone-id") || "");
      return;
    }
  });

  refreshBtn?.addEventListener("click", loadZones);
  createBtn?.addEventListener("click", openCreateModal);
  modalCloseBtn?.addEventListener("click", closeCreateModal);
  cancelCreateBtn?.addEventListener("click", closeCreateModal);
  createForm?.addEventListener("submit", createDropZone);
  brandLogoPickBtn?.addEventListener("click", () => brandLogoFileInput?.click());
  brandLogoFileInput?.addEventListener("change", handleBrandLogoFileSelected);
  brandLogoClearBtn?.addEventListener("click", clearBrandLogo);
  brandLogoUrlInput?.addEventListener("input", () => {
    clearBrandLogoPreviewStatus();
    updateBrandLogoPreview();
  });
  brandTemplateApplyBtn?.addEventListener("click", applySelectedBrandTemplate);
  brandTemplateSaveBtn?.addEventListener("click", saveCurrentBrandTemplate);
  brandTemplateDeleteBtn?.addEventListener("click", deleteSelectedBrandTemplate);
  brandTemplateSelect?.addEventListener("change", applySelectedBrandTemplate);

  createModal?.addEventListener("click", (ev) => {
    if (ev.target && ev.target.getAttribute("data-close-modal") === "1") {
      closeCreateModal();
    }
  });

  document.addEventListener("keydown", (ev) => {
    if (ev.key === "Escape" && createModal && !createModal.classList.contains("hidden")) {
      closeCreateModal();
    }
  });

  function startDropZoneApp() {
    try {
      if (window.PQNAS_I18N && typeof window.PQNAS_I18N.apply === "function") {
        window.PQNAS_I18N.apply(document);
      }
    } catch (_) {}
    loadZones();
  }

  window.addEventListener("pqnas-language-changed", () => {
    try {
      if (window.PQNAS_I18N && typeof window.PQNAS_I18N.apply === "function") {
        window.PQNAS_I18N.apply(document);
      }
    } catch (_) {}
    refreshBrandTemplateSelect();
    loadZones();
  });

  if (window.PQNAS_I18N && typeof window.PQNAS_I18N.ready === "function") {
    window.PQNAS_I18N.ready().then(startDropZoneApp).catch(startDropZoneApp);
  } else {
    startDropZoneApp();
  }
})();