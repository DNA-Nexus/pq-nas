"use strict";

(() => {
    // This page expects no input. Remove any query/fragment before the first
    // request so attacker-supplied URL data cannot survive in browser history.
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

    const run = async () => {
        try {
            const response =
                await fetch(
                    "/api/storage/handoff/preauth",
                    {
                        method: "POST",
                        credentials: "same-origin",
                        cache: "no-store",
                        redirect: "error",
                        referrerPolicy: "no-referrer",
                        headers: {
                            "Accept": "application/json"
                        }
                    }
                );

            let body = null;

            try {
                body = await response.json();
            } catch (_) {
                body = null;
            }

            if (
                !response.ok ||
                !body ||
                body.ok !== true ||
                !validCapability43(
                    body.browser_binding_hash
                ) ||
                !canonicalHttpsOrigin(
                    body.director_origin
                )
            ) {
                throw new Error("preauth_failed");
            }

            const bindingHash =
                body.browser_binding_hash;

            const directorOrigin =
                body.director_origin;

            setStatus("Returning to Director...");

            window.location.replace(
                directorOrigin +
                "/storage/handoff/director#handoff=" +
                bindingHash
            );
        } catch (_) {
            setStatus(
                "Secure browser binding could not be created."
            );
        }
    };

    void run();
})();
