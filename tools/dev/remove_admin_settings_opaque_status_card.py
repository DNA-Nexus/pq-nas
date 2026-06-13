#!/usr/bin/env python3
from pathlib import Path
import re
import sys

html = Path("server/src/static/admin_settings.html")
js = Path("server/src/static/admin_settings.js")

for p in (html, js):
    if not p.exists():
        print(f"ERROR: missing {p}", file=sys.stderr)
        sys.exit(1)

h = html.read_text()

# Remove the full "Security • OPAQUE Status" accordion card.
pattern = re.compile(
    r'\n\s*<div class="card accordion" data-acc="settings">\s*'
    r'\n\s*<div class="hd" role="button" tabindex="0" aria-expanded="false">\s*'
    r'\n\s*<div class="h">Security • OPAQUE Status</div>'
    r'.*?'
    r'\n\s*</div>\s*'
    r'\n\s*(?=<div class="card accordion open" data-acc="settings">)',
    re.S
)

h2, n = pattern.subn("\n\n            ", h, count=1)
if n == 0:
    if "Security • OPAQUE Status" in h:
        print("ERROR: OPAQUE status card text found, but block regex did not match", file=sys.stderr)
        sys.exit(1)
    print("unchanged: OPAQUE status card already removed from admin_settings.html")
else:
    h = h2
    print("removed OPAQUE status card from admin_settings.html")

h = re.sub(
    r'/static/admin_settings\.js\?v=[^"]+',
    '/static/admin_settings.js?v=20260613-remove-opaque-status-card-1',
    h
)

html.write_text(h)

s = js.read_text()

# Remove DOM refs for the old card.
old_dom = '''    // --- OPAQUE status ---
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

'''
if old_dom in s:
    s = s.replace(old_dom, "", 1)
    print("removed OPAQUE status DOM refs from admin_settings.js")
else:
    print("unchanged: OPAQUE DOM refs already removed or changed")

# Remove OPAQUE status helper/render functions.
func_pattern = re.compile(
    r'\n\s*function setOpaqueLight\(el, kind\) \{.*?'
    r'\n\s*async function refreshOpaqueStatus\(\) \{.*?\n\s*\}\s*'
    r'\n\s*// ---------------------------\s*'
    r'\n\s*// System Backups\s*'
    r'\n\s*// ---------------------------',
    re.S
)

s2, n = func_pattern.subn(
    "\n\n    // ---------------------------\n    // System Backups\n    // ---------------------------",
    s,
    count=1
)
if n == 0:
    if "refreshOpaqueStatus" in s:
        print("ERROR: refreshOpaqueStatus still found, but function block regex did not match", file=sys.stderr)
        sys.exit(1)
    print("unchanged: OPAQUE status functions already removed")
else:
    s = s2
    print("removed OPAQUE status functions from admin_settings.js")

# Remove old event wiring.
old_wire = '''    // ---------------------------
    // Wire OPAQUE status
    // ---------------------------
    btnOpaqueStatusReload?.addEventListener("click", (ev) => {
        ev.preventDefault();
        refreshOpaqueStatus();
    });

'''
if old_wire in s:
    s = s.replace(old_wire, "", 1)
    print("removed OPAQUE status event wiring from admin_settings.js")
else:
    print("unchanged: OPAQUE status event wiring already removed")

js.write_text(s)

# Remove obsolete patch script if it exists.
old_patch = Path("tools/dev/patch_admin_settings_opaque_status_card.py")
if old_patch.exists():
    old_patch.unlink()
    print(f"removed obsolete {old_patch}")

print("done")
