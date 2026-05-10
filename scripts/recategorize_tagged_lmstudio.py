#!/usr/bin/env python3
"""Recategorize already-tagged Digital Workshop models with a bounded taxonomy."""

from __future__ import annotations

import argparse
import json
import re
import sqlite3
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path


DEFAULT_DB = Path.home() / ".local/share/digitalworkshop/library.db"
DEFAULT_ENDPOINT = "http://127.0.0.1:1234/v1/chat/completions"
DEFAULT_MODEL = "qwen/qwen3.5-9b"

TAXONOMY: dict[str, dict[str, list[str]]] = {
    "Animals": {
        "Birds": ["Eagle", "Raptor", "Songbird", "General"],
        "Mammals": ["Bear", "Dog", "Horse", "Lion", "Wolf", "General"],
        "Mythical": ["Dragon", "Griffin", "Creature", "General"],
    },
    "Architecture": {
        "Columns": ["Fluted", "Capital", "Base", "General"],
        "Corbels": ["Scroll", "Leaf", "Bracket", "General"],
        "Moldings": ["Trim", "Panel", "Border", "General"],
        "Railings": ["Baluster", "Newel", "General"],
    },
    "Decor": {
        "Frames": ["Oval", "Picture", "Mirror", "Ornate", "General"],
        "Panels": ["Relief", "Medallion", "Wall Art", "General"],
        "Ornaments": ["Scroll", "Rosette", "Accent", "General"],
    },
    "Functional": {
        "Adapters": ["Ring", "Cylinder", "Mount", "General"],
        "Grilles": ["Vent", "Perforated", "Openwork", "General"],
        "Holders": ["Bracket", "Cover", "Stand", "General"],
    },
    "Kitchen": {
        "Boards": ["Cutting Board", "Serving Board", "General"],
        "Utensils": ["Spoon", "Tray", "General"],
    },
    "Nature": {
        "Botanical": ["Flower", "Leaf", "Tree", "Vine", "General"],
        "Landscape": ["Mountain", "Scene", "General"],
    },
    "Patterns": {
        "Scrollwork": ["Acanthus", "Baroque", "Rococo", "Floral", "General"],
        "Geometric": ["Celtic", "Abstract", "Texture", "General"],
    },
    "People": {
        "Figures": ["Bust", "Face", "Character", "Cherub", "General"],
        "Scenes": ["Family", "Portrait", "General"],
    },
    "Religious": {
        "Christian": ["Cross", "Icon", "Saint", "Angel", "General"],
        "Memorial": ["Plaque", "Emblem", "General"],
    },
    "Signs": {
        "Plaques": ["Text", "Logo", "Badge", "Emblem", "General"],
        "Labels": ["Number", "Name", "General"],
    },
    "Tools": {
        "Hand Tools": ["Wrench", "Fixture", "Jig", "General"],
        "Machine Parts": ["Hardware", "Instrument", "General"],
    },
    "Vehicles": {
        "Parts": ["Car", "Aircraft", "Boat", "Train", "General"],
    },
    "Other": {"General": ["Unsorted"]},
}


def model_rows(conn: sqlite3.Connection, limit: int | None) -> list[sqlite3.Row]:
    sql = """
        SELECT id, name, descriptor_title, descriptor_description, descriptor_hover, tags
        FROM models
        WHERE tag_status = 2
        ORDER BY id
    """
    if limit is not None:
        sql += " LIMIT ?"
        return list(conn.execute(sql, (limit,)))
    return list(conn.execute(sql))


def decode_tags(raw: str | None) -> list[str]:
    if not raw:
        return []
    try:
        values = json.loads(raw)
    except json.JSONDecodeError:
        return []
    if not isinstance(values, list):
        return []
    return [str(value) for value in values if str(value).strip()]


def classify(endpoint: str, model: str, row: sqlite3.Row) -> list[str]:
    taxonomy_lines = "\n".join(
        f"- {root}: "
        + "; ".join(f"{sub}: {', '.join(details)}" for sub, details in subs.items())
        for root, subs in TAXONOMY.items()
    )
    tags = ", ".join(decode_tags(row["tags"]))
    source = {
        "name": row["name"],
        "title": row["descriptor_title"] or "",
        "description": row["descriptor_description"] or "",
        "hover": row["descriptor_hover"] or "",
        "tags": tags,
    }
    body = {
        "model": model,
        "temperature": 0.1,
        "messages": [
            {
                "role": "system",
                "content": (
                    "Categorize one CNC model into one path with two to four levels. Use only "
                    "the taxonomy below. Return JSON only as "
                    "{\"categories\":[\"Root\",\"Subcategory\",\"Detail\",\"Specific\"]}.\n\n"
                    f"{taxonomy_lines}"
                ),
            },
            {
                "role": "user",
                "content": json.dumps(source, ensure_ascii=True),
            },
        ],
    }
    request = urllib.request.Request(
        endpoint,
        data=json.dumps(body).encode("utf-8"),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=120) as response:
        payload = json.loads(response.read().decode("utf-8"))

    content = payload["choices"][0]["message"]["content"].strip()
    content = extract_json_object(content)
    parsed = json.loads(content)
    chain = [str(value).strip() for value in parsed.get("categories", []) if str(value).strip()]
    return validate_chain(chain) or heuristic_category(row)


def extract_json_object(content: str) -> str:
    content = content.strip()
    if content.startswith("```"):
        content = content.strip("`")
        if content.startswith("json"):
            content = content[4:].strip()
    start = content.find("{")
    end = content.rfind("}")
    if start != -1 and end != -1 and start < end:
        return content[start : end + 1]
    return content


def validate_chain(chain: list[str]) -> list[str] | None:
    if not 2 <= len(chain) <= 4:
        return None
    root, sub = chain[0], chain[1]
    if root not in TAXONOMY or sub not in TAXONOMY[root]:
        return None
    allowed_details = TAXONOMY[root][sub]
    for value in chain[2:]:
        if value not in allowed_details:
            return None
    return chain


def heuristic_category(row: sqlite3.Row) -> list[str]:
    text = " ".join(
        [
            row["name"] or "",
            row["descriptor_title"] or "",
            row["descriptor_description"] or "",
            row["descriptor_hover"] or "",
            " ".join(decode_tags(row["tags"])),
        ]
    ).lower()
    checks = [
        (("vent", "ventilation"), ["Functional", "Grilles", "Vent"]),
        (("perforated", "openwork", "grille"), ["Functional", "Grilles", "Openwork"]),
        (("ring", "cylinder", "cylindrical"), ["Functional", "Adapters", "Ring"]),
        (("oval frame",), ["Decor", "Frames", "Oval"]),
        (("mirror",), ["Decor", "Frames", "Mirror"]),
        (("frame",), ["Decor", "Frames", "Ornate"]),
        (("baluster",), ["Architecture", "Railings", "Baluster"]),
        (("fluted",), ["Architecture", "Columns", "Fluted"]),
        (("column",), ["Architecture", "Columns", "General"]),
        (("corbel",), ["Architecture", "Corbels", "Bracket"]),
        (("molding",), ["Architecture", "Moldings", "Trim"]),
        (("acanthus",), ["Patterns", "Scrollwork", "Acanthus"]),
        (("baroque",), ["Patterns", "Scrollwork", "Baroque"]),
        (("rococo",), ["Patterns", "Scrollwork", "Rococo"]),
        (("scroll", "ornament"), ["Patterns", "Scrollwork", "General"]),
        (("flower", "floral"), ["Nature", "Botanical", "Flower"]),
        (("leaf", "leaves"), ["Nature", "Botanical", "Leaf"]),
        (("cross", "christ"), ["Religious", "Christian", "Cross"]),
        (("saint", "icon"), ["Religious", "Christian", "Icon"]),
        (("angel",), ["Religious", "Christian", "Angel"]),
        (("eagle",), ["Animals", "Birds", "Eagle"]),
        (("bird",), ["Animals", "Birds", "General"]),
        (("bear",), ["Animals", "Mammals", "Bear"]),
        (("dog",), ["Animals", "Mammals", "Dog"]),
        (("horse",), ["Animals", "Mammals", "Horse"]),
        (("lion",), ["Animals", "Mammals", "Lion"]),
        (("wolf",), ["Animals", "Mammals", "Wolf"]),
        (("dragon",), ["Animals", "Mythical", "Dragon"]),
        (("griffin",), ["Animals", "Mythical", "Griffin"]),
        (("logo",), ["Signs", "Plaques", "Logo"]),
        (("emblem",), ["Signs", "Plaques", "Emblem"]),
        (("plaque", "sign"), ["Signs", "Plaques", "General"]),
        (("cover",), ["Functional", "Holders", "Cover"]),
        (("holder", "bracket"), ["Functional", "Holders", "Bracket"]),
        (("tool", "wrench"), ["Tools", "Hand Tools", "Wrench"]),
        (("fixture", "jig"), ["Tools", "Hand Tools", "Fixture"]),
        (("cutting board",), ["Kitchen", "Boards", "Cutting Board"]),
        (("kitchen",), ["Kitchen", "Boards", "General"]),
        (("car", "truck"), ["Vehicles", "Parts", "Car"]),
        (("aircraft",), ["Vehicles", "Parts", "Aircraft"]),
        (("boat",), ["Vehicles", "Parts", "Boat"]),
        (("bust",), ["People", "Figures", "Bust"]),
        (("face",), ["People", "Figures", "Face"]),
        (("cherub",), ["People", "Figures", "Cherub"]),
        (("figure", "figurine"), ["People", "Figures", "General"]),
    ]
    for needles, category in checks:
        if any(has_term(text, needle) for needle in needles):
            return category
    return ["Other", "General", "Unsorted"]


def has_term(text: str, term: str) -> bool:
    if " " in term:
        return term in text
    return re.search(rf"\b{re.escape(term)}s?\b", text) is not None


def ensure_category(conn: sqlite3.Connection, name: str, parent_id: int | None) -> int:
    if parent_id is None:
        row = conn.execute(
            "SELECT id FROM categories WHERE name = ? AND parent_id IS NULL", (name,)
        ).fetchone()
    else:
        row = conn.execute(
            "SELECT id FROM categories WHERE name = ? AND parent_id = ?", (name, parent_id)
        ).fetchone()
    if row:
        return int(row["id"])
    cur = conn.execute(
        "INSERT INTO categories (name, parent_id) VALUES (?, ?)", (name, parent_id)
    )
    return int(cur.lastrowid)


def assign_category(conn: sqlite3.Connection, model_id: int, chain: list[str]) -> None:
    parent_id = None
    leaf_id = 0
    for name in chain[:4]:
        leaf_id = ensure_category(conn, name, parent_id)
        parent_id = leaf_id
    conn.execute("DELETE FROM model_categories WHERE model_id = ?", (model_id,))
    conn.execute(
        "INSERT OR IGNORE INTO model_categories (model_id, category_id) VALUES (?, ?)",
        (model_id, leaf_id),
    )


def prune_empty_categories(conn: sqlite3.Connection) -> int:
    removed = 0
    while True:
        rows = list(
            conn.execute(
                """
                SELECT c.id
                FROM categories c
                LEFT JOIN categories child ON child.parent_id = c.id
                LEFT JOIN model_categories mc ON mc.category_id = c.id
                WHERE child.id IS NULL AND mc.model_id IS NULL
                """
            )
        )
        if not rows:
            return removed
        conn.executemany("DELETE FROM categories WHERE id = ?", [(row["id"],) for row in rows])
        removed += len(rows)


def collapse_singleton_categories(conn: sqlite3.Connection) -> int:
    collapsed = 0
    while True:
        row = conn.execute(
            """
            SELECT c.id, c.parent_id, COUNT(mc.model_id) AS model_count
            FROM categories c
            JOIN model_categories mc ON mc.category_id = c.id
            LEFT JOIN categories child ON child.parent_id = c.id
            WHERE c.parent_id IS NOT NULL AND child.id IS NULL
            GROUP BY c.id
            HAVING model_count = 1
            ORDER BY c.id
            LIMIT 1
            """
        ).fetchone()
        if not row:
            return collapsed

        conn.execute(
            "UPDATE OR IGNORE model_categories SET category_id = ? WHERE category_id = ?",
            (row["parent_id"], row["id"]),
        )
        conn.execute("DELETE FROM model_categories WHERE category_id = ?", (row["id"],))
        conn.execute("DELETE FROM categories WHERE id = ?", (row["id"],))
        collapsed += 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--db", type=Path, default=DEFAULT_DB)
    parser.add_argument("--endpoint", default=DEFAULT_ENDPOINT)
    parser.add_argument("--model", default=DEFAULT_MODEL)
    parser.add_argument("--limit", type=int)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--heuristic-only", action="store_true")
    args = parser.parse_args()

    conn = sqlite3.connect(args.db)
    conn.row_factory = sqlite3.Row
    rows = model_rows(conn, args.limit)
    print(f"models={len(rows)} dry_run={args.dry_run}", flush=True)

    changed = 0
    started = time.time()
    try:
        if not args.dry_run:
            conn.execute("BEGIN IMMEDIATE")
        for index, row in enumerate(rows, start=1):
            chain = (
                heuristic_category(row)
                if args.heuristic_only
                else classify(args.endpoint, args.model, row)
            )
            if not args.dry_run:
                assign_category(conn, int(row["id"]), chain)
            changed += 1
            if index == 1 or index % 25 == 0 or index == len(rows):
                elapsed = time.time() - started
                print(
                    f"{index}/{len(rows)} model_id={row['id']} category={' > '.join(chain)} elapsed={elapsed:.1f}s",
                    flush=True,
                )
        removed = 0
        collapsed = 0
        if not args.dry_run:
            collapsed = collapse_singleton_categories(conn)
            removed = prune_empty_categories(conn)
            conn.commit()
        print(
            f"done changed={changed} collapsed_singleton_categories={collapsed} "
            f"removed_empty_categories={removed}",
            flush=True,
        )
        return 0
    except (urllib.error.URLError, urllib.error.HTTPError, json.JSONDecodeError, KeyError) as exc:
        if not args.dry_run:
            conn.rollback()
        print(f"failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
