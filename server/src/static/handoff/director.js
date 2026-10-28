"use strict";

(() => {
    // Security: fragment may contain H. Strip fragment and query from browser
    // history before performing any network operation.
    let rawFragment = window.location.hash;
    window.history.replaceState(
        null,
        document.title,
        window.location.pathname
    );

    const statusElement =
        document.getElementById("handoff-status");

    const setStatus = (text) => {
        if (statusElement) {
            statusElement.textContent = text;
        }
    };

    const validCapability43 = (value) =>
        typeof value === "string" &&
        /^[A-Za-z0-9_-]{42}[AEIMQUYcgkosw048]$/.test(value);

    const canonicalHttpsOrigin = (value) => {
        if (typeof value !== "string" || value.length > 2048) {
            return false;
        }

        try {
            const parsed = new URL(value);

            return (
                parsed.protocol === "https:" &&
                parsed.username === "" &&
                parsed.password === "" &&
                parsed.pathname === "/" &&
                parsed.search === "" &&
                parsed.hash === "" &&
                parsed.origin === value
            );
        } catch (_) {
            return false;
        }
    };

    let browserBindingHash = "";

    if (rawFragment !== "") {
        const prefix = "#handoff=";

        if (!rawFragment.startsWith(prefix)) {
            rawFragment = "";
            setStatus("Secure storage handoff could not be continued.");
            return;
        }

        browserBindingHash =
            rawFragment.slice(prefix.length);

        rawFragment = "";

        if (!validCapability43(browserBindingHash)) {
            browserBindingHash = "";
            setStatus("Secure storage handoff could not be continued.");
            return;
        }
    }

    const post = async (
        path,
        options = {}
    ) => {
        return fetch(
            path,
            {
                method: "POST",
                credentials: "same-origin",
                cache: "no-store",
                redirect: "error",
                referrerPolicy: "no-referrer",
                ...options
            }
        );
    };

    const readJson = async (response) => {
        try {
            return await response.json();
        } catch (_) {
            return null;
        }
    };

    const begin = async () => {
        setStatus("Locating your storage...");

        const response =
            await post(
                "/api/storage/handoff/begin",
                {
                    headers: {
                        "Accept": "application/json"
                    }
                }
            );

        const body =
            await readJson(response);

        if (
            !response.ok ||
            !body ||
            body.ok !== true ||
            !canonicalHttpsOrigin(body.storage_origin)
        ) {
            throw new Error("handoff_begin_failed");
        }

        window.location.replace(
            body.storage_origin +
            "/storage/handoff/bootstrap"
        );
    };

    const continueHandoff = async () => {
        setStatus("Authorizing your storage...");

        const response =
            await post(
                "/api/storage/handoff/start",
                {
                    headers: {
                        "Accept": "application/json",
                        "Content-Type": "application/json"
                    },
                    body: JSON.stringify({
                        browser_binding_hash:
                            browserBindingHash
                    })
                }
            );

        const body =
            await readJson(response);

        if (
            response.status === 409 &&
            body &&
            body.error === "handoff_restart_required"
        ) {
            browserBindingHash = "";

            window.location.replace(
                "/storage/handoff/director"
            );
            return;
        }

        if (
            !response.ok ||
            !body ||
            body.ok !== true ||
            !canonicalHttpsOrigin(body.storage_origin) ||
            !validCapability43(body.ticket)
        ) {
            throw new Error("handoff_start_failed");
        }

        const storageOrigin =
            body.storage_origin;

        const ticket =
            body.ticket;

        browserBindingHash = "";

        window.location.replace(
            storageOrigin +
            "/storage/handoff/finish#handoff=" +
            ticket
        );
    };

    const run = async () => {
        try {
            if (browserBindingHash === "") {
                await begin();
                return;
            }

            await continueHandoff();
        } catch (_) {
            browserBindingHash = "";
            setStatus(
                "Secure storage connection is temporarily unavailable."
            );
        }
    };

    void run();
})();
