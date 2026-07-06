(() => {
  "use strict";

  const VAULT_APP_ID = "dna-nexus-vault";
  const VAULT_PACKAGE_VERSION = 1;
  const VAULT_MIME = "application/vnd.dna-nexus.vault+json";
  const USER_KDF_ITERATIONS = 310000;
  const MAX_BROWSER_FILE_BYTES = 512 * 1024 * 1024;

  const state = {
    files: []
  };

  const el = {
    appVersion: document.getElementById("appVersion"),
    sessionBadge: document.getElementById("sessionBadge"),
    vaultFolderInput: document.getElementById("vaultFolderInput"),
    fileInput: document.getElementById("fileInput"),
    dropZone: document.getElementById("dropZone"),
    passphraseInput: document.getElementById("passphraseInput"),
    passphraseConfirmInput: document.getElementById("passphraseConfirmInput"),
    recoveryPublicKeyInput: document.getElementById("recoveryPublicKeyInput"),
    encryptUploadBtn: document.getElementById("encryptUploadBtn"),
    clearBtn: document.getElementById("clearBtn"),
    encryptStatus: document.getElementById("encryptStatus"),
    queueList: document.getElementById("queueList"),
    decryptFileInput: document.getElementById("decryptFileInput"),
    decryptPassphraseInput: document.getElementById("decryptPassphraseInput"),
    decryptBtn: document.getElementById("decryptBtn"),
    decryptStatus: document.getElementById("decryptStatus"),
    downloadSlot: document.getElementById("downloadSlot")
  };

  function setText(node, text) {
    if (node) node.textContent = String(text || "");
  }

  function utf8ToBytes(s) {
    return new TextEncoder().encode(String(s || ""));
  }

  function bytesToUtf8(bytes) {
    return new TextDecoder().decode(bytes);
  }

  function bytesToB64(bytes) {
    let s = "";
    const chunk = 0x8000;
    for (let i = 0; i < bytes.length; i += chunk) {
      s += String.fromCharCode(...bytes.subarray(i, i + chunk));
    }
    return btoa(s);
  }

  function b64ToBytes(b64) {
    const normalized = String(b64 || "").replace(/-/g, "+").replace(/_/g, "/");
    const bin = atob(normalized);
    const out = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
    return out;
  }

  function randomBytes(n) {
    const out = new Uint8Array(n);
    crypto.getRandomValues(out);
    return out;
  }

  async function sha256Hex(bytes) {
    const digest = new Uint8Array(await crypto.subtle.digest("SHA-256", bytes));
    let out = "";
    for (const b of digest) out += b.toString(16).padStart(2, "0");
    return out;
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
      } catch (_) {
      }
    }

    return "";
  }

  async function checkSession() {
    try {
      const r = await fetch("/api/v4/me", {
        credentials: "include",
        cache: "no-store",
        headers: { "Accept": "application/json" }
      });

      if (r.status === 401) {
        setText(el.sessionBadge, "Not signed in");
        el.sessionBadge.className = "pq-badge err";
        return;
      }

      if (r.status === 403) {
        setText(el.sessionBadge, "Account not allowed");
        el.sessionBadge.className = "pq-badge warn";
        return;
      }

      if (!r.ok) {
        setText(el.sessionBadge, "Session check failed");
        el.sessionBadge.className = "pq-badge warn";
        return;
      }

      const j = await r.json();
      const role = j && j.role ? String(j.role) : "user";
      setText(el.sessionBadge, `Signed in • ${role}`);
      el.sessionBadge.className = "pq-badge ok";
    } catch (_) {
      setText(el.sessionBadge, "Offline or unavailable");
      el.sessionBadge.className = "pq-badge warn";
    }
  }

  function normalizeFolderPath(input) {
    const raw = String(input || "").replace(/\\/g, "/").trim();
    const parts = raw.split("/").map((p) => p.trim()).filter(Boolean);
    const safe = [];

    for (const part of parts) {
      if (part === "." || part === "..") {
        throw new Error("Vault folder cannot contain . or .. path segments");
      }
      if (/[\u0000-\u001f\u007f]/u.test(part)) {
        throw new Error("Vault folder contains control characters");
      }
      safe.push(part);
    }

    return safe.length ? safe.join("/") : "Vault";
  }

  function safeFileName(name) {
    const base = String(name || "file")
      .replace(/[\\/]/g, "_")
      .replace(/[\u0000-\u001f\u007f]/gu, "_")
      .trim();

    if (!base || base === "." || base === "..") return "file";
    return base;
  }

  function formatBytes(n) {
    const units = ["B", "KiB", "MiB", "GiB", "TiB"];
    let value = Number(n || 0);
    let idx = 0;
    while (value >= 1024 && idx < units.length - 1) {
      value /= 1024;
      idx++;
    }
    return `${value.toFixed(idx === 0 ? 0 : 1)} ${units[idx]}`;
  }

  function renderQueue() {
    el.queueList.replaceChildren();

    for (const file of state.files) {
      const item = document.createElement("div");
      item.className = "vault-item";

      const title = document.createElement("div");
      title.className = "vault-item-title";
      title.textContent = file.name;

      const meta = document.createElement("div");
      meta.className = "vault-item-meta";
      meta.textContent = `${formatBytes(file.size)} • ${file.type || "application/octet-stream"}`;

      item.append(title, meta);
      el.queueList.append(item);
    }

    if (!state.files.length) {
      const item = document.createElement("div");
      item.className = "vault-item";
      const title = document.createElement("div");
      title.className = "vault-item-title";
      title.textContent = "No files selected.";
      item.append(title);
      el.queueList.append(item);
    }
  }

  function addFiles(fileList) {
    const files = Array.from(fileList || []);
    for (const file of files) {
      if (!file || typeof file.name !== "string") continue;
      state.files.push(file);
    }
    renderQueue();
    setText(el.encryptStatus, `${state.files.length} file(s) ready.`);
  }

  async function deriveUserWrapKey(passphrase, saltBytes) {
    const baseKey = await crypto.subtle.importKey(
      "raw",
      utf8ToBytes(passphrase),
      "PBKDF2",
      false,
      ["deriveKey"]
    );

    return crypto.subtle.deriveKey(
      {
        name: "PBKDF2",
        hash: "SHA-256",
        salt: saltBytes,
        iterations: USER_KDF_ITERATIONS
      },
      baseKey,
      { name: "AES-GCM", length: 256 },
      false,
      ["encrypt", "decrypt"]
    );
  }

  async function hkdfAesGcmKey(sharedSecretBytes, saltBytes, infoBytes, usage) {
    const baseKey = await crypto.subtle.importKey(
      "raw",
      sharedSecretBytes,
      "HKDF",
      false,
      ["deriveKey"]
    );

    return crypto.subtle.deriveKey(
      {
        name: "HKDF",
        hash: "SHA-256",
        salt: saltBytes,
        info: infoBytes
      },
      baseKey,
      { name: "AES-GCM", length: 256 },
      false,
      usage
    );
  }

  async function wrapCekForUser(rawCek, passphrase, aadBytes) {
    const salt = randomBytes(16);
    const iv = randomBytes(12);
    const key = await deriveUserWrapKey(passphrase, salt);
    const wrapped = new Uint8Array(await crypto.subtle.encrypt(
      {
        name: "AES-GCM",
        iv,
        additionalData: aadBytes
      },
      key,
      rawCek
    ));

    return {
      purpose: "user_passphrase",
      mode: "pbkdf2_sha256_aes256gcm_v1",
      kdf: {
        name: "PBKDF2-SHA256",
        iterations: USER_KDF_ITERATIONS,
        salt_b64: bytesToB64(salt)
      },
      wrap_iv_b64: bytesToB64(iv),
      wrapped_cek_b64: bytesToB64(wrapped)
    };
  }

  async function wrapCekForOrganization(rawCek, publicKeyB64, aadBytes) {
    const publicKeyBytes = b64ToBytes(publicKeyB64);
    if (!globalThis.PqShareMlKemV1 || !(await globalThis.PqShareMlKemV1.isAvailable())) {
      throw new Error("ML-KEM-768 browser helper is not available");
    }

    if (typeof globalThis.PqShareMlKemV1.encapsulate768 !== "function") {
      throw new Error("ML-KEM-768 encapsulation helper is missing");
    }

    const encap = await globalThis.PqShareMlKemV1.encapsulate768({ publicKeyB64 });
    const hkdfSalt = randomBytes(32);
    const hkdfInfo = utf8ToBytes("pqnas-vault-mlkem768-recovery-wrap-v1");
    const wrapKey = await hkdfAesGcmKey(
      b64ToBytes(encap.shared_secret_b64),
      hkdfSalt,
      hkdfInfo,
      ["encrypt"]
    );

    const wrapIv = randomBytes(12);
    const wrapped = new Uint8Array(await crypto.subtle.encrypt(
      {
        name: "AES-GCM",
        iv: wrapIv,
        additionalData: aadBytes
      },
      wrapKey,
      rawCek
    ));

    return {
      purpose: "organization_recovery",
      mode: "mlkem768_hkdf_sha256_aes256gcm_v1",
      kem_alg: "ML-KEM-768",
      recipient_public_key_sha256: await sha256Hex(publicKeyBytes),
      kem_ciphertext_b64: encap.ciphertext_b64,
      hkdf_salt_b64: bytesToB64(hkdfSalt),
      hkdf_info_b64: bytesToB64(hkdfInfo),
      wrap_iv_b64: bytesToB64(wrapIv),
      wrapped_cek_b64: bytesToB64(wrapped)
    };
  }

  async function encryptFileToVaultPackage(file, passphrase, recoveryPublicKeyB64) {
    if (file.size > MAX_BROWSER_FILE_BYTES) {
      throw new Error(`${file.name} is too large for the browser MVP guard`);
    }

    const original = {
      name: file.name,
      type: file.type || "application/octet-stream",
      size: file.size,
      last_modified: file.lastModified || 0
    };

    const aadObject = {
      v: VAULT_PACKAGE_VERSION,
      app: VAULT_APP_ID,
      created_at: new Date().toISOString(),
      original
    };

    const aadJson = JSON.stringify(aadObject);
    const aadBytes = utf8ToBytes(aadJson);

    const cek = await crypto.subtle.generateKey(
      { name: "AES-GCM", length: 256 },
      true,
      ["encrypt", "decrypt"]
    );
    const rawCek = new Uint8Array(await crypto.subtle.exportKey("raw", cek));

    const payloadIv = randomBytes(12);
    const plaintext = new Uint8Array(await file.arrayBuffer());

    const ciphertext = new Uint8Array(await crypto.subtle.encrypt(
      {
        name: "AES-GCM",
        iv: payloadIv,
        additionalData: aadBytes
      },
      cek,
      plaintext
    ));

    const wrappedKeys = [
      await wrapCekForUser(rawCek, passphrase, aadBytes)
    ];

    const trimmedRecoveryKey = String(recoveryPublicKeyB64 || "").trim();
    if (trimmedRecoveryKey) {
      wrappedKeys.push(await wrapCekForOrganization(rawCek, trimmedRecoveryKey, aadBytes));
    }

    return {
      v: VAULT_PACKAGE_VERSION,
      app: VAULT_APP_ID,
      mode: "aes256gcm_file_v1",
      aad_b64: bytesToB64(aadBytes),
      original,
      payload: {
        enc_alg: "AES-256-GCM",
        iv_b64: bytesToB64(payloadIv),
        ciphertext_b64: bytesToB64(ciphertext)
      },
      wrapped_keys: wrappedKeys
    };
  }

  async function uploadVaultPackage(targetPath, pkg) {
    const body = new Blob([JSON.stringify(pkg)], { type: VAULT_MIME });
    const url = `/api/v4/files/put?path=${encodeURIComponent(targetPath)}&overwrite=0`;

    const r = await fetch(url, {
      method: "PUT",
      credentials: "include",
      cache: "no-store",
      headers: {
        "Content-Type": VAULT_MIME
      },
      body
    });

    let responseJson = null;
    try {
      responseJson = await r.json();
    } catch (_) {
    }

    if (!r.ok || !responseJson?.ok) {
      const msg = responseJson?.message || responseJson?.error || `upload failed with HTTP ${r.status}`;
      throw new Error(msg);
    }

    return responseJson;
  }

  function getPassphrasePair() {
    const passphrase = el.passphraseInput.value;
    const confirm = el.passphraseConfirmInput.value;

    if (!passphrase || passphrase.length < 12) {
      throw new Error("Use a Vault passphrase with at least 12 characters.");
    }

    if (passphrase !== confirm) {
      throw new Error("Passphrase confirmation does not match.");
    }

    return passphrase;
  }

  async function encryptAndUploadSelectedFiles() {
    if (!crypto?.subtle || !crypto?.getRandomValues) {
      throw new Error("This browser does not support the required Web Crypto APIs.");
    }

    if (!state.files.length) {
      throw new Error("Select at least one file.");
    }

    const folder = normalizeFolderPath(el.vaultFolderInput.value);
    const passphrase = getPassphrasePair();
    const recoveryPublicKey = el.recoveryPublicKeyInput.value.trim();

    el.encryptUploadBtn.disabled = true;
    try {
      for (let i = 0; i < state.files.length; i++) {
        const file = state.files[i];
        setText(el.encryptStatus, `Encrypting ${i + 1}/${state.files.length}: ${file.name}`);

        const pkg = await encryptFileToVaultPackage(file, passphrase, recoveryPublicKey);
        const targetName = `${safeFileName(file.name)}.dnavault.json`;
        const targetPath = `${folder}/${targetName}`;

        setText(el.encryptStatus, `Uploading encrypted package: ${targetName}`);
        await uploadVaultPackage(targetPath, pkg);

        setText(el.encryptStatus, `Uploaded encrypted package: ${targetPath}`);
      }
    } finally {
      el.encryptUploadBtn.disabled = false;
    }

    setText(el.encryptStatus, "Done. Files were encrypted before upload.");
  }

  async function unwrapCekWithUserPassphrase(pkg, passphrase) {
    const aadBytes = b64ToBytes(pkg.aad_b64);
    const userWrap = Array.isArray(pkg.wrapped_keys)
      ? pkg.wrapped_keys.find((k) => k && k.purpose === "user_passphrase")
      : null;

    if (!userWrap) throw new Error("No user passphrase wrapped key found in Vault package.");

    const salt = b64ToBytes(userWrap.kdf?.salt_b64);
    const key = await deriveUserWrapKey(passphrase, salt);

    return new Uint8Array(await crypto.subtle.decrypt(
      {
        name: "AES-GCM",
        iv: b64ToBytes(userWrap.wrap_iv_b64),
        additionalData: aadBytes
      },
      key,
      b64ToBytes(userWrap.wrapped_cek_b64)
    ));
  }

  async function decryptVaultPackage(pkg, passphrase) {
    if (!pkg || pkg.app !== VAULT_APP_ID || pkg.mode !== "aes256gcm_file_v1") {
      throw new Error("Not a supported DNA-Nexus Vault package.");
    }

    const rawCek = await unwrapCekWithUserPassphrase(pkg, passphrase);
    const cek = await crypto.subtle.importKey(
      "raw",
      rawCek,
      { name: "AES-GCM" },
      false,
      ["decrypt"]
    );

    const plaintext = await crypto.subtle.decrypt(
      {
        name: "AES-GCM",
        iv: b64ToBytes(pkg.payload?.iv_b64),
        additionalData: b64ToBytes(pkg.aad_b64)
      },
      cek,
      b64ToBytes(pkg.payload?.ciphertext_b64)
    );

    return {
      bytes: new Uint8Array(plaintext),
      name: safeFileName(pkg.original?.name || "vault-file"),
      type: pkg.original?.type || "application/octet-stream"
    };
  }

  async function decryptSelectedVaultFile() {
    const file = el.decryptFileInput.files && el.decryptFileInput.files[0];
    if (!file) throw new Error("Select a Vault package first.");

    const passphrase = el.decryptPassphraseInput.value;
    if (!passphrase) throw new Error("Enter the Vault passphrase.");

    el.decryptBtn.disabled = true;
    el.downloadSlot.replaceChildren();

    try {
      setText(el.decryptStatus, "Reading encrypted package…");
      const pkg = JSON.parse(await file.text());

      setText(el.decryptStatus, "Decrypting locally…");
      const decrypted = await decryptVaultPackage(pkg, passphrase);

      const blob = new Blob([decrypted.bytes], { type: decrypted.type });
      const url = URL.createObjectURL(blob);

      const link = document.createElement("a");
      link.href = url;
      link.download = decrypted.name;
      link.textContent = `Download decrypted file: ${decrypted.name}`;

      el.downloadSlot.append(link);
      setText(el.decryptStatus, "Decryption complete. The server was not involved.");
    } finally {
      el.decryptBtn.disabled = false;
    }
  }

  function wireEvents() {
    el.fileInput.addEventListener("change", () => addFiles(el.fileInput.files));

    el.dropZone.addEventListener("dragover", (ev) => {
      ev.preventDefault();
      el.dropZone.classList.add("dragging");
    });

    el.dropZone.addEventListener("dragleave", () => {
      el.dropZone.classList.remove("dragging");
    });

    el.dropZone.addEventListener("drop", (ev) => {
      ev.preventDefault();
      el.dropZone.classList.remove("dragging");
      addFiles(ev.dataTransfer?.files);
    });

    el.encryptUploadBtn.addEventListener("click", async () => {
      try {
        await encryptAndUploadSelectedFiles();
      } catch (err) {
        setText(el.encryptStatus, err && err.message ? err.message : "Encryption/upload failed.");
      }
    });

    el.clearBtn.addEventListener("click", () => {
      state.files = [];
      el.fileInput.value = "";
      el.passphraseInput.value = "";
      el.passphraseConfirmInput.value = "";
      renderQueue();
      setText(el.encryptStatus, "Ready.");
    });

    el.decryptBtn.addEventListener("click", async () => {
      try {
        await decryptSelectedVaultFile();
      } catch (err) {
        setText(el.decryptStatus, err && err.message ? err.message : "Decrypt failed.");
      }
    });
  }

  async function init() {
    const ver = await getAppVersion();
    if (ver) setText(el.appVersion, `v${ver}`);

    renderQueue();
    wireEvents();
    await checkSession();
  }

  init().catch((err) => {
    setText(el.encryptStatus, err && err.message ? err.message : "Vault init failed.");
  });
})();
