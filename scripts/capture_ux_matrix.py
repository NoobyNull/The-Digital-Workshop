#!/usr/bin/env python3
"""Capture the compile-gated Digital Workshop UX matrix under isolated Xvfb."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import queue
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time
from dataclasses import dataclass
from pathlib import Path

from Xlib import X, XK, display, error
from Xlib.ext import xtest


REPO_ROOT = Path(__file__).resolve().parents[1]
CONFIG_TEMPLATE = (
    REPO_ROOT
    / ".planning"
    / "project-centered-workshop-release"
    / "guided-smoke-config.ini"
)


@dataclass(frozen=True)
class CaptureEnvironment:
    name: str
    width: int
    height: int
    scale: float


ENVIRONMENTS = (
    CaptureEnvironment("R1", 1366, 768, 1.0),
    CaptureEnvironment("R2", 1366, 768, 1.5),
    CaptureEnvironment("R3", 1366, 768, 2.0),
    CaptureEnvironment("R4", 3840, 2160, 1.0),
    CaptureEnvironment("R5", 3840, 2160, 1.5),
    CaptureEnvironment("R6", 3840, 2160, 2.0),
)

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


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument(
        "--binary",
        type=Path,
        default=REPO_ROOT / "build-ux-capture" / "digital_workshop",
    )
    result.add_argument(
        "--output",
        type=Path,
        default=REPO_ROOT / "artifacts" / "ux-capture",
    )
    result.add_argument("--scenario", action="append", choices=SCENARIOS)
    result.add_argument(
        "--environment",
        action="append",
        choices=[item.name for item in ENVIRONMENTS],
    )
    result.add_argument("--hold-ms", type=int, default=5000)
    result.add_argument("--timeout", type=float, default=90.0)

    # The public supervisor invokes this worker only inside xvfb-run.
    result.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    result.add_argument("--environment-name", help=argparse.SUPPRESS)
    result.add_argument("--width", type=int, help=argparse.SUPPRESS)
    result.add_argument("--height", type=int, help=argparse.SUPPRESS)
    result.add_argument("--scale", type=float, help=argparse.SUPPRESS)
    result.add_argument("--worker-scenario", choices=SCENARIOS, help=argparse.SUPPRESS)
    result.add_argument("--result", type=Path, help=argparse.SUPPRESS)
    return result


def atomic_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def png_dimensions(path: Path) -> tuple[int, int]:
    with path.open("rb") as stream:
        header = stream.read(24)
    if header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        raise RuntimeError(f"capture is not a PNG: {path}")
    return struct.unpack(">II", header[16:24])


def render_config(profile: Path, width: int, height: int, scale: float) -> None:
    if not CONFIG_TEMPLATE.is_file():
        raise RuntimeError(f"clean-profile template is missing: {CONFIG_TEMPLATE}")
    config = CONFIG_TEMPLATE.read_text(encoding="utf-8")
    config = config.replace("[WIDTH]", str(width))
    config = config.replace("[HEIGHT]", str(height))
    config = config.replace("[SCALE]", str(scale))
    target = profile / "config" / "digitalworkshop" / "config.ini"
    target.parent.mkdir(parents=True)
    target.write_text(config, encoding="utf-8")
    target.chmod(0o600)


def clean_environment(profile: Path) -> dict[str, str]:
    result = os.environ.copy()
    directories = {
        "HOME": profile / "home",
        "XDG_CONFIG_HOME": profile / "config",
        "XDG_DATA_HOME": profile / "data",
        "XDG_CACHE_HOME": profile / "cache",
        "XDG_STATE_HOME": profile / "state",
        "XDG_RUNTIME_DIR": profile / "runtime",
        "TMPDIR": profile / "tmp",
    }
    for path in directories.values():
        path.mkdir(parents=True, exist_ok=True)
    directories["XDG_RUNTIME_DIR"].chmod(0o700)
    result.update({key: str(value) for key, value in directories.items()})
    result.update(
        SDL_VIDEODRIVER="x11",
        LIBGL_ALWAYS_SOFTWARE="1",
    )
    return result


def children(window):
    try:
        return window.query_tree().children
    except error.XError:
        return []


def find_app_window(connection: display.Display, timeout: float):
    deadline = time.monotonic() + timeout
    root = connection.screen().root
    while time.monotonic() < deadline:
        pending = list(children(root))
        while pending:
            window = pending.pop()
            try:
                name = window.get_wm_name() or ""
                attributes = window.get_attributes()
                geometry = window.get_geometry()
                if (
                    "Digital Workshop" in str(name)
                    and attributes.map_state == X.IsViewable
                    and geometry.width > 0
                    and geometry.height > 0
                ):
                    return window
            except error.XError:
                continue
            pending.extend(children(window))
        time.sleep(0.05)
    raise RuntimeError("Digital Workshop SDL window did not become viewable")


def window_geometry(connection: display.Display, window) -> dict[str, int]:
    geometry = window.get_geometry()
    translated = window.translate_coords(connection.screen().root, 0, 0)
    return {
        "x": int(translated.x),
        "y": int(translated.y),
        "width": int(geometry.width),
        "height": int(geometry.height),
    }


def send_keyboard_right(connection: display.Display, window) -> None:
    window.set_input_focus(X.RevertToParent, X.CurrentTime)
    connection.sync()
    time.sleep(0.1)
    right = connection.keysym_to_keycode(XK.string_to_keysym("Right"))
    if not right:
        raise RuntimeError("X11 did not provide a Right-arrow keycode")
    xtest.fake_input(connection, X.KeyPress, right)
    connection.sync()
    time.sleep(0.08)
    xtest.fake_input(connection, X.KeyRelease, right)
    connection.sync()


def line_reader(stream, output: queue.Queue[str], transcript: list[str]) -> None:
    for line in iter(stream.readline, ""):
        transcript.append(line)
        output.put(line)
    stream.close()


def wait_for_ready(
    process: subprocess.Popen[str],
    connection: display.Display,
    timeout: float,
) -> tuple[dict[str, object], object, list[str]]:
    lines: queue.Queue[str] = queue.Queue()
    transcript: list[str] = []
    reader = threading.Thread(
        target=line_reader,
        args=(process.stdout, lines, transcript),
        daemon=True,
    )
    reader.start()
    deadline = time.monotonic() + timeout
    window = None
    while time.monotonic() < deadline:
        if process.poll() is not None and lines.empty():
            break
        try:
            line = lines.get(timeout=min(0.25, max(0.01, deadline - time.monotonic())))
        except queue.Empty:
            continue
        stripped = line.strip()
        if stripped.startswith("DW_UX_CAPTURE_AWAIT_FOCUS="):
            window = window or find_app_window(connection, 5.0)
            send_keyboard_right(connection, window)
        if stripped.startswith("DW_UX_CAPTURE_READY="):
            metadata = json.loads(stripped.split("=", 1)[1])
            return metadata, window or find_app_window(connection, 5.0), transcript
        if stripped.startswith("DW_UX_CAPTURE_ERROR="):
            detail = "".join(transcript[-40:]).strip()
            raise RuntimeError(f"{stripped}\n{detail}")
    detail = "".join(transcript[-40:]).strip()
    raise RuntimeError(
        f"capture app exited or timed out before ready (code={process.poll()}):\n{detail}"
    )


def worker(args: argparse.Namespace) -> int:
    display_name = os.environ.get("DISPLAY", "")
    if not display_name or display_name in {":0", ":0.0"}:
        raise RuntimeError("capture worker requires its own non-desktop Xvfb DISPLAY")
    if not all(
        [
            args.environment_name,
            args.width,
            args.height,
            args.scale,
            args.worker_scenario,
            args.result,
        ]
    ):
        raise RuntimeError("capture worker arguments are incomplete")

    binary = args.binary.resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise RuntimeError(f"capture binary is not executable: {binary}")

    connection = display.Display()
    profile = Path(tempfile.mkdtemp(prefix="dw-ux-capture-"))
    process = None
    try:
        render_config(profile, args.width, args.height, args.scale)
        environment = clean_environment(profile)
        framebuffer_output = profile / "framebuffer.png"
        command = [
            str(binary),
            "--ux-capture",
            args.worker_scenario,
            "--ux-capture-hold-ms",
            str(args.hold_ms),
            "--ux-capture-output",
            str(framebuffer_output),
        ]
        process = subprocess.Popen(
            command,
            env=environment,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=1,
        )
        metadata, window, _ = wait_for_ready(process, connection, args.timeout)
        geometry = window_geometry(connection, window)
        scale_label = int(round(args.scale * 100))
        filename = (
            f"{args.environment_name}-{args.width}x{args.height}-s{scale_label}-"
            f"{int(metadata['ordinal']):02d}-{args.worker_scenario}.png"
        )
        output = args.output.resolve() / filename
        if not framebuffer_output.is_file():
            raise RuntimeError("application did not write its capture framebuffer")
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(framebuffer_output, output)
        image_width, image_height = png_dimensions(output)
        if (image_width, image_height) != (geometry["width"], geometry["height"]):
            raise RuntimeError(
                "uncropped window capture dimensions do not match X11 geometry: "
                f"PNG={image_width}x{image_height}, "
                f"X11={geometry['width']}x{geometry['height']}"
            )
        if (int(metadata["actual_width"]), int(metadata["actual_height"])) != (
            geometry["width"],
            geometry["height"],
        ):
            raise RuntimeError("application and X11 disagree about the window dimensions")
        if abs(float(metadata["ui_scale"]) - args.scale) > 0.001:
            raise RuntimeError("application did not load the requested UI scale")

        entry = {
            "environment": args.environment_name,
            "scenario": metadata["scenario"],
            "ordinal": metadata["ordinal"],
            "surface": metadata["surface"],
            "image": filename,
            "image_sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
            "requested_width": args.width,
            "requested_height": args.height,
            "actual_width": image_width,
            "actual_height": image_height,
            "actual_x": geometry["x"],
            "actual_y": geometry["y"],
            "requested_ui_scale": args.scale,
            "actual_ui_scale": metadata["ui_scale"],
            "build_id": metadata["build_id"],
            "version": metadata["version"],
            "route": metadata["route"],
            "project": metadata["project"],
            "selected_design": metadata["selected_design"],
            "keyboard_focused_control": metadata["keyboard_focused_control"],
            "virtual_cnc": metadata["virtual_cnc"],
            "window_id": hex(window.id),
            "display": display_name,
        }
        if "direct_stage" in metadata:
            entry["direct_stage"] = metadata["direct_stage"]
        atomic_json(args.result, entry)
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        connection.close()
        shutil.rmtree(profile, ignore_errors=True)
    return 0


def supervisor(args: argparse.Namespace) -> int:
    if shutil.which("xvfb-run") is None:
        raise RuntimeError("xvfb-run is required")
    binary = args.binary.resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise RuntimeError(f"capture binary is not executable: {binary}")

    environments = [
        item
        for item in ENVIRONMENTS
        if not args.environment or item.name in args.environment
    ]
    scenarios = args.scenario or list(SCENARIOS)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    captures: list[dict[str, object]] = []
    manifest = {
        "schema_version": 1,
        "binary": str(binary),
        "captures": captures,
    }
    manifest_path = output / "manifest.json"

    with tempfile.TemporaryDirectory(prefix="dw-ux-supervisor-") as temporary:
        result_path = Path(temporary) / "result.json"
        for environment in environments:
            for scenario in scenarios:
                command = [
                    "xvfb-run",
                    "-a",
                    "-s",
                    f"-screen 0 {environment.width}x{environment.height}x24 -dpi 96",
                    sys.executable,
                    str(Path(__file__).resolve()),
                    "--worker",
                    "--binary",
                    str(binary),
                    "--output",
                    str(output),
                    "--environment-name",
                    environment.name,
                    "--width",
                    str(environment.width),
                    "--height",
                    str(environment.height),
                    "--scale",
                    str(environment.scale),
                    "--worker-scenario",
                    scenario,
                    "--hold-ms",
                    str(args.hold_ms),
                    "--timeout",
                    str(args.timeout),
                    "--result",
                    str(result_path),
                ]
                result_path.unlink(missing_ok=True)
                print(f"capture {environment.name} {scenario}", flush=True)
                completed = subprocess.run(command, check=False)
                if completed.returncode != 0:
                    raise RuntimeError(
                        f"capture failed for {environment.name} {scenario} "
                        f"(exit {completed.returncode})"
                    )
                captures.append(json.loads(result_path.read_text(encoding="utf-8")))
                atomic_json(manifest_path, manifest)
    print(f"wrote {len(captures)} captures and {manifest_path}")
    return 0


def main() -> int:
    args = parser().parse_args()
    return worker(args) if args.worker else supervisor(args)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as exc:
        print(f"capture_ux_matrix.py: {exc}", file=sys.stderr)
        raise SystemExit(1)
