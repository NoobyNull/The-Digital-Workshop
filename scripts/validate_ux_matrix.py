#!/usr/bin/env python3
"""Fail closed unless a Digital Workshop UX capture matrix is complete."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import sys
from pathlib import Path


ENVIRONMENTS = {
    "R1": (1366, 768, 1.0),
    "R2": (1366, 768, 1.5),
    "R3": (1366, 768, 2.0),
    "R4": (3840, 2160, 1.0),
    "R5": (3840, 2160, 1.5),
    "R6": (3840, 2160, 2.0),
}

SCENARIOS = (
    "guided-home",
    "library-start-project",
    "library-preview",
    "project-plan",
    "prepare-design-size",
    "prepare-material-blank",
    "prepare-choose-tool",
    "prepare-carve-preview",
    "review-missing",
    "review-ready",
    "run-streaming",
    "run-paused-abort-focus",
)

EXPECTED_STATE = {
    "guided-home": ("Home", "River Sign", "", "", None),
    "library-start-project": ("Design Library", "", "Primary", "", None),
    "library-preview": ("Design Library", "River Sign", "Preview Only", "", None),
    "project-plan": ("Project", "River Sign", "Primary", "", None),
    "prepare-design-size": ("Project", "River Sign", "Primary", "", "Design & Size"),
    "prepare-material-blank": (
        "Project", "River Sign", "Primary", "", "Material & Blank"
    ),
    "prepare-choose-tool": ("Project", "River Sign", "Primary", "", "Choose Tool"),
    "prepare-carve-preview": (
        "Project", "River Sign", "Primary", "", "Carve Preview"
    ),
    "review-missing": ("Project", "River Sign", "Primary", "", "Review & Run"),
    "review-ready": ("Project", "River Sign", "Primary", "", "Review & Run"),
    "run-streaming": ("Run CNC", "River Sign", "Primary", "", "Run CNC"),
    "run-paused-abort-focus": (
        "Run CNC", "River Sign", "Primary", "Hold to Abort", "Run CNC"
    ),
}


def png_dimensions(path: Path) -> tuple[int, int]:
    with path.open("rb") as stream:
        header = stream.read(24)
    if header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        raise ValueError("not a PNG")
    return struct.unpack(">II", header[16:24])


def unique_colors(path: Path) -> int:
    completed = subprocess.run(
        ["magick", str(path), "-sample", "64x64", "-format", "%k", "info:"],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if completed.returncode != 0:
        raise ValueError(f"ImageMagick sampling failed: {completed.stdout.strip()}")
    return int(completed.stdout.strip())


def fail(errors: list[str], message: str) -> None:
    errors.append(message)


def validate_entry(root: Path, entry: dict[str, object], errors: list[str]) -> None:
    environment = str(entry.get("environment", ""))
    scenario = str(entry.get("scenario", ""))
    label = f"{environment}/{scenario}"
    if environment not in ENVIRONMENTS or scenario not in SCENARIOS:
        fail(errors, f"{label}: unknown environment or scenario")
        return

    width, height, scale = ENVIRONMENTS[environment]
    ordinal = SCENARIOS.index(scenario) + 1
    image_name = str(entry.get("image", ""))
    image = root / image_name
    expected_name = (
        f"{environment}-{width}x{height}-s{int(scale * 100)}-"
        f"{ordinal:02d}-{scenario}.png"
    )
    if image_name != expected_name:
        fail(errors, f"{label}: image name is {image_name!r}, expected {expected_name!r}")
    if not image.is_file():
        fail(errors, f"{label}: image is missing")
        return

    try:
        dimensions = png_dimensions(image)
    except (OSError, ValueError) as exc:
        fail(errors, f"{label}: {exc}")
        return
    if dimensions != (width, height):
        fail(errors, f"{label}: PNG is {dimensions}, expected {(width, height)}")

    digest = hashlib.sha256(image.read_bytes()).hexdigest()
    if entry.get("image_sha256") != digest:
        fail(errors, f"{label}: SHA-256 does not match the manifest")
    if unique_colors(image) < 4:
        fail(errors, f"{label}: sampled capture has fewer than four unique colors")

    for key in ("requested_width", "actual_width"):
        if entry.get(key) != width:
            fail(errors, f"{label}: {key} does not equal {width}")
    for key in ("requested_height", "actual_height"):
        if entry.get(key) != height:
            fail(errors, f"{label}: {key} does not equal {height}")
    for key in ("requested_ui_scale", "actual_ui_scale"):
        if abs(float(entry.get(key, -1.0)) - scale) > 0.001:
            fail(errors, f"{label}: {key} does not equal {scale}")
    if entry.get("ordinal") != ordinal:
        fail(errors, f"{label}: ordinal does not equal {ordinal}")
    if entry.get("virtual_cnc") is not True:
        fail(errors, f"{label}: Virtual CNC is not active")

    route, project, design, focus, direct_stage = EXPECTED_STATE[scenario]
    expected = {
        "route": route,
        "project": project,
        "selected_design": design,
        "keyboard_focused_control": focus,
    }
    for key, value in expected.items():
        if entry.get(key) != value:
            fail(errors, f"{label}: {key} is {entry.get(key)!r}, expected {value!r}")
    if direct_stage is None:
        if "direct_stage" in entry:
            fail(errors, f"{label}: unexpected Direct Carve stage")
    elif entry.get("direct_stage") != direct_stage:
        fail(errors, f"{label}: Direct Carve stage is not {direct_stage!r}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("matrix", type=Path)
    args = parser.parse_args()
    root = args.matrix.resolve()
    manifest_path = root / "manifest.json"
    if shutil.which("magick") is None:
        raise RuntimeError("ImageMagick magick is required")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    captures = manifest.get("captures")
    if not isinstance(captures, list):
        raise ValueError("manifest captures must be an array")

    errors: list[str] = []
    pairs: set[tuple[str, str]] = set()
    images: set[str] = set()
    build_ids: set[str] = set()
    versions: set[str] = set()
    for entry in captures:
        if not isinstance(entry, dict):
            fail(errors, "manifest contains a non-object capture")
            continue
        pair = (str(entry.get("environment", "")), str(entry.get("scenario", "")))
        if pair in pairs:
            fail(errors, f"{pair[0]}/{pair[1]}: duplicate capture")
        pairs.add(pair)
        image_name = str(entry.get("image", ""))
        if image_name in images:
            fail(errors, f"{image_name}: duplicate image path")
        images.add(image_name)
        build_ids.add(str(entry.get("build_id", "")))
        versions.add(str(entry.get("version", "")))
        validate_entry(root, entry, errors)

    expected_pairs = {(environment, scenario) for environment in ENVIRONMENTS
                      for scenario in SCENARIOS}
    for environment, scenario in sorted(expected_pairs - pairs):
        fail(errors, f"{environment}/{scenario}: capture is missing")
    for environment, scenario in sorted(pairs - expected_pairs):
        fail(errors, f"{environment}/{scenario}: unexpected capture")
    if len(build_ids) != 1 or "" in build_ids:
        fail(errors, f"matrix has inconsistent build IDs: {sorted(build_ids)}")
    if len(versions) != 1 or "" in versions:
        fail(errors, f"matrix has inconsistent versions: {sorted(versions)}")

    if errors:
        for error in errors:
            print(f"FAIL {error}", file=sys.stderr)
        print(f"UX matrix failed with {len(errors)} issue(s)", file=sys.stderr)
        return 1
    print(
        f"PASS: {len(captures)} captures; 6 environments x 12 states; "
        f"build {next(iter(build_ids))}; version {next(iter(versions))}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as exc:
        print(f"validate_ux_matrix.py: {exc}", file=sys.stderr)
        raise SystemExit(1)
