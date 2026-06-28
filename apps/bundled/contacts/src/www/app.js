
(function () {
  "use strict";

  const API = {
    list: "/api/v4/people/list",
    upsert: "/api/v4/people/upsert",
    delete: "/api/v4/people/delete",
    localUsers: "/api/v4/people/local-users"
  };

  const state = {
    contacts: [],
    knownIdentities: [],
    selectedFingerprint: "",
    search: "",
    type: "",
    status: "",
    loading: false
  };

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
    if (kind === "manual_contact") return "Manual";
    if (kind === "local_user") return "Local user";
    if (kind === "external_dna") return "External DNA";
    return "Fingerprint";
  }

  function typeLabel(type) {
    if (type === "company") return "Company";
    if (type === "customer") return "Customer";
    if (type === "supplier") return "Supplier";
    if (type === "family") return "Family";
    if (type === "other") return "Other";
    return "Person";
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
    title.textContent = opts.title || "Confirm action";
    cancelBtn.textContent = opts.cancelText || "Cancel";
    okBtn.textContent = opts.confirmText || "OK";

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
    empty.textContent = "Choose a known DNA-Nexus person…";
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
      ? "Loading contacts…"
      : `${items.length} shown / ${state.contacts.length} total`;

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
    const badge = $("selectedBadge");
    const title = $("editorTitle");
    const deleteBtn = $("deleteBtn");

    if (badge) {
      badge.classList.remove("ok", "warn", "err", "info", "muted");
      badge.classList.add(mode === "edit" ? "ok" : "info");
      badge.textContent = mode === "edit" ? "editing" : "new";
    }

    if (title) title.textContent = mode === "edit" ? "Edit contact" : "New contact";
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

  function duplicateItems(hits) {
    const items = hits.slice(0, 5).map((hit) => {
      const c = hit.contact || {};
      const label = c.display_name || c.company || shortFingerprint(c.subject_fingerprint);
      return `${label}: ${hit.reasons.join(", ")}`;
    });

    if (hits.length > 5) {
      items.push(`plus ${hits.length - 5} more`);
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
      setNotice("Nothing to copy.", "warn");
      return;
    }

    try {
      await navigator.clipboard.writeText(value);
      setNotice(successMessage || "Copied.", "ok");
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
      setNotice(successMessage || "Copied.", "ok");
    } catch (_) {
      setNotice("Copy failed. Select and copy manually.", "err");
    } finally {
      area.remove();
    }
  }

  function currentContactForAction() {
    return selectedContact() || contactLikeFromForm();
  }

  function copyContactCard() {
    copyText(formatContactCard(currentContactForAction()), "Contact card copied.");
  }

  function copyContactAddress() {
    copyText(formatAddress(currentContactForAction()), "Address copied.");
  }

  function copyContactEmail() {
    copyText(currentContactForAction().email, "Email copied.");
  }

  function copyContactPhone() {
    const c = currentContactForAction();
    copyText(c.phone || c.mobile, "Phone copied.");
  }

  function openContactWebsite() {
    let url = String(currentContactForAction().website || "").trim();
    if (!url) {
      setNotice("No website saved for this contact.", "warn");
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
      setNotice(`Failed to load contacts: ${error.message || error}`, "err");
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
      setNotice("Identity anchor is missing.", "err");
      return;
    }

    if (!existing && payload.subject_kind === "local_user" && !knownIdentityByFingerprint(payload.subject_fingerprint)) {
      setNotice("Choose a known DNA-Nexus user first.", "err");
      $("identitySelect").focus();
      return;
    }

    if (!existing && contactByFingerprint(payload.subject_fingerprint)) {
      setNotice("This identity is already saved. Select it from the address book.", "warn");
      return;
    }

    if (!payload.display_name) {
      setNotice("Display name is required.", "err");
      $("displayNameInput").focus();
      return;
    }

    const dupes = duplicateCandidates(payload);
    if (dupes.length) {
      const ok = await askConfirm({
        title: "Possible duplicate",
        message: "A similar contact already exists.",
        items: duplicateItems(dupes),
        detail: "Save this contact anyway?",
        confirmText: "Save anyway",
        cancelText: "Review"
      });

      if (!ok) {
        setNotice("Save cancelled because a possible duplicate was found.", "warn");
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
      setNotice("Contact saved.", "ok");
      await loadContacts();
    } catch (error) {
      setNotice(`Save failed: ${error.message || error}`, "err");
    }
  }

  async function deleteSelected() {
    const contact = selectedContact();
    if (!contact) return;

    const fp = normalizeFingerprint(contact.subject_fingerprint);
    const label = contact.display_name || shortFingerprint(fp);
    const ok = await askConfirm({
      title: "Delete contact?",
      message: `Delete ${label} from Contacts?`,
      detail: "This removes the contact from your private address book.",
      confirmText: "Delete",
      cancelText: "Cancel",
      danger: true
    });
    if (!ok) return;

    try {
      await apiJson(API.delete, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ subject_fingerprint: fp })
      });

      setNotice("Contact deleted.", "ok");
      clearForm();
      await loadContacts();
    } catch (error) {
      setNotice(`Delete failed: ${error.message || error}`, "err");
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
      setNotice("No contacts to export.", "warn");
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

  function csvEscape(value) {
    const s = String(value || "");
    if (/[",\n\r]/.test(s)) return `"${s.replace(/"/g, '""')}"`;
    return s;
  }

  function exportCSV() {
    const contacts = filteredContacts();
    if (!contacts.length) {
      setNotice("No contacts to export.", "warn");
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
      if (!c.display_name) c.display_name = c.company || c.email || c.phone || c.mobile || "Imported contact";

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
      if (!out.display_name) out.display_name = out.company || out.email || out.phone || "Imported contact";

      return out;
    }).filter((c) => c.subject_fingerprint && c.display_name);
  }

  async function importContactsFromList(contacts, label) {
    if (!contacts.length) {
      setNotice(`No importable contacts found in ${label}.`, "warn");
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

    setNotice(`Imported ${imported} contact(s). Skipped ${skipped} possible duplicate(s).`, skipped ? "warn" : "ok");
    await loadContacts();
  }

  async function importFile(file) {
    if (!file) return;

    try {
      const text = await file.text();
      const name = String(file.name || "").toLowerCase();
      const looksLikeVCard = /BEGIN:VCARD/i.test(text) || name.endsWith(".vcf");
      const contacts = looksLikeVCard ? parseVCard(text) : parseCSVContacts(text);
      await importContactsFromList(contacts, looksLikeVCard ? "vCard" : "CSV");
    } catch (error) {
      setNotice(`Import failed: ${error.message || error}`, "err");
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
    badge.textContent = version ? `Contacts v${version}` : "Contacts";
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

  document.addEventListener("DOMContentLoaded", async () => {
    bindConfirmModalEvents();
    bindEvents();
    setEditorMode("new");
    clearForm();
    await initVersionBadge();
    await loadContacts();
  });
})();
