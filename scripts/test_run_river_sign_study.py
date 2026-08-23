#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import json
import os
import signal
import stat
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path


HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location(
    "river_sign_runner", HERE / "run_river_sign_study.py"
)
RUNNER = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(RUNNER)


class RiverSignStudyRunnerTest(unittest.TestCase):
    def assert_mode(self, path: Path, expected: int):
        if os.name == "posix":
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), expected)

    def test_participant_and_attempt_are_exact(self):
        self.assertEqual(RUNNER.validate_participant("P01"), "P01")
        self.assertEqual(RUNNER.validate_participant("P05"), "P05")
        for value in ("P00", "P06", "P1", "p01", "P01-A"):
            with self.subTest(value=value), self.assertRaises(Exception):
                RUNNER.validate_participant(value)
        self.assertEqual(RUNNER.positive_attempt("2"), 2)
        with self.assertRaises(Exception):
            RUNNER.positive_attempt("0")

    def test_config_is_fixed_to_1366_by_768_guided_at_requested_scale(self):
        with tempfile.TemporaryDirectory() as temporary:
            profile = Path(temporary)
            target = RUNNER.render_config(profile, 1.0)
            config = target.read_text(encoding="utf-8")
            self.assertIn("width=1366", config)
            self.assertIn("height=768", config)
            self.assertIn("scale=1", config)
            self.assertIn("active_preset=0", config)
            self.assertNotIn("[WIDTH]", config)
            self.assert_mode(profile / "config", 0o700)
            self.assert_mode(target.parent, 0o700)
            self.assert_mode(target, 0o600)

    def test_study_root_is_created_private_and_unsafe_roots_fail_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            parent = Path(temporary)
            private = parent / "new-cohort"
            RUNNER.prepare_private_study_root(private)
            self.assert_mode(private, 0o700)

            if os.name == "posix":
                unsafe = parent / "unsafe-cohort"
                unsafe.mkdir(mode=0o755)
                unsafe.chmod(0o755)
                with self.assertRaises(RUNNER.StudyLaunchError):
                    RUNNER.prepare_private_study_root(unsafe)

            target = parent / "private-target"
            target.mkdir(mode=0o700)
            alias = parent / "cohort-alias"
            alias.symlink_to(target, target_is_directory=True)
            with self.assertRaises(RUNNER.StudyLaunchError):
                RUNNER.prepare_private_study_root(alias)

    def test_ready_contract_rejects_any_nonfresh_or_nonvirtual_state(self):
        ready = {
            "version": "0.7.0",
            "git_hash": "abc",
            "route": "Design Library",
            "model_names": RUNNER.EXPECTED_MODELS,
            "model_count": 3,
            "no_project": True,
            "virtual_cnc": True,
            "picker_purpose": "StartProject",
            "selection_count": 0,
            "membership_count": 0,
            "ui_scale": 1.0,
        }
        RUNNER.validate_ready(ready, 1.0)
        for key, bad in (
            ("no_project", False),
            ("virtual_cnc", False),
            ("model_count", 2),
            ("picker_purpose", "Manage"),
            ("membership_count", 1),
        ):
            with self.subTest(key=key), self.assertRaises(RUNNER.StudyLaunchError):
                changed = dict(ready)
                changed[key] = bad
                RUNNER.validate_ready(changed, 1.0)

    def test_cohort_lock_rejects_binary_fixture_or_display_drift(self):
        with tempfile.TemporaryDirectory() as temporary:
            lock = Path(temporary) / "cohort-lock.json"
            expected = {
                "schema_version": 1,
                "binary_sha256": "a",
                "fixture_sha256": {"one": "b"},
                "display": ":0",
                "ui_scale": 1.0,
            }
            RUNNER.atomic_json(lock, {**expected, "version": "0.7.0", "git_hash": "abc"})
            self.assert_mode(lock, 0o600)
            self.assertIsNotNone(RUNNER.verify_existing_lock(lock, expected))
            for key, value in (
                ("binary_sha256", "changed"),
                ("fixture_sha256", {"one": "changed"}),
                ("display", ":1"),
                ("ui_scale", 1.5),
            ):
                with self.subTest(key=key), self.assertRaises(RUNNER.StudyLaunchError):
                    changed = dict(expected)
                    changed[key] = value
                    RUNNER.verify_existing_lock(lock, changed)

    def test_attempt_directory_is_never_reused(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            attempt = RUNNER.reserve_attempt_directory(root, "P01", 1)
            self.assertEqual(attempt, root / "P01" / "attempt-01")
            self.assert_mode(root / "P01", 0o700)
            self.assert_mode(attempt, 0o700)
            with self.assertRaises(RUNNER.StudyLaunchError):
                RUNNER.reserve_attempt_directory(root, "P01", 1)
            second = RUNNER.reserve_attempt_directory(root, "P01", 2)
            self.assertEqual(second, root / "P01" / "attempt-02")

    def test_fixture_directory_requires_exact_three_stls(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in RUNNER.FIXTURE_FILES:
                (root / name).write_bytes(name.encode())
            hashes = RUNNER.fixture_hashes(root)
            self.assertEqual(set(hashes), set(RUNNER.FIXTURE_FILES))
            (root / "unexpected.stl").write_bytes(b"extra")
            with self.assertRaises(RUNNER.StudyLaunchError):
                RUNNER.fixture_hashes(root)

    def test_private_attempt_gets_blank_results_and_unfilled_forms(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            attempt = RUNNER.reserve_attempt_directory(root, "P01", 1)
            RUNNER.preserve_blank_materials(root, attempt)
            results = root / "river_sign_study_results.json"
            self.assertTrue(results.is_file())
            self.assertIn('"human_study_status": "NOT_RUN"',
                          results.read_text(encoding="utf-8"))
            self.assertTrue((attempt / "RIVER-SIGN-CONSENT.md").is_file())
            self.assertTrue((attempt / "RIVER-SIGN-SESSION-WORKSHEET.md").is_file())
            self.assert_mode(results, 0o600)
            self.assert_mode(attempt / "RIVER-SIGN-CONSENT.md", 0o600)
            self.assert_mode(attempt / "RIVER-SIGN-SESSION-WORKSHEET.md", 0o600)

    @unittest.skipUnless(os.name == "posix", "POSIX signal lifecycle")
    def test_sigterm_stops_child_and_records_interruption_without_human_results(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            root = base / "cohort"
            fake_binary = base / "fake-digital-workshop"
            fake_binary.write_text(
                f"""#!{sys.executable}
import json
import os
import time
from pathlib import Path

Path(os.environ["TMPDIR"], "fake-child.pid").write_text(str(os.getpid()))
ready = {{
    "version": "0.7.0",
    "git_hash": "signal-test",
    "route": "Design Library",
    "model_names": ["Primary", "Alternate", "Preview Only"],
    "model_count": 3,
    "no_project": True,
    "virtual_cnc": True,
    "ui_scale": 1.0,
    "picker_purpose": "StartProject",
    "selection_count": 0,
    "membership_count": 0,
}}
print("DW_STUDY_READY=" + json.dumps(ready), flush=True)
while True:
    time.sleep(1)
""",
                encoding="utf-8",
            )
            fake_binary.chmod(0o700)
            environment = os.environ.copy()
            environment["DISPLAY"] = ":study-signal-test"
            environment["XDG_SESSION_TYPE"] = "x11"
            runner = subprocess.Popen(
                [
                    sys.executable,
                    str(HERE / "run_river_sign_study.py"),
                    "P01",
                    "--binary",
                    str(fake_binary),
                    "--study-root",
                    str(root),
                    "--attempt",
                    "1",
                    "--timeout",
                    "10",
                ],
                env=environment,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            preflight = root / "P01" / "attempt-01" / "preflight.json"
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline and not preflight.is_file():
                if runner.poll() is not None:
                    break
                time.sleep(0.05)
            if not preflight.is_file():
                output, _ = runner.communicate(timeout=2)
                self.fail(f"fake study never reached readiness: {output}")

            child_pid = int(
                (root / "P01" / "attempt-01" / "profile" / "tmp" /
                 "fake-child.pid").read_text(encoding="utf-8")
            )
            runner.send_signal(signal.SIGTERM)
            output, _ = runner.communicate(timeout=10)
            self.assertEqual(runner.returncode, 128 + signal.SIGTERM, output)

            exit_record = json.loads(
                (root / "P01" / "attempt-01" / "process-exit.json")
                .read_text(encoding="utf-8")
            )
            self.assertTrue(exit_record["interrupted"])
            self.assertEqual(exit_record["interrupt_signal"], "SIGTERM")
            self.assertEqual(exit_record["machine_preflight"], "READY")
            self.assertEqual(exit_record["runner_exit_status"], 143)
            with self.assertRaises(ProcessLookupError):
                os.kill(child_pid, 0)

            results = json.loads(
                (root / "river_sign_study_results.json").read_text(encoding="utf-8")
            )
            self.assertEqual(results["human_study_status"], "NOT_RUN")


if __name__ == "__main__":
    unittest.main()
