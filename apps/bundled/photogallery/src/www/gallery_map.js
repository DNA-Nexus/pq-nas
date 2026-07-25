(() => {
    "use strict";

    window.PQNAS_PHOTOGALLERY = window.PQNAS_PHOTOGALLERY || {};

    function mapT(key, params, fallback) {
        try {
            const api = window.PQNAS_I18N;
            if (api && typeof api.t === "function") {
                return api.t(key, params || null, fallback);
            }
        } catch (_) {}

        let out = String(fallback || key || "");
        const p = params || {};

        for (const name of Object.keys(p)) {
            out = out.split(`{${name}}`).join(String(p[name]));
        }

        return out;
    }

    const mod = {
        runtime: {
            leafletPromise: null,
            map: null,
            markersLayer: null,
            tileLayer: null
        },

        escapeHtml(value) {
            return String(value || "")
                .replace(/&/g, "&amp;")
                .replace(/</g, "&lt;")
                .replace(/>/g, "&gt;")
                .replace(/"/g, "&quot;")
                .replace(/'/g, "&#39;");
        },

        destroyMap() {
            if (mod.runtime.map) {
                try {
                    mod.runtime.map.remove();
                } catch (_) {}
            }

            mod.runtime.map = null;
            mod.runtime.markersLayer = null;
            mod.runtime.tileLayer = null;
        },

        ensureLeafletLoaded() {
            if (window.L) return Promise.resolve(window.L);
            if (mod.runtime.leafletPromise) return mod.runtime.leafletPromise;

            mod.runtime.leafletPromise = new Promise((resolve, reject) => {
                const cssHref = "./leaflet.css";
                const jsSrc = "./leaflet.js";

                const hasCss = Array.from(
                    document.querySelectorAll('link[rel="stylesheet"]')
                ).some((element) => {
                    return (element.getAttribute("href") || "") === cssHref;
                });

                if (!hasCss) {
                    const link = document.createElement("link");
                    link.rel = "stylesheet";
                    link.href = cssHref;
                    document.head.appendChild(link);
                }

                const existingScript = Array.from(
                    document.querySelectorAll("script")
                ).find((element) => {
                    return (element.getAttribute("src") || "") === jsSrc;
                });

                if (window.L) {
                    resolve(window.L);
                    return;
                }

                if (existingScript) {
                    existingScript.addEventListener(
                        "load",
                        () => resolve(window.L),
                        { once: true }
                    );
                    existingScript.addEventListener(
                        "error",
                        () => reject(new Error("Failed to load Leaflet script")),
                        { once: true }
                    );
                    return;
                }

                const script = document.createElement("script");
                script.src = jsSrc;
                script.async = true;
                script.onload = () => {
                    if (window.L) {
                        resolve(window.L);
                    } else {
                        reject(new Error("Leaflet loaded but window.L is missing"));
                    }
                };
                script.onerror = () => {
                    reject(new Error("Failed to load Leaflet script"));
                };

                document.head.appendChild(script);
            });

            return mod.runtime.leafletPromise;
        },

        buildPopupCard(item, deps) {
            const rel = deps.currentRelPathFor(item);
            const lat = Number(item.gps_latitude);
            const lon = Number(item.gps_longitude);

            const card = document.createElement("div");
            card.className = "mapLeafletPopup";

            const thumbWrap = document.createElement("div");
            thumbWrap.className = "mapLeafletPopupThumbWrap";

            const image = document.createElement("img");
            image.className = "mapLeafletPopupThumb";
            image.alt = item.name || mapT(
                "photogallery.map.photo_alt",
                null,
                "photo"
            );
            image.loading = "eager";
            image.decoding = "async";
            image.src = deps.galleryThumbUrl(
                rel,
                640,
                item.mtime_unix || 0
            );

            image.addEventListener("error", () => {
                const placeholder = document.createElement("div");
                placeholder.className = "mapLeafletPopupPlaceholder";
                placeholder.textContent = mapT(
                    "photogallery.map.thumbnail_not_available",
                    null,
                    "Thumbnail not available."
                );
                thumbWrap.replaceChildren(placeholder);
            }, { once: true });

            image.addEventListener("dblclick", (event) => {
                event.preventDefault();
                event.stopPropagation();
                deps.openPreviewFor(item);
            });

            thumbWrap.appendChild(image);

            const body = document.createElement("div");
            body.className = "mapLeafletPopupBody";

            const title = document.createElement("div");
            title.className = "mapLeafletPopupTitle";
            title.textContent = item.name || mapT(
                "photogallery.map.unnamed",
                null,
                "(unnamed)"
            );

            const metadata = document.createElement("div");
            metadata.className = "mapLeafletPopupMeta";

            const path = document.createElement("div");
            path.textContent = "/" + rel;

            const time = document.createElement("div");
            time.textContent =
                deps.fmtTime(item.capture_time_unix || 0) ||
                mapT(
                    "photogallery.map.no_capture_time",
                    null,
                    "no capture time"
                );

            const coordinates = document.createElement("div");
            coordinates.textContent =
                `${lat.toFixed(6)}, ${lon.toFixed(6)}`;

            metadata.appendChild(path);
            metadata.appendChild(time);
            metadata.appendChild(coordinates);

            if (item.gps_altitude != null) {
                const altitude = document.createElement("div");
                altitude.textContent = mapT(
                    "photogallery.map.altitude",
                    { altitude: item.gps_altitude },
                    "Altitude: {altitude}"
                );
                metadata.appendChild(altitude);
            }

            const actions = document.createElement("div");
            actions.className = "mapLeafletPopupActions";

            const openButton = document.createElement("button");
            openButton.type = "button";
            openButton.className = "btn";
            openButton.textContent = mapT(
                "photogallery.menu.open_preview",
                null,
                "Open preview"
            );

            openButton.addEventListener("click", (event) => {
                event.preventDefault();
                event.stopPropagation();
                deps.openPreviewFor(item);
            });

            actions.appendChild(openButton);

            body.appendChild(title);
            body.appendChild(metadata);
            body.appendChild(actions);

            card.appendChild(thumbWrap);
            card.appendChild(body);

            return card;
        },

        render(mapCanvas, items, deps) {
            if (!mapCanvas) return;

            mod.destroyMap();
            mapCanvas.replaceChildren();

            if (!items.length) {
                const empty = document.createElement("div");
                empty.className = "emptyState";
                empty.innerHTML = `
                    <div class="h">${mod.escapeHtml(mapT(
                        "photogallery.map.no_photos_with_location",
                        null,
                        "No photos with location"
                    ))}</div>
                    <div class="p">${mod.escapeHtml(mapT(
                        "photogallery.map.no_gps_in_current_view",
                        null,
                        "Nothing in the current view has GPS coordinates yet."
                    ))}</div>
                `;

                mapCanvas.appendChild(empty);
                deps.refreshFooterStats?.();
                return;
            }

            const pane = document.createElement("div");
            pane.className = "mapPaneReal";

            const viewport = document.createElement("div");
            viewport.className = "mapViewport";

            const mapHost = document.createElement("div");
            mapHost.className = "mapHost";

            const summary = document.createElement("div");
            summary.className = "mapSummary";
            summary.innerHTML = `
                <div class="h">${mod.escapeHtml(mapT(
                    "photogallery.map.title",
                    null,
                    "Map"
                ))}</div>
                <div class="p">${mod.escapeHtml(mapT(
                    "photogallery.map.gps_photos_current_view",
                    { count: items.length },
                    "GPS photos in current view: {count}"
                ))}</div>
            `;

            viewport.appendChild(mapHost);
            viewport.appendChild(summary);
            pane.appendChild(viewport);
            mapCanvas.appendChild(pane);

            deps.refreshFooterStats?.();

            mod.ensureLeafletLoaded().then((L) => {
                if (!mapHost.isConnected) return;

                mod.runtime.map = L.map(mapHost, {
                    zoomControl: true,
                    worldCopyJump: true
                });

                mod.runtime.tileLayer = L.tileLayer(
                    "https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png",
                    {
                        maxZoom: 19,
                        attribution: "&copy; OpenStreetMap contributors"
                    }
                ).addTo(mod.runtime.map);

                mod.runtime.markersLayer =
                    L.layerGroup().addTo(mod.runtime.map);

                const bounds = [];
                const markerByPath = new Map();

                for (const item of items) {
                    const lat = Number(item.gps_latitude);
                    const lon = Number(item.gps_longitude);

                    if (!Number.isFinite(lat) || !Number.isFinite(lon)) {
                        continue;
                    }

                    const rel = deps.currentRelPathFor(item);

                    const marker = L.circleMarker([lat, lon], {
                        radius: 8,
                        weight: 2,
                        opacity: 1,
                        fillOpacity: 0.85
                    });

                    marker.bindPopup(
                        mod.buildPopupCard(item, deps),
                        {
                            className: "mapPhotoPopupShell",
                            minWidth: 280,
                            maxWidth: 380,
                            autoPan: true,
                            autoPanPadding: [24, 24]
                        }
                    );

                    marker.addTo(mod.runtime.markersLayer);
                    markerByPath.set(rel, marker);
                    bounds.push([lat, lon]);
                }

                const focusRel = String(deps.focusRelPath || "");
                const focusMarker =
                    focusRel ? markerByPath.get(focusRel) : null;

                if (focusMarker) {
                    mod.runtime.map.setView(
                        focusMarker.getLatLng(),
                        15,
                        { animate: false }
                    );
                } else if (bounds.length === 1) {
                    mod.runtime.map.setView(bounds[0], 13);
                } else if (bounds.length > 1) {
                    mod.runtime.map.fitBounds(bounds, {
                        padding: [28, 28]
                    });
                } else {
                    mod.runtime.map.setView([0, 0], 2);
                }

                window.setTimeout(() => {
                    if (!mod.runtime.map || !mapHost.isConnected) return;

                    try {
                        mod.runtime.map.invalidateSize();
                    } catch (_) {}

                    if (focusMarker) {
                        mod.runtime.map.setView(
                            focusMarker.getLatLng(),
                            Math.max(mod.runtime.map.getZoom(), 15),
                            { animate: true }
                        );
                        focusMarker.openPopup();
                        deps.onFocusApplied?.(focusRel);
                    }
                }, 0);
            }).catch((error) => {
                if (!mapHost.isConnected) return;

                const message = String(
                    error && error.message
                        ? error.message
                        : error || "Map failed to load"
                );

                viewport.replaceChildren();

                const errorBox = document.createElement("div");
                errorBox.className = "emptyState";
                errorBox.innerHTML = `
                    <div class="h">Map failed to load</div>
                    <div class="p">${mod.escapeHtml(message)}</div>
                `;

                viewport.appendChild(errorBox);
            });
        }
    };

    window.PQNAS_PHOTOGALLERY.map = mod;
})();
