#!/usr/bin/env python3
"""Launch one reproducible River Sign novice-study attempt.

This runner creates an isolated profile and records machine-verifiable preflight
metadata. It never fills consent, eligibility, task observations, ratings, or
pass/fail results. Those facts must come from the real participant session.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import queue
import re
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import threading
import time
from datetime import datetime
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
RELEASE_DIR = REPO_ROOT / ".planning" / "project-centered-workshop-release"
DEFAULT_BINARY = REPO_ROOT / "build" / "digital_workshop"
DEFAULT_FIXTURES = REPO_ROOT / "tests" / "fixtures" / "ux" / "river_sign"
DEFAULT_STUDY_ROOT = (
    Path.home() / ".local" / "share" / "digital-workshop" / "river-sign-study"
)
CONFIG_TEMPLATE = RELEASE_DIR / "guided-smoke-config.ini"
BLANK_RESULTS = RELEASE_DIR / "river_sign_study_results.blank.json"
READY_PREFIX = "DW_STUDY_READY="
ERROR_PREFIX = "DW_STUDY_ERROR="
EXPECTED_MODELS = ["Primary", "Alternate", "Preview Only"]
FIXTURE_FILES = [
    "river_sign_primary.stl",
    "river_sign_alternate.stl",
    "river_sign_preview_only.stl",
]
PARTICIPANT_PATTERN = re.compile(r"P0[1-5]")


class StudyLaunchError(RuntimeError):
    """A fail-closed study setup or launch error."""


class StudyInterrupted(BaseException):
    """A facilitator or process manager interrupted the live attempt."""

    def __init__(self, signal_number: int):
        super().__init__(signal_number)
        self.signal_number = signal_number


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def atomic_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        "w", encoding="utf-8", dir=path.parent, delete=False
    ) as stream:
        os.fchmod(stream.fileno(), 0o600)
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write("\n")
        temporary = Path(stream.name)
    temporary.replace(path)
    path.chmod(0o600)


def require_private_directory(path: Path, description: str) -> None:
    if path.is_symlink():
        raise StudyLaunchError(f"{description} must not be a symlink: {path}")
    try:
        details = path.stat()
    except FileNotFoundError as error:
        raise StudyLaunchError(f"{description} does not exist: {path}") from error
    if not stat.S_ISDIR(details.st_mode):
        raise StudyLaunchError(f"{description} is not a directory: {path}")
    if hasattr(os, "geteuid") and details.st_uid != os.geteuid():
        raise StudyLaunchError(f"{description} is not owned by the current user: {path}")
    if os.name == "posix" and stat.S_IMODE(details.st_mode) != 0o700:
        raise StudyLaunchError(f"{description} must have permissions 0700: {path}")


def create_private_directory(path: Path, description: str) -> None:
    if path.is_symlink():
        raise StudyLaunchError(f"{description} must not be a symlink: {path}")
    try:
        path.mkdir(mode=0o700)
    except FileExistsError:
        if path.is_symlink() or not path.is_dir():
            raise StudyLaunchError(f"{description} is not a safe directory: {path}")
    path.chmod(0o700)
    require_private_directory(path, description)


def prepare_private_study_root(path: Path) -> None:
    if path.is_symlink():
        raise StudyLaunchError(f"study root must not be a symlink: {path}")
    if not path.exists():
        path.mkdir(parents=True, mode=0o700)
        path.chmod(0o700)
    require_private_directory(path, "study root")


def require_private_file(path: Path, description: str) -> None:
    if path.is_symlink():
        raise StudyLaunchError(f"{description} must not be a symlink: {path}")
    try:
        details = path.stat()
    except FileNotFoundError as error:
        raise StudyLaunchError(f"{description} does not exist: {path}") from error
    if not stat.S_ISREG(details.st_mode):
        raise StudyLaunchError(f"{description} is not a regular file: {path}")
    if hasattr(os, "geteuid") and details.st_uid != os.geteuid():
        raise StudyLaunchError(f"{description} is not owned by the current user: {path}")
    if os.name == "posix" and stat.S_IMODE(details.st_mode) != 0o600:
        raise StudyLaunchError(f"{description} must have permissions 0600: {path}")


def copy_private_file(source: Path, target: Path, description: str) -> None:
    if target.is_symlink() or target.exists():
        raise StudyLaunchError(f"{description} already exists: {target}")
    shutil.copyfile(source, target)
    target.chmod(0o600)
    require_private_file(target, description)


def validate_participant(value: str) -> str:
    if PARTICIPANT_PATTERN.fullmatch(value) is None:
        raise argparse.ArgumentTypeError("participant must be exactly P01 through P05")
    return value


def positive_attempt(value: str) -> int:
    try:
        attempt = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("attempt must be a positive integer") from error
    if attempt < 1:
        raise argparse.ArgumentTypeError("attempt must be a positive integer")
    return attempt


def render_config(profile: Path, ui_scale: float) -> Path:
    if not CONFIG_TEMPLATE.is_file():
        raise StudyLaunchError(f"missing config template: {CONFIG_TEMPLATE}")
    config = CONFIG_TEMPLATE.read_text(encoding="utf-8")
    config = config.replace("[WIDTH]", "1366")
    config = config.replace("[HEIGHT]", "768")
    config = config.replace("[SCALE]", f"{ui_scale:g}")
    if "[WIDTH]" in config or "[HEIGHT]" in config or "[SCALE]" in config:
        raise StudyLaunchError("config template replacement did not complete")
    create_private_directory(profile, "study profile")
    config_root = profile / "config"
    create_private_directory(config_root, "study config root")
    target_directory = config_root / "digitalworkshop"
    create_private_directory(target_directory, "study application config root")
    target = target_directory / "config.ini"
    target.write_text(config, encoding="utf-8")
    target.chmod(0o600)
    require_private_file(target, "study config")
    return target


def isolated_environment(profile: Path) -> dict[str, str]:
    environment = os.environ.copy()
    create_private_directory(profile, "study profile")
    directories = {
        "HOME": profile / "home",
        "XDG_CONFIG_HOME": profile / "config",
        "XDG_DATA_HOME": profile / "data",
        "XDG_CACHE_HOME": profile / "cache",
        "XDG_STATE_HOME": profile / "state",
        "XDG_RUNTIME_DIR": profile / "runtime",
        "TMPDIR": profile / "tmp",
    }
    for name, directory in directories.items():
        create_private_directory(directory, f"isolated {name} directory")
    environment.update({name: str(path) for name, path in directories.items()})
    return environment


def fixture_hashes(fixture_dir: Path) -> dict[str, str]:
    hashes: dict[str, str] = {}
    for name in FIXTURE_FILES:
        path = fixture_dir / name
        if not path.is_file():
            raise StudyLaunchError(f"missing River Sign fixture: {path}")
        hashes[name] = sha256_file(path)
    extras = sorted(path.name for path in fixture_dir.glob("*.stl")
                    if path.name not in FIXTURE_FILES)
    if extras:
        raise StudyLaunchError(f"fixture directory contains unexpected STL files: {extras}")
    return hashes


def static_cohort_lock(binary: Path, fixture_dir: Path, ui_scale: float) -> dict[str, Any]:
    display = os.environ.get("DISPLAY", "")
    if not display:
        raise StudyLaunchError("DISPLAY is not set; an interactive desktop is required")
    return {
        "schema_version": 1,
        "binary_path": str(binary),
        "binary_sha256": sha256_file(binary),
        "fixture_directory": str(fixture_dir),
        "fixture_sha256": fixture_hashes(fixture_dir),
        "window": {"width": 1366, "height": 768},
        "ui_scale": ui_scale,
        "platform": platform.platform(),
        "display": display,
        "session_type": os.environ.get("XDG_SESSION_TYPE", ""),
    }


def verify_existing_lock(lock_path: Path, expected: dict[str, Any]) -> dict[str, Any] | None:
    if lock_path.is_symlink():
        raise StudyLaunchError(f"cohort lock must not be a symlink: {lock_path}")
    if not lock_path.exists():
        return None
    require_private_file(lock_path, "cohort lock")
    try:
        actual = json.loads(lock_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise StudyLaunchError(f"cohort lock is unreadable: {error}") from error
    for key, value in expected.items():
        if actual.get(key) != value:
            raise StudyLaunchError(
                f"cohort lock mismatch for {key}: {actual.get(key)!r} != {value!r}"
            )
    return actual


def validate_ready(payload: dict[str, Any], ui_scale: float) -> None:
    expected = {
        "route": "Design Library",
        "model_names": EXPECTED_MODELS,
        "model_count": 3,
        "no_project": True,
        "virtual_cnc": True,
        "picker_purpose": "StartProject",
        "selection_count": 0,
        "membership_count": 0,
    }
    for key, value in expected.items():
        if payload.get(key) != value:
            raise StudyLaunchError(
                f"study readiness mismatch for {key}: {payload.get(key)!r} != {value!r}"
            )
    if abs(float(payload.get("ui_scale", -1.0)) - ui_scale) > 0.001:
        raise StudyLaunchError("study readiness reported the wrong UI scale")
    for key in ("version", "git_hash"):
        if not isinstance(payload.get(key), str) or not payload[key].strip():
            raise StudyLaunchError(f"study readiness omitted {key}")


def reader_thread(stream, messages: queue.Queue[str], log_path: Path) -> None:
    flags = os.O_WRONLY | os.O_CREAT | os.O_TRUNC
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    descriptor = os.open(log_path, flags, 0o600)
    os.fchmod(descriptor, 0o600)
    with os.fdopen(descriptor, "w", encoding="utf-8") as log:
        for line in iter(stream.readline, ""):
            log.write(line)
            log.flush()
            sys.stdout.write(line)
            sys.stdout.flush()
            messages.put(line.rstrip("\n"))
    stream.close()


def wait_for_ready(
    process: subprocess.Popen[str], messages: queue.Queue[str], timeout: float
) -> dict[str, Any]:
    deadline = time.monotonic() + timeout
    errors: list[str] = []
    while time.monotonic() < deadline:
        try:
            line = messages.get(timeout=0.1)
        except queue.Empty:
            if process.poll() is not None:
                break
            continue
        if line.startswith(ERROR_PREFIX):
            errors.append(line[len(ERROR_PREFIX):])
        if line.startswith(READY_PREFIX):
            try:
                value = json.loads(line[len(READY_PREFIX):])
            except json.JSONDecodeError as error:
                raise StudyLaunchError(f"malformed DW_STUDY_READY JSON: {error}") from error
            if not isinstance(value, dict):
                raise StudyLaunchError("DW_STUDY_READY must contain a JSON object")
            return value
    detail = f": {'; '.join(errors)}" if errors else ""
    raise StudyLaunchError(f"application did not reach study-ready state{detail}")


def stop_process(
    process: subprocess.Popen[str], signal_number: int = signal.SIGTERM
) -> int:
    status = process.poll()
    if status is not None:
        return status
    try:
        process.send_signal(signal_number)
    except ProcessLookupError:
        return process.wait()
    try:
        return process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        process.terminate()
    try:
        return process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        process.kill()
        return process.wait()


def record_process_exit(
    attempt_dir: Path,
    exit_status: int,
    preflight_only: bool,
    *,
    interrupt_signal: int | None = None,
    reached_ready: bool = False,
) -> None:
    result: dict[str, Any] = {
        "exited_at": datetime.now().astimezone().isoformat(),
        "exit_status": exit_status,
        "preflight_only": preflight_only,
        "interrupted": interrupt_signal is not None,
    }
    if interrupt_signal is not None:
        try:
            signal_name = signal.Signals(interrupt_signal).name
        except ValueError:
            signal_name = str(interrupt_signal)
        result.update(
            interrupt_signal=signal_name,
            machine_preflight="READY" if reached_ready else "NOT_READY",
            runner_exit_status=128 + interrupt_signal,
        )
    atomic_json(attempt_dir / "process-exit.json", result)


def preserve_blank_materials(study_root: Path, attempt_dir: Path) -> None:
    results = study_root / "river_sign_study_results.json"
    if results.is_symlink():
        raise StudyLaunchError(f"scorer input must not be a symlink: {results}")
    if not results.exists():
        copy_private_file(BLANK_RESULTS, results, "scorer input")
    else:
        require_private_file(results, "scorer input")
    for name in ("RIVER-SIGN-CONSENT.md", "RIVER-SIGN-SESSION-WORKSHEET.md"):
        source = RELEASE_DIR / name
        if source.is_file():
            copy_private_file(source, attempt_dir / name, name)


def reserve_attempt_directory(
    study_root: Path, participant: str, attempt: int
) -> Path:
    require_private_directory(study_root, "study root")
    participant_dir = study_root / participant
    create_private_directory(participant_dir, "participant directory")
    attempt_dir = participant_dir / f"attempt-{attempt:02d}"
    try:
        attempt_dir.mkdir(mode=0o700, exist_ok=False)
    except FileExistsError as error:
        raise StudyLaunchError(
            f"attempt already exists and will not be overwritten: {attempt_dir}"
        ) from error
    attempt_dir.chmod(0o700)
    require_private_directory(attempt_dir, "attempt directory")
    return attempt_dir


def finalize_cohort_lock(
    lock_path: Path, static_lock: dict[str, Any], ready: dict[str, Any]
) -> dict[str, Any]:
    complete = dict(static_lock)
    complete.update(version=ready["version"], git_hash=ready["git_hash"])
    existing = verify_existing_lock(lock_path, static_lock)
    if existing is not None:
        for key in ("version", "git_hash"):
            if existing.get(key) != complete[key]:
                raise StudyLaunchError(
                    f"cohort lock mismatch for {key}: {existing.get(key)!r} != {complete[key]!r}"
                )
        return existing
    atomic_json(lock_path, complete)
    return complete


def launch(args: argparse.Namespace) -> int:
    binary = args.binary.resolve()
    fixture_dir = args.fixture_dir.resolve()
    study_root = Path(os.path.abspath(args.study_root.expanduser()))
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise StudyLaunchError(f"binary is not executable: {binary}")
    if args.ui_scale != 1.0:
        raise StudyLaunchError("the fixed release cohort requires --ui-scale 1.0")

    prepare_private_study_root(study_root)
    static_lock = static_cohort_lock(binary, fixture_dir, args.ui_scale)
    lock_path = study_root / "cohort-lock.json"
    verify_existing_lock(lock_path, static_lock)

    attempt_dir = reserve_attempt_directory(
        study_root, args.participant, args.attempt)
    profile = attempt_dir / "profile"
    environment = isolated_environment(profile)
    render_config(profile, args.ui_scale)
    preserve_blank_materials(study_root, attempt_dir)

    command = [str(binary), "--river-sign-study", str(fixture_dir)]
    previous_umask = os.umask(0o077)
    try:
        process = subprocess.Popen(
            command,
            env=environment,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=1,
            start_new_session=os.name == "posix",
        )
    finally:
        os.umask(previous_umask)
    assert process.stdout is not None
    messages: queue.Queue[str] = queue.Queue()
    thread = threading.Thread(
        target=reader_thread,
        args=(process.stdout, messages, attempt_dir / "application.log"),
        daemon=True,
    )
    thread.start()

    received_signal: list[int | None] = [None]

    def interrupt_handler(signal_number: int, _frame: object) -> None:
        if received_signal[0] is None:
            received_signal[0] = signal_number
            raise StudyInterrupted(signal_number)

    previous_handlers = {
        signal_number: signal.signal(signal_number, interrupt_handler)
        for signal_number in (signal.SIGINT, signal.SIGTERM)
    }
    ready: dict[str, Any] | None = None
    try:
        ready = wait_for_ready(process, messages, args.timeout)
        validate_ready(ready, args.ui_scale)
        cohort_lock = finalize_cohort_lock(lock_path, static_lock, ready)
        preflight = {
            "schema_version": 1,
            "machine_preflight": "READY",
            "participant_slot": args.participant,
            "attempt": args.attempt,
            "attempt_id": f"{args.participant}-A{args.attempt:02d}",
            "created_at": datetime.now().astimezone().isoformat(),
            "command": command,
            "profile": str(profile),
            "cohort_lock_sha256": hashlib.sha256(
                json.dumps(cohort_lock, sort_keys=True).encode("utf-8")
            ).hexdigest(),
            "ready": ready,
        }
        atomic_json(attempt_dir / "preflight.json", preflight)
        print(f"STUDY_ATTEMPT_READY={preflight['attempt_id']}")
        print(f"STUDY_EVIDENCE_DIR={attempt_dir}")
        if args.preflight_only:
            exit_status = stop_process(process, signal.SIGTERM)
        else:
            exit_status = process.wait()
        record_process_exit(attempt_dir, exit_status, args.preflight_only)
        if args.preflight_only and exit_status in (0, -signal.SIGTERM):
            return 0
        return exit_status
    except StudyInterrupted as interruption:
        exit_status = stop_process(process, interruption.signal_number)
        record_process_exit(
            attempt_dir,
            exit_status,
            args.preflight_only,
            interrupt_signal=interruption.signal_number,
            reached_ready=ready is not None,
        )
        return 128 + interruption.signal_number
    except BaseException:
        stop_process(process)
        raise
    finally:
        for signal_number, previous in previous_handlers.items():
            signal.signal(signal_number, previous)
        thread.join(timeout=2)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("participant", type=validate_participant)
    parser.add_argument("--attempt", type=positive_attempt, default=1)
    parser.add_argument("--binary", type=Path, default=DEFAULT_BINARY)
    parser.add_argument("--fixture-dir", type=Path, default=DEFAULT_FIXTURES)
    parser.add_argument("--study-root", type=Path, default=DEFAULT_STUDY_ROOT)
    parser.add_argument("--ui-scale", type=float, default=1.0)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument(
        "--preflight-only",
        action="store_true",
        help="verify app readiness and terminate without conducting a human session",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    try:
        return launch(parse_args(argv))
    except (OSError, StudyLaunchError, ValueError, json.JSONDecodeError) as error:
        print(f"run_river_sign_study.py: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
