#!/usr/bin/env python3
from __future__ import annotations

import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ROUTES_DIR = ROOT / "server" / "src" / "routes"
CATALOG = ROOT / "server" / "src" / "static" / "admin_api_catalog_v1.json"
DOCS = ROOT / "docs" / "technical" / "api-main-routes.md"

METHODS = {
    "Get": "GET",
    "Post": "POST",
    "Patch": "PATCH",
    "Delete": "DELETE",
}

CLUSTER_ROUTE_RE = re.compile(
    r'server\.(Get|Post|Patch|Delete)\(\s*"(/api/v4/admin/cluster[^"]*)"',
    re.DOTALL,
)

DOC_ROUTE_RE = re.compile(
    r"\| `([A-Z]+)` \| "
    r"`(/api/v4/admin/cluster[^`]*)` \|"
)


def source_cluster_routes() -> set[tuple[str, str]]:
    routes: set[tuple[str, str]] = set()

    for source in ROUTES_DIR.glob("*"):
        if source.suffix not in {".cpp", ".cc", ".inc"}:
            continue

        text = source.read_text(
            encoding="utf-8",
            errors="replace",
        )

        for method_name, path in CLUSTER_ROUTE_RE.findall(text):
            routes.add((METHODS[method_name], path))

    return routes


def catalog_cluster_routes() -> set[tuple[str, str]]:
    payload = json.loads(CATALOG.read_text(encoding="utf-8"))

    return {
        (str(route["method"]), str(route["path"]))
        for route in payload["routes"]
        if str(route.get("path", "")).startswith(
            "/api/v4/admin/cluster"
        )
    }


def documented_cluster_routes() -> set[tuple[str, str]]:
    return set(
        DOC_ROUTE_RE.findall(
            DOCS.read_text(encoding="utf-8")
        )
    )


class AdminApiCatalogClusterRouteTests(unittest.TestCase):
    def test_catalog_matches_registered_cluster_routes(self) -> None:
        registered = source_cluster_routes()

        self.assertTrue(
            registered,
            "expected registered admin cluster routes",
        )
        self.assertEqual(
            catalog_cluster_routes(),
            registered,
            "Admin API Explorer cluster catalog drifted "
            "from registered backend routes",
        )

    def test_docs_match_registered_cluster_routes(self) -> None:
        self.assertEqual(
            documented_cluster_routes(),
            source_cluster_routes(),
            "api-main-routes.md cluster inventory drifted "
            "from registered backend routes",
        )

    def test_internal_node_routes_are_not_in_catalog(self) -> None:
        payload = json.loads(CATALOG.read_text(encoding="utf-8"))

        internal = [
            str(route.get("path", ""))
            for route in payload["routes"]
            if str(route.get("path", "")).startswith(
                "/internal/node/"
            )
        ]

        self.assertEqual(
            internal,
            [],
            "internal node routes must not be exposed "
            "in the Admin API Explorer catalog",
        )


if __name__ == "__main__":
    unittest.main()
