(() => {
  "use strict";

  const DEFAULT_PATH = "/Vault";
  const STORAGE_KEY = "pqnas_vault_fm_path_v2";

  function onReady(fn) {
    if (document.readyState === "loading") {
      document.addEventListener("DOMContentLoaded", fn, { once: true });
    } else {
      fn();
    }
  }

  function text(value) {
    return value == null ? "" : String(value);
  }

  function normalizeDisplayPath(raw) {
    let value = text(raw).trim();
    if (!value) value = DEFAULT_PATH;
    if (!value.startsWith("/")) value = "/" + value;

    const parts = [];
    for (const part of value.replace(/\\/g, "/").split("/")) {
      const clean = String(part || "").trim();
      if (!clean || clean === ".") continue;
      if (clean === "..") continue;
      parts.push(clean);
    }

    return "/" + parts.join("/");
  }

  function apiPathFromDisplayPath(path) {
    // Files API expects user-relative paths without a leading slash.
    return normalizeDisplayPath(path).replace(/^\/+/, "");
  }

  function inputPathFromDisplayPath(path) {
    // Keep the toolbar path user-relative like File Manager/API paths.
    // Internally we still normalize through /Vault-style display paths.
    return apiPathFromDisplayPath(path) || "/";
  }

  function displayPathFromApiPath(path) {
    return normalizeDisplayPath(path || DEFAULT_PATH);
  }

  function parentPath(path) {
    const normalized = normalizeDisplayPath(path);
    const parts = normalized.split("/").filter(Boolean);
    if (parts.length <= 1) return "/";
    parts.pop();
    return "/" + parts.join("/");
  }

  function fileNameFromPath(path) {
    const parts = text(path).split("/").filter(Boolean);
    return parts.length ? parts[parts.length - 1] : "";
  }

  function dirNameFromDisplayPath(path) {
    const parts = normalizeDisplayPath(path).split("/").filter(Boolean);
    if (parts.length <= 1) return "/";
    parts.pop();
    return "/" + parts.join("/");
  }

  function friendlyVaultName(name) {
    const value = text(name);
    return value.endsWith(".dnavault.json")
      ? value.slice(0, -".dnavault.json".length)
      : value;
  }

  function joinDisplayPath(dir, name) {
    const cleanDir = normalizeDisplayPath(dir);
    if (cleanDir === "/") return normalizeDisplayPath(name);
    return normalizeDisplayPath(`${cleanDir}/${name}`);
  }

  function b64ToBytes(b64) {
    const clean = String(b64 || "").replace(/\s+/g, "");
    const bin = atob(clean);
    const out = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i += 1) out[i] = bin.charCodeAt(i);
    return out;
  }

  function pickKnownVaultField(obj, path) {
    // Security: Vault packages are untrusted JSON. Use an explicit field
    // whitelist instead of dynamic object path traversal, so prototype-related
    // properties can never be walked through package data.
    if (!obj || typeof obj !== "object") return "";

    switch (path) {
      case "salt_b64":
        return obj.salt_b64;
      case "kdf_salt_b64":
        return obj.kdf_salt_b64;
      case "iv_b64":
        return obj.iv_b64;
      case "wrap_iv_b64":
        return obj.wrap_iv_b64;
      case "nonce_b64":
        return obj.nonce_b64;
      case "wrapped_cek_b64":
        return obj.wrapped_cek_b64;
      case "wrapped_key_b64":
        return obj.wrapped_key_b64;
      case "key_ciphertext_b64":
        return obj.key_ciphertext_b64;
      case "ciphertext_b64":
        return obj.ciphertext_b64;
      case "data_b64":
        return obj.data_b64;
      case "iterations":
        return obj.iterations;
      case "kdf_iterations":
        return obj.kdf_iterations;
      case "kdf.salt_b64":
        return obj.kdf && typeof obj.kdf === "object" ? obj.kdf.salt_b64 : "";
      case "kdf.salt":
        return obj.kdf && typeof obj.kdf === "object" ? obj.kdf.salt : "";
      case "kdf.iterations":
        return obj.kdf && typeof obj.kdf === "object" ? obj.kdf.iterations : "";
      case "wrap.iv_b64":
        return obj.wrap && typeof obj.wrap === "object" ? obj.wrap.iv_b64 : "";
      case "wrap.ciphertext_b64":
        return obj.wrap && typeof obj.wrap === "object" ? obj.wrap.ciphertext_b64 : "";
      case "enc.iv_b64":
        return obj.enc && typeof obj.enc === "object" ? obj.enc.iv_b64 : "";
      case "enc.ciphertext_b64":
        return obj.enc && typeof obj.enc === "object" ? obj.enc.ciphertext_b64 : "";
      default:
        return "";
    }
  }

  function pickPath(obj, paths) {
    for (const path of paths) {
      const value = pickKnownVaultField(obj, path);
      if (value !== null && value !== undefined && value !== "") return value;
    }

    return "";
  }

  async function derivePassphraseAesKey(passphrase, salt, iterations) {
    const baseKey = await crypto.subtle.importKey(
      "raw",
      new TextEncoder().encode(passphrase),
      "PBKDF2",
      false,
      ["deriveKey"]
    );

    return crypto.subtle.deriveKey(
      {
        name: "PBKDF2",
        salt,
        iterations: Number(iterations) || 310000,
        hash: "SHA-256"
      },
      baseKey,
      { name: "AES-GCM", length: 256 },
      false,
      ["decrypt"]
    );
  }

  async function aesGcmDecryptWithOptionalAad(key, iv, ciphertext, aadBytes) {
    const aadOptions = aadBytes && aadBytes.length ? [aadBytes, null] : [null];

    let lastErr = null;
    for (const aad of aadOptions) {
      try {
        const alg = { name: "AES-GCM", iv };
        if (aad) alg.additionalData = aad;
        return new Uint8Array(await crypto.subtle.decrypt(alg, key, ciphertext));
      } catch (err) {
        lastErr = err;
      }
    }

    throw lastErr || new Error("AES-GCM decrypt failed");
  }

  function findUserWrappedKey(pkg) {
    const keys = Array.isArray(pkg && pkg.wrapped_keys) ? pkg.wrapped_keys : [];
    if (!keys.length) throw new Error("Vault package has no wrapped keys");

    const userKey = keys.find((rec) => {
      const label = [
        rec && rec.kind,
        rec && rec.type,
        rec && rec.role,
        rec && rec.recipient,
        rec && rec.alg,
        rec && rec.wrap_alg
      ].map((v) => String(v || "").toLowerCase()).join(" ");

      return label.includes("user") || label.includes("passphrase") || label.includes("password");
    });

    return userKey || keys[0];
  }

  async function decryptVaultPackageForDownload(pkg, passphrase) {
    if (!pkg || typeof pkg !== "object") throw new Error("Invalid Vault package");
    if (!passphrase) throw new Error("Vault passphrase required");

    const wrapped = findUserWrappedKey(pkg);
    const aadBytes = pkg.aad_b64 ? b64ToBytes(pkg.aad_b64) : null;

    const saltB64 = pickPath(wrapped, [
      "salt_b64",
      "kdf_salt_b64",
      "kdf.salt_b64",
      "kdf.salt"
    ]);

    const ivB64 = pickPath(wrapped, [
      "iv_b64",
      "wrap_iv_b64",
      "nonce_b64",
      "wrap.iv_b64",
      "enc.iv_b64"
    ]);

    const wrappedCekB64 = pickPath(wrapped, [
      "wrapped_cek_b64",
      "wrapped_key_b64",
      "key_ciphertext_b64",
      "ciphertext_b64",
      "wrap.ciphertext_b64",
      "enc.ciphertext_b64"
    ]);

    const iterations = pickPath(wrapped, [
      "iterations",
      "kdf_iterations",
      "kdf.iterations"
    ]) || 310000;

    if (!saltB64 || !ivB64 || !wrappedCekB64) {
      throw new Error("Unsupported user-wrapped key shape");
    }

    const wrapKey = await derivePassphraseAesKey(
      passphrase,
      b64ToBytes(saltB64),
      iterations
    );

    const cekBytes = await aesGcmDecryptWithOptionalAad(
      wrapKey,
      b64ToBytes(ivB64),
      b64ToBytes(wrappedCekB64),
      aadBytes
    );

    const payload = pkg.payload || {};
    const payloadIvB64 = pickPath(payload, ["iv_b64", "nonce_b64"]);
    const payloadCipherB64 = pickPath(payload, ["ciphertext_b64", "data_b64"]);

    if (!payloadIvB64 || !payloadCipherB64) {
      throw new Error("Unsupported Vault payload shape");
    }

    const fileKey = await crypto.subtle.importKey(
      "raw",
      cekBytes,
      { name: "AES-GCM" },
      false,
      ["decrypt"]
    );

    const fileBytes = await aesGcmDecryptWithOptionalAad(
      fileKey,
      b64ToBytes(payloadIvB64),
      b64ToBytes(payloadCipherB64),
      aadBytes
    );

    const original = pkg.original && typeof pkg.original === "object" ? pkg.original : {};
    const filename = text(
      original.name ||
      original.filename ||
      original.path ||
      "vault-file.bin"
    ).split(/[\\/]/).pop() || "vault-file.bin";

    const mime = text(original.mime || original.mime_type || "application/octet-stream") || "application/octet-stream";

    return {
      filename,
      blob: new Blob([fileBytes], { type: mime })
    };
  }

  function downloadBlob(blob, filename) {
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");

    a.href = url;
    a.download = filename || "vault-file.bin";
    a.rel = "noopener";
    document.body.appendChild(a);
    a.click();

    window.setTimeout(() => {
      URL.revokeObjectURL(url);
      a.remove();
    }, 1000);
  }

  function formatBytes(size) {
    const n = Number(size);
    if (!Number.isFinite(n) || n < 0) return "—";
    if (n < 1024) return `${n} B`;

    const units = ["KiB", "MiB", "GiB", "TiB"];
    let value = n / 1024;

    for (const unit of units) {
      if (value < 1024) return `${value.toFixed(value >= 10 ? 0 : 1)} ${unit}`;
      value /= 1024;
    }

    return `${value.toFixed(1)} PiB`;
  }

  function formatTime(value) {
    if (!value) return "—";

    const n = Number(value);
    const date = Number.isFinite(n)
      ? new Date(n > 1000000000000 ? n : n * 1000)
      : new Date(value);

    if (Number.isNaN(date.getTime())) return "—";
    return date.toLocaleString();
  }

  function extractEntries(payload) {
    if (Array.isArray(payload)) return payload;
    if (!payload || typeof payload !== "object") return [];

    for (const key of ["items", "entries", "files", "children"]) {
      if (Array.isArray(payload[key])) return payload[key];
    }

    if (payload.data && typeof payload.data === "object") {
      return extractEntries(payload.data);
    }

    return [];
  }

  function normalizeEntry(item, basePath) {
    const rawName = text(item.name || item.basename || fileNameFromPath(item.path) || "unnamed");
    const type = text(item.type || item.kind || "");
    const isDir = Boolean(
      item.is_dir ||
      item.isdir ||
      item.directory ||
      type === "dir" ||
      type === "directory"
    );

    const rawPath = text(item.path || item.full_path || item.relpath || "");
    const serverPath = rawPath
      ? displayPathFromApiPath(rawPath)
      : normalizeDisplayPath(`${basePath}/${rawName}`);

    const size = item.size_bytes ?? item.size ?? item.bytes ?? item.length ?? null;
    const modified = item.mtime_unix ?? item.modified ?? item.mtime ?? item.updated_at ?? item.updated_at_epoch ?? null;
    const mime = text(item.mime || item.mime_type || item.content_type || "");
    const isVaultPackage = rawName.endsWith(".dnavault.json") || mime.includes("dna-nexus.vault");

    const name = isVaultPackage ? friendlyVaultName(rawName) : rawName;
    const displayPath = isVaultPackage
      ? joinDisplayPath(dirNameFromDisplayPath(serverPath), name)
      : serverPath;

    return {
      name,
      path: displayPath,
      serverPath,
      isDir,
      size,
      modified,
      mime,
      raw: item,
      isVaultPackage
    };
  }

  function makeButton(label, className = "vaultFmBtn") {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = className;
    btn.textContent = label;
    return btn;
  }

  function triggerVaultRedAlert() {
    function findVaultFrame(doc) {
      return Array.from(doc.querySelectorAll("iframe"))
        .find((frame) => String(frame.src || "").includes("/apps/vault/"));
    }

    function targetDocumentAndRect() {
      try {
        if (window.parent && window.parent !== window && window.parent.document) {
          const frame = window.frameElement || findVaultFrame(window.parent.document);
          if (frame) {
            return {
              doc: window.parent.document,
              rect: frame.getBoundingClientRect()
            };
          }
        }
      } catch {
        // Cross-frame access may be blocked. Fall back to the Vault document.
      }

      const shell = document.querySelector(".vaultFmShell");
      return {
        doc: document,
        rect: shell
          ? shell.getBoundingClientRect()
          : {
              left: 0,
              top: 0,
              right: document.documentElement.clientWidth || window.innerWidth,
              bottom: document.documentElement.clientHeight || window.innerHeight
            }
      };
    }

    function addPanel(doc, layer, left, top, width, height) {
      if (width <= 1 || height <= 1) return;

      const panel = doc.createElement("div");
      panel.className = "pqnasVaultRedAlertPanel";
      panel.style.position = "fixed";
      panel.style.left = `${Math.max(0, left)}px`;
      panel.style.top = `${Math.max(0, top)}px`;
      panel.style.width = `${Math.max(0, width)}px`;
      panel.style.height = `${Math.max(0, height)}px`;
      panel.style.pointerEvents = "none";

      // UX-only secure-mode signal. This does not change auth, crypto state,
      // server-side access checks, file visibility, or encryption behavior.
      panel.style.background = "var(--danger, red)";
      panel.style.opacity = "0.35";

      layer.appendChild(panel);
    }

    const { doc, rect } = targetDocumentAndRect();
    if (!doc || !doc.body || !doc.documentElement || !rect) return;

    for (const old of doc.querySelectorAll(".pqnasVaultRedAlertLayer")) {
      old.remove();
    }

    const vw = doc.documentElement.clientWidth || window.innerWidth;
    const vh = doc.documentElement.clientHeight || window.innerHeight;

    const left = Math.max(0, Math.min(vw, rect.left));
    const top = Math.max(0, Math.min(vh, rect.top));
    const right = Math.max(0, Math.min(vw, rect.right));
    const bottom = Math.max(0, Math.min(vh, rect.bottom));

    const layer = doc.createElement("div");
    layer.className = "pqnasVaultRedAlertLayer";
    layer.style.position = "fixed";
    layer.style.inset = "0";
    layer.style.zIndex = "2147483000";
    layer.style.pointerEvents = "none";

    addPanel(doc, layer, 0, 0, vw, top);
    addPanel(doc, layer, 0, top, left, Math.max(0, bottom - top));
    addPanel(doc, layer, right, top, Math.max(0, vw - right), Math.max(0, bottom - top));
    addPanel(doc, layer, 0, bottom, vw, Math.max(0, vh - bottom));

    doc.body.appendChild(layer);

    window.setTimeout(() => {
      layer.remove();
    }, 1700);
  }

  function makeDialog(id, titleText, kind) {
    const dialog = document.createElement("dialog");
    dialog.id = id;
    dialog.className = `vaultDetachedDialog vaultDetachedDialog_${kind}`;
    dialog.innerHTML = `
      <div class="vaultDetachedChrome">
        <div class="vaultDetachedTitle">${titleText}</div>
        <button type="button" class="vaultDetachedClose">Close</button>
      </div>
      <div class="vaultDetachedContent"></div>
    `;

    const closeBtn = dialog.querySelector(".vaultDetachedClose");
    closeBtn.addEventListener("click", () => dialog.close());

    dialog.addEventListener("click", (ev) => {
      if (ev.target === dialog) dialog.close();
    });

    return dialog;
  }

  function openDialog(dialog, focusSelector = "") {
    if (!dialog) return;

    if (typeof dialog.showModal === "function") {
      dialog.showModal();
    } else {
      dialog.setAttribute("open", "");
    }

    window.requestAnimationFrame(() => {
      const target = focusSelector ? dialog.querySelector(focusSelector) : null;
      if (target && typeof target.focus === "function") {
        target.focus({ preventScroll: true });
      }
    });
  }

  function makeVaultDownloadDialog() {
    const dialog = makeDialog("vaultDownloadDialog", "Download", "download");

    const content = dialog.querySelector(".vaultDetachedContent");
    content.innerHTML = "";

    const card = document.createElement("section");
    card.className = "vaultDownloadPanel pq-card";
    card.innerHTML = `
      <div class="vault-panel-head">
        <div>
          <h2>Download from Vault</h2>
          <p>The encrypted package is fetched from the server, decrypted in your browser, and saved as the original file.</p>
        </div>
      </div>

      <div class="vaultDownloadFile">
        <span>File</span>
        <strong class="vaultDownloadFileName">No file selected</strong>
      </div>

      <label class="vault-field">
        <span>Vault passphrase</span>
        <input class="pq-input vaultDownloadPassphrase" type="password" autocomplete="current-password">
      </label>

      <div class="vault-actions">
        <button class="pq-btn primary vaultDownloadBtn" type="button">Download</button>
      </div>

      <div class="vault-status vaultDownloadStatus" aria-live="polite">Ready.</div>
    `;

    content.appendChild(card);

    const nameEl = card.querySelector(".vaultDownloadFileName");
    const passEl = card.querySelector(".vaultDownloadPassphrase");
    const btn = card.querySelector(".vaultDownloadBtn");
    const statusEl = card.querySelector(".vaultDownloadStatus");

    let currentEntry = null;

    async function runDownload() {
      if (!currentEntry) {
        statusEl.textContent = "No file selected.";
        return;
      }

      const passphrase = String(passEl.value || "");
      if (!passphrase) {
        statusEl.textContent = "Enter Vault passphrase.";
        passEl.focus();
        return;
      }

      const serverPath = currentEntry.serverPath || currentEntry.path;
      const apiPath = apiPathFromDisplayPath(serverPath);

      btn.disabled = true;
      statusEl.textContent = "Downloading encrypted package...";

      try {
        const res = await fetch(`/api/v4/files/get?path=${encodeURIComponent(apiPath)}`, {
          method: "GET",
          credentials: "include",
          cache: "no-store",
          headers: { "Accept": "application/json" }
        });

        const raw = await res.text();

        if (!res.ok) {
          throw new Error(raw || `HTTP ${res.status}`);
        }

        statusEl.textContent = "Decrypting locally...";
        const pkg = JSON.parse(raw);
        const out = await decryptVaultPackageForDownload(pkg, passphrase);

        downloadBlob(out.blob, out.filename || currentEntry.name);
        statusEl.textContent = `Downloaded ${out.filename || currentEntry.name}.`;
        dialog.close();
      } catch (err) {
        statusEl.textContent = `Download failed: ${err && err.message ? err.message : err}`;
      } finally {
        btn.disabled = false;
      }
    }

    btn.addEventListener("click", () => {
      runDownload().catch((err) => {
        statusEl.textContent = `Download failed: ${err && err.message ? err.message : err}`;
        btn.disabled = false;
      });
    });

    passEl.addEventListener("keydown", (ev) => {
      if (ev.key === "Enter") {
        runDownload().catch((err) => {
          statusEl.textContent = `Download failed: ${err && err.message ? err.message : err}`;
          btn.disabled = false;
        });
      }
    });

    return {
      dialog,
      open(entry) {
        currentEntry = entry;
        nameEl.textContent = entry && entry.name ? entry.name : "No file selected";
        passEl.value = "";
        statusEl.textContent = "Ready.";
        openDialog(dialog, ".vaultDownloadPassphrase");
      }
    };
  }

  function makeVaultDetailsDialog(onDownload) {
    const dialog = makeDialog("vaultDetailsDialog", "Details", "details");

    const content = dialog.querySelector(".vaultDetachedContent");
    content.textContent = "";

    const card = document.createElement("section");
    card.className = "vaultDetailsPanel pq-card";

    const head = document.createElement("div");
    head.className = "vault-panel-head";

    const headText = document.createElement("div");
    const title = document.createElement("h2");
    title.textContent = "File details";

    const intro = document.createElement("p");
    intro.textContent = "Vault shows the friendly file name. The stored package path remains encrypted-package specific.";

    headText.append(title, intro);
    head.appendChild(headText);

    const grid = document.createElement("dl");
    grid.className = "vaultDetailsGrid";

    const actions = document.createElement("div");
    actions.className = "vaultDetailsActions";

    const downloadBtn = document.createElement("button");
    downloadBtn.type = "button";
    downloadBtn.className = "pq-btn primary";
    downloadBtn.textContent = "Download";

    const copyBtn = document.createElement("button");
    copyBtn.type = "button";
    copyBtn.className = "pq-btn secondary";
    copyBtn.textContent = "Copy Vault path";

    const status = document.createElement("div");
    status.className = "vault-status";
    status.setAttribute("aria-live", "polite");
    status.textContent = "Ready.";

    actions.append(downloadBtn, copyBtn);
    card.append(head, grid, actions, status);
    content.appendChild(card);

    let currentEntry = null;

    function addRow(label, value, isMono = false) {
      const dt = document.createElement("dt");
      dt.textContent = label;

      const dd = document.createElement("dd");
      dd.textContent = value || "—";
      if (isMono) dd.className = "vaultDetailsMono";

      grid.append(dt, dd);
    }

    function render(entry) {
      grid.textContent = "";

      const isFolder = Boolean(entry && entry.isDir);
      const storedPath = entry && entry.serverPath ? entry.serverPath : entry && entry.path ? entry.path : "";
      const packageName = storedPath ? fileNameFromPath(storedPath) : "";

      addRow("Name", entry && entry.name ? entry.name : "—");
      addRow("Type", isFolder ? "Folder" : entry && entry.isVaultPackage ? "Encrypted Vault file" : "File");
      addRow("Size", isFolder ? "—" : formatBytes(entry && entry.size));
      addRow("Modified", formatTime(entry && entry.modified));
      addRow("Vault path", entry && entry.path ? entry.path : "—", true);
      addRow("Stored package", storedPath, true);
      addRow("Package file", packageName);
      addRow("MIME", entry && entry.mime ? entry.mime : "—");

      downloadBtn.hidden = isFolder;
      status.textContent = "Ready.";
    }

    copyBtn.addEventListener("click", async () => {
      if (!currentEntry) return;

      const value = currentEntry.path || "";
      try {
        await navigator.clipboard.writeText(value);
        status.textContent = "Vault path copied.";
      } catch {
        status.textContent = value || "Could not copy path.";
      }
    });

    downloadBtn.addEventListener("click", () => {
      if (!currentEntry || currentEntry.isDir) return;

      dialog.close();
      if (typeof onDownload === "function") {
        onDownload(currentEntry);
      }
    });

    return {
      dialog,
      open(entry) {
        currentEntry = entry;
        render(entry);
        openDialog(dialog, ".vaultDetachedClose");
      }
    };
  }

  function syncVaultFolderInput(path) {
    const input = document.getElementById("vaultFolderInput");
    if (!input) return;
    input.value = apiPathFromDisplayPath(path);
  }

  function buildRecoveryPanel(notePanel, recoveryField) {
    const panel = document.createElement("section");
    panel.className = "vaultRecoveryPanel pq-card";
    panel.innerHTML = `
      <div class="vault-panel-head">
        <div>
          <h2>Keys & recovery</h2>
          <p>Configure the optional organization recovery recipient for future encrypted uploads.</p>
        </div>
      </div>
      <div class="vaultRecoveryBody"></div>
    `;

    const body = panel.querySelector(".vaultRecoveryBody");

    if (recoveryField) {
      const info = document.createElement("p");
      info.className = "vaultDetachedHelp";
      info.textContent = "When this field is set, each new Vault package also wraps the file key for the organization recovery public key.";
      body.append(info, recoveryField);
    }

    if (notePanel) {
      body.appendChild(notePanel);
    }

    return panel;
  }

  function mountDetachedModals() {
    const originalRoot = document.querySelector(".vault-shell");
    const uploadPanel = document.querySelector('[aria-labelledby="encryptTitle"]');
    const decryptPanel = document.querySelector('[aria-labelledby="decryptTitle"]');
    const notePanel = document.querySelector(".vault-note");
    const recoveryField = document.getElementById("recoveryPublicKeyInput")?.closest(".vault-field") || null;

    if (!originalRoot || !uploadPanel || !decryptPanel) {
      throw new Error("Vault source panels missing");
    }

    const uploadDialog = makeDialog("vaultUploadDialog", "Encrypted upload", "upload");
    const decryptDialog = makeDialog("vaultDecryptDialog", "Advanced import", "decrypt");
    const recoveryDialog = makeDialog("vaultRecoveryDialog", "Keys & recovery", "recovery");

    const decryptTitle = decryptPanel.querySelector("#decryptTitle");
    if (decryptTitle) decryptTitle.textContent = "Advanced import";

    const decryptIntro = decryptPanel.querySelector(".vault-panel-head p");
    if (decryptIntro) {
      decryptIntro.textContent = "Open an exported .dnavault.json package and decrypt it locally with your passphrase.";
    }

    const decryptAction = decryptPanel.querySelector("#decryptBtn");
    if (decryptAction) decryptAction.textContent = "Decrypt package";

    uploadDialog.querySelector(".vaultDetachedContent").appendChild(uploadPanel);
    decryptDialog.querySelector(".vaultDetachedContent").appendChild(decryptPanel);
    recoveryDialog.querySelector(".vaultDetachedContent").appendChild(buildRecoveryPanel(notePanel, recoveryField));

    originalRoot.hidden = true;
    originalRoot.setAttribute("aria-hidden", "true");

    document.body.append(uploadDialog, decryptDialog, recoveryDialog);

    return { uploadDialog, decryptDialog, recoveryDialog };
  }

  function mountLayout() {
    if (document.body.dataset.vaultFmReady === "1") return;
    document.body.dataset.vaultFmReady = "1";

    const dialogs = mountDetachedModals();
    const downloadDialog = makeVaultDownloadDialog();
    const detailsDialog = makeVaultDetailsDialog((entry) => downloadDialog.open(entry));
    document.body.append(downloadDialog.dialog, detailsDialog.dialog);

    const shell = document.createElement("div");
    shell.className = "vaultFmShell";

    const topbar = document.createElement("header");
    topbar.className = "vaultFmTopbar";

    const brand = document.createElement("div");
    brand.className = "vaultFmBrand";

    const brandIcon = document.createElement("div");
    brandIcon.className = "vaultFmBrandIcon";
    brandIcon.setAttribute("aria-hidden", "true");
    brandIcon.textContent = "🔒";

    const brandText = document.createElement("div");
    brandText.textContent = "Vault";

    brand.append(brandIcon, brandText);

    const pathBar = document.createElement("div");
    pathBar.className = "vaultFmPathBar";

    const upBtn = makeButton("Up");
    const pathInput = document.createElement("input");
    pathInput.className = "vaultFmPathInput";
    pathInput.type = "text";
    pathInput.spellcheck = false;
    pathInput.autocomplete = "off";
    pathInput.value = inputPathFromDisplayPath(localStorage.getItem(STORAGE_KEY) || DEFAULT_PATH);
    pathInput.setAttribute("aria-label", "Vault path");

    pathBar.append(upBtn, pathInput);

    const actions = document.createElement("div");
    actions.className = "vaultFmActions";

    const refreshBtn = makeButton("Refresh");
    const uploadBtn = makeButton("Encrypted upload", "vaultFmBtn vaultFmPrimary");
    const decryptBtn = makeButton("Advanced import");
    const keysBtn = makeButton("Keys & recovery");

    actions.append(refreshBtn, uploadBtn, decryptBtn, keysBtn);
    topbar.append(brand, pathBar, actions);

    const main = document.createElement("main");
    main.className = "vaultFmMain";

    const fileArea = document.createElement("section");
    fileArea.className = "vaultFmFileArea";
    fileArea.setAttribute("aria-label", "Vault files");

    const header = document.createElement("div");
    header.className = "vaultFmListHeader";

    for (const label of ["Name", "Size", "Modified", "Type"]) {
      const cell = document.createElement("div");
      cell.textContent = label;
      if (label !== "Name" && label !== "Size") cell.className = "vaultFmCellOptional";
      header.appendChild(cell);
    }

    const list = document.createElement("div");
    list.className = "vaultFmList";
    list.tabIndex = 0;
    fileArea.append(header, list);

    const side = document.createElement("aside");
    side.className = "vaultFmSide";
    side.innerHTML = `
      <h2>Encrypted space</h2>
      <p>Files in this view are encrypted locally before upload. Server-side previews and media playback stay disabled for Vault packages.</p>
      <span class="vaultFmPill">AES-256-GCM package</span>
      <p></p>
      <span class="vaultFmPill">Optional ML-KEM recovery wrap</span>
    `;

    main.append(fileArea, side);

    const status = document.createElement("footer");
    status.className = "vaultFmStatus";
    status.textContent = "Ready";

    shell.append(topbar, main, status);
    document.body.prepend(shell);

    window.requestAnimationFrame(() => {
      triggerVaultRedAlert();
    });

    const menu = document.createElement("div");
    menu.className = "vaultContextMenu";
    menu.hidden = true;
    document.body.appendChild(menu);

    function renderEmpty(message) {
      list.textContent = "";
      const empty = document.createElement("div");
      empty.className = "vaultFmEmpty";
      empty.textContent = message;
      list.appendChild(empty);
    }

    function renderRows(entries) {
      list.textContent = "";

      if (!entries.length) {
        renderEmpty("This Vault folder is empty.");
        return;
      }

      for (const entry of entries) {
        const row = document.createElement("button");
        row.type = "button";
        row.className = "vaultFmRow";
        row.dataset.path = entry.path;
        row.dataset.kind = entry.isDir ? "dir" : "file";

        const nameCell = document.createElement("div");
        nameCell.className = "vaultFmName";

        const icon = document.createElement("span");
        icon.className = "vaultFmIcon";
        icon.textContent = entry.isDir ? "📁" : "🔒";

        const nameText = document.createElement("span");
        nameText.className = "vaultFmNameText";
        nameText.textContent = entry.name;

        nameCell.append(icon, nameText);

        const sizeCell = document.createElement("div");
        sizeCell.className = "vaultFmCellMuted";
        sizeCell.textContent = entry.isDir ? "—" : formatBytes(entry.size);

        const modifiedCell = document.createElement("div");
        modifiedCell.className = "vaultFmCellMuted vaultFmCellOptional";
        modifiedCell.textContent = formatTime(entry.modified);

        const typeCell = document.createElement("div");
        typeCell.className = "vaultFmCellMuted vaultFmCellOptional";
        typeCell.textContent = entry.isDir ? "Folder" : (entry.isVaultPackage ? "Vault package" : "Encrypted file");

        row.append(nameCell, sizeCell, modifiedCell, typeCell);

        row.addEventListener("dblclick", () => {
          if (entry.isDir) {
            pathInput.value = entry.path;
            refresh().catch(() => {});
          } else {
            downloadDialog.open(entry);
          }
        });

        row.addEventListener("contextmenu", (ev) => {
          ev.preventDefault();
          showContextMenu(ev.clientX, ev.clientY, entry);
        });

        list.appendChild(row);
      }
    }

    async function createVaultFolder() {
      const path = normalizeDisplayPath(pathInput.value);
      const apiPath = apiPathFromDisplayPath(path);

      if (!apiPath) {
        status.textContent = "Cannot create root as a Vault folder.";
        return;
      }

      status.textContent = `Creating ${path}...`;

      try {
        const res = await fetch(`/api/v4/files/mkdir?path=${encodeURIComponent(apiPath)}`, {
          method: "POST",
          credentials: "include",
          cache: "no-store",
          headers: { "Accept": "application/json" }
        });

        const body = await res.text().catch(() => "");

        if (!res.ok && res.status !== 409) {
          throw new Error(body || `HTTP ${res.status}`);
        }

        status.textContent = `${path} is ready.`;
        await refresh();
      } catch (err) {
        status.textContent = `Create folder failed: ${err && err.message ? err.message : err}`;
      }
    }

    function renderMissingFolder(path) {
      list.textContent = "";

      const empty = document.createElement("div");
      empty.className = "vaultFmEmpty";

      const msg = document.createElement("div");
      msg.textContent = `${path} does not exist yet.`;

      const btn = makeButton("Create Vault folder");
      btn.classList.add("vaultFmEmptyAction");
      btn.addEventListener("click", createVaultFolder);

      empty.append(msg, btn);
      list.appendChild(empty);
    }

    async function refresh() {
      const path = normalizeDisplayPath(pathInput.value);
      pathInput.value = inputPathFromDisplayPath(path);
      localStorage.setItem(STORAGE_KEY, path);
      syncVaultFolderInput(path);

      status.textContent = "Loading...";
      list.textContent = "";

      try {
        const apiPath = apiPathFromDisplayPath(path);
        const url = apiPath
          ? `/api/v4/files/list?path=${encodeURIComponent(apiPath)}`
          : "/api/v4/files/list";

        const res = await fetch(url, {
          credentials: "include",
          cache: "no-store",
          headers: { "Accept": "application/json" }
        });

        if (!res.ok) {
          const body = await res.text().catch(() => "");
          const err = new Error(body || `list failed: HTTP ${res.status}`);
          err.status = res.status;
          throw err;
        }

        const payload = await res.json();
        const entries = extractEntries(payload)
          .map((item) => normalizeEntry(item, path))
          .sort((a, b) => {
            if (a.isDir !== b.isDir) return a.isDir ? -1 : 1;
            return a.name.localeCompare(b.name);
          });

        renderRows(entries);
        status.textContent = `${entries.length} item${entries.length === 1 ? "" : "s"} in ${path}`;
      } catch (err) {
        if (err && err.status === 404) {
          renderMissingFolder(path);
          status.textContent = `Vault folder missing: ${path}`;
          return;
        }

        renderEmpty(`Could not list ${path}.`);
        status.textContent = err && err.message ? err.message : "Vault list failed";
      }
    }

    function showContextMenu(x, y, entry) {
      menu.textContent = "";
      menu.hidden = false;

      const open = document.createElement("button");
      open.type = "button";
      open.textContent = entry.isDir ? "Open folder" : "Download";
      open.addEventListener("click", () => {
        hideContextMenu();

        if (entry.isDir) {
          pathInput.value = entry.path;
          refresh().catch(() => {});
        } else {
          downloadDialog.open(entry);
        }
      });

      const details = document.createElement("button");
      details.type = "button";
      details.textContent = "Details";
      details.addEventListener("click", () => {
        hideContextMenu();
        detailsDialog.open(entry);
      });

      const copyPath = document.createElement("button");
      copyPath.type = "button";
      copyPath.textContent = "Copy path";
      copyPath.addEventListener("click", async () => {
        hideContextMenu();

        try {
          await navigator.clipboard.writeText(entry.path);
          status.textContent = "Path copied";
        } catch {
          status.textContent = entry.path;
        }
      });

      menu.append(open, details, copyPath);

      if (!entry.isDir) {
        const downloadEncrypted = document.createElement("button");
        downloadEncrypted.type = "button";
        downloadEncrypted.textContent = "Download encrypted package";
        downloadEncrypted.addEventListener("click", () => {
          hideContextMenu();
          // Security: download the encrypted package as stored; do not preview or
          // decrypt on the server side.
          window.location.href = `/api/v4/files/get?path=${encodeURIComponent(apiPathFromDisplayPath(entry.serverPath || entry.path))}`;
        });
        menu.appendChild(downloadEncrypted);
      }

      const rect = menu.getBoundingClientRect();
      const left = Math.min(x, Math.max(8, window.innerWidth - rect.width - 8));
      const top = Math.min(y, Math.max(8, window.innerHeight - rect.height - 8));
      menu.style.left = `${left}px`;
      menu.style.top = `${top}px`;
    }

    function showFolderContextMenu(x, y) {
      const currentPath = normalizeDisplayPath(pathInput.value);
      const visiblePath = inputPathFromDisplayPath(currentPath);

      menu.textContent = "";
      menu.hidden = false;

      function addMenuButton(label, handler) {
        const btn = document.createElement("button");
        btn.type = "button";
        btn.textContent = label;
        btn.addEventListener("click", () => {
          hideContextMenu();
          handler();
        });
        menu.appendChild(btn);
      }

      addMenuButton("Upload encrypted file", () => {
        syncVaultFolderInput(pathInput.value);
        openDialog(dialogs.uploadDialog, "#fileInput");
      });

      addMenuButton("Refresh", () => {
        refresh().catch(() => {});
      });

      addMenuButton("Advanced import", () => {
        openDialog(dialogs.decryptDialog, "#decryptFileInput");
      });

      addMenuButton("Folder details", () => {
        detailsDialog.open({
          name: visiblePath,
          path: currentPath,
          serverPath: currentPath,
          isDir: true,
          size: null,
          modified: null,
          mime: ""
        });
      });

      addMenuButton("Copy folder path", async () => {
        try {
          await navigator.clipboard.writeText(visiblePath);
          status.textContent = "Folder path copied.";
        } catch {
          status.textContent = visiblePath;
        }
      });

      const rect = menu.getBoundingClientRect();
      const left = Math.min(x, Math.max(8, window.innerWidth - rect.width - 8));
      const top = Math.min(y, Math.max(8, window.innerHeight - rect.height - 8));
      menu.style.left = `${left}px`;
      menu.style.top = `${top}px`;
    }

    function hideContextMenu() {
      menu.hidden = true;
    }

    uploadBtn.addEventListener("click", () => {
      syncVaultFolderInput(pathInput.value);
      openDialog(dialogs.uploadDialog, "#fileInput");
    });

    const encryptUploadActionBtn = document.getElementById("encryptUploadBtn");
    if (encryptUploadActionBtn) {
      encryptUploadActionBtn.addEventListener("click", () => {
        // Upload happens in app.js. Refresh the Vault browser after the browser
        // has had time to encrypt and PUT the package.
        window.setTimeout(() => refresh().catch(() => {}), 1800);
        window.setTimeout(() => refresh().catch(() => {}), 6000);
      });
    }

    decryptBtn.addEventListener("click", () => {
      openDialog(dialogs.decryptDialog, "#decryptFileInput");
    });

    keysBtn.addEventListener("click", () => {
      openDialog(dialogs.recoveryDialog, "#recoveryPublicKeyInput");
    });

    refreshBtn.addEventListener("click", () => refresh().catch(() => {}));

    upBtn.addEventListener("click", () => {
      pathInput.value = parentPath(pathInput.value);
      refresh().catch(() => {});
    });

    pathInput.addEventListener("keydown", (ev) => {
      if (ev.key === "Enter") refresh().catch(() => {});
    });

    fileArea.addEventListener("contextmenu", (ev) => {
      if (ev.target.closest(".vaultFmRow")) return;

      ev.preventDefault();
      ev.stopPropagation();

      showFolderContextMenu(ev.clientX, ev.clientY);
    });

    document.addEventListener("click", hideContextMenu);
    document.addEventListener("keydown", (ev) => {
      if (ev.key === "Escape") hideContextMenu();
    });

    refresh().catch(() => {});
  }

  window.pqnasVaultRedAlertTest = triggerVaultRedAlert;
  onReady(mountLayout);
})();
