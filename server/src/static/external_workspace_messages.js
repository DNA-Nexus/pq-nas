(() => {
    "use strict";

    function tr(key, vars, fallback) {
        try {
            const i18n = window.PQNAS_I18N;
            if (i18n && typeof i18n.t === "function") {
                return i18n.t(key, vars || null, fallback || key);
            }
        } catch (_) {}
        return String(fallback || key);
    }

    function workspaceIdFromLocation() {
        try {
            const params = new URLSearchParams(window.location.search || "");
            return String(params.get("workspace_id") || "").trim();
        } catch (_) {
            return "";
        }
    }

    function el(tag, cls, text) {
        const node = document.createElement(tag);
        if (cls) node.className = cls;
        if (text != null) node.textContent = text;
        return node;
    }

    function apiUrl(path, workspaceId) {
        const u = new URL(path, window.location.origin);
        u.searchParams.set("workspace_id", workspaceId);
        return u.toString();
    }

    function formatTime(value) {
        try {
            if (typeof value === "number") return new Date(value * 1000).toLocaleString();
            if (value) return new Date(String(value)).toLocaleString();
        } catch (_) {}
        return "";
    }

    function findToolbarHost() {
        return document.getElementById("externalWorkspaceHeaderActions") ||
               document.querySelector(".externalWorkspaceHeaderActions") ||
               document.querySelector(".fileToolbarMain") ||
               document.querySelector(".toolbar");
    }

    function parseContactCardText(text) {
        const raw = String(text || "");
        const start = raw.indexOf("[DNA-NEXUS-CONTACT]");
        const end = raw.indexOf("[/DNA-NEXUS-CONTACT]");
        if (start < 0 || end < 0 || end <= start) return null;

        const before = raw.slice(0, start).trim();
        const body = raw.slice(start + "[DNA-NEXUS-CONTACT]".length, end).trim();
        const after = raw.slice(end + "[/DNA-NEXUS-CONTACT]".length).trim();

        const card = {};
        for (const line of body.split(/\r?\n/)) {
            const idx = line.indexOf(":");
            if (idx <= 0) continue;

            const key = line.slice(0, idx).trim().toLowerCase();
            const value = line.slice(idx + 1).trim();
            if (!key || !value) continue;

            if (key === "name") card.name = value;
            else if (key === "company") card.company = value;
            else if (key === "title") card.title = value;
            else if (key === "email") card.email = value;
            else if (key === "phone") card.phone = value;
            else if (key === "mobile") card.mobile = value;
            else if (key === "website") card.website = value;
            else if (key === "address") card.address = value;
            else if (key === "tags") card.tags = value;
            else if (key === "identity") card.identity = value;
        }

        if (!card.name && !card.company && !card.email && !card.phone && !card.mobile) return null;
        return { before, after, card };
    }

    function formatContactCardForClipboard(card) {
        const c = card || {};
        const lines = [
            "[DNA-NEXUS-CONTACT]",
            `Name: ${c.name || ""}`,
            `Company: ${c.company || ""}`,
            `Title: ${c.title || ""}`,
            `Email: ${c.email || ""}`,
            `Phone: ${c.phone || ""}`,
            `Mobile: ${c.mobile || ""}`,
            `Website: ${c.website || ""}`,
            `Address: ${c.address || ""}`,
            `Tags: ${c.tags || ""}`,
            `Identity: ${c.identity || ""}`,
            "[/DNA-NEXUS-CONTACT]"
        ];
        return lines.filter((line) => !line.endsWith(": ")).join("\n");
    }

    function copyText(value, onDone) {
        const text = String(value || "").trim();
        if (!text) return;

        const done = () => {
            if (typeof onDone === "function") onDone();
        };

        if (navigator.clipboard && typeof navigator.clipboard.writeText === "function") {
            navigator.clipboard.writeText(text).then(done).catch(() => fallbackCopyText(text, done));
            return;
        }

        fallbackCopyText(text, done);
    }

    function fallbackCopyText(text, done) {
        const area = document.createElement("textarea");
        area.value = text;
        area.setAttribute("readonly", "");
        area.style.position = "fixed";
        area.style.left = "-1000px";
        area.style.top = "-1000px";
        document.body.appendChild(area);
        area.select();

        try {
            document.execCommand("copy");
            done();
        } catch (_) {
        } finally {
            area.remove();
        }
    }

    function appendContactLine(parent, label, value) {
        const v = String(value || "").trim();
        if (!v) return;

        const line = el("div", "extWsMsgContactLine");
        const strong = el("strong", "", label + ": ");
        const span = el("span", "", v);
        line.append(strong, span);
        parent.append(line);
    }

    function contactCardNode(parsed, setStatus) {
        const wrap = el("div", "extWsMsgContactWrap");

        if (parsed.before) {
            wrap.append(el("div", "extWsMsgContactText", parsed.before));
        }

        const card = parsed.card || {};
        const box = el("div", "extWsMsgContactCard");

        const top = el("div", "extWsMsgContactTop");
        top.append(
            el("div", "extWsMsgContactName", card.name || card.company || card.email || tr("external.messages.contact_card", null, "Contact card")),
            el("div", "extWsMsgContactBadge", tr("external.messages.contact", null, "Contact"))
        );
        box.append(top);

        const metaParts = [card.company, card.title].filter(Boolean);
        if (metaParts.length) {
            box.append(el("div", "extWsMsgContactMeta", metaParts.join(" • ")));
        }

        appendContactLine(box, tr("external.messages.email", null, "Email"), card.email);
        appendContactLine(box, tr("external.messages.phone", null, "Phone"), card.phone);
        appendContactLine(box, tr("external.messages.mobile", null, "Mobile"), card.mobile);
        appendContactLine(box, tr("external.messages.website", null, "Website"), card.website);
        appendContactLine(box, tr("external.messages.address", null, "Address"), card.address);
        appendContactLine(box, tr("external.messages.tags", null, "Tags"), card.tags);

        const actions = el("div", "extWsMsgContactActions");

        const copyCard = el("button", "extWsMsgContactAction", tr("external.messages.copy_contact", null, "Copy contact"));
        copyCard.type = "button";
        copyCard.addEventListener("click", (ev) => {
            ev.preventDefault();
            ev.stopPropagation();
            copyText(formatContactCardForClipboard(card), () => setStatus(tr("external.messages.contact_copied", null, "Contact copied.")));
        });
        actions.append(copyCard);

        if (card.address) {
            const copyAddress = el("button", "extWsMsgContactAction", tr("external.messages.copy_address", null, "Copy address"));
            copyAddress.type = "button";
            copyAddress.addEventListener("click", (ev) => {
                ev.preventDefault();
                ev.stopPropagation();
                copyText(card.address, () => setStatus(tr("external.messages.address_copied", null, "Address copied.")));
            });
            actions.append(copyAddress);
        }

        if (card.email) {
            const copyEmail = el("button", "extWsMsgContactAction", tr("external.messages.copy_email", null, "Copy email"));
            copyEmail.type = "button";
            copyEmail.addEventListener("click", (ev) => {
                ev.preventDefault();
                ev.stopPropagation();
                copyText(card.email, () => setStatus(tr("external.messages.email_copied", null, "Email copied.")));
            });
            actions.append(copyEmail);
        }

        if (card.phone || card.mobile) {
            const copyPhone = el("button", "extWsMsgContactAction", tr("external.messages.copy_phone", null, "Copy phone"));
            copyPhone.type = "button";
            copyPhone.addEventListener("click", (ev) => {
                ev.preventDefault();
                ev.stopPropagation();
                copyText(card.phone || card.mobile, () => setStatus(tr("external.messages.phone_copied", null, "Phone copied.")));
            });
            actions.append(copyPhone);
        }

        if (card.website) {
            const openWebsite = el("button", "extWsMsgContactAction", tr("external.messages.open_website", null, "Open website"));
            openWebsite.type = "button";
            openWebsite.addEventListener("click", (ev) => {
                ev.preventDefault();
                ev.stopPropagation();
                let url = String(card.website || "").trim();
                if (!/^https?:\/\//i.test(url)) url = "https://" + url;
                window.open(url, "_blank", "noopener,noreferrer");
            });
            actions.append(openWebsite);
        }

        box.append(actions);
        wrap.append(box);

        if (parsed.after) {
            wrap.append(el("div", "extWsMsgContactText", parsed.after));
        }

        return wrap;
    }

    function messageBodyNode(text, setStatus) {
        const parsed = parseContactCardText(text);
        if (parsed) return contactCardNode(parsed, setStatus);

        return el("div", "extWsMsgBody", String(text || ""));
    }

    function buildUi(workspaceId) {
        if (!workspaceId) return null;
        if (document.getElementById("externalWorkspaceMessagesDrawer")) {
            return document.getElementById("externalWorkspaceMessagesDrawer");
        }

        const host = findToolbarHost();
        if (!host) return null;

        const button = document.createElement("button");
        button.id = "externalWorkspaceMessagesButton";
        button.type = "button";
        button.className = "pq-btn secondary extWsMsgButton";

        const buttonLabel = document.createElement("span");
        buttonLabel.textContent = tr("external.messages.button", null, "Messages");

        const badge = document.createElement("span");
        badge.id = "externalWorkspaceMessagesBadge";
        badge.className = "extWsMsgBadge";
        badge.textContent = "!";
        badge.hidden = true;

        button.append(buttonLabel, badge);

        const drawer = document.createElement("section");
        drawer.id = "externalWorkspaceMessagesDrawer";
        drawer.setAttribute("aria-hidden", "true");

        const head = el("div", "extWsMsgHead");
        const titleWrap = el("div", "");
        titleWrap.append(
            el("div", "extWsMsgTitle", tr("external.messages.title", null, "Workspace messages")),
            el("div", "extWsMsgSub", tr("external.messages.subtitle", null, "Visible to members of this external workspace."))
        );

        const close = el("button", "pq-btn secondary extWsMsgClose", "×");
        close.type = "button";
        close.setAttribute("aria-label", tr("common.close", null, "Close"));
        head.append(titleWrap, close);

        const list = el("div", "extWsMsgList");
        list.id = "externalWorkspaceMessagesList";
        list.append(el("div", "extWsMsgEmpty", tr("external.messages.not_loaded", null, "Open messages to load the workspace message board.")));

        const foot = el("div", "extWsMsgFoot");
        const input = document.createElement("textarea");
        input.id = "externalWorkspaceMessagesInput";
        input.className = "extWsMsgInput";
        input.maxLength = 4000;
        input.placeholder = tr("external.messages.placeholder", null, "Write a message for this workspace…");

        const actions = el("div", "extWsMsgActions");
        const status = el("div", "extWsMsgStatus");
        status.id = "externalWorkspaceMessagesStatus";
        const send = el("button", "pq-btn primary", tr("external.messages.send", null, "Send"));
        send.id = "externalWorkspaceMessagesSend";
        send.type = "button";
        actions.append(status, send);
        foot.append(input, actions);

        drawer.append(head, list, foot);
        document.body.append(drawer);
        host.append(button);

        let isOpen = false;
        let loadedOnce = false;
        let latestId = 0;

        const seenKey = "pqnas.externalWorkspaceMessages.lastSeen." + workspaceId;
        let lastSeenId = 0;
        try {
            lastSeenId = Number(window.localStorage.getItem(seenKey) || "0") || 0;
        } catch (_) {
            lastSeenId = 0;
        }
        let baselineInitialized = lastSeenId > 0;

        function setBadge(show) {
            badge.hidden = !show;
            button.classList.toggle("hasUnread", !!show);
            if (show) {
                button.setAttribute("aria-label", tr("external.messages.button_unread", null, "Messages, new messages"));
                button.title = tr("external.messages.button_unread", null, "Messages, new messages");
            } else {
                button.setAttribute("aria-label", tr("external.messages.button", null, "Messages"));
                button.title = tr("external.messages.button", null, "Messages");
            }
        }

        function rememberSeen(id) {
            const n = Number(id || 0);
            if (!Number.isFinite(n) || n <= 0) return;
            lastSeenId = Math.max(lastSeenId || 0, n);
            baselineInitialized = true;
            try {
                window.localStorage.setItem(seenKey, String(lastSeenId));
            } catch (_) {}
        }

        function setStatus(text, bad) {
            status.textContent = text || "";
            status.classList.toggle("bad", !!bad);
        }

        function renderMessages(messages) {
            list.replaceChildren();

            if (!Array.isArray(messages) || messages.length === 0) {
                list.append(el("div", "extWsMsgEmpty", tr("external.messages.empty", null, "No messages yet.")));
                return;
            }

            for (const m of messages) {
                const row = el("div", "extWsMsgRow");
                if (m && m.is_own) row.classList.add("own");

                const meta = el("div", "extWsMsgMeta");
                meta.append(
                    el("div", "extWsMsgAuthor", String((m && m.author_name) || tr("external.messages.member", null, "Workspace member"))),
                    el("div", "extWsMsgTime", formatTime(m && (m.created_at || m.created_at_epoch)))
                );

                row.append(meta, messageBodyNode((m && m.body) || "", setStatus));
                list.append(row);
            }

            list.scrollTop = list.scrollHeight;
        }

        async function markRead(id) {
            if (!id) return;
            try {
                await fetch("/api/v4/workspaces/external-messages/read", {
                    method: "POST",
                    credentials: "include",
                    cache: "no-store",
                    headers: {
                        "Accept": "application/json",
                        "Content-Type": "application/json"
                    },
                    body: JSON.stringify({
                        workspace_id: workspaceId,
                        last_seen_id: id
                    })
                });
            } catch (_) {}
        }

        async function loadMessages() {
            setStatus(tr("external.messages.loading", null, "Loading messages…"));
            const res = await fetch(apiUrl("/api/v4/workspaces/external-messages/list", workspaceId) + "&limit=100", {
                method: "GET",
                credentials: "include",
                cache: "no-store",
                headers: { "Accept": "application/json" }
            });

            const data = await res.json().catch(() => ({}));
            if (!res.ok || !data.ok) {
                const msg = data && data.message ? data.message : tr("external.messages.load_failed", null, "Could not load messages.");
                throw new Error(msg);
            }

            const messages = Array.isArray(data.messages) ? data.messages : [];
            latestId = Number(data.latest_id || 0);
            renderMessages(messages);
            setStatus("");
            loadedOnce = true;
            await markRead(latestId);
            rememberSeen(latestId);
            setBadge(false);
        }

        async function sendMessage() {
            const body = String(input.value || "").trim();
            if (!body) {
                setStatus(tr("external.messages.empty_error", null, "Write a message first."), true);
                return;
            }

            send.disabled = true;
            setStatus(tr("external.messages.sending", null, "Sending…"));

            try {
                const res = await fetch("/api/v4/workspaces/external-messages/post", {
                    method: "POST",
                    credentials: "include",
                    cache: "no-store",
                    headers: {
                        "Accept": "application/json",
                        "Content-Type": "application/json"
                    },
                    body: JSON.stringify({
                        workspace_id: workspaceId,
                        body
                    })
                });

                const data = await res.json().catch(() => ({}));
                if (!res.ok || !data.ok) {
                    throw new Error(data.message || tr("external.messages.send_failed", null, "Message could not be sent."));
                }

                input.value = "";
                await loadMessages();
            } catch (e) {
                setStatus(String(e && e.message ? e.message : "Send failed"), true);
            } finally {
                send.disabled = false;
            }
        }

        async function checkForNewMessages() {
            if (isOpen || document.hidden) return;

            const res = await fetch(apiUrl("/api/v4/workspaces/external-messages/list", workspaceId) + "&limit=1", {
                method: "GET",
                credentials: "include",
                cache: "no-store",
                headers: { "Accept": "application/json" }
            });

            const data = await res.json().catch(() => ({}));
            if (!res.ok || !data.ok) return;

            const remoteLatestId = Number(data.latest_id || 0);
            if (!Number.isFinite(remoteLatestId) || remoteLatestId <= 0) return;

            // First closed-poll establishes a baseline so old messages do not
            // immediately create a false "new" indicator in a fresh browser.
            if (!baselineInitialized) {
                rememberSeen(remoteLatestId);
                setBadge(false);
                return;
            }

            if (remoteLatestId > lastSeenId) {
                latestId = Math.max(latestId || 0, remoteLatestId);
                setBadge(true);
            }
        }

        async function openDrawer() {
            isOpen = true;
            drawer.classList.add("show");
            drawer.setAttribute("aria-hidden", "false");

            if (!loadedOnce) {
                try {
                    await loadMessages();
                } catch (e) {
                    list.replaceChildren(
                        el("div", "extWsMsgEmpty", String(e && e.message ? e.message : "Could not load messages."))
                    );
                    setStatus("", true);
                }
            }
        }

        function closeDrawer() {
            isOpen = false;
            drawer.classList.remove("show");
            drawer.setAttribute("aria-hidden", "true");
        }

        button.addEventListener("click", () => {
            if (isOpen) closeDrawer();
            else void openDrawer();
        });

        close.addEventListener("click", closeDrawer);
        send.addEventListener("click", sendMessage);

        input.addEventListener("keydown", (e) => {
            if ((e.ctrlKey || e.metaKey) && e.key === "Enter") {
                e.preventDefault();
                void sendMessage();
            }
        });

        input.addEventListener("paste", () => {
            window.setTimeout(() => {
                if (parseContactCardText(input.value)) {
                    setStatus(tr("external.messages.contact_card_detected", null, "Contact card detected. Send to share it with workspace members."));
                }
            }, 0);
        });

        window.setInterval(() => {
            if (document.hidden) return;

            if (isOpen) {
                loadMessages().catch(() => {});
            } else {
                checkForNewMessages().catch(() => {});
            }
        }, 30000);

        window.setTimeout(() => {
            checkForNewMessages().catch(() => {});
        }, 5000);

        return drawer;
    }

    function init() {
        try {
            const workspaceId = workspaceIdFromLocation();
            if (!workspaceId) return;
            buildUi(workspaceId);
        } catch (e) {
            console.warn("[external messages] disabled:", e);
        }
    }

    if (document.readyState === "loading") {
        document.addEventListener("DOMContentLoaded", init, { once: true });
    } else {
        init();
    }
})();
