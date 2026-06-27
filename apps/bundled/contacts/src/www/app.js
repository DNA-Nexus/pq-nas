
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
    kind: "",
    loading: false
  };

  function $(id) {
    return document.getElementById(id);
  }

  function normalizeFingerprint(value) {
    return String(value || "")
      .trim()
      .replace(/[\s:-]+/g, "")
      .toLowerCase();
  }

  function shortFingerprint(value) {
    const s = String(value || "");
    if (s.length <= 18) return s;
    return `${s.slice(0, 10)}…${s.slice(-6)}`;
  }

  function kindLabel(kind) {
    if (kind === "local_user") return "Local user";
    if (kind === "external_dna") return "External DNA";
    return "Fingerprint";
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
        if (state.kind && String(c.subject_kind || "fingerprint") !== state.kind) return false;
        if (!q) return true;

        const haystack = [
          c.display_name,
          c.nickname,
          c.notes,
          c.subject_kind,
          c.subject_fingerprint,
          c.subject_fingerprint_short
        ].join(" ").toLowerCase();

        return haystack.includes(q);
      })
      .sort((a, b) => {
        const an = String(a.display_name || a.subject_fingerprint || "");
        const bn = String(b.display_name || b.subject_fingerprint || "");
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
      name.textContent = contact.display_name || shortFingerprint(fp);

      const badge = document.createElement("span");
      badge.className = "pq-badge muted";
      badge.textContent = kindLabel(contact.subject_kind || "fingerprint");

      top.appendChild(name);
      top.appendChild(badge);

      const meta = document.createElement("div");
      meta.className = "contacts-rowMeta";
      meta.textContent = shortFingerprint(fp);

      row.appendChild(top);
      row.appendChild(meta);

      const nickname = String(contact.nickname || "").trim();
      if (nickname) {
        const nick = document.createElement("div");
        nick.className = "contacts-rowNotes";
        nick.textContent = nickname;
        row.appendChild(nick);
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

  function clearForm() {
    state.selectedFingerprint = "";
    $("displayNameInput").value = "";
    $("nicknameInput").value = "";
    $("fingerprintInput").value = "";
    $("kindInput").value = "local_user";
    $("notesInput").value = "";
    renderIdentityOptions(null);
    setEditorMode("new");
    renderList();
  }

  function fillForm(contact) {
    $("displayNameInput").value = contact.display_name || "";
    $("nicknameInput").value = contact.nickname || "";
    $("fingerprintInput").value = normalizeFingerprint(contact.subject_fingerprint);
    $("kindInput").value = contact.subject_kind || "fingerprint";
    $("notesInput").value = contact.notes || "";
    renderIdentityOptions(contact);
    setEditorMode("edit");
  }

  function applySelectedIdentity() {
    const select = $("identitySelect");
    const fp = normalizeFingerprint(select && select.value);
    const item = knownIdentityByFingerprint(fp);

    $("fingerprintInput").value = fp;
    $("kindInput").value = "local_user";

    if (item && !$("displayNameInput").value.trim()) {
      $("displayNameInput").value = String(item.display_name || item.name || shortFingerprint(fp));
    }
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
      } catch (e) {
        state.knownIdentities = [];
        setNotice(`Known identity list unavailable: ${e.message || e}`, "warn");
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

    const fp = normalizeFingerprint($("fingerprintInput").value);
    const existing = selectedContact();

    const payload = {
      subject_fingerprint: fp,
      subject_kind: existing ? ($("kindInput").value || "fingerprint") : "local_user",
      display_name: String($("displayNameInput").value || "").trim(),
      nickname: String($("nicknameInput").value || "").trim(),
      notes: String($("notesInput").value || "").trim()
    };

    if (!payload.subject_fingerprint) {
      setNotice("Choose a known DNA-Nexus person first.", "err");
      $("identitySelect").focus();
      return;
    }

    if (!existing && !knownIdentityByFingerprint(payload.subject_fingerprint)) {
      setNotice("Cannot save an invented fingerprint. Choose a known DNA-Nexus person.", "err");
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
    const ok = window.confirm(`Delete ${label} from Contacts?`);
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
      const lines = [
        "BEGIN:VCARD",
        "VERSION:4.0",
        `FN:${escapeVCardValue(c.display_name || shortFingerprint(c.subject_fingerprint))}`,
        `X-DNA-NEXUS-FINGERPRINT:${escapeVCardValue(normalizeFingerprint(c.subject_fingerprint))}`,
        `X-DNA-NEXUS-KIND:${escapeVCardValue(c.subject_kind || "fingerprint")}`
      ];

      if (c.nickname) lines.push(`NICKNAME:${escapeVCardValue(c.nickname)}`);
      if (c.notes) lines.push(`NOTE:${escapeVCardValue(c.notes)}`);

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

  function unescapeVCardValue(value) {
    return String(value || "")
      .replace(/\\n/gi, "\n")
      .replace(/\\,/g, ",")
      .replace(/\\;/g, ";")
      .replace(/\\\\/g, "\\");
  }

  function parseVCard(text) {
    const blocks = String(text || "").split(/BEGIN:VCARD/i).slice(1);
    return blocks.map((block) => {
      const lines = block.split(/\r?\n/);
      const out = {
        subject_fingerprint: "",
        subject_kind: "fingerprint",
        display_name: "",
        nickname: "",
        notes: ""
      };

      for (const line of lines) {
        const idx = line.indexOf(":");
        if (idx < 0) continue;

        const rawKey = line.slice(0, idx).split(";")[0].trim().toUpperCase();
        const value = unescapeVCardValue(line.slice(idx + 1).trim());

        if (rawKey === "FN") out.display_name = value;
        if (rawKey === "NICKNAME") out.nickname = value;
        if (rawKey === "NOTE") out.notes = value;
        if (rawKey === "X-DNA-NEXUS-FINGERPRINT") out.subject_fingerprint = normalizeFingerprint(value);
        if (rawKey === "X-DNA-NEXUS-KIND") out.subject_kind = value || "fingerprint";
      }

      return out;
    }).filter((c) => c.subject_fingerprint && c.display_name);
  }

  async function importVCard(file) {
    if (!file) return;

    try {
      const text = await file.text();
      const parsed = parseVCard(text);
      if (!parsed.length) {
        setNotice("No importable DNA-Nexus contacts found in vCard.", "warn");
        return;
      }

      const accepted = [];
      let rejected = 0;

      for (const contact of parsed) {
        const fp = normalizeFingerprint(contact.subject_fingerprint);
        if (contactByFingerprint(fp) || knownIdentityByFingerprint(fp)) {
          accepted.push(contact);
        } else {
          rejected += 1;
        }
      }

      for (const contact of accepted) {
        await apiJson(API.upsert, {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify(contact)
        });
      }

      if (accepted.length) {
        setNotice(`Imported ${accepted.length} contact(s). Rejected ${rejected} unknown identity value(s).`, rejected ? "warn" : "ok");
        await loadContacts();
      } else {
        setNotice(`No contacts imported. Rejected ${rejected} unknown identity value(s).`, "err");
      }
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
      $("identitySelect").focus();
    });

    $("identitySelect").addEventListener("change", applySelectedIdentity);

    $("searchInput").addEventListener("input", (event) => {
      state.search = event.target.value || "";
      renderList();
    });

    $("kindFilter").addEventListener("change", (event) => {
      state.kind = event.target.value || "";
      renderList();
    });

    $("contactForm").addEventListener("submit", saveFromForm);
    $("resetBtn").addEventListener("click", clearForm);
    $("deleteBtn").addEventListener("click", deleteSelected);
    $("exportBtn").addEventListener("click", exportVCard);

    $("importInput").addEventListener("change", async (event) => {
      const file = event.target.files && event.target.files[0];
      await importVCard(file);
      event.target.value = "";
    });
  }

  document.addEventListener("DOMContentLoaded", async () => {
    bindEvents();
    setEditorMode("new");
    await initVersionBadge();
    await loadContacts();
  });
})();
