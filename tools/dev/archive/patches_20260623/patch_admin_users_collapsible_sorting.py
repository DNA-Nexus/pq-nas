#!/usr/bin/env python3
from pathlib import Path
import json
import re
import sys

html = Path("server/src/static/admin_users.html")
js = Path("server/src/static/admin_users.js")
i18n_dir = Path("server/src/static/i18n")

for p in (html, js, i18n_dir):
    if not p.exists():
        print(f"ERROR: missing {p}", file=sys.stderr)
        sys.exit(1)

# ----------------------------------------------------------------------
# HTML/CSS: collapsible editor card + sortable table headers.
# ----------------------------------------------------------------------
h = html.read_text()

if ".profileEditorHead" not in h:
    css = r'''
        .profileEditorHead{
            display:flex;
            align-items:center;
            justify-content:space-between;
            gap:12px;
            margin-bottom:10px;
        }

        .profileEditorHead h2{
            margin-bottom:0;
        }

        .profileEditorToggle{
            border:1px solid rgba(var(--fg-rgb),0.18);
            border-radius:12px;
            padding:8px 12px;
            background:rgba(0,0,0,0.12);
            color:rgba(var(--fg-rgb),0.92);
            font-weight:850;
            cursor:pointer;
        }

        .profileEditorToggle:hover{
            background:rgba(var(--fg-rgb),0.06);
        }

        .profileEditorBody.collapsed{
            display:none;
        }

        .sortTh{
            width:100%;
            display:inline-flex;
            align-items:center;
            justify-content:flex-start;
            gap:6px;
            border:0;
            padding:0;
            margin:0;
            background:transparent;
            color:inherit;
            font:inherit;
            font-weight:900;
            letter-spacing:inherit;
            text-transform:inherit;
            text-align:left;
            cursor:pointer;
        }

        .sortTh:hover{
            color:rgba(var(--fg-rgb),0.95);
            text-decoration:underline;
            text-underline-offset:3px;
        }

        .sortTh .sortIcon{
            min-width:1.1em;
            opacity:.55;
            font-size:10px;
            line-height:1;
        }

        .sortTh.active .sortIcon{
            opacity:1;
        }
'''
    h = h.replace("    </style>", css + "\n    </style>", 1)
    print("patched admin_users.html: added collapsible/sort CSS")
else:
    print("unchanged: admin user CSS already present")

if 'id="profileEditorToggle"' not in h:
    pattern = re.compile(
        r'(?s)(?P<indent>\s*)<section class="pane" aria-label="Controls" data-i18n-aria-label="admin\.users\.controls">(?P<body>.*?)\n(?P=indent)</section>'
    )
    m = pattern.search(h)
    if not m:
        print("ERROR: controls section not found", file=sys.stderr)
        sys.exit(1)

    indent = m.group("indent")
    body = m.group("body")

    h2 = re.search(r'(?s)\n\s*<h2 data-i18n="admin\.users\.edit_profile">.*?</h2>\s*', body)
    if not h2:
        print("ERROR: edit_profile h2 not found", file=sys.stderr)
        sys.exit(1)

    rest = body[h2.end():]

    new_section = f'''{indent}<section class="pane profileEditorPane" id="profileEditorPane" aria-label="Controls" data-i18n-aria-label="admin.users.controls">
                <div class="profileEditorHead">
                    <h2 data-i18n="admin.users.edit_profile">Edit profile</h2>
                    <button class="profileEditorToggle" id="profileEditorToggle" type="button" aria-expanded="false" data-i18n="admin.users.show_editor">Show editor</button>
                </div>

                <div class="profileEditorBody collapsed" id="profileEditorBody">{rest}
                </div>
{indent}</section>'''

    h = h[:m.start()] + new_section + h[m.end():]
    print("patched admin_users.html: editor card collapsed by default")
else:
    print("unchanged: editor toggle already present")

def sort_th(width, key, i18n, text):
    return (
        f'<th style="width:{width};">'
        f'<button class="sortTh" type="button" data-sort="{key}" aria-label="Sort by {text}">'
        f'<span data-i18n="{i18n}">{text}</span>'
        f'<span class="sortIcon" aria-hidden="true">↕</span>'
        f'</button></th>'
    )

if 'class="sortTh"' not in h:
    replacements = {
        '<th style="width:28%;" data-i18n="admin.users.fingerprint">Fingerprint</th>':
            sort_th("28%", "fingerprint", "admin.users.fingerprint", "Fingerprint"),
        '<th style="width:22%;" data-i18n="admin.users.name_notes">Name / Notes</th>':
            sort_th("22%", "name", "admin.users.name_notes", "Name / Notes"),
        '<th style="width:6%;" data-i18n="admin.users.role">Role</th>':
            sort_th("6%", "role", "admin.users.role", "Role"),
        '<th style="width:7%;" data-i18n="admin.users.status">Status</th>':
            sort_th("7%", "status", "admin.users.status", "Status"),
        '<th style="width:8%;" data-i18n="admin.users.group">Group</th>':
            sort_th("8%", "group", "admin.users.group", "Group"),
        '<th style="width:7%;" data-i18n="admin.users.storage">Storage</th>':
            sort_th("7%", "storage", "admin.users.storage", "Storage"),
        '<th style="width:6%;" data-i18n="admin.users.quota">Quota</th>':
            sort_th("6%", "quota", "admin.users.quota", "Quota"),
        '<th style="width:12%;" data-i18n="admin.users.added">Added</th>':
            sort_th("12%", "added", "admin.users.added", "Added"),
    }

    for old, new in replacements.items():
        if old not in h:
            print(f"ERROR: header anchor not found: {old}", file=sys.stderr)
            sys.exit(1)
        h = h.replace(old, new, 1)

    print("patched admin_users.html: sortable headers")
else:
    print("unchanged: sortable headers already present")

h = re.sub(
    r'/static/admin_users\.js\?v=[^"]+',
    '/static/admin_users.js?v=20260613-collapsible-sort-1',
    h
)

html.write_text(h)

# ----------------------------------------------------------------------
# JS: sorting state, header binding, editor toggle behavior.
# ----------------------------------------------------------------------
s = js.read_text()

if "ADMIN_USERS_SORT_STORAGE_KEY" not in s:
    s = s.replace(
'''let allUsers = [];
let actorFp = "";
''',
'''let allUsers = [];
let actorFp = "";

const ADMIN_USERS_SORT_STORAGE_KEY = "pqnas_admin_users_sort_v1";
let adminUsersSort = (() => {
    try {
        const raw = JSON.parse(localStorage.getItem(ADMIN_USERS_SORT_STORAGE_KEY) || "{}");
        const key = String(raw.key || "fingerprint");
        const dir = String(raw.dir || "asc") === "desc" ? "desc" : "asc";
        return { key, dir };
    } catch (_) {
        return { key: "fingerprint", dir: "asc" };
    }
})();
'''
    )
    print("patched admin_users.js: sort state")
else:
    print("unchanged: sort state already present")

if "function setProfileEditorOpen" not in s:
    anchor = '''function setMsg(text) {
    const el = $("msg");
    if (el) el.textContent = text || "";
}
'''
    if anchor not in s:
        print("ERROR: setMsg anchor not found", file=sys.stderr)
        sys.exit(1)

    helpers = r'''

function isProfileEditorOpen() {
    const body = $("profileEditorBody");
    return !!body && !body.classList.contains("collapsed");
}

function setProfileEditorOpen(open) {
    const body = $("profileEditorBody");
    const btn = $("profileEditorToggle");
    if (!body || !btn) return;

    body.classList.toggle("collapsed", !open);
    btn.setAttribute("aria-expanded", open ? "true" : "false");
    btn.textContent = open
        ? tr("admin.users.hide_editor", null, "Hide editor")
        : tr("admin.users.show_editor", null, "Show editor");
}

function adminUsersSortValue(u, key) {
    switch (String(key || "")) {
        case "name":
            return String(`${u.name || ""} ${u.email || ""} ${u.notes || ""}`).toLowerCase();
        case "role":
            return String(u.role || "").toLowerCase();
        case "status":
            return String(u.status || "").toLowerCase();
        case "group":
            return String(u.group || "").toLowerCase();
        case "storage":
            return String(`${u.storage_state || ""} ${storagePoolIdForUser(u)}`).toLowerCase();
        case "quota":
            return Number(u.quota_bytes || 0);
        case "added": {
            const t = Date.parse(String(u.added_at || ""));
            return Number.isFinite(t) ? t : 0;
        }
        case "fingerprint":
        default:
            return String(u.fingerprint || "").toLowerCase();
    }
}

function compareAdminUserValues(a, b) {
    if (typeof a === "number" || typeof b === "number") {
        const an = Number(a || 0);
        const bn = Number(b || 0);
        return an === bn ? 0 : (an < bn ? -1 : 1);
    }

    return String(a || "").localeCompare(String(b || ""), undefined, {
        numeric: true,
        sensitivity: "base"
    });
}

function sortAdminUserRows(rows) {
    const key = adminUsersSort.key || "fingerprint";
    const dir = adminUsersSort.dir === "desc" ? -1 : 1;

    return [...rows].sort((a, b) => {
        const primary = compareAdminUserValues(
            adminUsersSortValue(a, key),
            adminUsersSortValue(b, key)
        );

        if (primary !== 0) return primary * dir;

        const tieName = compareAdminUserValues(
            adminUsersSortValue(a, "name"),
            adminUsersSortValue(b, "name")
        );
        if (tieName !== 0) return tieName;

        return compareAdminUserValues(
            adminUsersSortValue(a, "fingerprint"),
            adminUsersSortValue(b, "fingerprint")
        );
    });
}

function saveAdminUsersSort() {
    try {
        localStorage.setItem(ADMIN_USERS_SORT_STORAGE_KEY, JSON.stringify(adminUsersSort));
    } catch (_) {}
}

function updateAdminUsersSortIndicators() {
    document.querySelectorAll("button.sortTh[data-sort]").forEach(btn => {
        const key = btn.getAttribute("data-sort") || "";
        const active = key === adminUsersSort.key;
        btn.classList.toggle("active", active);

        const icon = btn.querySelector(".sortIcon");
        if (icon) {
            icon.textContent = active
                ? (adminUsersSort.dir === "desc" ? "▼" : "▲")
                : "↕";
        }

        const th = btn.closest("th");
        if (th) {
            th.setAttribute(
                "aria-sort",
                active
                    ? (adminUsersSort.dir === "desc" ? "descending" : "ascending")
                    : "none"
            );
        }
    });
}

function bindAdminUsersSortHeaders() {
    document.querySelectorAll("button.sortTh[data-sort]").forEach(btn => {
        btn.addEventListener("click", () => {
            const key = btn.getAttribute("data-sort") || "fingerprint";

            if (adminUsersSort.key === key) {
                adminUsersSort.dir = adminUsersSort.dir === "asc" ? "desc" : "asc";
            } else {
                adminUsersSort.key = key;
                adminUsersSort.dir = key === "added" || key === "quota" ? "desc" : "asc";
            }

            saveAdminUsersSort();
            render();
        });
    });

    updateAdminUsersSortIndicators();
}
'''
    s = s.replace(anchor, anchor + helpers, 1)
    print("patched admin_users.js: editor/sort helpers")
else:
    print("unchanged: editor/sort helpers already present")

old_render = '''function render() {
    const f = ($("filter")?.value || "").toLowerCase().trim();
    const rows = allUsers.filter(u => {
        const hay = [
            u.fingerprint, u.name, u.notes, u.role, u.status,
            u.group, u.email, u.storage_state,
            String(u.quota_bytes || "")
        ].join(" ").toLowerCase();
        return !f || hay.includes(f);
    });
'''
new_render = '''function render() {
    const f = ($("filter")?.value || "").toLowerCase().trim();
    let rows = allUsers.filter(u => {
        const hay = [
            u.fingerprint, u.name, u.notes, u.role, u.status,
            u.group, u.email, u.storage_state,
            String(u.quota_bytes || "")
        ].join(" ").toLowerCase();
        return !f || hay.includes(f);
    });

    rows = sortAdminUserRows(rows);
'''
if old_render in s:
    s = s.replace(old_render, new_render, 1)
    print("patched admin_users.js: render uses sortable rows")
elif "rows = sortAdminUserRows(rows);" in s:
    print("unchanged: render sorting already present")
else:
    print("ERROR: render filter anchor not found", file=sys.stderr)
    sys.exit(1)

old_join = '''    }).join("");

    // ✅ Attach avatar modal click via delegation (works across rerenders)
'''
new_join = '''    }).join("");

    updateAdminUsersSortIndicators();

    // ✅ Attach avatar modal click via delegation (works across rerenders)
'''
if old_join in s:
    s = s.replace(old_join, new_join, 1)
    print("patched admin_users.js: render updates sort indicators")
elif "updateAdminUsersSortIndicators();" in s:
    print("unchanged: sort indicator update already present")
else:
    print("ERROR: join anchor not found", file=sys.stderr)
    sys.exit(1)

old_edit = '''            const u = allUsers.find(x => String(x.fingerprint || "") === fp);
            if (!u) return;

            // Fill the edit form
'''
new_edit = '''            const u = allUsers.find(x => String(x.fingerprint || "") === fp);
            if (!u) return;

            setProfileEditorOpen(true);

            // Fill the edit form
'''
if old_edit in s:
    s = s.replace(old_edit, new_edit, 1)
    print("patched admin_users.js: edit opens editor")
elif "setProfileEditorOpen(true);" in s:
    print("unchanged: edit opens editor already present")
else:
    print("ERROR: edit button anchor not found", file=sys.stderr)
    sys.exit(1)

old_load = '''window.addEventListener("load", async () => {
    $("btnRefresh")?.addEventListener("click", refresh);
    $("filter")?.addEventListener("input", render);

    $("btnAdd")?.addEventListener("click", async () => {
'''
new_load = '''window.addEventListener("load", async () => {
    $("btnRefresh")?.addEventListener("click", refresh);
    $("filter")?.addEventListener("input", render);

    setProfileEditorOpen(false);
    $("profileEditorToggle")?.addEventListener("click", () => {
        setProfileEditorOpen(!isProfileEditorOpen());
    });
    bindAdminUsersSortHeaders();

    $("btnAdd")?.addEventListener("click", async () => {
'''
if old_load in s:
    s = s.replace(old_load, new_load, 1)
    print("patched admin_users.js: load wires editor toggle and sort headers")
elif "bindAdminUsersSortHeaders();" in s:
    print("unchanged: load wiring already present")
else:
    print("ERROR: load anchor not found", file=sys.stderr)
    sys.exit(1)

old_lang = '''window.addEventListener("pqnas-language-changed", () => {
    applyStaticI18n();
    try { render(); } catch (_) {}
});
'''
new_lang = '''window.addEventListener("pqnas-language-changed", () => {
    applyStaticI18n();
    try {
        setProfileEditorOpen(isProfileEditorOpen());
        render();
    } catch (_) {}
});
'''
if old_lang in s:
    s = s.replace(old_lang, new_lang, 1)
    print("patched admin_users.js: language change refreshes editor label")
elif "setProfileEditorOpen(isProfileEditorOpen());" in s:
    print("unchanged: language change already patched")
else:
    print("ERROR: language change anchor not found", file=sys.stderr)
    sys.exit(1)

js.write_text(s)

# ----------------------------------------------------------------------
# i18n: add show/hide labels to all languages.
# ----------------------------------------------------------------------
translations = {
    "en": {
        "admin.users.show_editor": "Show editor",
        "admin.users.hide_editor": "Hide editor"
    },
    "fi": {
        "admin.users.show_editor": "Näytä muokkaus",
        "admin.users.hide_editor": "Piilota muokkaus"
    },
    "sv": {
        "admin.users.show_editor": "Visa redigering",
        "admin.users.hide_editor": "Dölj redigering"
    },
    "de": {
        "admin.users.show_editor": "Bearbeitung anzeigen",
        "admin.users.hide_editor": "Bearbeitung ausblenden"
    },
    "es": {
        "admin.users.show_editor": "Mostrar editor",
        "admin.users.hide_editor": "Ocultar editor"
    },
    "fr": {
        "admin.users.show_editor": "Afficher l’éditeur",
        "admin.users.hide_editor": "Masquer l’éditeur"
    },
    "it": {
        "admin.users.show_editor": "Mostra editor",
        "admin.users.hide_editor": "Nascondi editor"
    },
    "et": {
        "admin.users.show_editor": "Näita muutmist",
        "admin.users.hide_editor": "Peida muutmine"
    },
    "pl": {
        "admin.users.show_editor": "Pokaż edytor",
        "admin.users.hide_editor": "Ukryj edytor"
    },
    "tr": {
        "admin.users.show_editor": "Düzenleyiciyi göster",
        "admin.users.hide_editor": "Düzenleyiciyi gizle"
    },
    "uk": {
        "admin.users.show_editor": "Показати редактор",
        "admin.users.hide_editor": "Сховати редактор"
    },
    "zh": {
        "admin.users.show_editor": "显示编辑器",
        "admin.users.hide_editor": "隐藏编辑器"
    }
}

for p in sorted(i18n_dir.glob("*.json")):
    lang = p.stem
    data = json.loads(p.read_text())
    add = translations.get(lang, translations["en"])
    changed = False
    for k, v in add.items():
        if data.get(k) != v:
            data[k] = v
            changed = True
    if changed:
        p.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n")
        print(f"patched i18n: {p}")

print("done")
