#!/usr/bin/env python3
from __future__ import annotations

import sys
import zipfile
import posixpath
import xml.etree.ElementTree as ET
from pathlib import PurePosixPath


REL_NS = "{http://schemas.openxmlformats.org/package/2006/relationships}"
OFFICE_REL_NS = "{http://schemas.openxmlformats.org/officeDocument/2006/relationships}"
XDR_NS = "{http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing}"
A_NS = "{http://schemas.openxmlformats.org/drawingml/2006/main}"


def usage() -> None:
    print("usage: tools/dev/inspect_xlsx_images.py <workbook.xlsx>")


def local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def read_xml(zf: zipfile.ZipFile, name: str) -> ET.Element | None:
    try:
        data = zf.read(name)
    except KeyError:
        return None

    try:
        return ET.fromstring(data)
    except ET.ParseError as e:
        print(f"WARN: failed to parse XML {name}: {e}")
        return None


def rels_path_for_part(part_name: str) -> str:
    folder, base = posixpath.split(part_name)
    return posixpath.join(folder, "_rels", base + ".rels")


def resolve_rel_target(source_part: str, target: str) -> str:
    # Security: relationship targets are treated as zip-internal POSIX paths only.
    # We never extract files to disk and never interpret these as filesystem paths.
    source_dir = posixpath.dirname(source_part)
    normalized = posixpath.normpath(posixpath.join(source_dir, target))
    return str(PurePosixPath(normalized))


def parse_rels(zf: zipfile.ZipFile, rels_name: str) -> dict[str, dict[str, str]]:
    root = read_xml(zf, rels_name)
    if root is None:
        return {}

    out: dict[str, dict[str, str]] = {}
    for rel in root.findall(f"{REL_NS}Relationship"):
        rel_id = rel.attrib.get("Id", "")
        if not rel_id:
            continue
        out[rel_id] = {
            "type": rel.attrib.get("Type", ""),
            "target": rel.attrib.get("Target", ""),
        }
    return out


def child_text(el: ET.Element | None, child_name: str) -> str:
    if el is None:
        return ""
    child = el.find(f"{XDR_NS}{child_name}")
    return "" if child is None or child.text is None else child.text.strip()


def anchor_marker(anchor: ET.Element, name: str) -> dict[str, str]:
    marker = anchor.find(f"{XDR_NS}{name}")
    if marker is None:
        return {}

    return {
        "col": child_text(marker, "col"),
        "colOff": child_text(marker, "colOff"),
        "row": child_text(marker, "row"),
        "rowOff": child_text(marker, "rowOff"),
    }


def inspect_drawing(zf: zipfile.ZipFile, drawing_path: str, sheet_path: str) -> list[dict[str, object]]:
    root = read_xml(zf, drawing_path)
    if root is None:
        return []

    drawing_rels = parse_rels(zf, rels_path_for_part(drawing_path))
    images: list[dict[str, object]] = []

    for anchor in root:
        anchor_type = local_name(anchor.tag)
        if anchor_type not in {"twoCellAnchor", "oneCellAnchor", "absoluteAnchor"}:
            continue

        pic = anchor.find(f"{XDR_NS}pic")
        if pic is None:
            continue

        c_nv_pr = pic.find(f"{XDR_NS}nvPicPr/{XDR_NS}cNvPr")
        blip = pic.find(f"{XDR_NS}blipFill/{A_NS}blip")

        embed = ""
        if blip is not None:
            embed = blip.attrib.get(f"{OFFICE_REL_NS}embed", "")

        media_path = ""
        media_rel = drawing_rels.get(embed)
        if media_rel and media_rel.get("target"):
            media_path = resolve_rel_target(drawing_path, media_rel["target"])

        images.append({
            "sheet": sheet_path,
            "drawing": drawing_path,
            "anchorType": anchor_type,
            "from": anchor_marker(anchor, "from"),
            "to": anchor_marker(anchor, "to"),
            "embed": embed,
            "media": media_path,
            "name": c_nv_pr.attrib.get("name", "") if c_nv_pr is not None else "",
            "descr": c_nv_pr.attrib.get("descr", "") if c_nv_pr is not None else "",
        })

    return images


def inspect_xlsx(path: str) -> int:
    with zipfile.ZipFile(path) as zf:
        names = set(zf.namelist())
        sheets = sorted(n for n in names if n.startswith("xl/worksheets/sheet") and n.endswith(".xml"))
        media = sorted(n for n in names if n.startswith("xl/media/"))
        drawings = sorted(n for n in names if n.startswith("xl/drawings/drawing") and n.endswith(".xml"))

        print(f"workbook: {path}")
        print(f"worksheets: {len(sheets)}")
        print(f"drawings:   {len(drawings)}")
        print(f"media:      {len(media)}")
        print()

        if media:
            print("media files:")
            for name in media:
                info = zf.getinfo(name)
                print(f"  - {name} ({info.file_size} bytes)")
            print()

        all_images: list[dict[str, object]] = []

        for sheet_path in sheets:
            sheet_root = read_xml(zf, sheet_path)
            if sheet_root is None:
                continue

            sheet_rels = parse_rels(zf, rels_path_for_part(sheet_path))
            drawing_refs = []

            for drawing in sheet_root.iter():
                if local_name(drawing.tag) != "drawing":
                    continue
                rel_id = drawing.attrib.get(f"{OFFICE_REL_NS}id", "")
                if rel_id:
                    drawing_refs.append(rel_id)

            if not drawing_refs:
                continue

            print(f"worksheet: {sheet_path}")
            for rel_id in drawing_refs:
                rel = sheet_rels.get(rel_id)
                if not rel:
                    print(f"  - drawing {rel_id}: missing relationship")
                    continue

                drawing_path = resolve_rel_target(sheet_path, rel.get("target", ""))
                print(f"  - drawing {rel_id}: {drawing_path}")

                for img in inspect_drawing(zf, drawing_path, sheet_path):
                    all_images.append(img)

            print()

        if all_images:
            print("image anchors:")
            for i, img in enumerate(all_images, 1):
                print(f"  image {i}:")
                print(f"    sheet:   {img['sheet']}")
                print(f"    drawing: {img['drawing']}")
                print(f"    media:   {img['media']}")
                print(f"    name:    {img['name']}")
                print(f"    descr:   {img['descr']}")
                print(f"    anchor:  {img['anchorType']}")
                print(f"    from:    {img['from']}")
                print(f"    to:      {img['to']}")
        else:
            print("No worksheet image anchors found.")

    return 0


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        usage()
        return 0

    try:
        return inspect_xlsx(argv[1])
    except FileNotFoundError:
        print(f"ERROR: file not found: {argv[1]}")
        return 0
    except zipfile.BadZipFile:
        print(f"ERROR: not a valid .xlsx zip file: {argv[1]}")
        return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
