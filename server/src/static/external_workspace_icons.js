(function () {
    "use strict";

    function fileExtLower(name) {
        const n = String(name || "").toLowerCase().trim();
        const slash = Math.max(n.lastIndexOf("/"), n.lastIndexOf("\\"));
        const base = slash >= 0 ? n.slice(slash + 1) : n;

        if (base.startsWith(".") && base.indexOf(".", 1) === -1) return "";

        if (base.endsWith(".tar.gz")) return "gz";
        if (base.endsWith(".tar.bz2")) return "bz2";
        if (base.endsWith(".tar.xz")) return "xz";

        const dot = base.lastIndexOf(".");
        if (dot <= 0 || dot === base.length - 1) return "";
        return base.slice(dot + 1);
    }

    function normalizeIconExt(ext) {
        const e = String(ext || "").toLowerCase();

        const alias = {
            jpeg: "jpg",
            htm: "html",
            yml: "yaml",
            cxx: "cpp",
            hh: "hpp",
            hxx: "hpp",
            markdown: "md",
            text: "txt",
            cfg: "conf"
        };

        return alias[e] || e;
    }

    function iconMap() {
        return (window.PQNAS_FILE_ICONS && typeof window.PQNAS_FILE_ICONS === "object")
            ? window.PQNAS_FILE_ICONS
            : {};
    }

    function iconMarkupFor(name, isDir) {
        const icons = iconMap();

        if (isDir) {
            return icons.folder || icons.directory || icons.default || "";
        }

        const ext = normalizeIconExt(fileExtLower(name));
        if (ext && icons[ext]) return icons[ext];

        const genericMap = {
            mp4: "generic_video",
            mov: "generic_video",
            mkv: "generic_video",
            avi: "generic_video",
            webm: "generic_video",

            mp3: "generic_audio",
            wav: "generic_audio",
            flac: "generic_audio",
            ogg: "generic_audio",
            m4a: "generic_audio",
            aac: "generic_audio",

            js: "generic_code",
            jsx: "generic_code",
            ts: "generic_code",
            tsx: "generic_code",
            py: "generic_code",
            c: "generic_code",
            cc: "generic_code",
            cpp: "generic_code",
            cxx: "generic_code",
            h: "generic_code",
            hh: "generic_code",
            hpp: "generic_code",
            hxx: "generic_code",
            java: "generic_code",
            php: "generic_code",
            go: "generic_code",
            rs: "generic_code",
            rb: "generic_code",
            lua: "generic_code",
            swift: "generic_code",
            kt: "generic_code",
            sh: "generic_code",
            bash: "generic_code",
            ps1: "generic_code",
            zsh: "generic_code",
            css: "generic_code",
            html: "generic_code",
            htm: "generic_code",
            json: "generic_code",
            xml: "generic_code",
            yaml: "generic_code",
            yml: "generic_code",
            toml: "generic_code",
            sql: "generic_code",

            zip: "generic_archive",
            rar: "generic_archive",
            gz: "generic_archive",
            bz2: "generic_archive",
            xz: "generic_archive",
            tgz: "generic_archive",
            tar: "generic_archive",
            "7z": "generic_archive",

            png: "generic_image",
            jpg: "generic_image",
            jpeg: "generic_image",
            gif: "generic_image",
            bmp: "generic_image",
            tiff: "generic_image",
            webp: "generic_image",
            heic: "generic_image",
            svg: "generic_image",
            ico: "generic_image",

            pdf: "generic_document",
            txt: "generic_document",
            md: "generic_document",
            doc: "generic_document",
            docx: "generic_document",
            odt: "generic_document",
            rtf: "generic_document",

            xls: "generic_spreadsheet",
            xlsx: "generic_spreadsheet",
            csv: "generic_spreadsheet",
            tsv: "generic_spreadsheet",
            ods: "generic_spreadsheet",

            ppt: "generic_presentation",
            pptx: "generic_presentation",
            odp: "generic_presentation",

            db: "generic_database",
            sqlite: "generic_database"
        };

        if (ext && genericMap[ext] && icons[genericMap[ext]]) {
            return icons[genericMap[ext]];
        }

        return icons.default || "";
    }

    function fileIconHtml(name, isDir) {
        const svg = iconMarkupFor(name, !!isDir);
        if (svg && String(svg).trim().startsWith("<svg")) {
            return `<div class="fileIcon svgFileIcon" aria-hidden="true">${svg}</div>`;
        }

        return `<div class="fileIcon">${isDir ? "📁" : "📄"}</div>`;
    }

    window.PQNAS_EXTERNAL_ICONS = {
        fileIconHtml,
        iconMarkupFor,
        normalizeIconExt,
        fileExtLower
    };
})();
