#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]

def die(msg: str) -> None:
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)

def p(rel: str) -> Path:
    return ROOT / rel

def read(rel: str) -> str:
    path = p(rel)
    if not path.exists():
        die(f"missing file: {rel}")
    return path.read_text(encoding="utf-8")

def write(rel: str, text: str) -> None:
    p(rel).write_text(text, encoding="utf-8")
    print(f"patched: {rel}")

def replace_once(rel: str, old: str, new: str) -> None:
    text = read(rel)
    if new in text:
        print(f"unchanged: {rel}")
        return
    if old not in text:
        die(f"anchor not found in {rel}: {old!r}")
    write(rel, text.replace(old, new, 1))

# 1) Add OPAQUE status card after password card, before audit verbosity.
replace_once(
    "server/src/static/admin_settings.html",
    """            <div class="card accordion open" data-acc="settings">
                <div class="hd" role="button" tabindex="0" aria-expanded="true">
                    <div class="h"><span data-i18n="admin.audit.verbosity">Audit verbosity</span></div>
""",
    """            <div class="card accordion" data-acc="settings">
                <div class="hd" role="button" tabindex="0" aria-expanded="false">
                    <div class="h">Security • OPAQUE Status</div>
                    <div class="pill warn" id="opaqueStatusPill">
                        <span class="k">Status:</span> <span class="v">loading…</span>
                    </div>
                    <span class="chev" aria-hidden="true">▸</span>
                </div>

                <div class="bd">
                    <div class="row">
                        <button id="btnOpaqueStatusReload" class="pq-btn primary" type="button">Refresh OPAQUE status</button>
                    </div>

                    <div class="note">
                        Admin-only diagnostics for the OPAQUE login backend. This does not enable OPAQUE login; public OPAQUE endpoints remain fail-closed until real OPAQUE crypto and session integration are implemented.
                    </div>

                    <div class="statusGrid">
                        <div class="statusTile">
                            <div class="label"><span id="opaqueReadyLight" class="lightDot warn"></span><span>Login readiness</span></div>
                            <div id="opaqueReadyValue" class="value">—</div>
                        </div>

                        <div class="statusTile">
                            <div class="label"><span id="opaqueHelperLight" class="lightDot warn"></span><span>Helper preflight</span></div>
                            <div id="opaqueHelperValue" class="value">—</div>
                        </div>

                        <div class="statusTile">
                            <div class="label"><span id="opaqueCredentialsLight" class="lightDot warn"></span><span>Credentials file</span></div>
                            <div id="opaqueCredentialsValue" class="value">—</div>
                        </div>

                        <div class="statusTile">
                            <div class="label"><span id="opaqueServerSetupLight" class="lightDot warn"></span><span>Server setup file</span></div>
                            <div id="opaqueServerSetupValue" class="value">—</div>
                        </div>

                        <div class="statusTile">
                            <div class="label"><span class="lightDot info"></span><span>Resolved paths</span></div>
                            <div id="opaquePathsValue" class="value">—</div>
                        </div>

                        <div class="statusTile">
                            <div class="label"><span class="lightDot warn"></span><span>Missing / not ready</span></div>
                            <div id="opaqueMissingValue" class="value">—</div>
                        </div>
                    </div>
                </div>
            </div>

            <div class="card accordion open" data-acc="settings">
                <div class="hd" role="button" tabindex="0" aria-expanded="true">
                    <div class="h"><span data-i18n="admin.audit.verbosity">Audit verbosity</span></div>
""",
)

# 2) Bump JS cache version.
replace_once(
    "server/src/static/admin_settings.html",
    """<script src="/static/admin_settings.js?v=20260526-nodus-i18n-1"></script>
""",
    """<script src="/static/admin_settings.js?v=20260613-opaque-status-1"></script>
""",
)

# 3) Add DOM handles.
replace_once(
    "server/src/static/admin_settings.js",
    """    // --- System Backups ---
    const systemBackupPill = $("systemBackupPill");
""",
    """    // --- OPAQUE status ---
    const opaqueStatusPill = $("opaqueStatusPill");
    const btnOpaqueStatusReload = $("btnOpaqueStatusReload");
    const opaqueReadyLight = $("opaqueReadyLight");
    const opaqueReadyValue = $("opaqueReadyValue");
    const opaqueHelperLight = $("opaqueHelperLight");
    const opaqueHelperValue = $("opaqueHelperValue");
    const opaqueCredentialsLight = $("opaqueCredentialsLight");
    const opaqueCredentialsValue = $("opaqueCredentialsValue");
    const opaqueServerSetupLight = $("opaqueServerSetupLight");
    const opaqueServerSetupValue = $("opaqueServerSetupValue");
    const opaquePathsValue = $("opaquePathsValue");
    const opaqueMissingValue = $("opaqueMissingValue");

    // --- System Backups ---
    const systemBackupPill = $("systemBackupPill");
""",
)

# 4) Add OPAQUE status functions before System Backups section.
replace_once(
    "server/src/static/admin_settings.js",
    """    // ---------------------------
    // System Backups
    // ---------------------------
""",
    """    // ---------------------------
    // OPAQUE backend status
    // ---------------------------
    async function apiOpaqueStatus() {
        return await fetchJsonOrThrow("/api/admin/auth/opaque/status", {
            credentials: "include",
            cache: "no-store"
        });
    }

    function setOpaqueStatusPill(kind, text) {
        if (!opaqueStatusPill) return;
        opaqueStatusPill.className = "pill " + (kind || "");
        opaqueStatusPill.innerHTML = `<span class="k">${escapeHtml(adminLabel("status"))}:</span> <span class="v">${escapeHtml(text || "—")}</span>`;
    }

    function setOpaqueLight(el, kind) {
        if (!el) return;
        el.className = "lightDot " + (kind || "warn");
    }

    function yesNo(v) {
        return v ? "yes" : "no";
    }

    function pathLine(label, value) {
        return `${label}: ${value || "—"}`;
    }

    function renderOpaqueStatus(j) {
        if (!j || j.ok !== true) {
            setOpaqueStatusPill("fail", "error");
            return;
        }

        const ready = !!j.ready_for_login;

        const helperOk =
            !!j.helper_exists &&
            !!j.helper_executable &&
            !!j.helper_version_ok &&
            !!j.helper_self_test_ok;

        const credsOk = !!j.credentials_file_exists && !!j.credentials_file_readable;
        const setupOk = !!j.server_setup_file_exists && !!j.server_setup_file_readable;

        setOpaqueStatusPill(
            ready ? "ok" : (helperOk ? "warn" : "fail"),
            ready ? "ready" : (helperOk ? "helper ok • login disabled" : "needs attention")
        );

        setOpaqueLight(opaqueReadyLight, ready ? "ok" : "warn");
        if (opaqueReadyValue) {
            opaqueReadyValue.textContent = ready
                ? "ready_for_login=true"
                : "ready_for_login=false • fail-closed";
        }

        setOpaqueLight(opaqueHelperLight, helperOk ? "ok" : "fail");
        if (opaqueHelperValue) {
            const probe = String(j.helper_probe_error || "").trim();
            opaqueHelperValue.textContent =
                `exists=${yesNo(j.helper_exists)} • executable=${yesNo(j.helper_executable)} • version=${yesNo(j.helper_version_ok)} • self-test=${yesNo(j.helper_self_test_ok)}${probe ? " • " + probe : ""}`;
        }

        setOpaqueLight(opaqueCredentialsLight, credsOk ? "ok" : "warn");
        if (opaqueCredentialsValue) {
            opaqueCredentialsValue.textContent =
                `exists=${yesNo(j.credentials_file_exists)} • readable=${yesNo(j.credentials_file_readable)}`;
        }

        setOpaqueLight(opaqueServerSetupLight, setupOk ? "ok" : "warn");
        if (opaqueServerSetupValue) {
            opaqueServerSetupValue.textContent =
                `exists=${yesNo(j.server_setup_file_exists)} • readable=${yesNo(j.server_setup_file_readable)}`;
        }

        if (opaquePathsValue) {
            opaquePathsValue.textContent = [
                pathLine("credentials", j.credentials_path),
                pathLine("server_setup", j.server_setup_path),
                pathLine("helper", j.helper_path)
            ].join("\\n");
        }

        if (opaqueMissingValue) {
            const missing = Array.isArray(j.missing_or_not_ready) ? j.missing_or_not_ready : [];
            opaqueMissingValue.textContent = missing.length ? missing.join("\\n") : "—";
        }
    }

    async function refreshOpaqueStatus() {
        setOpaqueStatusPill("warn", "loading…");

        try {
            const j = await apiOpaqueStatus();
            renderOpaqueStatus(j);
        } catch (e) {
            console.error(e);
            setOpaqueStatusPill("fail", "error");
            setOpaqueLight(opaqueReadyLight, "fail");
            setOpaqueLight(opaqueHelperLight, "fail");
            setOpaqueLight(opaqueCredentialsLight, "fail");
            setOpaqueLight(opaqueServerSetupLight, "fail");
            if (opaqueReadyValue) opaqueReadyValue.textContent = String(e.message || e);
            showToast("fail", "OPAQUE status failed", String(e.message || e));
        }
    }

    // ---------------------------
    // System Backups
    // ---------------------------
""",
)

# 5) Include OPAQUE status in refreshAll.
replace_once(
    "server/src/static/admin_settings.js",
    """            // Nodus federation
            await refreshNodusStatus();

            // System Backups
""",
    """            // Nodus federation
            await refreshNodusStatus();

            // OPAQUE backend status
            await refreshOpaqueStatus();

            // System Backups
""",
)

# 6) Wire manual refresh button.
replace_once(
    "server/src/static/admin_settings.js",
    """    // ---------------------------
    // Wire System Backups
    // ---------------------------
""",
    """    // ---------------------------
    // Wire OPAQUE status
    // ---------------------------
    btnOpaqueStatusReload?.addEventListener("click", (ev) => {
        ev.preventDefault();
        refreshOpaqueStatus();
    });

    // ---------------------------
    // Wire System Backups
    // ---------------------------
""",
)

# 7) Doc update.
replace_once(
    "docs/technical/opaque_login_design.md",
    """- Admin-only OPAQUE diagnostics include resolved credentials/setup/helper paths for troubleshooting.
""",
    """- Admin-only OPAQUE diagnostics include resolved credentials/setup/helper paths for troubleshooting.
- Admin Settings UI includes an OPAQUE Status card that displays the admin-only backend diagnostics without enabling public OPAQUE login.
""",
)

print("done")
