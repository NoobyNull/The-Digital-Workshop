#!/usr/bin/env python3
"""Move Digital Workshop library model files into a local categorized root."""

from __future__ import annotations

import argparse
import re
import shutil
import sqlite3
import sys
from pathlib import Path


DEFAULT_DB = Path.home() / ".local/share/digitalworkshop/library.db"
DEFAULT_DEST = Path("/data/models")


def safe_part(value: str, fallback: str) -> str:
    value = value.strip() or fallback
    value = re.sub(r"[^\w.\- ]+", "_", value)
    value = re.sub(r"\s+", " ", value).strip()
    return value[:80] or fallback


def model_rows(conn: sqlite3.Connection) -> list[sqlite3.Row]:
    return list(
        conn.execute(
            """
            SELECT m.id, m.hash, m.name, m.file_format, m.file_path,
                   COALESCE(root.name, 'Uncategorized') AS root_category,
                   COALESCE(leaf.name, 'General') AS leaf_category
            FROM models m
            LEFT JOIN model_categories mc ON mc.model_id = m.id
            LEFT JOIN categories leaf ON leaf.id = mc.category_id
            LEFT JOIN categories root ON root.id = leaf.parent_id
            GROUP BY m.id
            ORDER BY m.id
            """
        )
    )


def source_for(row: sqlite3.Row, source_prefix: str | None, source_replacement: str | None) -> Path:
    source = str(row["file_path"])
    if source_prefix and source_replacement and source.startswith(source_prefix):
        source = source_replacement + source[len(source_prefix) :]
    return Path(source)


def destination_for(row: sqlite3.Row, dest_root: Path, source: Path) -> Path:
    ext = source.suffix
    if not ext:
        fmt = str(row["file_format"] or "").strip().lower()
        ext = f".{fmt}" if fmt else ".stl"

    root = safe_part(str(row["root_category"]), "Uncategorized")
    leaf = safe_part(str(row["leaf_category"]), "General")
    name = safe_part(str(row["name"]), f"model-{row['id']}")
    hash_value = str(row["hash"])
    filename = f"{name}--{hash_value}{ext.lower()}"
    return dest_root / root / leaf / filename


def same_file_reference(source: Path, dest: Path) -> bool:
    try:
        return source.resolve() == dest.resolve()
    except OSError:
        return False


def run(args: argparse.Namespace) -> int:
    conn = sqlite3.connect(args.db)
    conn.row_factory = sqlite3.Row
    rows = model_rows(conn)

    planned = []
    missing = []
    already_local = 0
    existing_dest = 0
    total_bytes = 0

    for row in rows:
        source = source_for(row, args.source_prefix, args.source_replacement)
        dest = destination_for(row, args.dest, source)
        if same_file_reference(source, dest):
            already_local += 1
            continue
        if dest.exists():
            existing_dest += 1
            planned.append((row, source, dest, "update-db"))
            continue
        if not source.exists():
            missing.append((row, source, dest))
            continue
        total_bytes += source.stat().st_size
        planned.append((row, source, dest, "move"))

    print(f"db={args.db}")
    print(f"dest={args.dest}")
    print(f"models={len(rows)} planned={len(planned)} already_local={already_local}")
    print(f"missing_sources={len(missing)} existing_destinations={existing_dest}")
    print(f"move_bytes={total_bytes}")

    if missing:
        print("missing sample:", file=sys.stderr)
        for row, source, _dest in missing[:20]:
            print(f"  model_id={row['id']} source={source}", file=sys.stderr)
        if not args.allow_missing:
            print("aborting because sources are missing; rerun with --allow-missing to skip them", file=sys.stderr)
            return 2

    for row, source, dest, action in planned[:10]:
        print(f"{action}: model_id={row['id']} {source} -> {dest}")

    if args.dry_run:
        return 0

    backup_path = args.db.with_name(args.db.name + f".backup-before-relocate")
    shutil.copy2(args.db, backup_path)
    print(f"backup={backup_path}")

    moved = 0
    updated = 0
    try:
        conn.execute("BEGIN IMMEDIATE")
        for row, source, dest, action in planned:
            dest.parent.mkdir(parents=True, exist_ok=True)
            if action == "move":
                shutil.move(str(source), str(dest))
                moved += 1
            conn.execute("UPDATE models SET file_path = ? WHERE id = ?", (str(dest), row["id"]))
            updated += 1
            if updated == 1 or updated % 250 == 0 or updated == len(planned):
                print(f"progress updated={updated}/{len(planned)} moved={moved}", flush=True)
        conn.commit()
    except Exception:
        conn.rollback()
        raise

    print(f"done moved={moved} updated={updated}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--db", type=Path, default=DEFAULT_DB)
    parser.add_argument("--dest", type=Path, default=DEFAULT_DEST)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--allow-missing", action="store_true")
    parser.add_argument("--source-prefix")
    parser.add_argument("--source-replacement")
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
