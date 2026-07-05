
(function () {
  "use strict";

  const API = {
    list: "/api/v4/contacts/list",
    upsert: "/api/v4/contacts/upsert",
    delete: "/api/v4/contacts/delete",
    localUsers: "/api/v4/contacts/local-users"
  };

  const state = {
    contacts: [],
    knownIdentities: [],
    selectedFingerprint: "",
    search: "",
    type: "",
    status: "",
    loading: false,
    editorMode: "new"
  };

  function tr(key, vars, fallback) {
    try {
      const i18n = window.PQNAS_I18N;
      if (i18n && typeof i18n.t === "function") {
        return i18n.t(key, vars || null, fallback || key);
      }
    } catch (_) {}
    return String(fallback || key);
  }

  function setNodeText(node, key, fallback, vars) {
    if (node) node.textContent = tr(key, vars || null, fallback);
  }

  function setText(id, key, fallback, vars) {
    setNodeText($(id), key, fallback, vars);
  }

  function setPlaceholder(id, key, fallback) {
    const node = $(id);
    if (node) node.placeholder = tr(key, null, fallback);
  }

  function setFieldLabel(inputId, key, fallback) {
    const node = $(inputId);
    const label = node && node.closest ? node.closest("label") : null;
    const span = label ? label.querySelector("span") : null;
    setNodeText(span, key, fallback);
  }

  function setOptionText(selectId, value, key, fallback) {
    const select = $(selectId);
    if (!select) return;
    const opt = Array.from(select.options || []).find((item) => String(item.value) === String(value));
    if (opt) opt.textContent = tr(key, null, fallback);
  }

  function setFirstTextNode(selector, key, fallback) {
    const node = document.querySelector(selector);
    if (!node) return;

    for (const child of Array.from(node.childNodes)) {
      if (child.nodeType === Node.TEXT_NODE) {
        child.nodeValue = tr(key, null, fallback);
        return;
      }
    }
  }

  function translateStaticContactsUi() {
    document.title = tr("contacts.page_title", null, "DNA-Nexus • Contacts");

    const h1 = document.querySelector(".contacts-heroText h1");
    setNodeText(h1, "contacts.title", "Contacts");

    setText("refreshBtn", "contacts.refresh", "Refresh");
    setText("newBtn", "contacts.new_contact", "New contact");
    setText("exportBtn", "contacts.export_vcard", "Export vCard");
    setText("exportCsvBtn", "contacts.export_csv", "Export CSV");
    setFirstTextNode(".contacts-fileBtn", "contacts.import_file", "Import file");

    setFieldLabel("searchInput", "contacts.search", "Search");
    setPlaceholder("searchInput", "contacts.search_placeholder", "Name, company, email, phone, city, tags or notes");

    setFieldLabel("typeFilter", "contacts.type", "Type");
    setFieldLabel("statusFilter", "contacts.status", "Status");

    setOptionText("typeFilter", "", "contacts.all", "All");
    setOptionText("typeFilter", "person", "contacts.type.person", "Person");
    setOptionText("typeFilter", "company", "contacts.type.company", "Company");
    setOptionText("typeFilter", "customer", "contacts.type.customer", "Customer");
    setOptionText("typeFilter", "supplier", "contacts.type.supplier", "Supplier");
    setOptionText("typeFilter", "family", "contacts.type.family", "Family / relatives");
    setOptionText("typeFilter", "other", "contacts.type.other", "Other");

    setOptionText("statusFilter", "", "contacts.all", "All");
    setOptionText("statusFilter", "active", "contacts.status.active", "Active");
    setOptionText("statusFilter", "inactive", "contacts.status.inactive", "Inactive");
    setOptionText("statusFilter", "archived", "contacts.status.archived", "Archived");

    const listTitle = document.querySelector(".contacts-listCard .contacts-sectionHead h2");
    setNodeText(listTitle, "contacts.address_book", "Address book");
    setText("emptyState", "contacts.empty", "No contacts yet. Add your first customer, supplier, relative or other contact.");

    const editorSub = document.querySelector(".contacts-editor .contacts-sectionHead p");
    setNodeText(editorSub, "contacts.editor_subtitle", "Saved to your private DNA-Nexus Contacts list.");

    const formSectionTitles = Array.from(document.querySelectorAll(".contacts-formSection h3"));
    setNodeText(formSectionTitles[0], "contacts.section.basic", "Basic");
    setNodeText(formSectionTitles[1], "contacts.section.details", "Contact details");
    setNodeText(formSectionTitles[2], "contacts.section.address", "Address");
    setNodeText(formSectionTitles[3], "contacts.section.notes", "Notes");

    setFieldLabel("contactTypeInput", "contacts.contact_type", "Contact type");
    setFieldLabel("identityModeSelect", "contacts.source", "Source");
    setFieldLabel("identitySelect", "contacts.known_person", "Known person");
    setFieldLabel("displayNameInput", "contacts.display_name", "Display name");
    setFieldLabel("companyInput", "contacts.company_org", "Company / organization");
    setFieldLabel("titleInput", "contacts.title_role", "Title / role");
    setFieldLabel("nicknameInput", "contacts.nickname", "Nickname");
    setFieldLabel("statusInput", "contacts.status", "Status");
    setFieldLabel("tagsInput", "contacts.tags", "Tags");

    setOptionText("contactTypeInput", "person", "contacts.type.person", "Person");
    setOptionText("contactTypeInput", "company", "contacts.type.company", "Company");
    setOptionText("contactTypeInput", "customer", "contacts.type.customer", "Customer");
    setOptionText("contactTypeInput", "supplier", "contacts.type.supplier", "Supplier");
    setOptionText("contactTypeInput", "family", "contacts.type.family", "Family / relatives");
    setOptionText("contactTypeInput", "other", "contacts.type.other", "Other");

    setOptionText("identityModeSelect", "manual_contact", "contacts.kind.manual_contact", "Manual contact");
    setOptionText("identityModeSelect", "local_user", "contacts.known_dna_user", "Known DNA-Nexus user");

    setOptionText("statusInput", "active", "contacts.status.active", "Active");
    setOptionText("statusInput", "inactive", "contacts.status.inactive", "Inactive");
    setOptionText("statusInput", "archived", "contacts.status.archived", "Archived");

    setPlaceholder("tagsInput", "contacts.tags_placeholder", "customer, supplier, christmas-card");

    setFieldLabel("emailInput", "contacts.email", "Email");
    setFieldLabel("phoneInput", "contacts.phone", "Phone");
    setFieldLabel("mobileInput", "contacts.mobile", "Mobile");
    setFieldLabel("websiteInput", "contacts.website", "Website");

    setFieldLabel("streetInput", "contacts.street", "Street address");
    setFieldLabel("postalCodeInput", "contacts.postal_code", "Postal code");
    setFieldLabel("cityInput", "contacts.city", "City");
    setFieldLabel("countryInput", "contacts.country", "Country");

    setFieldLabel("notesInput", "contacts.private_notes", "Private notes");
    setPlaceholder("notesInput", "contacts.notes_placeholder", "Customer notes, supplier context, delivery instructions or Christmas card reminders.");

    const actionTitle = document.querySelector(".contacts-actionsPanel h3");
    setNodeText(actionTitle, "contacts.quick_actions", "Quick actions");

    setText("copyCardBtn", "contacts.copy_card", "Copy card");
    setText("copyAddressBtn", "contacts.copy_address", "Copy address");
    setText("copyEmailBtn", "contacts.copy_email", "Copy email");
    setText("copyPhoneBtn", "contacts.copy_phone", "Copy phone");
    setText("openWebsiteBtn", "contacts.open_website", "Open website");

    const advancedSummary = document.querySelector(".contacts-advanced summary");
    setNodeText(advancedSummary, "contacts.technical_identity", tr("contacts.identity.technical_title", null, "Technical identity"));
    setFieldLabel("fingerprintInput", "contacts.identity_anchor", "Identity anchor");
    setFieldLabel("kindInput", "contacts.kind", "Kind");
    setOptionText("kindInput", "manual_contact", "contacts.kind.manual_contact", "Manual contact");
    setOptionText("kindInput", "fingerprint", "contacts.kind.fingerprint", "Fingerprint");
    setOptionText("kindInput", "local_user", "contacts.kind.local_user", "Local user");
    setOptionText("kindInput", "external_dna", "contacts.kind.external_dna", "External DNA");

    const help = document.querySelector(".contacts-help");
    setNodeText(help, "contacts.identity_help", tr("contacts.identity.technical_hint", null, "This identity anchor is internal. Manual contacts get an automatically generated private anchor. DNA-Nexus users and workspace members can use real DNA identity fingerprints."));

    setText("deleteBtn", "contacts.delete", "Delete");
    setText("resetBtn", "contacts.clear", "Clear");
    setText("saveBtn", "contacts.save_contact", "Save contact");

    renderIdentityOptions(selectedContact());
    setEditorMode(state.editorMode || "new");
  }

  let confirmResolve = null;
  let confirmLastFocus = null;

  const FIELD_IDS = [
    "contactTypeInput", "identityModeSelect", "identitySelect",
    "displayNameInput", "companyInput", "titleInput", "nicknameInput",
    "statusInput", "tagsInput",
    "emailInput", "phoneInput", "mobileInput", "websiteInput",
    "streetInput", "postalCodeInput", "cityInput", "countryInput",
    "deliveryNameInput", "deliveryStreetInput", "deliveryPostalCodeInput", "deliveryCityInput", "deliveryCountryInput",
    "notesInput", "fingerprintInput", "kindInput"
  ];

  const CSV_FIELDS = [
    "display_name", "company", "title", "nickname", "contact_type", "status", "tags",
    "email", "phone", "mobile", "website",
    "street", "postal_code", "city", "country",
    "notes", "subject_kind", "subject_fingerprint"
  ];

  const MAX_IMPORT_BYTES = 2 * 1024 * 1024;
  const MAX_IMPORT_CONTACTS = 1000;

  function $(id) {
    return document.getElementById(id);
  }

  function val(id) {
    const el = $(id);
    return el ? String(el.value || "").trim() : "";
  }

  function setVal(id, value) {
    const el = $(id);
    if (el) el.value = String(value || "");
  }

  function normalizeFingerprint(value) {
    return String(value || "")
      .trim()
      .replace(/[\s:-]+/g, "")
      .toLowerCase();
  }

  function normalizeEmail(value) {
    return String(value || "").trim().toLowerCase();
  }

  function normalizePhone(value) {
    return String(value || "").replace(/[^0-9+]/g, "");
  }

  function normalizeNameKey(value) {
    return String(value || "")
      .trim()
      .toLowerCase()
      .replace(/\s+/g, " ");
  }

  function randomHex(bytes) {
    const n = bytes || 32;
    try {
      const arr = new Uint8Array(n);
      crypto.getRandomValues(arr);
      return Array.from(arr, (x) => x.toString(16).padStart(2, "0")).join("");
    } catch (_) {
      const seed = `${Date.now()}-${Math.random()}-${Math.random()}`;
      let out = "";
      for (let i = 0; i < seed.length; i += 1) {
        out += seed.charCodeAt(i).toString(16).padStart(2, "0");
      }
      return (out + "0".repeat(n * 2)).slice(0, n * 2);
    }
  }

  function shortFingerprint(value) {
    const s = String(value || "");
    if (s.length <= 18) return s;
    return `${s.slice(0, 10)}…${s.slice(-6)}`;
  }

  function allocateManualAnchor() {
    for (let i = 0; i < 12; i += 1) {
      const fp = normalizeFingerprint(randomHex(32));
      if (fp && !contactByFingerprint(fp)) return fp;
    }

    return normalizeFingerprint(randomHex(32));
  }

  function kindLabel(kind) {
    if (kind === "manual_contact") return tr("contacts.kind.manual", null, "Manual");
    if (kind === "local_user") return tr("contacts.kind.local_user", null, "Local user");
    if (kind === "external_dna") return tr("contacts.kind.external_dna", null, "External DNA");
    return tr("contacts.kind.fingerprint", null, "Fingerprint");
  }

  function typeLabel(type) {
    if (type === "company") return tr("contacts.type.company", null, "Company");
    if (type === "customer") return tr("contacts.type.customer", null, "Customer");
    if (type === "supplier") return tr("contacts.type.supplier", null, "Supplier");
    if (type === "family") return tr("contacts.type.family_short", null, "Family");
    if (type === "other") return tr("contacts.type.other", null, "Other");
    return tr("contacts.type.person", null, "Person");
  }

  function contactByFingerprint(fp) {
    const clean = normalizeFingerprint(fp);
    return state.contacts.find((c) => normalizeFingerprint(c.subject_fingerprint) === clean) || null;
  }

  function selectedContact() {
    return contactByFingerprint(state.selectedFingerprint);
  }

  function knownIdentityByFingerprint(fp) {
    const clean = normalizeFingerprint(fp);
    return state.knownIdentities.find((c) => normalizeFingerprint(c.fingerprint || c.subject_fingerprint) === clean) || null;
  }

  function closeConfirmModal(result) {
    const modal = $("confirmModal");
    if (!modal) return;

    modal.hidden = true;
    modal.setAttribute("aria-hidden", "true");

    const resolve = confirmResolve;
    confirmResolve = null;

    if (confirmLastFocus && typeof confirmLastFocus.focus === "function") {
      try { confirmLastFocus.focus(); } catch (_) {}
    }
    confirmLastFocus = null;

    if (resolve) resolve(!!result);
  }

  function askConfirm(options) {
    const modal = $("confirmModal");
    const title = $("confirmModalTitle");
    const body = $("confirmModalBody");
    const cancelBtn = $("confirmModalCancel");
    const okBtn = $("confirmModalOk");

    if (!modal || !title || !body || !cancelBtn || !okBtn) {
      return Promise.resolve(false);
    }

    if (confirmResolve) closeConfirmModal(false);

    const opts = options || {};
    title.textContent = opts.title || tr("contacts.confirm_action", null, "Confirm action");
    cancelBtn.textContent = opts.cancelText || tr("contacts.cancel", null, "Cancel");
    okBtn.textContent = opts.confirmText || tr("contacts.ok", null, "OK");

    okBtn.classList.toggle("danger", !!opts.danger);

    body.replaceChildren();

    const intro = String(opts.message || "").trim();
    if (intro) {
      const p = document.createElement("p");
      p.textContent = intro;
      body.appendChild(p);
    }

    const items = Array.isArray(opts.items) ? opts.items : [];
    if (items.length) {
      const list = document.createElement("div");
      list.className = "contacts-modalList";
      for (const item of items) {
        const row = document.createElement("p");
        row.className = "contacts-modalListItem";
        row.textContent = String(item || "");
        list.appendChild(row);
      }
      body.appendChild(list);
    }

    const detail = String(opts.detail || "").trim();
    if (detail) {
      const p = document.createElement("p");
      p.textContent = detail;
      body.appendChild(p);
    }

    confirmLastFocus = document.activeElement;
    modal.hidden = false;
    modal.setAttribute("aria-hidden", "false");

    return new Promise((resolve) => {
      confirmResolve = resolve;
      setTimeout(() => {
        try { okBtn.focus(); } catch (_) {}
      }, 0);
    });
  }

  function bindConfirmModalEvents() {
    const modal = $("confirmModal");
    const cancelBtn = $("confirmModalCancel");
    const okBtn = $("confirmModalOk");

    if (!modal || !cancelBtn || !okBtn) return;

    cancelBtn.addEventListener("click", () => closeConfirmModal(false));
    okBtn.addEventListener("click", () => closeConfirmModal(true));

    modal.addEventListener("click", (event) => {
      if (event.target === modal) closeConfirmModal(false);
    });

    document.addEventListener("keydown", (event) => {
      if (modal.hidden) return;
      if (event.key === "Escape") {
        event.preventDefault();
        closeConfirmModal(false);
      }
    });
  }

  async function apiJson(path, opts) {
    const res = await fetch(path, {
      credentials: "include",
      cache: "no-store",
      ...(opts || {})
    });

    const text = await res.text();
    let body = {};
    try {
      body = text ? JSON.parse(text) : {};
    } catch (_) {
      throw new Error(`Unexpected response from ${path}`);
    }

    if (!res.ok || body.ok === false) {
      throw new Error(body.message || body.error || `HTTP ${res.status}`);
    }

    return body;
  }

  function setNotice(message, kind) {
    const el = $("notice");
    if (!el) return;

    const text = String(message || "").trim();
    el.textContent = text;
    el.hidden = !text;
    el.classList.remove("ok", "warn", "err", "info", "muted");
    el.classList.add(kind || "info");
  }

  function filteredContacts() {
    const q = state.search.trim().toLowerCase();

    return state.contacts
      .filter((c) => {
        if (state.type && String(c.contact_type || "person") !== state.type) return false;
        if (state.status && String(c.status || "active") !== state.status) return false;
        if (!q) return true;

        const haystack = [
          c.display_name, c.nickname, c.company, c.title,
          c.email, c.phone, c.mobile, c.website,
          c.street, c.postal_code, c.city, c.country,
          c.delivery_name, c.delivery_street, c.delivery_postal_code, c.delivery_city, c.delivery_country,
          c.tags, c.notes, c.contact_type, c.status,
          c.subject_kind, c.subject_fingerprint, c.subject_fingerprint_short
        ].join(" ").toLowerCase();

        return haystack.includes(q);
      })
      .sort((a, b) => {
        const an = String(a.display_name || a.company || a.subject_fingerprint || "");
        const bn = String(b.display_name || b.company || b.subject_fingerprint || "");
        return an.localeCompare(bn);
      });
  }

  function renderIdentityOptions(currentContact) {
    const select = $("identitySelect");
    if (!select) return;

    const currentFp = normalizeFingerprint(currentContact && currentContact.subject_fingerprint);
    const used = new Set(
      state.contacts
        .map((c) => normalizeFingerprint(c.subject_fingerprint))
        .filter((fp) => fp && fp !== currentFp)
    );

    select.replaceChildren();

    const empty = document.createElement("option");
    empty.value = "";
    empty.textContent = tr("contacts.choose_known_person", null, "Choose a known DNA-Nexus person…");
    select.appendChild(empty);

    const candidates = state.knownIdentities
      .filter((c) => {
        const fp = normalizeFingerprint(c.fingerprint || c.subject_fingerprint);
        return fp && !used.has(fp);
      })
      .sort((a, b) => {
        const an = String(a.display_name || a.name || a.fingerprint || "");
        const bn = String(b.display_name || b.name || b.fingerprint || "");
        return an.localeCompare(bn);
      });

    let hasCurrent = false;
    for (const item of candidates) {
      const fp = normalizeFingerprint(item.fingerprint || item.subject_fingerprint);
      const label = String(item.display_name || item.name || shortFingerprint(fp)).trim();

      const opt = document.createElement("option");
      opt.value = fp;
      opt.textContent = `${label} • ${shortFingerprint(fp)}`;
      if (fp === currentFp) {
        opt.selected = true;
        hasCurrent = true;
      }
      select.appendChild(opt);
    }

    if (currentContact && currentFp && !hasCurrent) {
      const opt = document.createElement("option");
      opt.value = currentFp;
      opt.selected = true;
      opt.textContent = `${currentContact.display_name || shortFingerprint(currentFp)} • ${shortFingerprint(currentFp)}`;
      select.appendChild(opt);
    }

    select.disabled = !!currentContact;
  }

  function renderList() {
    const list = $("contactsList");
    const empty = $("emptyState");
    const count = $("countText");
    if (!list || !empty || !count) return;

    const items = filteredContacts();

    list.replaceChildren();
    count.textContent = state.loading
      ? tr("contacts.loading", null, "Loading contacts…")
      : tr("contacts.count", { shown: items.length, total: state.contacts.length }, `${items.length} shown / ${state.contacts.length} total`);

    empty.hidden = state.loading || items.length > 0;

    for (const contact of items) {
      const fp = normalizeFingerprint(contact.subject_fingerprint);
      const row = document.createElement("button");
      row.type = "button";
      row.className = "contacts-row";
      row.setAttribute("role", "listitem");
      if (fp === state.selectedFingerprint) row.classList.add("is-selected");

      const top = document.createElement("div");
      top.className = "contacts-rowTop";

      const name = document.createElement("div");
      name.className = "contacts-rowName";
      name.textContent = contact.display_name || contact.company || shortFingerprint(fp);

      const badge = document.createElement("span");
      badge.className = "pq-badge muted";
      badge.textContent = typeLabel(contact.contact_type || "person");

      top.appendChild(name);
      top.appendChild(badge);

      const meta = document.createElement("div");
      meta.className = "contacts-rowMeta";
      meta.textContent = [
        contact.company,
        contact.city,
        contact.email,
        contact.phone || contact.mobile
      ].filter(Boolean).join(" • ") || `${kindLabel(contact.subject_kind)} • ${shortFingerprint(fp)}`;

      row.appendChild(top);
      row.appendChild(meta);

      const noteParts = [contact.tags, contact.title].filter(Boolean);
      if (noteParts.length) {
        const line = document.createElement("div");
        line.className = "contacts-rowNotes";
        line.textContent = noteParts.join(" • ");
        row.appendChild(line);
      }

      row.addEventListener("click", () => {
        state.selectedFingerprint = fp;
        fillForm(contact);
        renderList();
      });

      list.appendChild(row);
    }
  }

  function setEditorMode(mode) {
    state.editorMode = mode === "edit" ? "edit" : "new";

    const badge = $("selectedBadge");
    const title = $("editorTitle");
    const deleteBtn = $("deleteBtn");

    if (badge) {
      badge.classList.remove("ok", "warn", "err", "info", "muted");
      badge.classList.add(mode === "edit" ? "ok" : "info");
      badge.textContent = mode === "edit"
        ? tr("contacts.editing", null, "editing")
        : tr("contacts.new_badge", null, "new");
    }

    if (title) {
      title.textContent = mode === "edit"
        ? tr("contacts.edit_contact", null, "Edit contact")
        : tr("contacts.new_contact", null, "New contact");
    }
    if (deleteBtn) deleteBtn.disabled = mode !== "edit";
  }

  function applyIdentityMode() {
    const mode = val("identityModeSelect") || "manual_contact";
    const field = $("identitySelectField");
    const existing = !!selectedContact();

    if (field) field.hidden = mode !== "local_user";
    if ($("identityModeSelect")) $("identityModeSelect").disabled = existing;

    if (mode === "manual_contact" && !existing) {
      setVal("kindInput", "manual_contact");
      setVal("fingerprintInput", "");
    }

    if (mode === "local_user") {
      setVal("kindInput", "local_user");
      applySelectedIdentity();
    }
  }

  function clearForm() {
    state.selectedFingerprint = "";

    for (const id of FIELD_IDS) setVal(id, "");

    setVal("contactTypeInput", "person");
    setVal("identityModeSelect", "manual_contact");
    setVal("kindInput", "manual_contact");
    setVal("fingerprintInput", allocateManualAnchor());
    setVal("statusInput", "active");

    if ($("identityModeSelect")) $("identityModeSelect").disabled = false;

    renderIdentityOptions(null);
    applyIdentityMode();
    setEditorMode("new");
    renderList();
  }

  function fillForm(contact) {
    setVal("contactTypeInput", contact.contact_type || "person");
    setVal("identityModeSelect", contact.subject_kind === "local_user" ? "local_user" : "manual_contact");
    setVal("displayNameInput", contact.display_name || "");
    setVal("companyInput", contact.company || "");
    setVal("titleInput", contact.title || "");
    setVal("nicknameInput", contact.nickname || "");
    setVal("statusInput", contact.status || "active");
    setVal("tagsInput", contact.tags || "");

    setVal("emailInput", contact.email || "");
    setVal("phoneInput", contact.phone || "");
    setVal("mobileInput", contact.mobile || "");
    setVal("websiteInput", contact.website || "");

    setVal("streetInput", contact.street || "");
    setVal("postalCodeInput", contact.postal_code || "");
    setVal("cityInput", contact.city || "");
    setVal("countryInput", contact.country || "");

    setVal("deliveryNameInput", contact.delivery_name || "");
    setVal("deliveryStreetInput", contact.delivery_street || "");
    setVal("deliveryPostalCodeInput", contact.delivery_postal_code || "");
    setVal("deliveryCityInput", contact.delivery_city || "");
    setVal("deliveryCountryInput", contact.delivery_country || "");

    setVal("notesInput", contact.notes || "");
    setVal("fingerprintInput", normalizeFingerprint(contact.subject_fingerprint));
    setVal("kindInput", contact.subject_kind || "manual_contact");

    renderIdentityOptions(contact);
    applyIdentityMode();
    setEditorMode("edit");
  }

  function applySelectedIdentity() {
    const fp = normalizeFingerprint(val("identitySelect"));
    const item = knownIdentityByFingerprint(fp);

    if (fp) {
      setVal("fingerprintInput", fp);
      setVal("kindInput", "local_user");
    }

    if (item && !val("displayNameInput")) {
      setVal("displayNameInput", String(item.display_name || item.name || shortFingerprint(fp)));
    }
  }

  function readPayload() {
    let fp = normalizeFingerprint(val("fingerprintInput"));
    const existing = selectedContact();
    const mode = val("identityModeSelect") || "manual_contact";

    if (!existing && mode === "manual_contact") {
      if (!fp || contactByFingerprint(fp)) {
        fp = allocateManualAnchor();
        setVal("fingerprintInput", fp);
      }
    }

    return {
      subject_fingerprint: fp,
      subject_kind: existing ? (val("kindInput") || "manual_contact") : mode,
      contact_type: val("contactTypeInput") || "person",
      display_name: val("displayNameInput"),
      company: val("companyInput"),
      title: val("titleInput"),
      nickname: val("nicknameInput"),
      status: val("statusInput") || "active",
      tags: val("tagsInput"),

      email: val("emailInput"),
      phone: val("phoneInput"),
      mobile: val("mobileInput"),
      website: val("websiteInput"),

      street: val("streetInput"),
      postal_code: val("postalCodeInput"),
      city: val("cityInput"),
      country: val("countryInput"),

      delivery_name: val("deliveryNameInput"),
      delivery_street: val("deliveryStreetInput"),
      delivery_postal_code: val("deliveryPostalCodeInput"),
      delivery_city: val("deliveryCityInput"),
      delivery_country: val("deliveryCountryInput"),

      notes: val("notesInput")
    };
  }

  function duplicateCandidates(payload) {
    const fp = normalizeFingerprint(payload.subject_fingerprint);
    const email = normalizeEmail(payload.email);
    const phone = normalizePhone(payload.phone);
    const mobile = normalizePhone(payload.mobile);
    const displayName = normalizeNameKey(payload.display_name);
    const company = normalizeNameKey(payload.company);

    const hits = [];

    for (const c of state.contacts) {
      const cfp = normalizeFingerprint(c.subject_fingerprint);
      if (cfp && cfp === fp) continue;

      const reasons = [];

      if (email && normalizeEmail(c.email) === email) reasons.push("same email");
      if (phone && normalizePhone(c.phone) === phone) reasons.push("same phone");
      if (mobile && normalizePhone(c.mobile) === mobile) reasons.push("same mobile");

      const cName = normalizeNameKey(c.display_name);
      const cCompany = normalizeNameKey(c.company);
      if (displayName && cName === displayName && company && cCompany === company) {
        reasons.push("same name and company");
      } else if (displayName && cName === displayName && !company && !cCompany) {
        reasons.push("same name");
      }

      if (reasons.length) {
        hits.push({
          contact: c,
          reasons
        });
      }
    }

    return hits;
  }

  function duplicateReasonLabel(reason) {
    const r = String(reason || "");
    if (r === "same email") return tr("contacts.duplicate.reason.same_email", null, "same email");
    if (r === "same phone") return tr("contacts.duplicate.reason.same_phone", null, "same phone");
    if (r === "same mobile") return tr("contacts.duplicate.reason.same_mobile", null, "same mobile");
    if (r === "same name and company") return tr("contacts.duplicate.reason.same_name_company", null, "same name and company");
    if (r === "same name") return tr("contacts.duplicate.reason.same_name", null, "same name");
    return r;
  }

  function duplicateItems(hits) {
    const items = hits.slice(0, 5).map((hit) => {
      const c = hit.contact || {};
      const label = c.display_name || c.company || shortFingerprint(c.subject_fingerprint);
      const reasons = (hit.reasons || []).map(duplicateReasonLabel).join(", ");
      return `${label}: ${reasons}`;
    });

    if (hits.length > 5) {
      const count = hits.length - 5;
      items.push(tr("contacts.duplicate.plus_more", { count }, `plus ${count} more`));
    }

    return items;
  }

  function contactLikeFromForm() {
    return {
      display_name: val("displayNameInput"),
      company: val("companyInput"),
      title: val("titleInput"),
      nickname: val("nicknameInput"),
      contact_type: val("contactTypeInput") || "person",
      status: val("statusInput") || "active",
      tags: val("tagsInput"),
      email: val("emailInput"),
      phone: val("phoneInput"),
      mobile: val("mobileInput"),
      website: val("websiteInput"),
      street: val("streetInput"),
      postal_code: val("postalCodeInput"),
      city: val("cityInput"),
      country: val("countryInput"),
      notes: val("notesInput"),
      subject_kind: val("kindInput") || "manual_contact",
      subject_fingerprint: normalizeFingerprint(val("fingerprintInput"))
    };
  }

  function formatAddress(c) {
    const lines = [
      c.street,
      [c.postal_code, c.city].filter(Boolean).join(" "),
      c.country
    ].filter((line) => String(line || "").trim());

    return lines.join("\n");
  }

  function formatContactCard(c) {
    const lines = [
      "[DNA-NEXUS-CONTACT]",
      `Name: ${c.display_name || ""}`,
      `Company: ${c.company || ""}`,
      `Title: ${c.title || ""}`,
      `Email: ${c.email || ""}`,
      `Phone: ${c.phone || ""}`,
      `Mobile: ${c.mobile || ""}`,
      `Website: ${c.website || ""}`,
      `Address: ${formatAddress(c).replace(/\n/g, ", ")}`,
      `Tags: ${c.tags || ""}`,
      `Identity: ${normalizeFingerprint(c.subject_fingerprint) || ""}`,
      "[/DNA-NEXUS-CONTACT]"
    ];

    return lines.filter((line) => !line.endsWith(": ")).join("\n");
  }

  async function copyText(text, successMessage) {
    const value = String(text || "").trim();
    if (!value) {
      setNotice(tr("contacts.copy_nothing", null, "Nothing to copy."), "warn");
      return;
    }

    try {
      await navigator.clipboard.writeText(value);
      setNotice(successMessage || tr("contacts.copied", null, "Copied."), "ok");
      return;
    } catch (_) {}

    const area = document.createElement("textarea");
    area.value = value;
    area.setAttribute("readonly", "");
    area.style.position = "fixed";
    area.style.left = "-1000px";
    area.style.top = "-1000px";
    document.body.appendChild(area);
    area.select();

    try {
      document.execCommand("copy");
      setNotice(successMessage || tr("contacts.copied", null, "Copied."), "ok");
    } catch (_) {
      setNotice(tr("contacts.copy_failed_manual", null, "Copy failed. Select and copy manually."), "err");
    } finally {
      area.remove();
    }
  }

  function currentContactForAction() {
    return selectedContact() || contactLikeFromForm();
  }

  function copyContactCard() {
    copyText(formatContactCard(currentContactForAction()), tr("contacts.contact_card_copied", null, "Contact card copied."));
  }

  function copyContactAddress() {
    copyText(formatAddress(currentContactForAction()), tr("contacts.address_copied", null, "Address copied."));
  }

  function copyContactEmail() {
    copyText(currentContactForAction().email, tr("contacts.email_copied", null, "Email copied."));
  }

  function copyContactPhone() {
    const c = currentContactForAction();
    copyText(c.phone || c.mobile, tr("contacts.phone_copied", null, "Phone copied."));
  }

  function openContactWebsite() {
    let url = String(currentContactForAction().website || "").trim();
    if (!url) {
      setNotice(tr("contacts.no_website", null, "No website saved for this contact."), "warn");
      return;
    }

    if (!/^https?:\/\//i.test(url)) url = `https://${url}`;
    window.open(url, "_blank", "noopener,noreferrer");
  }

  async function loadContacts() {
    state.loading = true;
    setNotice("", "info");
    renderList();

    try {
      const contactsBody = await apiJson(API.list);
      state.contacts = Array.isArray(contactsBody.contacts) ? contactsBody.contacts : [];

      try {
        const knownBody = await apiJson(API.localUsers);
        state.knownIdentities = Array.isArray(knownBody.candidates) ? knownBody.candidates : [];
      } catch (_) {
        state.knownIdentities = [];
      }

      const current = selectedContact();
      if (current) fillForm(current);
      else if (state.selectedFingerprint) clearForm();
      else renderIdentityOptions(null);

      renderList();
    } catch (error) {
      const detail = String(error && error.message ? error.message : error);
      setNotice(tr("contacts.load_failed", { error: detail }, `Failed to load contacts: ${detail}`), "err");
    } finally {
      state.loading = false;
      renderList();
    }
  }

  async function saveFromForm(event) {
    event.preventDefault();

    const payload = readPayload();
    const existing = selectedContact();

    if (!payload.subject_fingerprint) {
      setNotice(tr("contacts.identity_missing", null, "Identity anchor is missing."), "err");
      return;
    }

    if (!existing && payload.subject_kind === "local_user" && !knownIdentityByFingerprint(payload.subject_fingerprint)) {
      setNotice(tr("contacts.choose_known_user_first", null, "Choose a known DNA-Nexus user first."), "err");
      $("identitySelect").focus();
      return;
    }

    if (!existing && contactByFingerprint(payload.subject_fingerprint)) {
      setNotice(tr("contacts.identity_already_saved", null, "This identity is already saved. Select it from the address book."), "warn");
      return;
    }

    if (!payload.display_name) {
      setNotice(tr("contacts.display_name_required", null, "Display name is required."), "err");
      $("displayNameInput").focus();
      return;
    }

    const dupes = duplicateCandidates(payload);
    if (dupes.length) {
      const ok = await askConfirm({
        title: tr("contacts.duplicate_title", null, "Possible duplicate"),
        message: tr("contacts.duplicate_message", null, "A similar contact already exists."),
        items: duplicateItems(dupes),
        detail: tr("contacts.duplicate_detail", null, "Save this contact anyway?"),
        confirmText: tr("contacts.duplicate_save_anyway", null, "Save anyway"),
        cancelText: tr("contacts.duplicate_review", null, "Review")
      });

      if (!ok) {
        setNotice(tr("contacts.save_cancelled_duplicate", null, "Save cancelled because a possible duplicate was found."), "warn");
        return;
      }
    }

    try {
      const body = await apiJson(API.upsert, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload)
      });

      const saved = body.contact || payload;
      state.selectedFingerprint = normalizeFingerprint(saved.subject_fingerprint || payload.subject_fingerprint);
      setNotice(tr("contacts.contact_saved", null, "Contact saved."), "ok");
      await loadContacts();
    } catch (error) {
      const detail = String(error && error.message ? error.message : error);
      setNotice(tr("contacts.save_failed", { error: detail }, `Save failed: ${detail}`), "err");
    }
  }

  async function deleteSelected() {
    const contact = selectedContact();
    if (!contact) return;

    const fp = normalizeFingerprint(contact.subject_fingerprint);
    const label = contact.display_name || shortFingerprint(fp);
    const ok = await askConfirm({
      title: tr("contacts.delete_contact_title", null, "Delete contact?"),
      message: tr("contacts.delete_contact_message", { label }, `Delete ${label} from Contacts?`),
      detail: tr("contacts.delete_contact_detail", null, "This removes the contact from your private address book."),
      confirmText: tr("contacts.delete", null, "Delete"),
      cancelText: tr("contacts.cancel", null, "Cancel"),
      danger: true
    });
    if (!ok) return;

    try {
      await apiJson(API.delete, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ subject_fingerprint: fp })
      });

      setNotice(tr("contacts.contact_deleted", null, "Contact deleted."), "ok");
      clearForm();
      await loadContacts();
    } catch (error) {
      const detail = String(error && error.message ? error.message : error);
      setNotice(tr("contacts.delete_failed", { error: detail }, `Delete failed: ${detail}`), "err");
    }
  }

  function escapeVCardValue(value) {
    return String(value || "")
      .replace(/\\/g, "\\\\")
      .replace(/\n/g, "\\n")
      .replace(/,/g, "\\,")
      .replace(/;/g, "\\;");
  }

  function exportVCard() {
    const contacts = filteredContacts();
    if (!contacts.length) {
      setNotice(tr("contacts.no_contacts_export", null, "No contacts to export."), "warn");
      return;
    }

    const cards = contacts.map((c) => {
      const adr = ["", "", c.street || "", c.city || "", "", c.postal_code || "", c.country || ""]
        .map(escapeVCardValue)
        .join(";");

      const lines = [
        "BEGIN:VCARD",
        "VERSION:4.0",
        `FN:${escapeVCardValue(c.display_name || c.company || shortFingerprint(c.subject_fingerprint))}`,
        `X-DNA-NEXUS-FINGERPRINT:${escapeVCardValue(normalizeFingerprint(c.subject_fingerprint))}`,
        `X-DNA-NEXUS-KIND:${escapeVCardValue(c.subject_kind || "manual_contact")}`,
        `X-DNA-NEXUS-CONTACT-TYPE:${escapeVCardValue(c.contact_type || "person")}`,
        `X-DNA-NEXUS-STATUS:${escapeVCardValue(c.status || "active")}`
      ];

      if (c.company) lines.push(`ORG:${escapeVCardValue(c.company)}`);
      if (c.title) lines.push(`TITLE:${escapeVCardValue(c.title)}`);
      if (c.nickname) lines.push(`NICKNAME:${escapeVCardValue(c.nickname)}`);
      if (c.email) lines.push(`EMAIL:${escapeVCardValue(c.email)}`);
      if (c.phone) lines.push(`TEL;TYPE=WORK,VOICE:${escapeVCardValue(c.phone)}`);
      if (c.mobile) lines.push(`TEL;TYPE=CELL:${escapeVCardValue(c.mobile)}`);
      if (c.website) lines.push(`URL:${escapeVCardValue(c.website)}`);
      if (c.street || c.city || c.postal_code || c.country) lines.push(`ADR;TYPE=WORK:${adr}`);
      if (c.tags) lines.push(`CATEGORIES:${escapeVCardValue(c.tags)}`);
      if (c.notes) lines.push(`NOTE:${escapeVCardValue(c.notes)}`);

      if (c.delivery_name) lines.push(`X-DNA-NEXUS-DELIVERY-NAME:${escapeVCardValue(c.delivery_name)}`);
      if (c.delivery_street) lines.push(`X-DNA-NEXUS-DELIVERY-STREET:${escapeVCardValue(c.delivery_street)}`);
      if (c.delivery_postal_code) lines.push(`X-DNA-NEXUS-DELIVERY-POSTAL-CODE:${escapeVCardValue(c.delivery_postal_code)}`);
      if (c.delivery_city) lines.push(`X-DNA-NEXUS-DELIVERY-CITY:${escapeVCardValue(c.delivery_city)}`);
      if (c.delivery_country) lines.push(`X-DNA-NEXUS-DELIVERY-COUNTRY:${escapeVCardValue(c.delivery_country)}`);

      lines.push("END:VCARD");
      return lines.join("\n");
    }).join("\n");

    const blob = new Blob([cards + "\n"], { type: "text/vcard" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = "dna-nexus-contacts.vcf";
    document.body.appendChild(a);
    a.click();
    a.remove();
    URL.revokeObjectURL(url);
  }

  function csvSafeCell(value) {
    const s = String(value || "");

    // Prevent spreadsheet formula injection when exported CSV is opened
    // in Excel/LibreOffice/Google Sheets.
    if (/^[=+\-@\t\r\n]/.test(s)) return "'" + s;

    return s;
  }

  function csvEscape(value) {
    const s = csvSafeCell(value);
    if (/[",\n\r]/.test(s)) return `"${s.replace(/"/g, '""')}"`;
    return s;
  }

  function exportCSV() {
    const contacts = filteredContacts();
    if (!contacts.length) {
      setNotice(tr("contacts.no_contacts_export", null, "No contacts to export."), "warn");
      return;
    }

    const rows = [
      CSV_FIELDS.join(","),
      ...contacts.map((c) => CSV_FIELDS.map((field) => csvEscape(c[field])).join(","))
    ];

    const blob = new Blob([rows.join("\n") + "\n"], { type: "text/csv" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = "dna-nexus-contacts.csv";
    document.body.appendChild(a);
    a.click();
    a.remove();
    URL.revokeObjectURL(url);
  }

  function parseCsvRows(text) {
    const rows = [];
    let row = [];
    let cell = "";
    let quoted = false;

    const s = String(text || "");
    for (let i = 0; i < s.length; i += 1) {
      const ch = s[i];

      if (quoted) {
        if (ch === '"' && s[i + 1] === '"') {
          cell += '"';
          i += 1;
        } else if (ch === '"') {
          quoted = false;
        } else {
          cell += ch;
        }
        continue;
      }

      if (ch === '"') {
        quoted = true;
      } else if (ch === ",") {
        row.push(cell);
        cell = "";
      } else if (ch === "\n") {
        row.push(cell);
        rows.push(row);
        row = [];
        cell = "";
      } else if (ch !== "\r") {
        cell += ch;
      }
    }

    if (cell || row.length) {
      row.push(cell);
      rows.push(row);
    }

    return rows.filter((r) => r.some((c) => String(c || "").trim()));
  }

  function normalizeCsvHeader(value) {
    const raw = String(value || "")
      .trim()
      .toLowerCase()
      .replace(/\s+/g, "_")
      .replace(/[^a-z0-9_]/g, "");

    const aliases = {
      name: "display_name",
      fullname: "display_name",
      full_name: "display_name",
      organization: "company",
      organisation: "company",
      company_name: "company",
      job_title: "title",
      role: "title",
      telephone: "phone",
      cell: "mobile",
      cellphone: "mobile",
      postalcode: "postal_code",
      zip: "postal_code",
      zip_code: "postal_code",
      address: "street",
      address_line: "street",
      country_name: "country",
      categories: "tags"
    };

    return aliases[raw] || raw;
  }

  function parseCSVContacts(text) {
    const rows = parseCsvRows(text);
    if (rows.length < 2) return [];

    if (rows.length - 1 > MAX_IMPORT_CONTACTS) {
      throw new Error(`CSV import limit is ${MAX_IMPORT_CONTACTS} contacts.`);
    }

    const headers = rows[0].map(normalizeCsvHeader);
    const contacts = [];

    for (const row of rows.slice(1)) {
      const c = {
        subject_fingerprint: "",
        subject_kind: "manual_contact",
        contact_type: "person",
        status: "active"
      };

      headers.forEach((header, idx) => {
        if (!header) return;
        c[header] = String(row[idx] || "").trim();
      });

      if (!c.subject_fingerprint) c.subject_fingerprint = randomHex(32);
      c.subject_fingerprint = normalizeFingerprint(c.subject_fingerprint);
      if (!c.subject_kind) c.subject_kind = "manual_contact";
      if (!c.contact_type) c.contact_type = "person";
      if (!c.status) c.status = "active";
      if (!c.display_name) c.display_name = c.company || c.email || c.phone || c.mobile || tr("contacts.imported_contact", null, "Imported contact");

      contacts.push(c);
    }

    return contacts.filter((c) => c.subject_fingerprint && c.display_name);
  }

  function unescapeVCardValue(value) {
    return String(value || "")
      .replace(/\\n/gi, "\n")
      .replace(/\\,/g, ",")
      .replace(/\\;/g, ";")
      .replace(/\\\\/g, "\\");
  }

  function parseAdr(value) {
    const parts = String(value || "").split(";").map(unescapeVCardValue);
    return {
      street: parts[2] || "",
      city: parts[3] || "",
      postal_code: parts[5] || "",
      country: parts[6] || ""
    };
  }

  function unfoldVCard(text) {
    return String(text || "").replace(/\r?\n[ \t]/g, "");
  }

  function parseVCard(text) {
    const blocks = unfoldVCard(text).split(/BEGIN:VCARD/i).slice(1);

    if (blocks.length > MAX_IMPORT_CONTACTS) {
      throw new Error(`vCard import limit is ${MAX_IMPORT_CONTACTS} contacts.`);
    }

    return blocks.map((block) => {
      const lines = block.split(/\r?\n/);
      const out = {
        subject_fingerprint: "",
        subject_kind: "manual_contact",
        contact_type: "person",
        status: "active",
        display_name: "",
        company: "",
        title: "",
        nickname: "",
        email: "",
        phone: "",
        mobile: "",
        website: "",
        street: "",
        postal_code: "",
        city: "",
        country: "",
        delivery_name: "",
        delivery_street: "",
        delivery_postal_code: "",
        delivery_city: "",
        delivery_country: "",
        tags: "",
        notes: ""
      };

      for (const line of lines) {
        const idx = line.indexOf(":");
        if (idx < 0) continue;

        const keyPart = line.slice(0, idx);
        const rawKey = keyPart.split(";")[0].trim().toUpperCase();
        const value = unescapeVCardValue(line.slice(idx + 1).trim());
        const typeText = keyPart.toUpperCase();

        if (rawKey === "FN") out.display_name = value;
        if (rawKey === "ORG") out.company = value;
        if (rawKey === "TITLE") out.title = value;
        if (rawKey === "NICKNAME") out.nickname = value;
        if (rawKey === "EMAIL") out.email = value;
        if (rawKey === "TEL" && typeText.includes("CELL")) out.mobile = value;
        else if (rawKey === "TEL") out.phone = value;
        if (rawKey === "URL") out.website = value;
        if (rawKey === "ADR") Object.assign(out, parseAdr(line.slice(idx + 1).trim()));
        if (rawKey === "CATEGORIES") out.tags = value;
        if (rawKey === "NOTE") out.notes = value;

        if (rawKey === "X-DNA-NEXUS-FINGERPRINT") out.subject_fingerprint = normalizeFingerprint(value);
        if (rawKey === "X-DNA-NEXUS-KIND") out.subject_kind = value || "manual_contact";
        if (rawKey === "X-DNA-NEXUS-CONTACT-TYPE") out.contact_type = value || "person";
        if (rawKey === "X-DNA-NEXUS-STATUS") out.status = value || "active";

        if (rawKey === "X-DNA-NEXUS-DELIVERY-NAME") out.delivery_name = value;
        if (rawKey === "X-DNA-NEXUS-DELIVERY-STREET") out.delivery_street = value;
        if (rawKey === "X-DNA-NEXUS-DELIVERY-POSTAL-CODE") out.delivery_postal_code = value;
        if (rawKey === "X-DNA-NEXUS-DELIVERY-CITY") out.delivery_city = value;
        if (rawKey === "X-DNA-NEXUS-DELIVERY-COUNTRY") out.delivery_country = value;
      }

      if (!out.subject_fingerprint) out.subject_fingerprint = randomHex(32);
      if (!out.display_name) out.display_name = out.company || out.email || out.phone || tr("contacts.imported_contact", null, "Imported contact");

      return out;
    }).filter((c) => c.subject_fingerprint && c.display_name);
  }

  async function importContactsFromList(contacts, label) {
    if (!contacts.length) {
      setNotice(tr("contacts.no_importable", { label }, `No importable contacts found in ${label}.`), "warn");
      return;
    }

    let imported = 0;
    let skipped = 0;

    for (const contact of contacts) {
      const dupes = duplicateCandidates(contact);
      if (dupes.length) {
        skipped += 1;
        continue;
      }

      await apiJson(API.upsert, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(contact)
      });
      imported += 1;
    }

    setNotice(
      tr("contacts.import_result", { imported, skipped }, `Imported ${imported} contact(s). Skipped ${skipped} possible duplicate(s).`),
      skipped ? "warn" : "ok"
    );
    await loadContacts();
  }

  async function importFile(file) {
    if (!file) return;

    try {
      if (Number(file.size || 0) > MAX_IMPORT_BYTES) {
        throw new Error(tr("contacts.import_too_large", null, "Import file is too large. Maximum size is 2 MB."));
      }

      const text = await file.text();
      const name = String(file.name || "").toLowerCase();
      const looksLikeVCard = /BEGIN:VCARD/i.test(text) || name.endsWith(".vcf");
      const contacts = looksLikeVCard ? parseVCard(text) : parseCSVContacts(text);
      await importContactsFromList(contacts, looksLikeVCard ? "vCard" : "CSV");
    } catch (error) {
      const detail = String(error && error.message ? error.message : error);
      setNotice(tr("contacts.import_failed", { error: detail }, `Import failed: ${detail}`), "err");
    }
  }

  async function getAppVersion() {
    const fromPath = location.pathname.match(/^\/apps\/([^/]+)\/([^/]+)\//);
    if (fromPath && fromPath[2]) return decodeURIComponent(fromPath[2]);

    for (const url of ["../manifest.json", "./manifest.json"]) {
      try {
        const res = await fetch(url, {
          cache: "no-store",
          headers: { "Accept": "application/json" }
        });
        if (!res.ok) continue;
        const body = await res.json();
        if (body && typeof body.version === "string" && body.version.trim()) return body.version.trim();
      } catch (_) {}
    }

    return "";
  }

  async function initVersionBadge() {
    const badge = $("versionBadge");
    if (!badge) return;

    const version = await getAppVersion();
    badge.textContent = version
      ? tr("contacts.version", { version }, `Contacts v${version}`)
      : tr("contacts.title", null, "Contacts");
  }

  function bindEvents() {
    $("refreshBtn").addEventListener("click", loadContacts);
    $("newBtn").addEventListener("click", () => {
      clearForm();
      $("displayNameInput").focus();
    });

    $("identityModeSelect").addEventListener("change", applyIdentityMode);
    $("identitySelect").addEventListener("change", applySelectedIdentity);

    $("searchInput").addEventListener("input", (event) => {
      state.search = event.target.value || "";
      renderList();
    });

    $("typeFilter").addEventListener("change", (event) => {
      state.type = event.target.value || "";
      renderList();
    });

    $("statusFilter").addEventListener("change", (event) => {
      state.status = event.target.value || "";
      renderList();
    });

    $("contactForm").addEventListener("submit", saveFromForm);
    $("resetBtn").addEventListener("click", clearForm);
    $("deleteBtn").addEventListener("click", deleteSelected);
    $("exportBtn").addEventListener("click", exportVCard);
    $("exportCsvBtn").addEventListener("click", exportCSV);

    $("copyCardBtn").addEventListener("click", copyContactCard);
    $("copyAddressBtn").addEventListener("click", copyContactAddress);
    $("copyEmailBtn").addEventListener("click", copyContactEmail);
    $("copyPhoneBtn").addEventListener("click", copyContactPhone);
    $("openWebsiteBtn").addEventListener("click", openContactWebsite);

    $("importInput").addEventListener("change", async (event) => {
      const file = event.target.files && event.target.files[0];
      await importFile(file);
      event.target.value = "";
    });
  }

  async function waitForI18nReady() {
    try {
      const i18n = window.PQNAS_I18N;
      if (i18n && typeof i18n.ready === "function") {
        await i18n.ready();
      }
    } catch (_) {}
  }

  document.addEventListener("DOMContentLoaded", async () => {
    bindConfirmModalEvents();

    window.addEventListener("pqnas-language-changed", () => {
      translateStaticContactsUi();
      renderList();
      initVersionBadge().catch(() => {});
    });

    await waitForI18nReady();
    translateStaticContactsUi();

    bindEvents();
    setEditorMode("new");
    clearForm();
    await initVersionBadge();
    await loadContacts();
  });
})();
