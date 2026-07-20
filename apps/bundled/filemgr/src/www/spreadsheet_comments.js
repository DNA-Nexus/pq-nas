(function registerSpreadsheetComments(global) {
  "use strict";

  const MAX_COMMENT_CHARS = 8192;
  const MAX_AUTHOR_CHARS = 100;
  const MAX_COMMENTS_PER_WORKBOOK = 5000;
  const COMMENTS_CONTENT_TYPE =
    "application/vnd.openxmlformats-officedocument.spreadsheetml.comments+xml";
  const VML_CONTENT_TYPE =
    "application/vnd.openxmlformats-officedocument.vmlDrawing";
  const REL_COMMENTS_TYPE =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments";
  const REL_VML_TYPE =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing";

  let currentAuthorPromise = null;

  function cleanControlCharacters(value, keepLineBreaks) {
    const text = String(value == null ? "" : value);
    return text.replace(
      keepLineBreaks
        ? /[\u0000-\u0008\u000b\u000c\u000e-\u001f\u007f]/g
        : /[\u0000-\u001f\u007f]/g,
      ""
    );
  }

  function normalizeAuthor(value) {
    return cleanControlCharacters(value, false)
      .replace(/\s+/g, " ")
      .trim()
      .slice(0, MAX_AUTHOR_CHARS);
  }

  function normalizeCommentText(value) {
    return cleanControlCharacters(value, true)
      .replace(/\r\n?/g, "\n")
      .slice(0, MAX_COMMENT_CHARS);
  }

  function normalizeFingerprint(value) {
    return String(value || "")
      .trim()
      .replace(/[\s:-]+/g, "")
      .toLowerCase();
  }

  function shortFingerprint(value) {
    const fingerprint = normalizeFingerprint(value);
    if (!fingerprint) return "";
    if (fingerprint.length <= 16) return fingerprint;
    return `${fingerprint.slice(0, 8)}…${fingerprint.slice(-6)}`;
  }

  function columnName(index) {
    let value = Number(index) + 1;
    if (!Number.isInteger(value) || value <= 0) return "";
    let out = "";
    while (value > 0) {
      const remainder = (value - 1) % 26;
      out = String.fromCharCode(65 + remainder) + out;
      value = Math.floor((value - 1) / 26);
    }
    return out;
  }

  function columnIndex(name) {
    const text = String(name || "").toUpperCase();
    if (!/^[A-Z]+$/.test(text)) return -1;
    let out = 0;
    for (const character of text) {
      out = out * 26 + character.charCodeAt(0) - 64;
    }
    return out - 1;
  }

  function cellRef(row, col) {
    if (!Number.isInteger(row) || row < 0 || !Number.isInteger(col) || col < 0) {
      return "";
    }
    return `${columnName(col)}${row + 1}`;
  }

  function parseCellRef(value) {
    const match = String(value || "")
      .trim()
      .match(/^\$?([A-Z]+)\$?([1-9][0-9]*)$/i);
    if (!match) return null;
    const row = Number(match[2]) - 1;
    const col = columnIndex(match[1]);
    if (!Number.isInteger(row) || row < 0 || col < 0) return null;
    return { row, col };
  }

  function normalizeComment(value) {
    const source = value && typeof value === "object" ? value : {};
    const text = normalizeCommentText(source.text ?? source.t ?? "");
    if (!text.trim()) return null;
    return {
      author: normalizeAuthor(source.author ?? source.a ?? ""),
      text
    };
  }

  function ensureComments(sheet) {
    if (!sheet || typeof sheet !== "object") return {};
    if (!sheet.comments || typeof sheet.comments !== "object" || Array.isArray(sheet.comments)) {
      sheet.comments = {};
    }
    return sheet.comments;
  }

  function getComment(sheet, row, col) {
    const ref = cellRef(row, col);
    if (!ref || !sheet || !sheet.comments) return null;
    return normalizeComment(sheet.comments[ref]);
  }

  function setComment(sheet, row, col, value) {
    const ref = cellRef(row, col);
    if (!ref || !sheet || typeof sheet !== "object") return false;
    const comments = ensureComments(sheet);
    const next = normalizeComment(value);
    const previous = normalizeComment(comments[ref]);
    if (!next) {
      if (!Object.prototype.hasOwnProperty.call(comments, ref)) return false;
      delete comments[ref];
      return true;
    }
    if (previous && previous.author === next.author && previous.text === next.text) {
      return false;
    }
    comments[ref] = next;
    return true;
  }

  function removeComment(sheet, row, col) {
    return setComment(sheet, row, col, null);
  }

  function stripDuplicatedAuthorPrefix(text, author) {
    const body = normalizeCommentText(text);
    const normalizedAuthor = normalizeAuthor(author);
    if (!normalizedAuthor) return body;
    const prefix = `${normalizedAuthor}:`;
    if (!body.startsWith(prefix)) return body;
    const remainder = body.slice(prefix.length);
    if (!/^(?:\r?\n)/.test(remainder)) return body;
    return remainder.replace(/^\r?\n/, "");
  }

  function commentsFromWorksheet(worksheet, options = {}) {
    const source = worksheet && typeof worksheet === "object" ? worksheet : {};
    const maxRows = Number.isInteger(options.maxRows) ? Math.max(0, options.maxRows) : Infinity;
    const maxCols = Number.isInteger(options.maxCols) ? Math.max(0, options.maxCols) : Infinity;
    const out = {};
    let count = 0;
    for (const address of Object.keys(source).sort()) {
      if (count >= MAX_COMMENTS_PER_WORKBOOK || address.startsWith("!")) continue;
      const position = parseCellRef(address);
      if (!position || position.row >= maxRows || position.col >= maxCols) continue;
      const cell = source[address];
      const rawComments = cell && Array.isArray(cell.c) ? cell.c : [];
      const raw = rawComments.find((item) => {
        if (!item || typeof item !== "object") return false;
        return normalizeCommentText(item.t ?? item.text ?? "").trim();
      });
      if (!raw) continue;
      const author = normalizeAuthor(raw.a ?? raw.author ?? "");
      const comment = normalizeComment({
        author,
        text: stripDuplicatedAuthorPrefix(raw.t ?? raw.text ?? "", author)
      });
      if (!comment) continue;
      out[cellRef(position.row, position.col)] = comment;
      count += 1;
    }
    return out;
  }

  function commentEntries(sheet) {
    const comments = sheet && sheet.comments && typeof sheet.comments === "object"
      ? sheet.comments
      : {};
    const out = [];
    for (const [reference, raw] of Object.entries(comments)) {
      if (out.length >= MAX_COMMENTS_PER_WORKBOOK) break;
      const position = parseCellRef(reference);
      const comment = normalizeComment(raw);
      if (!position || !comment) continue;
      out.push({
        ref: cellRef(position.row, position.col),
        row: position.row,
        col: position.col,
        author: comment.author || "DNA-Nexus user",
        text: comment.text
      });
    }
    out.sort((left, right) => left.row - right.row || left.col - right.col);
    return out;
  }

  function commentBounds(value) {
    const sheet = value && value.comments
      ? value
      : { comments: value };

    let rows = 0;
    let cols = 0;

    for (const item of commentEntries(sheet)) {
      rows = Math.max(rows, item.row + 1);
      cols = Math.max(cols, item.col + 1);
    }

    return { rows, cols };
  }

  function adjustAxis(sheet, axis, index, count, action) {
    if (!sheet || !["row", "column"].includes(axis) ||
        !Number.isInteger(index) || index < 0 ||
        !Number.isInteger(count) || count <= 0 ||
        !["insert", "delete"].includes(action)) {
      return false;
    }
    const next = {};
    let changed = false;
    for (const item of commentEntries(sheet)) {
      let row = item.row;
      let col = item.col;
      const coordinate = axis === "row" ? row : col;
      if (action === "insert") {
        if (coordinate >= index) {
          if (axis === "row") row += count;
          else col += count;
          changed = true;
        }
      } else {
        const lastDeleted = index + count - 1;
        if (coordinate >= index && coordinate <= lastDeleted) {
          changed = true;
          continue;
        }
        if (coordinate > lastDeleted) {
          if (axis === "row") row -= count;
          else col -= count;
          changed = true;
        }
      }
      next[cellRef(row, col)] = { author: item.author, text: item.text };
    }
    if (changed) sheet.comments = next;
    return changed;
  }

  function reorderRows(sheet, targetStartRow, sourceRows) {
    if (!sheet || !Number.isInteger(targetStartRow) || targetStartRow < 0 ||
        !Array.isArray(sourceRows) || !sourceRows.length ||
        sourceRows.some((row) => !Number.isInteger(row) || row < 0)) {
      return false;
    }
    const destinationBySource = new Map();
    sourceRows.forEach((sourceRow, offset) => {
      destinationBySource.set(sourceRow, targetStartRow + offset);
    });
    const next = {};
    let changed = false;
    for (const item of commentEntries(sheet)) {
      const row = destinationBySource.has(item.row)
        ? destinationBySource.get(item.row)
        : item.row;
      if (row !== item.row) changed = true;
      next[cellRef(row, item.col)] = { author: item.author, text: item.text };
    }
    if (changed) sheet.comments = next;
    return changed;
  }

  function reorderRange(sheet, range, sourceRows) {
    if (!sheet || !range || !Array.isArray(sourceRows)) return false;

    const row1 = Number(range.row1);
    const row2 = Number(range.row2);
    const col1 = Number(range.col1);
    const col2 = Number(range.col2);

    if (
      ![row1, row2, col1, col2].every(Number.isInteger) ||
      row1 < 0 ||
      row2 < row1 ||
      col1 < 0 ||
      col2 < col1 ||
      sourceRows.length !== row2 - row1 + 1 ||
      sourceRows.some((row) => (
        !Number.isInteger(row) ||
        row < row1 ||
        row > row2
      ))
    ) {
      return false;
    }

    const destinationBySource = new Map();
    sourceRows.forEach((sourceRow, offset) => {
      destinationBySource.set(sourceRow, row1 + offset);
    });

    const next = {};
    let changed = false;

    for (const item of commentEntries(sheet)) {
      const insideRange =
        item.row >= row1 &&
        item.row <= row2 &&
        item.col >= col1 &&
        item.col <= col2;

      const row = insideRange && destinationBySource.has(item.row)
        ? destinationBySource.get(item.row)
        : item.row;

      if (row !== item.row) changed = true;

      next[cellRef(row, item.col)] = {
        author: item.author,
        text: item.text
      };
    }

    if (changed) sheet.comments = next;
    return changed;
  }

  function xmlEscape(value) {
    return String(value == null ? "" : value)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;");
  }

  function xmlAttr(value) {
    return xmlEscape(value).replace(/"/g, "&quot;").replace(/'/g, "&apos;");
  }

  function prepareExport(sheets) {
    const exportedSheets = [];
    let total = 0;
    (Array.isArray(sheets) ? sheets : []).forEach((sheet, sheetIndex) => {
      if (total >= MAX_COMMENTS_PER_WORKBOOK) return;
      const entries = commentEntries(sheet).slice(0, MAX_COMMENTS_PER_WORKBOOK - total);
      if (!entries.length) return;
      total += entries.length;
      exportedSheets.push({
        sheetIndex,
        partNumber: sheetIndex + 1,
        commentsRelId: `rIdPqnasComments${sheetIndex + 1}`,
        vmlRelId: `rIdPqnasVmlComments${sheetIndex + 1}`,
        entries
      });
    });
    return exportedSheets.length ? { sheets: exportedSheets, count: total } : null;
  }

  function exportSheet(exportInfo, sheetIndex) {
    if (!exportInfo || !Array.isArray(exportInfo.sheets)) return null;
    return exportInfo.sheets.find((item) => item.sheetIndex === sheetIndex) || null;
  }

  function worksheetLegacyDrawingRelId(exportInfo, sheetIndex) {
    const item = exportSheet(exportInfo, sheetIndex);
    return item ? item.vmlRelId : "";
  }

  function commentsXmlForSheet(item) {
    const authors = [];
    const authorIds = new Map();
    for (const comment of item.entries) {
      const author = normalizeAuthor(comment.author) || "DNA-Nexus user";
      if (!authorIds.has(author)) {
        authorIds.set(author, authors.length);
        authors.push(author);
      }
    }
    const authorsXml = authors.map((author) => `<author>${xmlEscape(author)}</author>`).join("");
    const commentsXml = item.entries.map((comment) => {
      const author = normalizeAuthor(comment.author) || "DNA-Nexus user";
      const authorId = authorIds.get(author) || 0;
      return `<comment ref="${xmlAttr(comment.ref)}" authorId="${authorId}"><text><t xml:space="preserve">${xmlEscape(comment.text)}</t></text></comment>`;
    }).join("");
    return '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>' +
      '<comments xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">' +
      `<authors>${authorsXml}</authors><commentList>${commentsXml}</commentList></comments>`;
  }

  function vmlAnchor(comment) {
    return [comment.col, 15, comment.row, 2, comment.col + 2, 15, comment.row + 4, 4].join(", ");
  }

  function vmlDrawingXmlForSheet(item) {
    const shapes = item.entries.map((comment, index) => {
      const shapeId = 1025 + index;
      /* Excel document colors; these are not DNA-Nexus theme colors. */
      return `<v:shape id="_x0000_s${shapeId}" type="#_x0000_t202" style="position:absolute;margin-left:80pt;margin-top:5pt;width:144pt;height:79pt;z-index:1;visibility:hidden" fillcolor="#ffffe1" o:insetmode="auto"><v:fill color2="#ffffe1"/><v:shadow on="t" color="black" obscured="t"/><v:path o:connecttype="none"/><v:textbox style="mso-direction-alt:auto"><div style="text-align:left"/></v:textbox><x:ClientData ObjectType="Note"><x:MoveWithCells/><x:SizeWithCells/><x:Anchor>${vmlAnchor(comment)}</x:Anchor><x:AutoFill>False</x:AutoFill><x:Row>${comment.row}</x:Row><x:Column>${comment.col}</x:Column></x:ClientData></v:shape>`;
    }).join("");
    return '<?xml version="1.0" encoding="UTF-8"?>' +
      '<xml xmlns:v="urn:schemas-microsoft-com:vml" xmlns:o="urn:schemas-microsoft-com:office:office" xmlns:x="urn:schemas-microsoft-com:office:excel">' +
      '<o:shapelayout v:ext="edit"><o:idmap v:ext="edit" data="1"/></o:shapelayout>' +
      '<v:shapetype id="_x0000_t202" coordsize="21600,21600" o:spt="202" path="m,l,21600r21600,l21600,xe"><v:stroke joinstyle="miter"/><v:path gradientshapeok="t" o:connecttype="rect"/></v:shapetype>' +
      shapes + '</xml>';
  }

  function contentTypeDefaultsXml(exportInfo) {
    return exportInfo ? `<Default Extension="vml" ContentType="${VML_CONTENT_TYPE}"/>` : "";
  }

  function contentTypeOverridesXml(exportInfo) {
    if (!exportInfo || !Array.isArray(exportInfo.sheets)) return "";
    return exportInfo.sheets.map((item) =>
      `<Override PartName="/xl/comments${item.partNumber}.xml" ContentType="${COMMENTS_CONTENT_TYPE}"/>`
    ).join("");
  }

  function worksheetRelationshipXml(item) {
    return `<Relationship Id="${xmlAttr(item.commentsRelId)}" Type="${REL_COMMENTS_TYPE}" Target="../comments${item.partNumber}.xml"/>` +
      `<Relationship Id="${xmlAttr(item.vmlRelId)}" Type="${REL_VML_TYPE}" Target="../drawings/vmlDrawing${item.partNumber}.vml"/>`;
  }

  function appendRelationships(entries, item) {
    const name = `xl/worksheets/_rels/sheet${item.sheetIndex + 1}.xml.rels`;
    const relationshipXml = worksheetRelationshipXml(item);
    const existing = entries.find((entry) => entry && entry.name === name);
    if (existing) {
      if (typeof existing.data !== "string" || !existing.data.includes("</Relationships>")) {
        throw new Error(`cannot merge worksheet relationships for ${name}`);
      }
      existing.data = existing.data.replace("</Relationships>", `${relationshipXml}</Relationships>`);
      return;
    }
    entries.push({
      name,
      data: '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>' +
        '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">' +
        relationshipXml + '</Relationships>'
    });
  }

  function appendExportEntries(entries, exportInfo) {
    if (!Array.isArray(entries) || !exportInfo || !Array.isArray(exportInfo.sheets)) return;
    const existingNames = new Set(entries.map((entry) => entry && entry.name).filter(Boolean));
    for (const item of exportInfo.sheets) {
      const commentsName = `xl/comments${item.partNumber}.xml`;
      const vmlName = `xl/drawings/vmlDrawing${item.partNumber}.vml`;
      if (!existingNames.has(commentsName)) {
        entries.push({ name: commentsName, data: commentsXmlForSheet(item) });
        existingNames.add(commentsName);
      }
      if (!existingNames.has(vmlName)) {
        entries.push({ name: vmlName, data: vmlDrawingXmlForSheet(item) });
        existingNames.add(vmlName);
      }
      appendRelationships(entries, item);
    }
  }

  function findIdentityFields(payload) {
    const candidates = [payload, payload && payload.user, payload && payload.me,
      payload && payload.profile, payload && payload.actor]
      .filter((item) => item && typeof item === "object");
    const nameKeys = ["display_name", "displayName", "name", "username", "login"];
    const fingerprintKeys = ["fingerprint", "fp", "fp_hex", "fingerprint_hex",
      "user_fingerprint", "subject_fingerprint"];
    let name = "";
    let fingerprint = "";
    for (const candidate of candidates) {
      if (!name) {
        for (const key of nameKeys) {
          name = normalizeAuthor(candidate[key]);
          if (name) break;
        }
      }
      if (!fingerprint) {
        for (const key of fingerprintKeys) {
          fingerprint = normalizeFingerprint(candidate[key]);
          if (fingerprint) break;
        }
      }
    }
    return { name, fingerprint };
  }

  function peopleResolvedDisplayName(payload) {
    const person =
      payload &&
      payload.person &&
      typeof payload.person === "object"
        ? payload.person
        : {};

    return normalizeAuthor(
      person.display_name ||
      person.displayName ||
      person.name ||
      ""
    );
  }

  async function resolvePeopleDisplayNameForFingerprint(
    fingerprint
  ) {
    const clean =
      normalizeFingerprint(fingerprint);

    if (!clean) return "";

    try {
      /*
       * Privacy: the full fingerprint is sent only to the
       * authenticated People resolver. It is never stored in
       * workbook comments or exposed in spreadsheet UI.
       */
      const response = await fetch(
        `/api/v4/people/resolve?fingerprint=${encodeURIComponent(clean)}`,
        {
          method: "GET",
          credentials: "include",
          cache: "no-store",
          headers: {
            Accept: "application/json"
          }
        }
      );

      const payload = await response
        .json()
        .catch(() => null);

      if (
        !response.ok ||
        !payload ||
        payload.ok === false
      ) {
        return "";
      }

      return peopleResolvedDisplayName(payload);
    } catch (_) {
      return "";
    }
  }

  function currentAuthor() {
    if (currentAuthorPromise) {
      return currentAuthorPromise;
    }

    currentAuthorPromise = fetch(
      "/api/v4/me",
      {
        method: "GET",
        credentials: "include",
        cache: "no-store",
        headers: {
          Accept: "application/json"
        }
      }
    )
      .then(async (response) => {
        const payload = await response
          .json()
          .catch(() => null);

        if (!response.ok || !payload) {
          return "DNA-Nexus user";
        }

        const identity =
          findIdentityFields(payload);

        /*
         * Keep author labels consistent with File Manager
         * version history and version-flag attribution:
         * prefer the private People/Contacts display name.
         */
        const peopleName =
          await resolvePeopleDisplayNameForFingerprint(
            identity.fingerprint
          );

        return (
          peopleName ||
          identity.name ||
          shortFingerprint(
            identity.fingerprint
          ) ||
          "DNA-Nexus user"
        );
      })
      .catch(() => "DNA-Nexus user");

    return currentAuthorPromise;
  }

  function renderMarker(cell, sheet, row, col) {
    if (!cell || typeof document === "undefined") return null;
    const comment = getComment(sheet, row, col);
    if (!comment) return null;
    const marker = document.createElement("span");
    marker.className = "spreadsheetCommentMarker";
    marker.setAttribute("aria-hidden", "true");

    /*
     * Compatibility: render the note indicator as an application-created
     * SVG shape. Host themes may reset span borders globally, but workbook
     * data can never alter this fixed SVG geometry.
     */
    const svgNamespace =
      "http://www.w3.org/2000/svg";

    const markerSvg =
      document.createElementNS(
        svgNamespace,
        "svg"
      );

    markerSvg.setAttribute(
      "viewBox",
      "0 0 10 10"
    );
    markerSvg.setAttribute("width", "10");
    markerSvg.setAttribute("height", "10");
    markerSvg.setAttribute(
      "focusable",
      "false"
    );
    markerSvg.setAttribute(
      "aria-hidden",
      "true"
    );

    const markerPath =
      document.createElementNS(
        svgNamespace,
        "path"
      );

    markerPath.setAttribute(
      "d",
      "M0 0H10V10Z"
    );
    markerPath.setAttribute(
      "fill",
      "currentColor"
    );

    markerSvg.appendChild(markerPath);
    marker.appendChild(markerSvg);

    /* Security: workbook text is assigned as text, never parsed as HTML. */
    const description = comment.author
      ? `${comment.author}\n${comment.text}`
      : comment.text;

    marker.title = description;

    /*
     * Security: the CSS tooltip reads a plain DOM attribute. Comment
     * text is never parsed as markup or assigned through innerHTML.
     */
    cell.dataset.spreadsheetComment = description;
    cell.classList.add("spreadsheetCommentCell");
    cell.appendChild(marker);
    return marker;
  }

  function translate(options, key, fallback) {
    try {
      if (options && typeof options.tr === "function") {
        return options.tr(key, null, fallback);
      }
    } catch (_) {}
    return fallback;
  }

  function openEditor(options = {}) {
    if (typeof document === "undefined") return Promise.resolve(null);
    const existing = normalizeComment(options.comment);
    const author = normalizeAuthor(options.author) || "DNA-Nexus user";
    return new Promise((resolve) => {
      const modal = document.createElement("div");
      modal.className = "modal show spreadsheetCommentDialog";
      modal.setAttribute("role", "dialog");
      modal.setAttribute("aria-modal", "true");
      const card = document.createElement("div");
      card.className = "modalCard spreadsheetCommentDialogCard";
      const head = document.createElement("div");
      head.className = "modalHead";
      const title = document.createElement("div");
      title.className = "modalTitle";
      title.textContent = existing
        ? translate(options, "filemgr.spreadsheet_editor.comment_edit", "Edit comment")
        : translate(options, "filemgr.spreadsheet_editor.comment_add", "Add comment");
      head.appendChild(title);
      const body = document.createElement("div");
      body.className = "modalBody spreadsheetCommentDialogBody";
      const authorLine = document.createElement("div");
      authorLine.className = "spreadsheetCommentAuthorLine";
      authorLine.textContent = `${translate(options, "filemgr.spreadsheet_editor.comment_as", "Comment as")}: ${author}`;
      const textarea = document.createElement("textarea");
      textarea.className = "spreadsheetCommentTextarea";
      textarea.maxLength = MAX_COMMENT_CHARS;
      textarea.value = existing ? existing.text : "";
      textarea.placeholder = translate(options, "filemgr.spreadsheet_editor.comment_placeholder", "Write a comment…");
      body.appendChild(authorLine);
      body.appendChild(textarea);
      const foot = document.createElement("div");
      foot.className = "modalFoot";
      const spacer = document.createElement("div");
      spacer.className = "spreadsheetCommentDialogSpacer";
      const cancel = document.createElement("button");
      cancel.type = "button";
      cancel.className = "btn secondary";
      cancel.textContent = translate(options, "common.cancel", "Cancel");
      const save = document.createElement("button");
      save.type = "button";
      save.className = "btn";
      save.textContent = translate(options, "common.save", "Save");
      foot.appendChild(spacer);
      foot.appendChild(cancel);
      foot.appendChild(save);
      card.appendChild(head);
      card.appendChild(body);
      card.appendChild(foot);
      modal.appendChild(card);
      document.body.appendChild(modal);
      const finish = (result) => {
        document.removeEventListener("keydown", onKey, true);
        modal.remove();
        resolve(result);
      };
      const submit = () => finish({ author, text: normalizeCommentText(textarea.value) });
      const onKey = (event) => {
        if (event.key === "Escape") {
          event.preventDefault();
          event.stopPropagation();
          finish(null);
        } else if (event.key === "Enter" && (event.ctrlKey || event.metaKey)) {
          event.preventDefault();
          event.stopPropagation();
          submit();
        }
      };
      document.addEventListener("keydown", onKey, true);
      modal.addEventListener("click", (event) => {
        if (event.target === modal) finish(null);
      });
      cancel.addEventListener("click", () => finish(null));
      save.addEventListener("click", submit);
      global.setTimeout(() => {
        textarea.focus();
        textarea.setSelectionRange(textarea.value.length, textarea.value.length);
      }, 0);
    });
  }

  const api = Object.freeze({
    MAX_COMMENT_CHARS,
    MAX_AUTHOR_CHARS,
    MAX_COMMENTS_PER_WORKBOOK,
    normalizeAuthor,
    normalizeCommentText,
    normalizeFingerprint,
    shortFingerprint,
    cellRef,
    parseCellRef,
    normalizeComment,
    ensureComments,
    getComment,
    setComment,
    removeComment,
    commentsFromWorksheet,
    commentEntries,
    commentBounds,
    adjustAxis,
    reorderRows,
    reorderRange,
    prepareExport,
    worksheetLegacyDrawingRelId,
    commentsXmlForSheet,
    vmlDrawingXmlForSheet,
    contentTypeDefaultsXml,
    contentTypeOverridesXml,
    appendExportEntries,
    findIdentityFields,
    peopleResolvedDisplayName,
    resolvePeopleDisplayNameForFingerprint,
    currentAuthor,
    renderMarker,
    openEditor
  });

  if (typeof module !== "undefined" && module.exports) module.exports = api;
  if (global) {
    global.PQNAS_FILEMGR = global.PQNAS_FILEMGR || {};
    global.PQNAS_FILEMGR.spreadsheetComments = api;
  }
})(typeof window !== "undefined" ? window : globalThis);
