/* Photo Gallery: read-only GPS classification and bounded subfolder traversal. */
(function (root, factory) {
    "use strict";
    const api = factory();
    if (typeof module === "object" && module.exports) module.exports = api;
    if (root && root.document) {
        root.PQNAS_PHOTOGALLERY = root.PQNAS_PHOTOGALLERY || {};
        root.PQNAS_PHOTOGALLERY.geoScope = api;
    }
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
    "use strict";

    function coordinate(value) {
        if (typeof value === "number") return Number.isFinite(value) ? value : null;
        if (typeof value !== "string" || !value.trim()) return null;
        // Reject booleans, arrays, hex literals and whitespace-as-zero.
        if (!/^[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?$/.test(value.trim())) return null;
        const number = Number(value);
        return Number.isFinite(number) ? number : null;
    }

    function classifyGps(item) {
        if (!item || item.type !== "file" || !(item.has_gps === true || item.has_gps === 1)) {
            return "missing";
        }
        const latitude = coordinate(item.gps_latitude);
        const longitude = coordinate(item.gps_longitude);
        if (latitude === null || longitude === null || latitude < -90 || latitude > 90 ||
                longitude < -180 || longitude > 180) return "invalid";
        // (0,0) is a real coordinate, but also a common uninitialized GPS value.
        // Classify, do not erase or silently rewrite it. A UI override can show it.
        return latitude === 0 && longitude === 0 ? "suspect_zero" : "usable";
    }

    function hasUsableGps(item, includeZero = false) {
        const kind = classifyGps(item);
        return kind === "usable" || (includeZero === true && kind === "suspect_zero");
    }

    function counts(items) {
        const result = { usable: 0, suspect_zero: 0, invalid: 0, missing: 0 };
        for (const item of items || []) {
            if (item && item.type === "file") result[classifyGps(item)]++;
        }
        return result;
    }

    function cleanPath(value) {
        if (typeof value !== "string" || value.includes("\\") || value.includes("\0") ||
                value.startsWith("/")) throw new Error("Unsafe gallery path");
        const parts = value.split("/").filter(Boolean);
        if (parts.some(part => part === "." || part === "..")) throw new Error("Unsafe gallery path");
        return parts.join("/");
    }

    function childPath(parent, name) {
        if (typeof name !== "string" || !name || name.includes("/") || name.includes("\\") ||
                name.includes("\0") || name === "." || name === "..") return null;
        if (name === ".pqnas" || name.startsWith(".pqnas_")) return null;
        return parent ? parent + "/" + name : name;
    }

    function abortError() {
        const error = new Error("Gallery scan cancelled");
        error.name = "AbortError";
        return error;
    }

    function createScanner(listFolder, options = {}) {
        if (typeof listFolder !== "function") throw new TypeError("listFolder must be a function");
        const maxFolders = options.maxFolders === undefined ? 1000 : options.maxFolders;
        const maxFiles = options.maxFiles === undefined ? 25000 : options.maxFiles;
        const timeoutMs = options.timeoutMs === undefined ? 60000 : options.timeoutMs;
        for (const limit of [maxFolders, maxFiles, timeoutMs]) {
            if (!Number.isSafeInteger(limit) || limit < 1) throw new TypeError("Scan limits must be positive integers");
        }
        let generation = 0;
        let active = null;

        function cancel() {
            generation++;
            if (active) active.abort();
            active = null;
        }

        async function scan(rootPath, initialItems, onProgress) {
            const root = cleanPath(rootPath);
            if (!Array.isArray(initialItems)) throw new TypeError("initialItems must be an array");
            cancel();
            const mine = generation;
            const controller = new AbortController();
            active = controller;
            const result = { root, items: [], folders: 0, truncated: false };
            const queue = [root];
            const scheduled = new Set(queue);
            const seenFiles = new Set();
            const check = () => {
                if (controller.signal.aborted || mine !== generation) throw abortError();
            };
            let stopAtFileLimit = false;

            async function readFolder(path) {
                check();
                let timer;
                let onAbort;
                const stop = new Promise((_, reject) => {
                    onAbort = () => reject(abortError());
                    controller.signal.addEventListener("abort", onAbort, { once: true });
                    timer = setTimeout(() => {
                        const error = new Error("Gallery folder request timed out");
                        error.name = "TimeoutError";
                        // Reject before aborting, so timeouts remain visible as errors.
                        reject(error);
                        controller.abort();
                    }, timeoutMs);
                });
                try {
                    const response = await Promise.race([
                        Promise.resolve().then(() => listFolder(path, controller.signal)), stop
                    ]);
                    check();
                    if (!response || response.ok !== true || !Array.isArray(response.items)) {
                        throw new Error("Invalid gallery folder response");
                    }
                    if (response.truncated === true) result.truncated = true;
                    return response.items;
                } finally {
                    clearTimeout(timer);
                    controller.signal.removeEventListener("abort", onAbort);
                }
            }

            try {
                // Sequential breadth-first traversal: one request in flight, no
                // full-library scan unless the user explicitly selects subfolders.
                for (let index = 0; index < queue.length && !stopAtFileLimit; index++) {
                    check();
                    const path = queue[index];
                    const items = index === 0 ? initialItems : await readFolder(path);
                    check();
                    result.folders++;
                    for (const item of items) {
                        check();
                        if (!item || (item.type !== "file" && item.type !== "dir")) continue;
                        const child = childPath(path, item.name);
                        if (child === null) continue;
                        if (item.type === "dir") {
                            if (!scheduled.has(child)) {
                                if (scheduled.size >= maxFolders) {
                                    result.truncated = true;
                                } else {
                                    scheduled.add(child);
                                    queue.push(child);
                                }
                            }
                        } else if (!seenFiles.has(child)) {
                            if (result.items.length >= maxFiles) {
                                result.truncated = true;
                                stopAtFileLimit = true;
                                break;
                            }
                            seenFiles.add(child);
                            result.items.push({ ...item, path: child, rel_path: child, base_path: path });
                        }
                    }
                    if (typeof onProgress === "function") {
                        onProgress({ folders: result.folders, files: result.items.length });
                    }
                }
                check();
                return result;
            } finally {
                if (mine === generation) active = null;
            }
        }
        return Object.freeze({ scan, cancel });
    }

    const messages = {
        "en": {
            "group": "Map search options",
            "subfolders": "Include subfolders",
            "subfoldersTip": "Search images in this folder and its subfolders. Turn off to cancel the scan.",
            "zeros": "Show suspicious (0,0) locations",
            "zerosTip": "Zero latitude and longitude may be uninitialized. Original metadata is not changed.",
            "current": "Current folder only",
            "recursive": "Current folder and subfolders",
            "loading": "Scanning subfolders: {folders} folders, {files} photos…",
            "failed": "Could not finish the subfolder scan. Try Refresh or turn off Include subfolders.",
            "hiddenZero": "{count} suspicious (0,0) locations hidden in this scope.",
            "shownZero": "{count} suspicious (0,0) locations allowed in this scope.",
            "partial": "Partial results: scan limit reached. Select a narrower folder to see the rest.",
            "mapCount": "Map: {count} GPS photos • {scope}",
            "indexNote": "Locations come from the server's EXIF metadata. This search does not rewrite photos or clear the index.",
            "ready": "Ready",
            "busy": "Loading…",
            "error": "Error"
        },
        "fi": {
            "group": "Kartan hakuasetukset",
            "subfolders": "Sisällytä alikansiot",
            "subfoldersTip": "Hae kuvat tästä kansiosta ja sen alikansioista. Poista valinta keskeyttääksesi haun.",
            "zeros": "Näytä myös epäilyttävät (0,0)-sijainnit",
            "zerosTip": "Nollakoordinaatit voivat tarkoittaa alustamatonta GPS-tietoa. Alkuperäisiä metatietoja ei muuteta.",
            "current": "Vain nykyinen kansio",
            "recursive": "Nykyinen kansio ja alikansiot",
            "loading": "Haetaan alikansioista: {folders} kansiota, {files} kuvaa…",
            "failed": "Alikansiohakua ei saatu valmiiksi. Paina Päivitä tai poista Sisällytä alikansiot -valinta.",
            "hiddenZero": "Haun alueella piilotettiin {count} epäilyttävää (0,0)-sijaintia.",
            "shownZero": "Haun alueella sallitaan {count} epäilyttävää (0,0)-sijaintia.",
            "partial": "Osittainen tulos: haun kokoraja tuli vastaan. Valitse pienempi kansio nähdäksesi loput.",
            "mapCount": "Kartta: {count} GPS-kuvaa • {scope}",
            "indexNote": "Sijainnit tulevat palvelimen EXIF-metatiedoista. Haku ei muuta kuvia eikä tyhjennä indeksiä.",
            "ready": "Valmis",
            "busy": "Ladataan…",
            "error": "Virhe"
        },
        "zh": {
            "group": "地图搜索选项",
            "subfolders": "包含子文件夹",
            "subfoldersTip": "搜索此文件夹及其子文件夹中的图片。取消勾选可停止搜索。",
            "zeros": "同时显示可疑的 (0,0) 位置",
            "zerosTip": "纬度和经度均为零可能表示 GPS 数据尚未初始化。不会更改原始元数据。",
            "current": "仅当前文件夹",
            "recursive": "当前文件夹及其子文件夹",
            "loading": "正在扫描子文件夹：{folders} 个文件夹，{files} 张照片…",
            "failed": "无法完成子文件夹扫描。请点击刷新或取消勾选“包含子文件夹”。",
            "hiddenZero": "已隐藏此搜索范围内的 {count} 个可疑 (0,0) 位置。",
            "shownZero": "允许显示此搜索范围内的 {count} 个可疑 (0,0) 位置。",
            "partial": "结果不完整：已达到扫描上限。请选择较小的文件夹以查看其余结果。",
            "mapCount": "地图：{count} 张带 GPS 信息的照片 • {scope}",
            "indexNote": "位置来自服务器的 EXIF 元数据。此搜索不会修改照片或清空索引。",
            "ready": "就绪",
            "busy": "正在加载…",
            "error": "错误"
        },
        "sv": {
            "group": "Kartans sökalternativ",
            "subfolders": "Inkludera undermappar",
            "subfoldersTip": "Sök efter bilder i den här mappen och dess undermappar. Avmarkera för att avbryta sökningen.",
            "zeros": "Visa även misstänkta (0,0)-positioner",
            "zerosTip": "Noll i både latitud och longitud kan betyda oinitialiserade GPS-data. Ursprungliga metadata ändras inte.",
            "current": "Endast aktuell mapp",
            "recursive": "Aktuell mapp och undermappar",
            "loading": "Söker i undermappar: {folders} mappar, {files} foton…",
            "failed": "Sökningen i undermappar kunde inte slutföras. Välj Uppdatera eller avmarkera Inkludera undermappar.",
            "hiddenZero": "{count} misstänkta (0,0)-positioner döljs inom sökområdet.",
            "shownZero": "{count} misstänkta (0,0)-positioner tillåts inom sökområdet.",
            "partial": "Delresultat: sökgränsen har nåtts. Välj en mindre mapp för att se resten.",
            "mapCount": "Karta: {count} GPS-foton • {scope}",
            "indexNote": "Positionerna kommer från serverns EXIF-metadata. Sökningen ändrar inte foton eller tömmer indexet.",
            "ready": "Klar",
            "busy": "Läser in…",
            "error": "Fel"
        },
        "uk": {
            "group": "Параметри пошуку на карті",
            "subfolders": "Включати вкладені папки",
            "subfoldersTip": "Шукати зображення в цій папці та її вкладених папках. Зніміть позначку, щоб скасувати пошук.",
            "zeros": "Показувати також підозрілі координати (0,0)",
            "zerosTip": "Нульові широта й довгота можуть означати неініціалізовані дані GPS. Початкові метадані не змінюються.",
            "current": "Лише поточна папка",
            "recursive": "Поточна папка та вкладені папки",
            "loading": "Пошук у вкладених папках: папок — {folders}, фотографій — {files}…",
            "failed": "Не вдалося завершити пошук у вкладених папках. Натисніть Оновити або зніміть позначку Включати вкладені папки.",
            "hiddenZero": "Приховано підозрілих координат (0,0) в області пошуку: {count}.",
            "shownZero": "Дозволено підозрілих координат (0,0) в області пошуку: {count}.",
            "partial": "Часткові результати: досягнуто межі пошуку. Виберіть меншу папку, щоб побачити решту.",
            "mapCount": "Карта: фотографій із GPS — {count} • {scope}",
            "indexNote": "Координати отримано з метаданих EXIF на сервері. Пошук не змінює фотографії та не очищає індекс.",
            "ready": "Готово",
            "busy": "Завантаження…",
            "error": "Помилка"
        },
        "de": {
            "group": "Suchoptionen der Karte",
            "subfolders": "Unterordner einbeziehen",
            "subfoldersTip": "Bilder in diesem Ordner und seinen Unterordnern suchen. Deaktivieren, um die Suche abzubrechen.",
            "zeros": "Auch verdächtige (0,0)-Positionen anzeigen",
            "zerosTip": "Null für Breiten- und Längengrad kann auf nicht initialisierte GPS-Daten hinweisen. Die ursprünglichen Metadaten bleiben unverändert.",
            "current": "Nur aktueller Ordner",
            "recursive": "Aktueller Ordner und Unterordner",
            "loading": "Unterordner werden durchsucht: {folders} Ordner, {files} Fotos…",
            "failed": "Die Suche in Unterordnern konnte nicht abgeschlossen werden. Aktualisieren wählen oder Unterordner einbeziehen deaktivieren.",
            "hiddenZero": "{count} verdächtige (0,0)-Positionen im Suchbereich ausgeblendet.",
            "shownZero": "{count} verdächtige (0,0)-Positionen im Suchbereich zugelassen.",
            "partial": "Teilergebnis: Suchlimit erreicht. Einen kleineren Ordner wählen, um den Rest zu sehen.",
            "mapCount": "Karte: {count} GPS-Fotos • {scope}",
            "indexNote": "Die Positionen stammen aus den EXIF-Metadaten des Servers. Diese Suche ändert keine Fotos und leert den Index nicht.",
            "ready": "Bereit",
            "busy": "Wird geladen…",
            "error": "Fehler"
        },
        "et": {
            "group": "Kaardi otsinguvalikud",
            "subfolders": "Kaasa alamkaustad",
            "subfoldersTip": "Otsi pilte sellest kaustast ja selle alamkaustadest. Otsingu katkestamiseks eemalda märge.",
            "zeros": "Näita ka kahtlaseid (0,0)-asukohti",
            "zerosTip": "Null nii laius- kui ka pikkuskraadina võib tähendada algväärtustamata GPS-andmeid. Algseid metaandmeid ei muudeta.",
            "current": "Ainult praegune kaust",
            "recursive": "Praegune kaust ja alamkaustad",
            "loading": "Otsitakse alamkaustadest: {folders} kausta, {files} fotot…",
            "failed": "Alamkaustade otsingut ei saanud lõpule viia. Vali Värskenda või eemalda märge Kaasa alamkaustad.",
            "hiddenZero": "Otsingualas peideti {count} kahtlast (0,0)-asukohta.",
            "shownZero": "Otsingualas on lubatud {count} kahtlast (0,0)-asukohta.",
            "partial": "Osalised tulemused: otsingu piirang on saavutatud. Ülejäänu nägemiseks vali väiksem kaust.",
            "mapCount": "Kaart: {count} GPS-fotot • {scope}",
            "indexNote": "Asukohad pärinevad serveri EXIF-metaandmetest. Otsing ei muuda fotosid ega tühjenda indeksit.",
            "ready": "Valmis",
            "busy": "Laadimine…",
            "error": "Viga"
        },
        "pl": {
            "group": "Opcje wyszukiwania na mapie",
            "subfolders": "Uwzględnij podfoldery",
            "subfoldersTip": "Wyszukuj obrazy w tym folderze i jego podfolderach. Usuń zaznaczenie, aby anulować wyszukiwanie.",
            "zeros": "Pokaż także podejrzane lokalizacje (0,0)",
            "zerosTip": "Zerowa szerokość i długość geograficzna mogą oznaczać niezainicjowane dane GPS. Oryginalne metadane pozostają bez zmian.",
            "current": "Tylko bieżący folder",
            "recursive": "Bieżący folder i podfoldery",
            "loading": "Przeszukiwanie podfolderów: foldery: {folders}, zdjęcia: {files}…",
            "failed": "Nie udało się ukończyć przeszukiwania podfolderów. Wybierz Odśwież lub odznacz Uwzględnij podfoldery.",
            "hiddenZero": "Ukryto podejrzane lokalizacje (0,0) w obszarze wyszukiwania: {count}.",
            "shownZero": "Dopuszczono podejrzane lokalizacje (0,0) w obszarze wyszukiwania: {count}.",
            "partial": "Wyniki częściowe: osiągnięto limit wyszukiwania. Wybierz mniejszy folder, aby zobaczyć pozostałe wyniki.",
            "mapCount": "Mapa: zdjęcia z GPS: {count} • {scope}",
            "indexNote": "Lokalizacje pochodzą z metadanych EXIF serwera. Wyszukiwanie nie zmienia zdjęć ani nie czyści indeksu.",
            "ready": "Gotowe",
            "busy": "Ładowanie…",
            "error": "Błąd"
        },
        "es": {
            "group": "Opciones de búsqueda del mapa",
            "subfolders": "Incluir subcarpetas",
            "subfoldersTip": "Buscar imágenes en esta carpeta y sus subcarpetas. Desmarca la opción para cancelar la búsqueda.",
            "zeros": "Mostrar también ubicaciones sospechosas (0,0)",
            "zerosTip": "Una latitud y una longitud iguales a cero pueden indicar datos GPS sin inicializar. Los metadatos originales no se modifican.",
            "current": "Solo la carpeta actual",
            "recursive": "Carpeta actual y subcarpetas",
            "loading": "Buscando en subcarpetas: {folders} carpetas, {files} fotos…",
            "failed": "No se pudo completar la búsqueda en subcarpetas. Pulsa Actualizar o desmarca Incluir subcarpetas.",
            "hiddenZero": "{count} ubicaciones sospechosas (0,0) ocultas en este ámbito.",
            "shownZero": "{count} ubicaciones sospechosas (0,0) permitidas en este ámbito.",
            "partial": "Resultados parciales: se alcanzó el límite de búsqueda. Selecciona una carpeta más pequeña para ver el resto.",
            "mapCount": "Mapa: {count} fotos con GPS • {scope}",
            "indexNote": "Las ubicaciones proceden de los metadatos EXIF del servidor. Esta búsqueda no modifica las fotos ni vacía el índice.",
            "ready": "Listo",
            "busy": "Cargando…",
            "error": "Error"
        },
        "fr": {
            "group": "Options de recherche de la carte",
            "subfolders": "Inclure les sous-dossiers",
            "subfoldersTip": "Rechercher des images dans ce dossier et ses sous-dossiers. Décochez cette option pour annuler la recherche.",
            "zeros": "Afficher aussi les positions suspectes (0,0)",
            "zerosTip": "Une latitude et une longitude nulles peuvent indiquer des données GPS non initialisées. Les métadonnées originales ne sont pas modifiées.",
            "current": "Dossier actuel uniquement",
            "recursive": "Dossier actuel et sous-dossiers",
            "loading": "Recherche dans les sous-dossiers : {folders} dossiers, {files} photos…",
            "failed": "La recherche dans les sous-dossiers n’a pas pu être terminée. Choisissez Actualiser ou décochez Inclure les sous-dossiers.",
            "hiddenZero": "{count} positions suspectes (0,0) masquées dans cette zone de recherche.",
            "shownZero": "{count} positions suspectes (0,0) autorisées dans cette zone de recherche.",
            "partial": "Résultats partiels : limite de recherche atteinte. Sélectionnez un dossier plus petit pour voir le reste.",
            "mapCount": "Carte : {count} photos avec GPS • {scope}",
            "indexNote": "Les positions proviennent des métadonnées EXIF du serveur. Cette recherche ne modifie pas les photos et ne vide pas l’index.",
            "ready": "Prêt",
            "busy": "Chargement…",
            "error": "Erreur"
        },
        "it": {
            "group": "Opzioni di ricerca della mappa",
            "subfolders": "Includi sottocartelle",
            "subfoldersTip": "Cerca immagini in questa cartella e nelle sue sottocartelle. Deseleziona per annullare la ricerca.",
            "zeros": "Mostra anche le posizioni sospette (0,0)",
            "zerosTip": "Latitudine e longitudine pari a zero possono indicare dati GPS non inizializzati. I metadati originali non vengono modificati.",
            "current": "Solo cartella corrente",
            "recursive": "Cartella corrente e sottocartelle",
            "loading": "Ricerca nelle sottocartelle: {folders} cartelle, {files} foto…",
            "failed": "Impossibile completare la ricerca nelle sottocartelle. Scegli Aggiorna o deseleziona Includi sottocartelle.",
            "hiddenZero": "{count} posizioni sospette (0,0) nascoste in questo ambito.",
            "shownZero": "{count} posizioni sospette (0,0) consentite in questo ambito.",
            "partial": "Risultati parziali: raggiunto il limite di ricerca. Seleziona una cartella più piccola per vedere il resto.",
            "mapCount": "Mappa: {count} foto con GPS • {scope}",
            "indexNote": "Le posizioni provengono dai metadati EXIF del server. La ricerca non modifica le foto né svuota l’indice.",
            "ready": "Pronto",
            "busy": "Caricamento…",
            "error": "Errore"
        },
        "tr": {
            "group": "Harita arama seçenekleri",
            "subfolders": "Alt klasörleri dahil et",
            "subfoldersTip": "Bu klasördeki ve alt klasörlerindeki resimleri ara. Aramayı iptal etmek için işareti kaldırın.",
            "zeros": "Şüpheli (0,0) konumlarını da göster",
            "zerosTip": "Enlem ve boylamın sıfır olması, GPS verilerinin başlatılmamış olduğunu gösterebilir. Orijinal meta veriler değiştirilmez.",
            "current": "Yalnızca geçerli klasör",
            "recursive": "Geçerli klasör ve alt klasörler",
            "loading": "Alt klasörler taranıyor: {folders} klasör, {files} fotoğraf…",
            "failed": "Alt klasör taraması tamamlanamadı. Yenile seçeneğini kullanın veya Alt klasörleri dahil et işaretini kaldırın.",
            "hiddenZero": "Bu arama kapsamında {count} şüpheli (0,0) konumu gizlendi.",
            "shownZero": "Bu arama kapsamında {count} şüpheli (0,0) konumuna izin veriliyor.",
            "partial": "Kısmi sonuçlar: tarama sınırına ulaşıldı. Kalanları görmek için daha küçük bir klasör seçin.",
            "mapCount": "Harita: {count} GPS fotoğrafı • {scope}",
            "indexNote": "Konumlar sunucunun EXIF meta verilerinden alınır. Bu arama fotoğrafları değiştirmez veya dizini temizlemez.",
            "ready": "Hazır",
            "busy": "Yükleniyor…",
            "error": "Hata"
        }
    };
    const languages = Object.freeze(Object.keys(messages));
    const messageKeys = Object.freeze(Object.keys(messages.en));

    // Match the language codes and regional normalization of PQNAS_I18N.
    // Dictionaries travel with the app ZIP; no server/static update is needed.
    function normalizeLanguage(language) {
        const raw = String(language || "").trim().toLowerCase().replace(/_/g, "-");
        if (Object.prototype.hasOwnProperty.call(messages, raw)) return raw;
        const base = raw.split("-")[0];
        return Object.prototype.hasOwnProperty.call(messages, base) ? base : "en";
    }
    function text(language, key, values = {}) {
        const dictionary = messages[normalizeLanguage(language)];
        const has = (object, name) => Object.prototype.hasOwnProperty.call(object, name);
        const value = has(dictionary, key) ? dictionary[key] : has(messages.en, key) ? messages.en[key] : String(key);
        // One pass: values are data, never HTML or another translation template.
        return value.replace(/\{([A-Za-z][A-Za-z0-9_]*)\}/g, (placeholder, name) =>
            values && has(values, name) ? String(values[name] ?? "") : placeholder);
    }
    return Object.freeze({ coordinate, classifyGps, hasUsableGps, counts, cleanPath, childPath, createScanner,
        text, languages, messageKeys, normalizeLanguage });
});
