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
    encryptUploadBtn: document.getElementById("encryptUploadBtn"),
    clearBtn: document.getElementById("clearBtn"),
    encryptStatus: document.getElementById("encryptStatus"),
    queueList: document.getElementById("queueList"),
    decryptFileInput: document.getElementById("decryptFileInput"),
    decryptPassphraseInput: document.getElementById("decryptPassphraseInput"),
    recoveryPrivateKeyInput: document.getElementById("recoveryPrivateKeyInput"),
    decryptBtn: document.getElementById("decryptBtn"),
    organizationRecoverBtn: document.getElementById("organizationRecoverBtn"),
    decryptStatus: document.getElementById("decryptStatus"),
    downloadSlot: document.getElementById("downloadSlot")
  };

  function tr(key, vars, fallback) {
    const i18n = window.PQNAS_I18N;
    if (i18n && typeof i18n.t === "function") {
      return i18n.t(key, vars, fallback);
    }

    let out = String(fallback ?? key ?? "");
    if (vars && typeof vars === "object") {
      for (const [name, value] of Object.entries(vars)) {
        out = out.replaceAll(`{${name}}`, String(value ?? ""));
      }
    }
    return out;
  }

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
        setText(el.sessionBadge, tr("vault.session.not_signed_in", null, "Not signed in"));
        el.sessionBadge.className = "pq-badge err";
        return;
      }

      if (r.status === 403) {
        setText(el.sessionBadge, tr("vault.session.account_not_allowed", null, "Account not allowed"));
        el.sessionBadge.className = "pq-badge warn";
        return;
      }

      if (!r.ok) {
        setText(el.sessionBadge, tr("vault.session.check_failed", null, "Session check failed"));
        el.sessionBadge.className = "pq-badge warn";
        return;
      }

      const j = await r.json();
      const role = j && j.role ? String(j.role) : "user";
      setText(el.sessionBadge, `Signed in • ${role}`);
      el.sessionBadge.className = "pq-badge ok";
    } catch (_) {
      setText(el.sessionBadge, tr("vault.session.offline", null, "Offline or unavailable"));
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
      title.textContent = tr("vault.queue.no_files_selected", null, "No files selected.");
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

  let lastDecryptObjectUrl = "";

  function zeroBytes(bytes) {
    // Security: reduce lifetime of raw CEK/plaintext/shared-secret buffers.
    // JS strings and WebCrypto CryptoKeys cannot be reliably zeroed, but
    // Uint8Array buffers we own should be wiped as soon as they are no longer needed.
    if (bytes && typeof bytes.fill === "function") {
      try {
        bytes.fill(0);
      } catch {
        // Best-effort memory hygiene only.
      }
    }
  }

  function revokeLastDecryptObjectUrl() {
    if (!lastDecryptObjectUrl) return;
    URL.revokeObjectURL(lastDecryptObjectUrl);
    lastDecryptObjectUrl = "";
  }

  function clearUploadSensitiveInputs() {
    if (el.passphraseInput) el.passphraseInput.value = "";
    if (el.passphraseConfirmInput) el.passphraseConfirmInput.value = "";
  }

  function clearAdvancedImportSensitiveInputs({ clearFile = false, clearDownload = false } = {}) {
    if (el.decryptPassphraseInput) el.decryptPassphraseInput.value = "";
    if (clearFile && el.decryptFileInput) el.decryptFileInput.value = "";
    if (clearFile && el.recoveryPrivateKeyInput) el.recoveryPrivateKeyInput.value = "";
    if (clearDownload && el.downloadSlot) {
      revokeLastDecryptObjectUrl();
      el.downloadSlot.replaceChildren();
    }
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

  function shortRecoveryKeyId(id) {
    const s = String(id || "");
    if (!s) return "";
    if (s.length <= 24) return s;
    return `${s.slice(0, 16)}…${s.slice(-8)}`;
  }

  async function loadMasterRecoveryPublicKey() {
    let res;
    let data = null;

    try {
      res = await fetch("/api/v4/vault/recovery-public-key", {
        method: "GET",
        cache: "no-store",
        credentials: "include"
      });
      data = await res.json();
    } catch {
      // Security: fail closed. Do not silently create a Vault package without
      // checking whether this user's Master recovery must be applied.
      throw new Error("Could not check Master recovery settings");
    }

    if (!res.ok || data?.ok === false) {
      throw new Error(data?.message || data?.error || "Could not load Master recovery settings");
    }

    const enabled = !!data.enabled && data.status === "active";
    const publicKeyB64 = String(data.public_key_b64 || "").trim();
    const recoveryKeyId = String(data.recovery_key_id || "").trim();

    if (!enabled || !publicKeyB64) {
      return null;
    }

    return {
      publicKeyB64,
      recoveryKeyId
    };
  }

  async function wrapCekForMasterRecovery(rawCek, publicKeyB64, aadBytes) {
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
    const sharedSecretBytes = encap.shared_secret_bytes;

    if (!(sharedSecretBytes instanceof Uint8Array)) {
      throw new Error("ML-KEM helper returned no wipeable shared secret bytes");
    }

    let wrapKey;
    try {
      wrapKey = await hkdfAesGcmKey(
        sharedSecretBytes,
        hkdfSalt,
        hkdfInfo,
        ["encrypt"]
      );
    } finally {
      zeroBytes(sharedSecretBytes);
    }

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
      purpose: "master_recovery",
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

    zeroBytes(plaintext);

    const wrappedKeys = [
      await wrapCekForUser(rawCek, passphrase, aadBytes)
    ];

    const trimmedRecoveryKey = String(recoveryPublicKeyB64 || "").trim();
    if (trimmedRecoveryKey) {
      wrappedKeys.push(await wrapCekForMasterRecovery(rawCek, trimmedRecoveryKey, aadBytes));
    }

    zeroBytes(rawCek);

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

    let masterRecovery = null;
    let recoveryPublicKey = "";

    el.encryptUploadBtn.disabled = true;
    try {
      setText(el.encryptStatus, tr("vault.status.checking_master_recovery", null, "Checking Master recovery…"));

      masterRecovery = await loadMasterRecoveryPublicKey();
      recoveryPublicKey = masterRecovery?.publicKeyB64 || "";

      const recoveryPrefix = masterRecovery
        ? `Master recovery active (${shortRecoveryKeyId(masterRecovery.recoveryKeyId)}).`
         : tr("vault.status.master_recovery_not_configured", null, "Master recovery not configured.");

      for (let i = 0; i < state.files.length; i++) {
        const file = state.files[i];
        setText(el.encryptStatus, `${recoveryPrefix} Encrypting ${i + 1}/${state.files.length}: ${file.name}`);

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

    setText(el.encryptStatus, tr("vault.status.upload_done", null, "Done. Files were encrypted before upload."));

    try {
      window.dispatchEvent(new CustomEvent("pqnas:vault-storage-changed"));
    } catch (_) {
      // Non-fatal UI refresh hint only. The upload itself has already succeeded.
    }
  }

  async function ensureMlKemHelper() {
    if (globalThis.PqShareMlKemV1?.decapsulate768) {
      return globalThis.PqShareMlKemV1;
    }

    await new Promise((resolve, reject) => {
      const script = document.createElement("script");
      script.src = "/static/share_pq_mlkem.js?v=20260706-vault-org-recovery-ui-1";
      script.onload = resolve;
      script.onerror = () => reject(new Error("Could not load ML-KEM recovery helper"));
      document.head.appendChild(script);
    });

    if (!globalThis.PqShareMlKemV1?.decapsulate768) {
      throw new Error("ML-KEM recovery helper is not available");
    }

    return globalThis.PqShareMlKemV1;
  }

  async function readMasterRecoveryPrivateKey(file) {
    if (!file) {
      throw new Error("Select the Master recovery private key JSON.");
    }

    const text = await file.text();
    let parsed = null;

    try {
      parsed = JSON.parse(text);
    } catch (_) {
      parsed = null;
    }

    const key = parsed?.private_key_b64 || text.trim();

    if (!key) {
      throw new Error("Recovery private key is empty.");
    }

    return String(key).trim();
  }

  async function unwrapCekWithMasterRecovery(pkg, privateKeyB64) {
    const aadBytes = b64ToBytes(pkg.aad_b64);
    const orgWrap = Array.isArray(pkg.wrapped_keys)
      ? pkg.wrapped_keys.find((k) => k && k.purpose === "master_recovery")
      : null;

    if (!orgWrap) {
      throw new Error("No Master recovery wrapped key found in Vault package.");
    }

    const mlkem = await ensureMlKemHelper();

    const ciphertextB64 = String(orgWrap.kem_ciphertext_b64 || "").trim();
    const hkdfSaltB64 = String(orgWrap.hkdf_salt_b64 || "").trim();
    const hkdfInfoB64 = String(orgWrap.hkdf_info_b64 || "").trim();
    const wrapIvB64 = String(orgWrap.wrap_iv_b64 || "").trim();
    const wrappedCekB64 = String(orgWrap.wrapped_cek_b64 || "").trim();

    if (!ciphertextB64) throw new Error("Master recovery wrap is missing ML-KEM ciphertext.");
    if (!hkdfSaltB64) throw new Error("Master recovery wrap is missing HKDF salt.");
    if (!wrapIvB64) throw new Error("Master recovery wrap is missing wrap IV.");
    if (!wrappedCekB64) throw new Error("Master recovery wrap is missing wrapped file key.");

    let sharedSecret = null;

    try {
      sharedSecret = await mlkem.decapsulate768({
        privateKeyB64,
        ciphertextB64
      });

      const hkdfKey = await crypto.subtle.importKey(
        "raw",
        sharedSecret,
        "HKDF",
        false,
        ["deriveKey"]
      );

      const wrapKey = await crypto.subtle.deriveKey(
        {
          name: "HKDF",
          hash: "SHA-256",
          salt: b64ToBytes(hkdfSaltB64),
          info: hkdfInfoB64
            ? b64ToBytes(hkdfInfoB64)
            : utf8ToBytes("pqnas-vault-mlkem768-recovery-wrap-v1")
        },
        hkdfKey,
        { name: "AES-GCM", length: 256 },
        false,
        ["decrypt"]
      );

      const rawCek = new Uint8Array(await crypto.subtle.decrypt(
        {
          name: "AES-GCM",
          iv: b64ToBytes(wrapIvB64),
          additionalData: aadBytes
        },
        wrapKey,
        b64ToBytes(wrappedCekB64)
      ));

      if (rawCek.length !== 32) {
        zeroBytes(rawCek);
        throw new Error("Master recovery returned an invalid file key.");
      }

      return rawCek;
    } finally {
      // Security: wipe the ML-KEM shared secret after deriving the CEK wrap key.
      zeroBytes(sharedSecret);
    }
  }

  async function decryptVaultPackageWithCek(pkg, rawCek) {
    if (!pkg || pkg.app !== VAULT_APP_ID || pkg.mode !== "aes256gcm_file_v1") {
      throw new Error("Not a supported DNA-Nexus Vault package.");
    }

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
    const rawCek = await unwrapCekWithUserPassphrase(pkg, passphrase);

    try {
      return await decryptVaultPackageWithCek(pkg, rawCek);
    } finally {
      // Security: wipe the passphrase-unwrapped CEK after decrypting the payload.
      zeroBytes(rawCek);
    }
  }

  async function decryptVaultPackageWithMasterRecovery(pkg, privateKeyB64) {
    const rawCek = await unwrapCekWithMasterRecovery(pkg, privateKeyB64);

    try {
      return await decryptVaultPackageWithCek(pkg, rawCek);
    } finally {
      // Security: wipe the organization-recovered CEK after decrypting the payload.
      zeroBytes(rawCek);
    }
  }

  function publishRecoveredDownload(decrypted, labelPrefix = "Download decrypted file") {
    const blob = new Blob([decrypted.bytes], { type: decrypted.type });
    zeroBytes(decrypted.bytes);

    revokeLastDecryptObjectUrl();
    const url = URL.createObjectURL(blob);
    lastDecryptObjectUrl = url;

    const link = document.createElement("a");
    link.href = url;
    link.download = decrypted.name;
    link.textContent = `${labelPrefix}: ${decrypted.name}`;

    el.downloadSlot.append(link);
  }

  async function decryptSelectedVaultFile() {
    const file = el.decryptFileInput.files && el.decryptFileInput.files[0];
    if (!file) throw new Error("Select a Vault package first.");

    const passphrase = el.decryptPassphraseInput.value;
    if (!passphrase) throw new Error("Enter the Vault passphrase.");

    el.decryptBtn.disabled = true;
    el.downloadSlot.replaceChildren();

    try {
      setText(el.decryptStatus, tr("vault.status.reading_package", null, "Reading encrypted package…"));
      const pkg = JSON.parse(await file.text());

      setText(el.decryptStatus, tr("vault.status.decrypting_locally", null, "Decrypting locally…"));
      const decrypted = await decryptVaultPackage(pkg, passphrase);

      publishRecoveredDownload(decrypted, tr("vault.download_decrypted_file", null, "Download decrypted file"));
      setText(el.decryptStatus, tr("vault.status.decryption_complete", null, "Decryption complete. The server was not involved."));
    } finally {
      clearAdvancedImportSensitiveInputs({ clearFile: true });
      el.decryptBtn.disabled = false;
    }
  }

  async function recoverSelectedVaultFileWithMasterRecoveryKey() {
    const file = el.decryptFileInput.files && el.decryptFileInput.files[0];
    if (!file) throw new Error("Select a Vault package first.");

    const keyFile = el.recoveryPrivateKeyInput?.files && el.recoveryPrivateKeyInput.files[0];
    if (!keyFile) throw new Error("Select the Master recovery private key JSON.");

    el.organizationRecoverBtn.disabled = true;
    el.downloadSlot.replaceChildren();

    try {
      setText(el.decryptStatus, tr("vault.status.reading_package_and_key", null, "Reading encrypted package and recovery key…"));
      const pkg = JSON.parse(await file.text());
      const privateKeyB64 = await readMasterRecoveryPrivateKey(keyFile);

      setText(el.decryptStatus, tr("vault.status.recovering_locally", null, "Recovering locally with organization private key…"));
      const decrypted = await decryptVaultPackageWithMasterRecovery(pkg, privateKeyB64);

      publishRecoveredDownload(decrypted, tr("vault.download_recovered_file", null, "Download recovered file"));
      setText(el.decryptStatus, tr("vault.status.master_recovery_complete", null, "Master recovery complete. The server was not involved."));
    } finally {
      clearAdvancedImportSensitiveInputs({ clearFile: true });
      el.organizationRecoverBtn.disabled = false;
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
      } finally {
        clearUploadSensitiveInputs();
      }
    });

    el.clearBtn.addEventListener("click", () => {
      state.files = [];
      el.fileInput.value = "";
      clearUploadSensitiveInputs();
      renderQueue();
      setText(el.encryptStatus, tr("vault.ready_dot", null, "Ready."));
    });

    el.decryptBtn.addEventListener("click", async () => {
      try {
        await decryptSelectedVaultFile();
      } catch (err) {
        clearAdvancedImportSensitiveInputs();
        setText(el.decryptStatus, err && err.message ? err.message : "Decrypt failed.");
      }
    });

    el.organizationRecoverBtn?.addEventListener("click", async () => {
      try {
        await recoverSelectedVaultFileWithMasterRecoveryKey();
      } catch (err) {
        clearAdvancedImportSensitiveInputs();
        setText(el.decryptStatus, err && err.message ? err.message : "Master recovery failed.");
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

  async function startWhenI18nReady() {
    const i18n = window.PQNAS_I18N;
    if (i18n && typeof i18n.ready === "function") {
      await i18n.ready();
    }

    await init();
  }

  startWhenI18nReady().catch((err) => {
    setText(el.encryptStatus, err && err.message ? err.message : tr("vault.init_failed", null, "Vault init failed."));
  });
})();
