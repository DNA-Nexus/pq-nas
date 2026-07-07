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

  function zeroBytes(bytes) {
    // Security: reduce lifetime of browser-owned CEK/plaintext buffers.
    // Passphrases are JS strings and cannot be reliably wiped, but Uint8Array
    // buffers should be cleared once Blob/CryptoKey creation no longer needs them.
    if (bytes && typeof bytes.fill === "function") {
      try {
        bytes.fill(0);
      } catch {
        // Best-effort memory hygiene only.
      }
    }
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

    zeroBytes(cekBytes);

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

    const blob = new Blob([fileBytes], { type: mime });
    zeroBytes(fileBytes);

    return {
      filename,
      blob
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
          <p>Configure the optional Master recovery recipient for future encrypted uploads.</p>
        </div>
      </div>
      <div class="vaultRecoveryBody"></div>
    `;

    const body = panel.querySelector(".vaultRecoveryBody");

    if (recoveryField) {
      const info = document.createElement("p");
      info.className = "vaultDetachedHelp";
      info.textContent = "When this field is set, each new Vault package also wraps the file key for the Master recovery public key.";
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

    const VIEW_MODE_STORAGE_KEY = "pqnas.vault.viewMode";
    const SORT_KEY_STORAGE_KEY = "pqnas.vault.sortKey";
    const SORT_DIR_STORAGE_KEY = "pqnas.vault.sortDir";
    const FOLDERS_FIRST_STORAGE_KEY = "pqnas.vault.foldersFirst";
    const SORT_KEYS = ["name", "size", "modified", "type"];
    const SORT_LABELS = {
      name: "Name",
      size: "Size",
      modified: "Modified",
      type: "Type"
    };

    let viewMode = "list";
    let sortKey = "name";
    let sortDir = "asc";
    let foldersFirst = true;

    try {
      viewMode = localStorage.getItem(VIEW_MODE_STORAGE_KEY) === "grid" ? "grid" : "list";

      const storedSortKey = localStorage.getItem(SORT_KEY_STORAGE_KEY);
      const storedSortDir = localStorage.getItem(SORT_DIR_STORAGE_KEY);
      const storedFoldersFirst = localStorage.getItem(FOLDERS_FIRST_STORAGE_KEY);

      sortKey = SORT_KEYS.includes(storedSortKey) ? storedSortKey : "name";
      sortDir = storedSortDir === "desc" ? "desc" : "asc";
      foldersFirst = storedFoldersFirst === null ? true : storedFoldersFirst !== "false";
    } catch (_) {
      viewMode = "list";
      sortKey = "name";
      sortDir = "asc";
      foldersFirst = true;
    }

    const sortCollator = new Intl.Collator(undefined, {
      numeric: true,
      sensitivity: "base"
    });

    const refreshBtn = makeButton("Refresh");
    const viewToggleBtn = makeButton("");
    const sortKeyBtn = makeButton("");
    const sortDirBtn = makeButton("");
    const foldersFirstBtn = makeButton("");
    const trashBtn = makeButton("Trash");
    const uploadBtn = makeButton("Encrypted upload", "vaultFmBtn vaultFmPrimary");
    const decryptBtn = makeButton("Advanced import");
    actions.append(refreshBtn, viewToggleBtn, sortKeyBtn, sortDirBtn, foldersFirstBtn, trashBtn, uploadBtn, decryptBtn);
    topbar.append(brand, pathBar, actions);

    const main = document.createElement("main");
    main.className = "vaultFmMain";

    const fileArea = document.createElement("section");
    fileArea.className = "vaultFmFileArea";
    fileArea.setAttribute("aria-label", "Vault files");

    const header = document.createElement("div");
    header.className = "vaultFmListHeader";

    const headerSortMap = {
      Name: "name",
      Size: "size",
      Modified: "modified",
      Type: "type"
    };

    for (const label of ["Name", "Size", "Modified", "Type"]) {
      const cell = document.createElement("div");
      const key = headerSortMap[label];

      cell.textContent = label;
      cell.dataset.sortKey = key;
      cell.title = `Sort by ${label.toLowerCase()}`;
      cell.setAttribute("role", "button");
      cell.tabIndex = 0;

      if (label !== "Name" && label !== "Size") cell.className = "vaultFmCellOptional";

      cell.addEventListener("click", () => {
        setSortKey(key);
      });

      cell.addEventListener("keydown", (ev) => {
        if (ev.key === "Enter" || ev.key === " ") {
          ev.preventDefault();
          setSortKey(key);
        }
      });

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
      <span class="vaultFmPill">Master recovery is per-user</span>
    `;

    main.append(fileArea, side);

    const status = document.createElement("footer");
    status.className = "vaultFmStatus";
    status.textContent = "Ready";

    shell.append(topbar, main, status);
    document.body.prepend(shell);

    function applyVaultViewMode() {
      const isGrid = viewMode === "grid";
      shell.classList.toggle("vaultFmGridView", isGrid);
      viewToggleBtn.textContent = isGrid ? "List view" : "Grid view";
      viewToggleBtn.setAttribute("aria-pressed", isGrid ? "true" : "false");
      viewToggleBtn.title = isGrid ? "Switch to list view" : "Switch to grid view";
    }

    function persistVaultSortSettings() {
      try {
        localStorage.setItem(SORT_KEY_STORAGE_KEY, sortKey);
        localStorage.setItem(SORT_DIR_STORAGE_KEY, sortDir);
        localStorage.setItem(FOLDERS_FIRST_STORAGE_KEY, foldersFirst ? "true" : "false");
      } catch (_) {
        // Non-fatal: sort settings still apply for the current session.
      }
    }

    function sortTypeLabel(entry) {
      if (entry?.isDir) return "Folder";
      return entry?.isVaultPackage ? "Vault package" : "Encrypted file";
    }

    function sortNumber(value) {
      const num = Number(value);
      if (Number.isFinite(num)) return num;

      const parsed = Date.parse(String(value || ""));
      return Number.isFinite(parsed) ? parsed : 0;
    }

    function compareVaultEntryValues(a, b) {
      if (sortKey === "size") {
        return sortNumber(a?.size) - sortNumber(b?.size);
      }

      if (sortKey === "modified") {
        return sortNumber(a?.modified) - sortNumber(b?.modified);
      }

      if (sortKey === "type") {
        return sortCollator.compare(sortTypeLabel(a), sortTypeLabel(b));
      }

      return sortCollator.compare(String(a?.name || ""), String(b?.name || ""));
    }

    function compareVaultEntries(a, b) {
      if (foldersFirst && !!a?.isDir !== !!b?.isDir) {
        return a?.isDir ? -1 : 1;
      }

      let result = compareVaultEntryValues(a, b);

      if (result === 0 && sortKey !== "name") {
        result = sortCollator.compare(String(a?.name || ""), String(b?.name || ""));
      }

      return sortDir === "desc" ? -result : result;
    }

    function sortVaultEntries(entries) {
      if (Array.isArray(entries)) entries.sort(compareVaultEntries);
      return entries;
    }

    function updateVaultSortControls() {
      const label = SORT_LABELS[sortKey] || "Name";
      const arrow = sortDir === "desc" ? "↓" : "↑";

      sortKeyBtn.textContent = `Sort: ${label}`;
      sortKeyBtn.title = "Cycle sort field";

      sortDirBtn.textContent = arrow;
      sortDirBtn.title = sortDir === "desc" ? "Sort descending" : "Sort ascending";
      sortDirBtn.setAttribute("aria-label", sortDir === "desc" ? "Sort descending" : "Sort ascending");

      foldersFirstBtn.textContent = foldersFirst ? "Folders first" : "Mixed folders";
      foldersFirstBtn.setAttribute("aria-pressed", foldersFirst ? "true" : "false");
      foldersFirstBtn.title = foldersFirst ? "Folders are grouped first" : "Folders are sorted with files";

      for (const cell of header.querySelectorAll("[data-sort-key]")) {
        const key = cell.dataset.sortKey || "";
        const cellLabel = SORT_LABELS[key] || cell.textContent || "";

        cell.classList.toggle("vaultFmSortActive", key === sortKey);
        cell.textContent = key === sortKey ? `${cellLabel} ${arrow}` : cellLabel;
        cell.setAttribute("aria-sort", key === sortKey ? (sortDir === "desc" ? "descending" : "ascending") : "none");
      }
    }

    function rerenderSortedVaultRows() {
      if (visibleEntries.length) {
        sortVaultEntries(visibleEntries);
        renderRows(visibleEntries);
      }

      updateVaultSortControls();
    }

    function setSortKey(nextKey) {
      if (!SORT_KEYS.includes(nextKey)) return;

      if (sortKey === nextKey) {
        sortDir = sortDir === "asc" ? "desc" : "asc";
      } else {
        sortKey = nextKey;
        sortDir = nextKey === "modified" || nextKey === "size" ? "desc" : "asc";
      }

      persistVaultSortSettings();
      rerenderSortedVaultRows();
    }

    function cycleSortKey() {
      const current = SORT_KEYS.indexOf(sortKey);
      const next = SORT_KEYS[(current + 1 + SORT_KEYS.length) % SORT_KEYS.length];
      setSortKey(next);
    }

    function toggleSortDirection() {
      sortDir = sortDir === "asc" ? "desc" : "asc";
      persistVaultSortSettings();
      rerenderSortedVaultRows();
    }

    function toggleFoldersFirst() {
      foldersFirst = !foldersFirst;
      persistVaultSortSettings();
      rerenderSortedVaultRows();
    }

    applyVaultViewMode();
    updateVaultSortControls();

    window.requestAnimationFrame(() => {
      triggerVaultRedAlert();
    });

    const menu = document.createElement("div");
    menu.className = "vaultContextMenu";
    menu.hidden = true;
    document.body.appendChild(menu);

    const selectedPaths = new Set();
    const entryByPath = new Map();
    let visibleEntries = [];
    let lastSelectedPath = "";
    let marqueeDrag = null;
    let trashMode = false;
    let lastLivePath = pathInput.value || "Vault";

    function ensureVaultSelectionStyles() {
      if (document.getElementById("vaultSelectionStyles")) return;

      const style = document.createElement("style");
      style.id = "vaultSelectionStyles";
      style.textContent = `
        .vaultFmFileArea{
          position:relative;
        }
        .vaultFmRow{
          position:relative;
        }
        .vaultFmRowSelected{
          background:rgba(245,158,11,.16) !important;
          box-shadow:inset 4px 0 0 #f59e0b, inset 0 0 0 1px rgba(245,158,11,.48);
        }
        .vaultFmRowSelected .vaultFmNameText{
          font-weight:950;
        }
        .vaultBulkBar{
          position:absolute;
          left:12px;
          right:12px;
          top:12px;
          z-index:55;
          display:flex;
          align-items:center;
          justify-content:space-between;
          gap:12px;
          padding:10px 12px;
          border:1px solid var(--border, #cbd5e1);
          border-radius:16px;
          background:#fff7ed;
          color:var(--text, #0f172a);
          margin:0;
          box-shadow:0 10px 26px rgba(15,23,42,.10);
        }
        .vaultBulkBar[hidden]{
          display:none !important;
        }
        .vaultBulkInfo{
          font-weight:900;
        }
        .vaultBulkActions{
          display:flex;
          align-items:center;
          gap:8px;
          flex-wrap:wrap;
          justify-content:flex-end;
        }
        .vaultSelectionMarquee{
          position:absolute;
          z-index:40;
          pointer-events:none;
          border:1px solid #f59e0b;
          background:rgba(245,158,11,.18);
          border-radius:10px;
          box-shadow:0 0 0 1px rgba(255,255,255,.35) inset;
        }
        .vaultSelectionMarquee[hidden]{
          display:none !important;
        }
        html[data-theme="dark"] .vaultBulkBar{
          background:#1f1308;
        }
        html[data-theme="dark"] .vaultFmRowSelected{
          background:rgba(245,158,11,.18) !important;
        }
        html[data-theme="cpunk_orange"] .vaultBulkBar{
          background:#231105;
        }
        html[data-theme="win_classic"] .vaultBulkBar{
          background:#fff4d6;
          border-radius:8px;
        }
      `;
      document.head.appendChild(style);
    }

    ensureVaultSelectionStyles();

    const selectionBar = document.createElement("div");
    selectionBar.className = "vaultBulkBar";
    selectionBar.hidden = true;

    const selectionInfo = document.createElement("span");
    selectionInfo.className = "vaultBulkInfo";
    selectionInfo.textContent = "0 selected";

    const selectionActions = document.createElement("div");
    selectionActions.className = "vaultBulkActions";

    const clearSelectionBtn = makeButton("Clear");
    const downloadSelectedBtn = makeButton("Download selected");
    const deleteSelectedBtn = makeButton("Move selected to trash", "vaultFmBtn vaultFmPrimary");

    selectionActions.append(clearSelectionBtn, downloadSelectedBtn, deleteSelectedBtn);
    selectionBar.append(selectionInfo, selectionActions);
    fileArea.insertBefore(selectionBar, header);

    const marqueeBox = document.createElement("div");
    marqueeBox.className = "vaultSelectionMarquee";
    marqueeBox.hidden = true;
    fileArea.appendChild(marqueeBox);

    function getSelectedEntries() {
      return [...selectedPaths]
        .map((path) => entryByPath.get(path))
        .filter(Boolean);
    }

    function getSelectedFileEntries() {
      return getSelectedEntries().filter((entry) => entry && !entry.isDir);
    }

    function updateSelectionBar() {
      const entries = getSelectedEntries();
      const files = entries.filter((entry) => !entry.isDir).length;
      const folders = entries.length - files;

      // Avoid layout flicker while marquee selection is active. Showing/hiding
      // the bulk bar during pointer movement shifts the list under the cursor,
      // which can make the marquee selection oscillate.
      if (marqueeDrag) {
        return;
      }

      selectionBar.hidden = entries.length === 0;

      if (!entries.length) {
        selectionInfo.textContent = "0 selected";
      } else {
        const parts = [];
        if (files) parts.push(`${files} file${files === 1 ? "" : "s"}`);
        if (folders) parts.push(`${folders} folder${folders === 1 ? "" : "s"}`);
        selectionInfo.textContent = `${entries.length} selected${parts.length ? ` (${parts.join(", ")})` : ""}`;
      }

      downloadSelectedBtn.disabled = trashMode || files === 0;
      downloadSelectedBtn.title = trashMode
        ? "Trash items cannot be downloaded from the Vault trash view."
        : "Download selected encrypted packages.";

      deleteSelectedBtn.textContent = trashMode ? "Restore selected" : "Move selected to trash";
      deleteSelectedBtn.disabled = entries.length === 0;
    }

    function applySelectionStateToRow(row, entry) {
      const selected = !!entry && selectedPaths.has(entry.path);
      row.classList.toggle("vaultFmRowSelected", selected);
      row.setAttribute("aria-selected", selected ? "true" : "false");
    }

    function updateRenderedSelection() {
      for (const row of list.querySelectorAll(".vaultFmRow")) {
        const entry = entryByPath.get(row.dataset.path || "");
        applySelectionStateToRow(row, entry);
      }

      updateSelectionBar();
    }

    function clearSelection() {
      selectedPaths.clear();
      lastSelectedPath = "";
      updateRenderedSelection();
    }

    function selectSingleEntry(entry) {
      selectedPaths.clear();
      if (entry?.path) {
        selectedPaths.add(entry.path);
        lastSelectedPath = entry.path;
      }
      updateRenderedSelection();
    }

    function toggleEntrySelection(entry) {
      if (!entry?.path) return;

      if (selectedPaths.has(entry.path)) {
        selectedPaths.delete(entry.path);
      } else {
        selectedPaths.add(entry.path);
      }

      lastSelectedPath = entry.path;
      updateRenderedSelection();
    }

    function selectRangeToEntry(entry) {
      if (!entry?.path) return;

      const from = visibleEntries.findIndex((item) => item.path === lastSelectedPath);
      const to = visibleEntries.findIndex((item) => item.path === entry.path);

      if (from < 0 || to < 0) {
        selectSingleEntry(entry);
        return;
      }

      selectedPaths.clear();

      const start = Math.min(from, to);
      const end = Math.max(from, to);

      for (let i = start; i <= end; i += 1) {
        if (visibleEntries[i]?.path) selectedPaths.add(visibleEntries[i].path);
      }

      updateRenderedSelection();
    }

    function handleRowClick(ev, entry) {
      if (ev.shiftKey) {
        selectRangeToEntry(entry);
        return;
      }

      if (ev.ctrlKey || ev.metaKey) {
        toggleEntrySelection(entry);
        return;
      }

      selectSingleEntry(entry);
    }

    function ensureEntrySelectedForContextMenu(entry) {
      if (!entry?.path) return;

      if (!selectedPaths.has(entry.path)) {
        selectedPaths.clear();
        selectedPaths.add(entry.path);
        lastSelectedPath = entry.path;
        updateRenderedSelection();
      }
    }

    function beginMarqueeSelection(ev) {
      if (ev.button !== 0) return;
      if (ev.target.closest(".vaultFmRow, .vaultBulkBar, .vaultContextMenu, button, input, textarea, select, a")) return;

      hideContextMenu();

      const areaRect = fileArea.getBoundingClientRect();

      marqueeDrag = {
        pointerId: ev.pointerId,
        startX: ev.clientX,
        startY: ev.clientY,
        additive: ev.ctrlKey || ev.metaKey,
        baseSelected: new Set(selectedPaths),
        didDrag: false
      };

      marqueeBox.hidden = false;
      marqueeBox.style.left = `${ev.clientX - areaRect.left}px`;
      marqueeBox.style.top = `${ev.clientY - areaRect.top}px`;
      marqueeBox.style.width = "0px";
      marqueeBox.style.height = "0px";

      try { fileArea.setPointerCapture(ev.pointerId); } catch (_) {}
      ev.preventDefault();
    }

    function updateMarqueeSelection(ev) {
      if (!marqueeDrag || marqueeDrag.pointerId !== ev.pointerId) return;

      const dx = ev.clientX - marqueeDrag.startX;
      const dy = ev.clientY - marqueeDrag.startY;

      if (Math.abs(dx) > 4 || Math.abs(dy) > 4) {
        marqueeDrag.didDrag = true;
      }

      const areaRect = fileArea.getBoundingClientRect();
      const left = Math.min(marqueeDrag.startX, ev.clientX);
      const top = Math.min(marqueeDrag.startY, ev.clientY);
      const right = Math.max(marqueeDrag.startX, ev.clientX);
      const bottom = Math.max(marqueeDrag.startY, ev.clientY);

      marqueeBox.style.left = `${left - areaRect.left}px`;
      marqueeBox.style.top = `${top - areaRect.top}px`;
      marqueeBox.style.width = `${right - left}px`;
      marqueeBox.style.height = `${bottom - top}px`;

      if (!marqueeDrag.didDrag) return;

      selectedPaths.clear();

      if (marqueeDrag.additive) {
        for (const path of marqueeDrag.baseSelected) selectedPaths.add(path);
      }

      const selectionRect = { left, top, right, bottom };

      for (const row of list.querySelectorAll(".vaultFmRow")) {
        const rect = row.getBoundingClientRect();
        const intersects =
          rect.left <= selectionRect.right &&
          rect.right >= selectionRect.left &&
          rect.top <= selectionRect.bottom &&
          rect.bottom >= selectionRect.top;

        if (intersects && row.dataset.path) {
          selectedPaths.add(row.dataset.path);
          lastSelectedPath = row.dataset.path;
        }
      }

      updateRenderedSelection();
    }

    function finishMarqueeSelection(ev) {
      if (!marqueeDrag || marqueeDrag.pointerId !== ev.pointerId) return;

      const didDrag = marqueeDrag.didDrag;
      const additive = marqueeDrag.additive;

      try { fileArea.releasePointerCapture(ev.pointerId); } catch (_) {}

      marqueeDrag = null;
      marqueeBox.hidden = true;

      if (!didDrag && !additive) {
        clearSelection();
      } else {
        updateSelectionBar();
      }
    }

    clearSelectionBtn.addEventListener("click", clearSelection);

    downloadSelectedBtn.addEventListener("click", () => {
      try {
        downloadSelectedEncryptedPackages();
      } catch (err) {
        status.textContent = err && err.message ? err.message : "Download selected failed.";
      }
    });

    deleteSelectedBtn.addEventListener("click", () => {
      if (trashMode) {
        showRestoreSelectedModal();
      } else {
        showRemoveSelectedModal();
      }
    });

    fileArea.addEventListener("pointerdown", beginMarqueeSelection);
    fileArea.addEventListener("pointermove", updateMarqueeSelection);
    fileArea.addEventListener("pointerup", finishMarqueeSelection);
    fileArea.addEventListener("pointercancel", finishMarqueeSelection);

    function renderEmpty(message) {
      list.textContent = "";
      const empty = document.createElement("div");
      empty.className = "vaultFmEmpty";
      empty.textContent = message;
      list.appendChild(empty);
    }

    function isHiddenVaultSystemEntry(entry) {
      const name = String(entry?.name || entry?.rawName || "").trim();

      // Security/UX: hide DNA-Nexus internal housekeeping entries from the Vault
      // browser so users do not accidentally move/delete .pqnas metadata folders.
      return name === ".pqnas" || name.startsWith(".pqnas_") || name.startsWith(".pqnas-");
    }

    function renderRows(entries) {
      list.textContent = "";
      entryByPath.clear();
      visibleEntries = Array.isArray(entries) ? entries.slice() : [];

      const livePaths = new Set(visibleEntries.map((entry) => entry && entry.path).filter(Boolean));
      for (const path of [...selectedPaths]) {
        if (!livePaths.has(path)) selectedPaths.delete(path);
      }

      for (const entry of visibleEntries) {
        if (entry?.path) entryByPath.set(entry.path, entry);
      }

      updateSelectionBar();

      if (!visibleEntries.length) {
        renderEmpty("This Vault folder is empty.");
        return;
      }

      for (const entry of visibleEntries) {
        const row = document.createElement("button");
        row.type = "button";
        row.className = "vaultFmRow";
        row.dataset.path = entry.path;
        row.dataset.kind = entry.isDir ? "dir" : "file";
        row.setAttribute("aria-selected", selectedPaths.has(entry.path) ? "true" : "false");

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
        applySelectionStateToRow(row, entry);

        row.addEventListener("click", (ev) => {
          handleRowClick(ev, entry);
        });

        row.addEventListener("dblclick", () => {
          if (entry.isTrash) {
            showRestoreTrashModal(entry);
          } else if (entry.isDir) {
            pathInput.value = entry.path;
            refresh().catch(() => {});
          } else {
            downloadDialog.open(entry);
          }
        });

        row.addEventListener("contextmenu", (ev) => {
          ev.preventDefault();
          ensureEntrySelectedForContextMenu(entry);
          showContextMenu(ev.clientX, ev.clientY, entry);
        });

        list.appendChild(row);
      }
    }

    function escapeHtml(value) {
      return String(value ?? "")
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;")
        .replace(/"/g, "&quot;")
        .replace(/'/g, "&#039;");
    }

    function ensureVaultActionModalStyles() {
      if (document.getElementById("vaultActionModalStyles")) return;

      const style = document.createElement("style");
      style.id = "vaultActionModalStyles";
      style.textContent = `
        .vaultActionModalBackdrop{
          position:fixed;
          inset:0;
          z-index:99990;
          background:rgba(15,23,42,0.38);
          display:flex;
          align-items:flex-start;
          justify-content:center;
          padding:96px 24px 24px;
        }
        .vaultActionModal{
          width:min(520px, calc(100vw - 32px));
          border:1px solid var(--border, #b8c2cf);
          border-radius:18px;
          background:#f8fbff;
          color:var(--text, #0f172a);
          box-shadow:0 28px 90px rgba(0,0,0,0.28);
          overflow:hidden;
        }
        .vaultActionModalHead{
          display:flex;
          align-items:center;
          justify-content:space-between;
          gap:12px;
          padding:14px 16px;
          background:#eef4ff;
          border-bottom:1px solid var(--border, #c7d2e2);
          cursor:move;
          user-select:none;
        }
        .vaultActionModalTitle{
          font-weight:900;
          letter-spacing:.01em;
        }
        .vaultActionModalBody{
          padding:16px;
          background:inherit;
        }
        .vaultActionModalFoot{
          display:flex;
          align-items:center;
          justify-content:flex-end;
          gap:10px;
          padding:14px 16px;
          background:#eef4ff;
          border-top:1px solid var(--border, #c7d2e2);
        }
        .vaultActionModalInput{
          width:100%;
          box-sizing:border-box;
          margin-top:10px;
          padding:12px 13px;
          border:1px solid var(--border, #b8c2cf);
          border-radius:12px;
          background:#fff;
          color:inherit;
          font:inherit;
          outline:none;
        }
        .vaultActionModalInput:focus{
          border-color:#60a5fa;
          box-shadow:0 0 0 3px rgba(96,165,250,.24);
        }
        .vaultActionWarning{
          padding:12px 13px;
          border:1px solid rgba(245,158,11,.52);
          border-radius:12px;
          background:rgba(245,158,11,.12);
          color:#92400e;
          line-height:1.45;
        }
        .vaultActionNote{
          margin-top:10px;
          color:var(--muted, #64748b);
          line-height:1.45;
        }
        html[data-theme="dark"] .vaultActionModal{
          background:#07111f;
        }
        html[data-theme="dark"] .vaultActionModalHead,
        html[data-theme="dark"] .vaultActionModalFoot{
          background:#0b1728;
        }
        html[data-theme="dark"] .vaultActionModalInput{
          background:#0f1b2d;
        }
        html[data-theme="cpunk_orange"] .vaultActionModal{
          background:#150b05;
        }
        html[data-theme="cpunk_orange"] .vaultActionModalHead,
        html[data-theme="cpunk_orange"] .vaultActionModalFoot{
          background:#1f1008;
        }
        html[data-theme="cpunk_orange"] .vaultActionModalInput{
          background:#211309;
        }
        html[data-theme="win_classic"] .vaultActionModal{
          background:#f0f0f0;
          border-radius:8px;
        }
        html[data-theme="win_classic"] .vaultActionModalHead,
        html[data-theme="win_classic"] .vaultActionModalFoot{
          background:#e6e6e6;
        }
      `;
      document.head.appendChild(style);
    }

    function showVaultActionModal({ title, bodyHtml, input, primaryLabel, danger, onSubmit }) {
      ensureVaultActionModalStyles();

      document.getElementById("vaultActionModalBackdrop")?.remove();

      const backdrop = document.createElement("div");
      backdrop.id = "vaultActionModalBackdrop";
      backdrop.className = "vaultActionModalBackdrop";

      const dialog = document.createElement("div");
      dialog.className = "vaultActionModal";
      dialog.setAttribute("role", "dialog");
      dialog.setAttribute("aria-modal", "true");

      const primaryClass = danger ? "vaultFmBtn vaultFmPrimary" : "vaultFmBtn vaultFmPrimary";

      dialog.innerHTML = `
        <div class="vaultActionModalHead">
          <div class="vaultActionModalTitle"></div>
          <button class="vaultFmBtn" type="button" data-action="cancel">×</button>
        </div>
        <div class="vaultActionModalBody">
          <div data-slot="body"></div>
          ${input ? '<input class="vaultActionModalInput" type="text" autocomplete="off" spellcheck="false">' : ''}
        </div>
        <div class="vaultActionModalFoot">
          <button class="vaultFmBtn" type="button" data-action="cancel">Cancel</button>
          <button class="${primaryClass}" type="button" data-action="primary"></button>
        </div>
      `;

      backdrop.appendChild(dialog);
      document.body.appendChild(backdrop);

      const titleEl = dialog.querySelector(".vaultActionModalTitle");
      const bodyEl = dialog.querySelector('[data-slot="body"]');
      const inputEl = dialog.querySelector(".vaultActionModalInput");
      const primaryBtn = dialog.querySelector('[data-action="primary"]');
      const cancelBtns = dialog.querySelectorAll('[data-action="cancel"]');
      const head = dialog.querySelector(".vaultActionModalHead");

      titleEl.textContent = title || "";
      bodyEl.innerHTML = bodyHtml || "";
      primaryBtn.textContent = primaryLabel || "OK";

      let drag = null;

      const close = () => {
        document.removeEventListener("keydown", onKey, true);
        backdrop.remove();
      };

      const onKey = (ev) => {
        if (ev.key === "Escape") {
          ev.preventDefault();
          close();
        }
        if (ev.key === "Enter" && inputEl && document.activeElement === inputEl) {
          ev.preventDefault();
          primaryBtn.click();
        }
      };

      document.addEventListener("keydown", onKey, true);

      cancelBtns.forEach((btn) => btn.addEventListener("click", close));
      backdrop.addEventListener("click", (ev) => {
        if (ev.target === backdrop) close();
      });

      primaryBtn.addEventListener("click", async () => {
        try {
          primaryBtn.disabled = true;
          const value = inputEl ? inputEl.value : "";
          await onSubmit(value);
          close();
        } catch (err) {
          primaryBtn.disabled = false;
          status.textContent = err && err.message ? err.message : String(err || "Action failed");
        }
      });

      head.addEventListener("pointerdown", (ev) => {
        if (ev.target && ev.target.closest && ev.target.closest("button,input,textarea,select,a")) return;

        const rect = dialog.getBoundingClientRect();
        drag = {
          pointerId: ev.pointerId,
          startX: ev.clientX,
          startY: ev.clientY,
          left: rect.left,
          top: rect.top
        };

        dialog.style.position = "fixed";
        dialog.style.left = `${rect.left}px`;
        dialog.style.top = `${rect.top}px`;
        dialog.style.margin = "0";

        try { head.setPointerCapture(ev.pointerId); } catch (_) {}
        ev.preventDefault();
      });

      head.addEventListener("pointermove", (ev) => {
        if (!drag || drag.pointerId !== ev.pointerId) return;

        const maxLeft = Math.max(16, window.innerWidth - dialog.offsetWidth - 16);
        const maxTop = Math.max(16, window.innerHeight - dialog.offsetHeight - 16);

        const left = Math.min(maxLeft, Math.max(16, drag.left + ev.clientX - drag.startX));
        const top = Math.min(maxTop, Math.max(16, drag.top + ev.clientY - drag.startY));

        dialog.style.left = `${left}px`;
        dialog.style.top = `${top}px`;
      });

      const stopDrag = (ev) => {
        if (!drag || drag.pointerId !== ev.pointerId) return;
        try { head.releasePointerCapture(ev.pointerId); } catch (_) {}
        drag = null;
      };

      head.addEventListener("pointerup", stopDrag);
      head.addEventListener("pointercancel", stopDrag);

      window.setTimeout(() => {
        if (inputEl) inputEl.focus();
        else primaryBtn.focus();
      }, 40);
    }

    function cleanFolderName(name) {
      const s = String(name || "").trim();

      if (!s) throw new Error("Folder name is required.");
      if (s === "." || s === "..") throw new Error("Folder name cannot be . or ..");
      if (s.includes("/") || s.includes("\\")) throw new Error("Folder name cannot contain slashes.");
      if (s.length > 120) throw new Error("Folder name is too long.");

      return s;
    }

    function childDisplayPath(parentPath, childName) {
      const parent = normalizeDisplayPath(parentPath || DEFAULT_PATH);
      const child = cleanFolderName(childName);
      if (parent === "/" || parent === "") return `/${child}`;
      return `${parent.replace(/\/+$/, "")}/${child}`;
    }

    async function createVaultFolderAt(displayPath) {
      const path = normalizeDisplayPath(displayPath);
      const apiPath = apiPathFromDisplayPath(path);

      if (!apiPath) {
        throw new Error("Cannot create root as a Vault folder.");
      }

      status.textContent = `Creating ${path}...`;

      const res = await fetch(`/api/v4/files/mkdir?path=${encodeURIComponent(apiPath)}`, {
        method: "POST",
        credentials: "include",
        cache: "no-store",
        headers: { "Accept": "application/json" }
      });

      const body = await res.text().catch(() => "");

      if (!res.ok && res.status !== 409) {
        throw new Error(body || `Create folder failed with HTTP ${res.status}`);
      }

      status.textContent = `${path} is ready.`;
      await refresh();
    }

    function showCreateFolderModal() {
      const currentPath = normalizeDisplayPath(pathInput.value);

      showVaultActionModal({
        title: "Create folder",
        input: true,
        primaryLabel: "Create folder",
        bodyHtml: `
          <div class="vaultActionNote">
            Create a new folder inside <strong>${escapeHtml(inputPathFromDisplayPath(currentPath))}</strong>.
          </div>
        `,
        onSubmit: async (name) => {
          const target = childDisplayPath(currentPath, name);
          await createVaultFolderAt(target);
        }
      });
    }

    async function deleteVaultEntry(entry) {
      if (!entry) {
        throw new Error("No Vault entry selected.");
      }

      const displayPath = entry.serverPath || entry.path;
      const apiPath = apiPathFromDisplayPath(displayPath);

      if (!apiPath) {
        throw new Error("Vault path is empty.");
      }

      const kind = entry.isDir ? "folder" : "file";
      status.textContent = `Removing ${entry.name}...`;

      const res = await fetch(`/api/v4/files/delete?path=${encodeURIComponent(apiPath)}`, {
        method: "POST",
        credentials: "include",
        cache: "no-store",
        headers: { "Accept": "application/json" }
      });

      const body = await res.text().catch(() => "");

      if (!res.ok) {
        throw new Error(body || `Remove ${kind} failed with HTTP ${res.status}`);
      }

      status.textContent = `${entry.name} removed.`;
      await refresh();
    }

    function showRemoveEntryModal(entry) {
      const isFolder = !!entry?.isDir;
      const title = isFolder ? "Move Vault folder to trash?" : "Move Vault file to trash?";
      const primaryLabel = isFolder ? "Move folder to trash" : "Move to trash";

      showVaultActionModal({
        title,
        primaryLabel,
        danger: true,
        bodyHtml: `
          <div class="vaultActionWarning">
            ${isFolder ? "Move folder to trash" : "Move file to trash"} <strong>${escapeHtml(entry?.name || "this item")}</strong> from Vault?
          </div>
          <div class="vaultActionNote">
            ${isFolder
              ? "This moves the selected Vault folder and its contents to trash."
              : "This removes the encrypted Vault package from the server. It does not decrypt or inspect the file."}
          </div>
        `,
        onSubmit: async () => {
          // Security: remove only the exact Vault entry selected by the user.
          // The server-side delete endpoint handles trashing; no decrypt/preview path is involved.
          await deleteVaultEntry(entry);
          selectedPaths.delete(entry?.path || "");
          updateRenderedSelection();
        }
      });
    }

    async function deleteVaultEntries(entries) {
      if (!Array.isArray(entries) || !entries.length) {
        throw new Error("No selected Vault entries.");
      }

      let removed = 0;

      for (const entry of entries) {
        const displayPath = entry.serverPath || entry.path;
        const apiPath = apiPathFromDisplayPath(displayPath);

        if (!apiPath) {
          throw new Error(`Vault path is empty for ${entry.name || "selected item"}.`);
        }

        status.textContent = `Removing ${removed + 1}/${entries.length}: ${entry.name}`;

        const res = await fetch(`/api/v4/files/delete?path=${encodeURIComponent(apiPath)}`, {
          method: "POST",
          credentials: "include",
          cache: "no-store",
          headers: { "Accept": "application/json" }
        });

        const body = await res.text().catch(() => "");

        if (!res.ok) {
          throw new Error(body || `Remove failed with HTTP ${res.status}`);
        }

        removed += 1;
      }

      selectedPaths.clear();
      status.textContent = `Removed ${removed} selected item${removed === 1 ? "" : "s"}.`;
      await refresh();
    }

    function showRemoveSelectedModal() {
      const entries = getSelectedEntries();

      if (!entries.length) {
        status.textContent = "No selected Vault entries.";
        return;
      }

      const files = entries.filter((entry) => !entry.isDir).length;
      const folders = entries.length - files;

      const parts = [];
      if (files) parts.push(`${files} file${files === 1 ? "" : "s"}`);
      if (folders) parts.push(`${folders} folder${folders === 1 ? "" : "s"}`);

      showVaultActionModal({
        title: "Move selected to trash Vault items?",
        primaryLabel: "Move selected to trash",
        danger: true,
        bodyHtml: `
          <div class="vaultActionWarning">
            Move to trash <strong>${entries.length}</strong> selected Vault item${entries.length === 1 ? "" : "s"}?
          </div>
          <div class="vaultActionNote">
            Selected: ${escapeHtml(parts.join(", ") || "items")}. Folders and their contents are moved to trash.
            Encrypted packages are removed as stored; no decrypt or preview path is involved.
          </div>
        `,
        onSubmit: async () => {
          // Security: bulk deletion only submits the exact selected Vault paths to
          // the existing trashing endpoint. No plaintext recovery path is involved.
          await deleteVaultEntries(entries);
        }
      });
    }

    function downloadEncryptedPackage(entry) {
      const displayPath = entry.serverPath || entry.path;
      const apiPath = apiPathFromDisplayPath(displayPath);

      if (!apiPath) {
        throw new Error(`Vault path is empty for ${entry.name || "selected file"}.`);
      }

      const a = document.createElement("a");
      a.href = `/api/v4/files/get?path=${encodeURIComponent(apiPath)}`;
      a.download = entry.name || "vault-package.dnavault.json";
      a.rel = "noopener";
      document.body.appendChild(a);
      a.click();
      a.remove();
    }

    function downloadSelectedEncryptedPackages() {
      const files = getSelectedFileEntries();

      if (!files.length) {
        status.textContent = "Select one or more files. Folders cannot be downloaded as encrypted packages.";
        return;
      }

      // Security: this downloads the encrypted Vault packages exactly as stored.
      // It does not decrypt, preview, or recover plaintext.
      for (const entry of files) {
        downloadEncryptedPackage(entry);
      }

      status.textContent = `Started encrypted package download for ${files.length} file${files.length === 1 ? "" : "s"}.`;
    }

    async function createVaultFolder() {
      try {
        await createVaultFolderAt(pathInput.value);
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
      if (trashMode) {
        await refreshVaultTrash();
        return;
      }

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

        const visibleEntries = entries.filter((entry) => !isHiddenVaultSystemEntry(entry));

        sortVaultEntries(visibleEntries);

        renderRows(visibleEntries);
        updateVaultSortControls();
        status.textContent = `${visibleEntries.length} item${visibleEntries.length === 1 ? "" : "s"} in ${path}`;
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

    function trashText(item, keys) {
      for (const key of keys) {
        const value = item && item[key];
        if (value !== undefined && value !== null && String(value).trim()) {
          return String(value).trim();
        }
      }
      return "";
    }

    function trashNumber(item, keys) {
      for (const key of keys) {
        const value = item && item[key];
        const num = Number(value);
        if (Number.isFinite(num)) return num;
      }
      return 0;
    }

    function basenameFromPath(path) {
      const clean = String(path || "").replace(/\\/g, "/").replace(/^\/+/, "").replace(/\/+$/, "");
      const parts = clean.split("/").filter(Boolean);
      return parts.length ? parts[parts.length - 1] : clean;
    }

    function normalizeTrashOriginalPath(item) {
      return trashText(item, [
        "original_rel_path",
        "original_path",
        "originalPath",
        "rel_path",
        "path"
      ]).replace(/\\/g, "/").replace(/^\/+/, "");
    }

    function isVaultTrashItem(item) {
      const originalPath = normalizeTrashOriginalPath(item);
      const rawName = trashText(item, ["name", "filename", "original_name"]) || basenameFromPath(originalPath);

      return originalPath === "Vault" ||
        originalPath.startsWith("Vault/") ||
        rawName.endsWith(".dnavault.json");
    }

    function trashItemToVaultEntry(item) {
      const trashId = trashText(item, ["trash_id", "trashId", "id"]);
      const originalPath = normalizeTrashOriginalPath(item);
      const rawName = trashText(item, ["name", "filename", "original_name"]) || basenameFromPath(originalPath) || trashId;
      const type = trashText(item, ["item_type", "type", "kind"]);
      const isDir = type === "dir" || type === "directory";

      return {
        name: rawName.endsWith(".dnavault.json") ? friendlyVaultName(rawName) : rawName,
        rawName,
        path: `trash:${trashId}`,
        serverPath: originalPath,
        originalPath,
        trashId,
        isTrash: true,
        isDir,
        isVaultPackage: rawName.endsWith(".dnavault.json"),
        size: trashNumber(item, ["size_bytes", "payload_size_bytes", "bytes", "size"]),
        modified: trashNumber(item, ["trashed_at_epoch", "trashed_at_unix", "deleted_at_epoch", "mtime_unix"]) ||
          trashText(item, ["trashed_at", "deleted_at", "created_at", "mtime"]),
        mime: ""
      };
    }

    async function fetchVaultTrashEntries() {
      const res = await fetch("/api/v4/trash/list?scope=user&limit=500", {
        method: "GET",
        credentials: "include",
        cache: "no-store",
        headers: { "Accept": "application/json" }
      });

      const text = await res.text().catch(() => "");

      if (!res.ok) {
        throw new Error(text || `Trash list failed with HTTP ${res.status}`);
      }

      const body = text ? JSON.parse(text) : {};
      const items = Array.isArray(body.items) ? body.items : [];

      return items
        .filter(isVaultTrashItem)
        .map(trashItemToVaultEntry)
        .filter((entry) => entry.trashId);
    }

    async function refreshVaultTrash() {
      clearSelection();
      list.textContent = "";
      status.textContent = "Loading Vault trash…";

      const entries = await fetchVaultTrashEntries();
      sortVaultEntries(entries);

      if (!entries.length) {
        entryByPath.clear();
        visibleEntries = [];
        updateSelectionBar();
        renderEmpty("Vault trash is empty.");
        status.textContent = "Vault trash is empty.";
        updateVaultSortControls();
        return;
      }

      renderRows(entries);
      updateVaultSortControls();
      status.textContent = `${entries.length} Vault trash item${entries.length === 1 ? "" : "s"}`;
    }

    async function restoreVaultTrashEntry(entry) {
      if (!entry?.trashId) {
        throw new Error("No trash item selected.");
      }

      status.textContent = `Restoring ${entry.name}…`;

      const res = await fetch("/api/v4/trash/restore", {
        method: "POST",
        credentials: "include",
        cache: "no-store",
        headers: {
          "Accept": "application/json",
          "Content-Type": "application/json"
        },
        body: JSON.stringify({
          trash_id: entry.trashId,
          rename_if_conflict: true
        })
      });

      const body = await res.text().catch(() => "");

      if (!res.ok) {
        throw new Error(body || `Restore failed with HTTP ${res.status}`);
      }

      status.textContent = `${entry.name} restored.`;
      await refresh();
    }

    async function restoreVaultTrashEntries(entries) {
      if (!Array.isArray(entries) || !entries.length) {
        throw new Error("No selected trash items.");
      }

      let restored = 0;

      for (const entry of entries) {
        status.textContent = `Restoring ${restored + 1}/${entries.length}: ${entry.name}`;
        await restoreVaultTrashEntry(entry);
        restored += 1;
      }

      selectedPaths.clear();
      status.textContent = `Restored ${restored} Vault item${restored === 1 ? "" : "s"}.`;
      await refresh();
    }

    function showRestoreTrashModal(entry) {
      showVaultActionModal({
        title: "Restore Vault item?",
        primaryLabel: "Restore",
        bodyHtml: `
          <div class="vaultActionWarning">
            Restore <strong>${escapeHtml(entry?.name || "this item")}</strong> back to
            <strong>${escapeHtml(entry?.originalPath || "its original Vault path")}</strong>?
          </div>
          <div class="vaultActionNote">
            If the original path already exists, DNA-Nexus may restore with a conflict-safe renamed path.
          </div>
        `,
        onSubmit: async () => {
          // Security: restore by trash_id only. The server validates ownership and
          // resolves the original path under the user's allowed storage root.
          await restoreVaultTrashEntry(entry);
        }
      });
    }

    function showRestoreSelectedModal() {
      const entries = getSelectedEntries().filter((entry) => entry?.isTrash);

      if (!entries.length) {
        status.textContent = "No Vault trash items selected.";
        return;
      }

      showVaultActionModal({
        title: "Restore selected Vault items?",
        primaryLabel: "Restore selected",
        bodyHtml: `
          <div class="vaultActionWarning">
            Restore <strong>${entries.length}</strong> selected Vault trash item${entries.length === 1 ? "" : "s"}?
          </div>
          <div class="vaultActionNote">
            Only Vault trash items are shown here. Other File Manager trash entries stay hidden from this view.
          </div>
        `,
        onSubmit: async () => {
          // Security: bulk restore submits only selected trash_id values from the
          // Vault-filtered trash view. The server re-checks ownership for each item.
          await restoreVaultTrashEntries(entries);
        }
      });
    }

    function applyTrashModeUi() {
      trashBtn.textContent = trashMode ? "Back to Vault" : "Trash";
      trashBtn.setAttribute("aria-pressed", trashMode ? "true" : "false");

      pathInput.disabled = trashMode;
      upBtn.disabled = trashMode;
      uploadBtn.hidden = trashMode;
      decryptBtn.hidden = trashMode;

      if (trashMode) {
        pathInput.value = "Trash / Vault";
      } else {
        pathInput.value = lastLivePath || "Vault";
      }

      updateSelectionBar();
    }

    function setTrashMode(enabled) {
      if (enabled === trashMode) return;

      if (enabled) {
        lastLivePath = pathInput.value || lastLivePath || "Vault";
      }

      trashMode = enabled;
      selectedPaths.clear();
      applyTrashModeUi();
      refresh().catch((err) => {
        status.textContent = err && err.message ? err.message : "Vault refresh failed.";
      });
    }

    function placeContextMenu(x, y) {
      const rect = menu.getBoundingClientRect();
      const left = Math.min(x, Math.max(8, window.innerWidth - rect.width - 8));
      const top = Math.min(y, Math.max(8, window.innerHeight - rect.height - 8));
      menu.style.left = `${left}px`;
      menu.style.top = `${top}px`;
    }

    function showTrashContextMenu(x, y, entry) {
      menu.textContent = "";
      menu.hidden = false;

      const restore = document.createElement("button");
      restore.type = "button";
      restore.textContent = "Restore";
      restore.addEventListener("click", () => {
        hideContextMenu();
        showRestoreTrashModal(entry);
      });

      const copyPath = document.createElement("button");
      copyPath.type = "button";
      copyPath.textContent = "Copy original path";
      copyPath.addEventListener("click", async () => {
        hideContextMenu();

        try {
          await navigator.clipboard.writeText(entry.originalPath || "");
          status.textContent = "Original path copied.";
        } catch {
          status.textContent = entry.originalPath || "";
        }
      });

      menu.append(restore, copyPath);
      placeContextMenu(x, y);
    }

    function vaultTrashText(item, keys) {
      for (const key of keys) {
        const value = item && item[key];
        if (value !== undefined && value !== null && String(value).trim()) {
          return String(value).trim();
        }
      }
      return "";
    }

    function vaultTrashNumber(item, keys) {
      for (const key of keys) {
        const value = item && item[key];
        const num = Number(value);
        if (Number.isFinite(num)) return num;
      }
      return 0;
    }

    function vaultTrashBasename(path) {
      const clean = String(path || "").replace(/\\/g, "/").replace(/^\/+/, "").replace(/\/+$/, "");
      const parts = clean.split("/").filter(Boolean);
      return parts.length ? parts[parts.length - 1] : clean;
    }

    function vaultTrashOriginalPath(item) {
      return vaultTrashText(item, [
        "original_rel_path",
        "original_path",
        "originalPath",
        "rel_path",
        "path"
      ]).replace(/\\/g, "/").replace(/^\/+/, "");
    }

    function isVaultTrashItem(item) {
      const originalPath = vaultTrashOriginalPath(item);
      const rawName = vaultTrashText(item, ["name", "filename", "original_name"]) || vaultTrashBasename(originalPath);

      return originalPath === "Vault" ||
        originalPath.startsWith("Vault/") ||
        rawName.endsWith(".dnavault.json");
    }

    function vaultTrashItemView(item) {
      const originalPath = vaultTrashOriginalPath(item);
      const trashId = vaultTrashText(item, ["trash_id", "trashId", "id"]);
      const rawName = vaultTrashText(item, ["name", "filename", "original_name"]) || vaultTrashBasename(originalPath) || trashId;
      const itemType = vaultTrashText(item, ["item_type", "type", "kind"]);
      const isDir = itemType === "dir" || itemType === "directory";
      const size = vaultTrashNumber(item, ["size_bytes", "payload_size_bytes", "bytes", "size"]);
      const trashedAt = vaultTrashText(item, ["trashed_at", "deleted_at", "created_at"]) ||
        vaultTrashNumber(item, ["trashed_at_epoch", "trashed_at_unix", "deleted_at_epoch"]);

      return {
        trashId,
        name: rawName.endsWith(".dnavault.json") ? friendlyVaultName(rawName) : rawName,
        rawName,
        originalPath,
        isDir,
        size,
        trashedAt,
        source: item
      };
    }

    async function fetchVaultTrashItems() {
      const res = await fetch("/api/v4/trash/list?scope=user&limit=500", {
        method: "GET",
        credentials: "include",
        cache: "no-store",
        headers: { "Accept": "application/json" }
      });

      const bodyText = await res.text().catch(() => "");

      if (!res.ok) {
        throw new Error(bodyText || `Trash list failed with HTTP ${res.status}`);
      }

      const body = bodyText ? JSON.parse(bodyText) : {};
      const items = Array.isArray(body.items) ? body.items : [];

      return items
        .filter(isVaultTrashItem)
        .map(vaultTrashItemView)
        .filter((item) => item.trashId);
    }

    async function restoreVaultTrashItem(item) {
      if (!item?.trashId) {
        throw new Error("No trash item selected.");
      }

      const res = await fetch("/api/v4/trash/restore", {
        method: "POST",
        credentials: "include",
        cache: "no-store",
        headers: {
          "Accept": "application/json",
          "Content-Type": "application/json"
        },
        body: JSON.stringify({
          trash_id: item.trashId,
          rename_if_conflict: true
        })
      });

      const bodyText = await res.text().catch(() => "");

      if (!res.ok) {
        throw new Error(bodyText || `Restore failed with HTTP ${res.status}`);
      }

      return bodyText ? JSON.parse(bodyText) : {};
    }

    function formatVaultTrashTime(value) {
      if (!value) return "—";

      if (typeof value === "number") {
        return formatTime(value);
      }

      const parsed = Date.parse(String(value));
      if (Number.isFinite(parsed)) {
        return new Date(parsed).toLocaleString();
      }

      return String(value);
    }

    function ensureVaultTrashModalStyles() {
      if (document.getElementById("vaultTrashModalStyles")) return;

      const style = document.createElement("style");
      style.id = "vaultTrashModalStyles";
      style.textContent = `
        .vaultTrashBackdrop{
          position:fixed;
          inset:0;
          z-index:10020;
          display:grid;
          place-items:center;
          padding:24px;
          background:rgba(15,23,42,.30);
          backdrop-filter:blur(8px);
        }
        .vaultTrashBackdrop[hidden]{
          display:none !important;
        }
        .vaultTrashDialog{
          width:min(900px, calc(100vw - 48px));
          max-height:min(720px, calc(100vh - 48px));
          display:grid;
          grid-template-rows:auto auto 1fr auto;
          overflow:hidden;
          border:1px solid var(--border);
          border-radius:18px;
          background:var(--card);
          color:var(--fg);
          box-shadow:0 28px 80px rgba(15,23,42,.28);
        }
        .vaultTrashHead{
          display:flex;
          align-items:center;
          justify-content:space-between;
          gap:12px;
          padding:14px 16px;
          border-bottom:1px solid var(--border);
          background:var(--panel);
        }
        .vaultTrashTitle{
          font-weight:950;
          font-size:17px;
        }
        .vaultTrashSub{
          color:var(--fg-dim, var(--muted));
          font-size:13px;
          margin-top:2px;
        }
        .vaultTrashStatus{
          padding:12px 16px;
          color:var(--fg-dim, var(--muted));
          font-family:var(--mono, monospace);
          border-bottom:1px solid var(--border);
        }
        .vaultTrashSummary{
          margin:12px 16px 0;
          padding:10px 12px;
          border:1px solid var(--border);
          border-radius:14px;
          background:color-mix(in oklab, var(--panel) 86%, transparent);
          display:flex;
          align-items:center;
          gap:8px;
          flex-wrap:wrap;
        }
        .vaultTrashPill{
          border:1px solid var(--border);
          border-radius:999px;
          padding:5px 9px;
          font-weight:850;
          font-size:12px;
          background:var(--card);
        }
        .vaultTrashList{
          overflow:auto;
          padding:10px 16px 16px;
          display:grid;
          gap:10px;
        }
        .vaultTrashItem{
          display:grid;
          grid-template-columns:minmax(0, 1fr) auto;
          gap:12px;
          align-items:center;
          border:1px solid var(--border);
          border-radius:14px;
          padding:12px;
          background:var(--panel);
        }
        .vaultTrashItemName{
          font-weight:950;
          overflow:hidden;
          text-overflow:ellipsis;
          white-space:nowrap;
        }
        .vaultTrashItemMeta{
          margin-top:4px;
          color:var(--fg-dim, var(--muted));
          font-size:12px;
          line-height:1.35;
        }
        .vaultTrashItemPath{
          margin-top:4px;
          color:var(--fg-dim, var(--muted));
          font-family:var(--mono, monospace);
          font-size:12px;
          overflow:hidden;
          text-overflow:ellipsis;
          white-space:nowrap;
        }
        .vaultTrashItemActions{
          display:flex;
          gap:8px;
          align-items:center;
        }
        .vaultTrashFoot{
          display:flex;
          align-items:center;
          justify-content:space-between;
          gap:12px;
          padding:12px 16px;
          border-top:1px solid var(--border);
          background:var(--panel);
        }
        .vaultTrashFootActions{
          display:flex;
          gap:8px;
          align-items:center;
          flex-wrap:wrap;
        }
        .vaultTrashEmpty{
          padding:32px 16px;
          text-align:center;
          color:var(--fg-dim, var(--muted));
          font-weight:800;
        }
        html[data-theme="win_classic"] .vaultTrashDialog{
          border-radius:8px;
        }
        @media (max-width:700px){
          .vaultTrashItem{
            grid-template-columns:1fr;
          }
          .vaultTrashItemActions{
            justify-content:flex-start;
          }
        }
      `;
      document.head.appendChild(style);
    }

    function openVaultTrashModal() {
      ensureVaultTrashModalStyles();

      const old = document.querySelector(".vaultTrashBackdrop");
      if (old) old.remove();

      const backdrop = document.createElement("div");
      backdrop.className = "vaultTrashBackdrop";

      const dialog = document.createElement("section");
      dialog.className = "vaultTrashDialog";
      dialog.setAttribute("role", "dialog");
      dialog.setAttribute("aria-modal", "true");
      dialog.setAttribute("aria-label", "Vault trash");

      const head = document.createElement("div");
      head.className = "vaultTrashHead";

      const headText = document.createElement("div");
      const title = document.createElement("div");
      title.className = "vaultTrashTitle";
      title.textContent = "Vault trash";
      const sub = document.createElement("div");
      sub.className = "vaultTrashSub";
      sub.textContent = "Only Vault items are shown here.";
      headText.append(title, sub);

      const closeBtn = makeButton("Close");
      head.append(headText, closeBtn);

      const statusEl = document.createElement("div");
      statusEl.className = "vaultTrashStatus";
      statusEl.textContent = "Loading Vault trash…";

      const summary = document.createElement("div");
      summary.className = "vaultTrashSummary";
      summary.hidden = true;

      const listEl = document.createElement("div");
      listEl.className = "vaultTrashList";

      const foot = document.createElement("div");
      foot.className = "vaultTrashFoot";

      const footActions = document.createElement("div");
      footActions.className = "vaultTrashFootActions";

      const refreshTrashBtn = makeButton("Refresh");
      footActions.append(refreshTrashBtn);

      const countEl = document.createElement("div");
      countEl.className = "vaultTrashSub";
      countEl.textContent = "";

      foot.append(footActions, countEl);
      dialog.append(head, statusEl, summary, listEl, foot);
      backdrop.appendChild(dialog);
      document.body.appendChild(backdrop);

      const close = () => {
        backdrop.remove();
        refresh().catch(() => {});
      };

      closeBtn.addEventListener("click", close);
      backdrop.addEventListener("click", (ev) => {
        if (ev.target === backdrop) close();
      });

      const onKey = (ev) => {
        if (ev.key === "Escape" && document.body.contains(backdrop)) {
          ev.preventDefault();
          document.removeEventListener("keydown", onKey);
          close();
        }
      };
      document.addEventListener("keydown", onKey);

      async function load() {
        statusEl.textContent = "Loading Vault trash…";
        listEl.textContent = "";
        summary.hidden = true;
        countEl.textContent = "";

        try {
          const items = await fetchVaultTrashItems();
          const files = items.filter((item) => !item.isDir).length;
          const folders = items.length - files;
          const bytes = items.reduce((sum, item) => sum + (Number(item.size) || 0), 0);

          summary.hidden = false;
          summary.textContent = "";

          const pills = [
            `Vault trash: ${items.length} item${items.length === 1 ? "" : "s"}`,
            `${folders} folder${folders === 1 ? "" : "s"}`,
            `${files} file${files === 1 ? "" : "s"}`,
            `Size: ${formatBytes(bytes)}`
          ];

          for (const text of pills) {
            const pill = document.createElement("span");
            pill.className = "vaultTrashPill";
            pill.textContent = text;
            summary.appendChild(pill);
          }

          if (!items.length) {
            const empty = document.createElement("div");
            empty.className = "vaultTrashEmpty";
            empty.textContent = "Vault trash is empty.";
            listEl.appendChild(empty);
            statusEl.textContent = "No Vault trash items.";
            countEl.textContent = "0 items";
            return;
          }

          statusEl.textContent = `Loaded ${items.length} Vault trash item${items.length === 1 ? "" : "s"}.`;
          countEl.textContent = `${items.length} item${items.length === 1 ? "" : "s"}`;

          for (const item of items) {
            const row = document.createElement("article");
            row.className = "vaultTrashItem";

            const body = document.createElement("div");
            const name = document.createElement("div");
            name.className = "vaultTrashItemName";
            name.textContent = item.name;

            const meta = document.createElement("div");
            meta.className = "vaultTrashItemMeta";
            meta.textContent = `${item.isDir ? "Folder" : "File"} · ${formatBytes(item.size || 0)} · Removed: ${formatVaultTrashTime(item.trashedAt)}`;

            const path = document.createElement("div");
            path.className = "vaultTrashItemPath";
            path.textContent = item.originalPath || "—";

            body.append(name, meta, path);

            const actions = document.createElement("div");
            actions.className = "vaultTrashItemActions";

            const restoreBtn = makeButton("Restore");
            restoreBtn.addEventListener("click", async () => {
              restoreBtn.disabled = true;
              statusEl.textContent = `Restoring ${item.name}…`;

              try {
                // Security: restore uses trash_id only. The server validates owner
                // scope and resolves the original path under the user's storage root.
                await restoreVaultTrashItem(item);
                await load();
              } catch (err) {
                restoreBtn.disabled = false;
                statusEl.textContent = err && err.message ? err.message : "Restore failed.";
              }
            });

            actions.appendChild(restoreBtn);
            row.append(body, actions);
            listEl.appendChild(row);
          }
        } catch (err) {
          const empty = document.createElement("div");
          empty.className = "vaultTrashEmpty";
          empty.textContent = "Could not load Vault trash.";
          listEl.appendChild(empty);
          statusEl.textContent = err && err.message ? err.message : "Trash list failed.";
        }
      }

      refreshTrashBtn.addEventListener("click", load);
      load().catch(() => {});
      closeBtn.focus();
    }

    function showContextMenu(x, y, entry) {
      if (entry?.isTrash) {
        showTrashContextMenu(x, y, entry);
        return;
      }

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

      if (entry.isDir) {
        const removeFolder = document.createElement("button");
        removeFolder.type = "button";
        removeFolder.textContent = "Move folder to trash";
        removeFolder.classList.add("vaultContextDanger");
        removeFolder.addEventListener("click", () => {
          hideContextMenu();
          showRemoveEntryModal(entry);
        });
        menu.appendChild(removeFolder);
      } else {
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

        const deleteFile = document.createElement("button");
        deleteFile.type = "button";
        deleteFile.textContent = "Move to trash";
        deleteFile.classList.add("vaultContextDanger");
        deleteFile.addEventListener("click", () => {
          hideContextMenu();
          showRemoveEntryModal(entry);
        });
        menu.appendChild(deleteFile);
      }

      const rect = menu.getBoundingClientRect();
      const left = Math.min(x, Math.max(8, window.innerWidth - rect.width - 8));
      const top = Math.min(y, Math.max(8, window.innerHeight - rect.height - 8));
      menu.style.left = `${left}px`;
      menu.style.top = `${top}px`;
    }

    function showFolderContextMenu(x, y) {
      if (trashMode) return;

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

      addMenuButton("Create folder", () => {
        showCreateFolderModal();
      });

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
      marqueeBox.hidden = true;
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

    viewToggleBtn.addEventListener("click", () => {
      viewMode = viewMode === "grid" ? "list" : "grid";

      try {
        localStorage.setItem(VIEW_MODE_STORAGE_KEY, viewMode);
      } catch (_) {
        // Non-fatal: view mode still changes for the current session.
      }

      applyVaultViewMode();
    });

    sortKeyBtn.addEventListener("click", cycleSortKey);
    sortDirBtn.addEventListener("click", toggleSortDirection);
    foldersFirstBtn.addEventListener("click", toggleFoldersFirst);

    trashBtn.addEventListener("click", () => {
      openVaultTrashModal();
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
