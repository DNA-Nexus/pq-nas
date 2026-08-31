"use strict";

(() => {
    // Security: T is a bearer capability. Copy it once and immediately replace
    // the current history entry before any network activity.
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

    const prefix = "#handoff=";

    if (!rawFragment.startsWith(prefix)) {
        rawFragment = "";
        setStatus("Secure storage ticket is missing.");
        return;
    }

    let ticket =
        rawFragment.slice(prefix.length);

    rawFragment = "";

    if (!validCapability43(ticket)) {
        ticket = "";
        setStatus("Secure storage ticket is invalid.");
        return;
    }

    const run = async () => {
        try {
            const response =
                await fetch(
                    "/api/storage/handoff/consume",
                    {
                        method: "POST",
                        credentials: "same-origin",
                        cache: "no-store",
                        redirect: "error",
                        referrerPolicy: "no-referrer",
                        headers: {
                            "Accept": "application/json",
                            "Content-Type":
                                "application/x-www-form-urlencoded"
                        },
                        body:
                            "ticket=" +
                            ticket
                    }
                );

            // Drop the application-owned reference as soon as fetch has
            // constructed the request.
            ticket = "";

            let body = null;

            try {
                body = await response.json();
            } catch (_) {
                body = null;
            }

            if (
                !response.ok ||
                !body ||
                body.ok !== true
            ) {
                throw new Error("consume_failed");
            }

            setStatus("Storage session ready.");

            window.location.replace("/app");
        } catch (_) {
            ticket = "";
            setStatus(
                "Secure storage session could not be created."
            );
        }
    };

    void run();
})();
