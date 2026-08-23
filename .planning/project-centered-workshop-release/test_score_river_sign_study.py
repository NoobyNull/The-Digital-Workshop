#!/usr/bin/env python3

import copy
import importlib.util
import json
import math
import sys
import unittest
from pathlib import Path


HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location(
    "river_sign_scorer", HERE / "score_river_sign_study.py")
SCORER = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = SCORER
SPEC.loader.exec_module(SCORER)

# These generated dictionaries are synthetic in-memory unit-test fixtures only.
# They are never written as participant evidence or accepted as a study run.


def timecode(seconds):
    total_milliseconds = round(seconds * 1000)
    hours, remainder = divmod(total_milliseconds, 3_600_000)
    minutes, remainder = divmod(remainder, 60_000)
    whole_seconds, milliseconds = divmod(remainder, 1000)
    if hours:
        return f"{hours:02d}:{minutes:02d}:{whole_seconds:02d}.{milliseconds:03d}"
    return f"{minutes:02d}:{whole_seconds:02d}.{milliseconds:03d}"


def set_elapsed(task, seconds):
    task.update(
        start_timestamp="00:00.000",
        end_timestamp=timecode(seconds),
        elapsed_seconds=seconds,
        observation_timecode=timecode(seconds),
    )


def passing_task(task_id):
    elapsed = 40.0 if task_id == "T1" else 20.0 if task_id == "T2" else 30.0
    return {
        "start_timestamp": "00:00.000",
        "end_timestamp": timecode(elapsed),
        "elapsed_seconds": elapsed,
        "success": True,
        "actions": 2 if task_id == "T5" else 1,
        "backtracks": 0,
        "workspace_tab_hunts": 0,
        "assistance": "NONE",
        "error_codes": [],
        "accidental_mutation": False,
        "project_answer": "River Sign",
        "project_correct": True,
        "project_response_seconds": 2.0,
        "design_answer": "Alternate" if task_id == "T2" else "Primary",
        "design_correct": True,
        "design_response_seconds": 2.0,
        "membership_answer": "Yes",
        "membership_answer_code": "MEMBER",
        "membership_correct": True,
        "membership_response_seconds": 2.0,
        "state_probe_pass": True,
        "next_action_answer": "Continue",
        "seq": 6,
        "observation_timecode": timecode(elapsed),
    }


def passing_participant(index):
    return {
        "id": f"P{index:02d}",
        "run_status": "COMPLETE",
        "session_datetime": f"2026-07-11T10:{index:02d}:00-07:00",
        "build_id": "test-build",
        "os_display": "Linux 1366x768",
        "ui_scale": 1.0,
        "prior_cad_cam": "none",
        "prior_hobby_cnc": "none",
        "consent": True,
        "eligible": True,
        "first_exposure_to_build": True,
        "not_routine_professional_cad_cam": True,
        "mouse_keyboard_unassisted": True,
        "did_not_observe_prior_session": True,
        "fresh_test_profile": True,
        "fixture_only_library": True,
        "guided_workshop": True,
        "virtual_cnc_only": True,
        "manual_ball_nose_diameter_mm": 3.175,
        "moderator": "M1",
        "tasks": {f"T{i}": passing_task(f"T{i}") for i in range(1, 8)},
        "back_to_project_activations": 1,
        "back_to_project_visibly_labeled": True,
        "continue_activations": 2,
        "preview_only_became_member": False,
        "wrong_project_asset_or_output": False,
        "layout_preset_opened": False,
        "unrelated_panel_tab_opened": False,
        "reopened_project": "River Sign",
        "reopened_selected_design": "Primary",
    }


def passing_cohort():
    return {
        "schema_version": 1,
        "study": "River Sign",
        "human_study_status": "COMPLETE",
        "participants": [passing_participant(i) for i in range(1, 6)],
    }


class RiverSignScorerTest(unittest.TestCase):
    def report(self, mutate=None):
        data = passing_cohort()
        if mutate:
            mutate(data)
        return SCORER.score_data(data)

    def assert_gate_fails(self, gate_name, mutate):
        report = self.report(mutate)
        self.assertFalse(report.errors)
        self.assertFalse(report.gate(gate_name).passed)
        self.assertEqual(SCORER.exit_code(report), 1)
        return report

    def test_passing_cohort_passes_every_gate_and_reports_raw_values(self):
        report = self.report()
        self.assertFalse(report.errors)
        self.assertTrue(report.passed)
        self.assertEqual(SCORER.exit_code(report), 0)
        self.assertIn("5/5", report.gate("Named project from Library").raw)
        self.assertIn("35/35", report.gate("State identification").raw)
        self.assertIn("mean=6.000000", report.gate("Ease").raw)

    def test_blank_missing_invalid_and_assisted_inputs_are_rejected(self):
        blank = json.loads((HERE / "river_sign_study_results.blank.json").read_text())
        self.assertEqual(SCORER.exit_code(SCORER.score_data(blank)), 2)

        missing = passing_cohort()
        del missing["participants"][0]["tasks"]["T1"]["seq"]
        self.assertEqual(SCORER.exit_code(SCORER.score_data(missing)), 2)

        invalid = passing_cohort()
        invalid["participants"][0]["tasks"]["T1"]["error_codes"] = ["NOT-A-CODE"]
        self.assertEqual(SCORER.exit_code(SCORER.score_data(invalid)), 2)

        assisted = passing_cohort()
        assisted["participants"][0]["tasks"]["T5"]["assistance"] = "RESCUE"
        self.assertEqual(SCORER.exit_code(SCORER.score_data(assisted)), 2)

        repeated = passing_cohort()
        repeated["participants"][0]["tasks"]["T1"]["assistance"] = "REPEAT"
        self.assertTrue(SCORER.score_data(repeated).passed)

    def test_failed_times_use_positive_infinity_and_fail_completion(self):
        def mutate(data):
            for participant in data["participants"][:3]:
                participant["tasks"]["T1"]["success"] = False

        report = self.assert_gate_fails("Start-project time", mutate)
        self.assertIn("+infinity", report.gate("Start-project time").raw)
        self.assertTrue(math.isinf(float("inf")))
        self.assertFalse(report.gate("Named project from Library").passed)
        self.assertFalse(report.gate("Full canonical completion").passed)

    def test_each_documented_formula_failure_is_detected(self):
        cases = {
            "Add second design": lambda d: d["participants"][0]["tasks"]["T2"].update(
                success=False),
            "State identification": lambda d: [
                d["participants"][i]["tasks"]["T1"].update(
                    project_answer="Wrong project", project_correct=False,
                    state_probe_pass=False, error_codes=["E-05"]) for i in range(4)],
            "Start-project time": lambda d: [
                set_elapsed(p["tasks"]["T1"], 60.0) for p in d["participants"]],
            "Add-design time": lambda d: [
                set_elapsed(p["tasks"]["T2"], 30.0) for p in d["participants"]],
            "Visible return": lambda d: (
                d["participants"][0].update(back_to_project_activations=2),
                d["participants"][0]["tasks"]["T3"].update(actions=2)),
            "Continue active operation": lambda d: (
                d["participants"][0].update(continue_activations=3),
                d["participants"][0]["tasks"]["T5"].update(actions=3)),
            "Context recovery hunting": lambda d: d["participants"][0]["tasks"]["T4"].update(
                workspace_tab_hunts=1, error_codes=["E-03"]),
            "Ease": lambda d: [task.update(seq=5) for p in d["participants"]
                               for task in p["tasks"].values()],
        }
        for gate, mutate in cases.items():
            with self.subTest(gate=gate):
                self.assert_gate_fails(gate, mutate)

        self.assert_gate_fails(
            "Visible return",
            lambda d: d["participants"][0].update(
                back_to_project_visibly_labeled=False))

    def test_state_threshold_is_exactly_thirty_two_of_thirty_five(self):
        data = passing_cohort()
        for participant in data["participants"][:3]:
            participant["tasks"]["T1"].update(
                project_answer="Wrong project", project_correct=False,
                state_probe_pass=False, error_codes=["E-05"])
        report = SCORER.score_data(data)
        self.assertTrue(report.gate("State identification").passed)
        self.assertIn("32/35", report.gate("State identification").raw)
        data["participants"][3]["tasks"]["T1"].update(
            project_answer="Wrong project", project_correct=False,
            state_probe_pass=False, error_codes=["E-05"])
        self.assertFalse(SCORER.score_data(data).gate("State identification").passed)

    def test_visible_return_requires_exactly_one_activation_from_every_participant(self):
        data = passing_cohort()
        for participant in data["participants"][1:]:
            participant["back_to_project_activations"] = 0
        report = SCORER.score_data(data)
        self.assertFalse(report.errors)
        self.assertFalse(report.gate("Visible return").passed)
        self.assertIn("exact_one=1/5", report.gate("Visible return").raw)
        self.assertEqual(SCORER.exit_code(report), 1)

    def test_timecodes_and_session_datetimes_are_validated_fail_closed(self):
        mutations = (
            lambda d: d["participants"][0].update(session_datetime="2026-07-11T10:01:00"),
            lambda d: d["participants"][0]["tasks"]["T1"].update(
                start_timestamp="0:00.000"),
            lambda d: d["participants"][0]["tasks"]["T1"].update(
                start_timestamp="00:41.000"),
            lambda d: d["participants"][0]["tasks"]["T1"].update(
                end_timestamp="00:39.900"),
            lambda d: d["participants"][0]["tasks"]["T1"].update(
                observation_timecode="00:39.999"),
        )
        for mutate in mutations:
            with self.subTest(mutate=mutate):
                report = self.report(mutate)
                self.assertTrue(report.errors)
                self.assertEqual(SCORER.exit_code(report), 2)

        data = passing_cohort()
        data["participants"][0]["tasks"]["T1"].update(
            start_timestamp="01:00:00.000",
            end_timestamp="01:00:40.000",
            observation_timecode="01:00:40.050",
        )
        self.assertTrue(SCORER.score_data(data).passed)

    def test_raw_probe_answers_cannot_contradict_correctness_booleans(self):
        mutations = (
            lambda d: d["participants"][0]["tasks"]["T4"].update(
                project_answer="Wrong project"),
            lambda d: d["participants"][0]["tasks"]["T2"].update(
                design_answer="Primary"),
            lambda d: d["participants"][0]["tasks"]["T6"].update(
                membership_answer="No", membership_answer_code="NOT_MEMBER"),
            lambda d: d["participants"][0]["tasks"]["T1"].update(
                project_correct=False),
        )
        for mutate in mutations:
            with self.subTest(mutate=mutate):
                report = self.report(mutate)
                self.assertTrue(report.errors)
                self.assertEqual(SCORER.exit_code(report), 2)

        data = passing_cohort()
        data["participants"][0]["tasks"]["T4"].update(
            project_answer="Wrong project", project_correct=False,
            state_probe_pass=False, error_codes=["E-05"])
        report = SCORER.score_data(data)
        self.assertFalse(report.errors)
        self.assertIn("34/35", report.gate("State identification").raw)

    def test_membership_answer_vocabulary_is_deterministic_and_fail_closed(self):
        accepted = {
            "YES!": "MEMBER",
            "It’s part of River Sign.": "MEMBER",
            "No—it isn’t.": "NOT_MEMBER",
            "It doesn't belong.": "NOT_MEMBER",
            "Maybe?": "UNRECOGNIZED",
        }
        for raw, expected in accepted.items():
            with self.subTest(raw=raw):
                self.assertEqual(SCORER._membership_answer_code(raw), expected)

        contradiction = passing_cohort()
        contradiction["participants"][0]["tasks"]["T1"].update(
            membership_answer="No", membership_answer_code="MEMBER")
        report = SCORER.score_data(contradiction)
        self.assertTrue(report.errors)
        self.assertEqual(SCORER.exit_code(report), 2)

        unrecognized = passing_cohort()
        unrecognized["participants"][0]["tasks"]["T1"].update(
            membership_answer="Maybe", membership_answer_code="UNRECOGNIZED",
            membership_correct=False, state_probe_pass=False,
            error_codes=["E-05"])
        report = SCORER.score_data(unrecognized)
        self.assertFalse(report.errors)
        self.assertTrue(report.passed)
        self.assertIn("34/35", report.gate("State identification").raw)

    def test_membership_vocabulary_is_published_in_both_protocol_forms(self):
        documents = (
            (HERE.parent / "PROJECT-CENTERED-WORKSHOP-USABILITY.md").read_text(),
            (HERE / "RIVER-SIGN-SESSION-WORKSHEET.md").read_text(),
        )
        vocabulary = (
            SCORER.MEMBER_ANSWER_VOCABULARY |
            SCORER.NOT_MEMBER_ANSWER_VOCABULARY)
        for document in documents:
            self.assertIn("`UNRECOGNIZED`", document)
            for answer in vocabulary:
                with self.subTest(answer=answer):
                    self.assertIn(f"`{answer}`", document)

    def test_e05_exactly_tracks_a_failed_state_probe(self):
        missing = passing_cohort()
        missing["participants"][0]["tasks"]["T1"].update(
            project_answer="Wrong", project_correct=False, state_probe_pass=False)
        self.assertEqual(SCORER.exit_code(SCORER.score_data(missing)), 2)

        spurious = passing_cohort()
        spurious["participants"][0]["tasks"]["T1"]["error_codes"] = ["E-05"]
        self.assertEqual(SCORER.exit_code(SCORER.score_data(spurious)), 2)

        aligned = passing_cohort()
        aligned["participants"][0]["tasks"]["T1"].update(
            project_answer="Wrong", project_correct=False, state_probe_pass=False,
            error_codes=["E-05"])
        report = SCORER.score_data(aligned)
        self.assertFalse(report.errors)
        self.assertTrue(report.passed)

    def test_preview_and_wrong_ownership_codes_fail_both_exact_gates(self):
        def preview_mutation(data):
            participant = data["participants"][0]
            participant["tasks"]["T3"]["error_codes"] = ["CE-02"]
            participant["tasks"]["T3"]["accidental_mutation"] = True
            participant["preview_only_became_member"] = True

        report = self.assert_gate_fails("Isolated preview and return", preview_mutation)
        self.assertFalse(report.gate("Wrong ownership").passed)

        def wrong_project(data):
            participant = data["participants"][0]
            participant["tasks"]["T6"]["error_codes"] = ["CE-01"]
            participant["wrong_project_asset_or_output"] = True

        self.assert_gate_fails("Wrong ownership", wrong_project)

    def test_protocol_safety_mutation_and_restore_prerequisites_fail_closed(self):
        self.assert_gate_fails(
            "Other critical safety errors",
            lambda d: d["participants"][0]["tasks"]["T6"].update(error_codes=["CE-05"]))
        self.assert_gate_fails(
            "Accidental mutation",
            lambda d: d["participants"][0]["tasks"]["T4"].update(
                accidental_mutation=True))
        self.assert_gate_fails(
            "Restored identity",
            lambda d: d["participants"][0].update(reopened_selected_design="Alternate"))

    def test_probe_claim_must_match_recorded_answers_and_times(self):
        data = passing_cohort()
        data["participants"][0]["tasks"]["T3"]["membership_response_seconds"] = 5.1
        report = SCORER.score_data(data)
        self.assertTrue(report.errors)
        self.assertEqual(SCORER.exit_code(report), 2)

    def test_exact_participant_ids_and_eligibility_are_required(self):
        duplicate = passing_cohort()
        duplicate["participants"][4]["id"] = "P04"
        self.assertEqual(SCORER.exit_code(SCORER.score_data(duplicate)), 2)
        ineligible = passing_cohort()
        ineligible["participants"][0]["eligible"] = False
        self.assertEqual(SCORER.exit_code(SCORER.score_data(ineligible)), 2)
        mixed_build = passing_cohort()
        mixed_build["participants"][4]["build_id"] = "different-build"
        self.assertEqual(SCORER.exit_code(SCORER.score_data(mixed_build)), 2)


if __name__ == "__main__":
    unittest.main()
