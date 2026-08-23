#!/usr/bin/env python3
"""Fail-closed scorer for the five-person River Sign novice study.

This program scores recorded human observations. It never creates, fills, or
infers participant results. Exit 0 means the input is complete, valid, and all
documented release gates plus protocol prerequisites pass. Exit 1 means valid
observations fail at least one gate. Exit 2 means the input is missing, invalid,
or contains navigation-rescue assistance and is not scoreable as a release cohort.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import statistics
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any


PARTICIPANT_IDS = tuple(f"P{i:02d}" for i in range(1, 6))
TASK_IDS = tuple(f"T{i}" for i in range(1, 8))
ERROR_CODES = {f"CE-{i:02d}" for i in range(1, 7)} | {
    f"E-{i:02d}" for i in range(1, 7)
}
OWNERSHIP_CODES = {"CE-01", "CE-02", "CE-03", "CE-06"}
OTHER_CRITICAL_CODES = {"CE-04", "CE-05"}
ASSISTANCE = {"NONE", "REPEAT", "RESCUE"}
MEMBERSHIP_ANSWER_CODES = {"MEMBER", "NOT_MEMBER", "UNRECOGNIZED"}
MEMBER_ANSWER_VOCABULARY = frozenset({
    "yes", "yeah", "yep", "member", "a member", "it is a member",
    "it's a member", "belongs", "it belongs", "yes it belongs",
    "it belongs to the project", "it belongs to river sign",
    "part of the project", "it is part of the project",
    "it's part of the project", "part of river sign",
    "it is part of river sign", "it's part of river sign",
})
NOT_MEMBER_ANSWER_VOCABULARY = frozenset({
    "no", "nope", "no it is not", "no it isn't", "not a member",
    "it is not a member", "it's not a member", "it isn't a member",
    "does not belong", "doesn't belong", "it does not belong",
    "it doesn't belong", "no it does not belong", "no it doesn't belong",
    "it does not belong to the project", "it doesn't belong to the project",
    "not part of the project", "it is not part of the project",
    "it's not part of the project", "it isn't part of the project",
    "not part of river sign", "it is not part of river sign",
    "it's not part of river sign", "it isn't part of river sign",
})
EXPECTED_PROJECT = "River Sign"
EXPECTED_DESIGNS = {
    task_id: "Alternate" if task_id == "T2" else "Primary"
    for task_id in TASK_IDS
}
TIMECODE_PATTERN = re.compile(
    r"^(?:(?P<hours>[0-9]{2}):)?(?P<minutes>[0-9]{2}):"
    r"(?P<seconds>[0-9]{2})\.(?P<milliseconds>[0-9]{3})$"
)


@dataclass(frozen=True)
class Gate:
    name: str
    passed: bool
    raw: str
    condition: str


@dataclass(frozen=True)
class Report:
    errors: tuple[str, ...]
    gates: tuple[Gate, ...]

    @property
    def passed(self) -> bool:
        return not self.errors and bool(self.gates) and all(g.passed for g in self.gates)

    def gate(self, name: str) -> Gate:
        return next(gate for gate in self.gates if gate.name == name)


def _is_bool(value: Any) -> bool:
    return isinstance(value, bool)


def _is_number(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def _is_nonnegative_int(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool) and value >= 0


def _format_number(value: float) -> str:
    if math.isinf(value):
        return "+infinity"
    return f"{value:g}"


def _normalize_answer(value: str) -> str:
    return " ".join(value.split()).casefold()


def _normalize_membership_answer(value: str) -> str:
    normalized = value.casefold().replace("’", "'")
    normalized = re.sub(r"[^a-z0-9']+", " ", normalized)
    return " ".join(normalized.split())


def _membership_answer_code(value: str) -> str:
    normalized = _normalize_membership_answer(value)
    if normalized in MEMBER_ANSWER_VOCABULARY:
        return "MEMBER"
    if normalized in NOT_MEMBER_ANSWER_VOCABULARY:
        return "NOT_MEMBER"
    return "UNRECOGNIZED"


def _parse_timecode(value: Any) -> float | None:
    if not isinstance(value, str):
        return None
    match = TIMECODE_PATTERN.fullmatch(value.strip())
    if match is None:
        return None
    hours = int(match.group("hours") or 0)
    minutes = int(match.group("minutes"))
    seconds = int(match.group("seconds"))
    if minutes >= 60 or seconds >= 60:
        return None
    milliseconds = int(match.group("milliseconds"))
    return hours * 3600 + minutes * 60 + seconds + milliseconds / 1000


def _is_timezone_aware_iso(value: Any) -> bool:
    if not isinstance(value, str) or not value.strip():
        return False
    try:
        parsed = datetime.fromisoformat(value.strip())
    except ValueError:
        return False
    return parsed.tzinfo is not None and parsed.utcoffset() is not None


def _validate(data: Any) -> tuple[list[dict[str, Any]], list[str]]:
    errors: list[str] = []
    if not isinstance(data, dict):
        return [], ["root: expected an object"]
    if data.get("schema_version") != 1:
        errors.append("schema_version: expected 1")
    if data.get("study") != "River Sign":
        errors.append("study: expected 'River Sign'")
    if data.get("human_study_status") != "COMPLETE":
        errors.append("human_study_status: expected COMPLETE; NOT_RUN is not scoreable")
    participants = data.get("participants")
    if not isinstance(participants, list):
        return [], errors + ["participants: expected an array"]
    ids = [item.get("id") for item in participants if isinstance(item, dict)]
    ids_valid = len(ids) == 5 and all(isinstance(pid, str) for pid in ids)
    if len(participants) != 5 or not ids_valid or set(ids) != set(PARTICIPANT_IDS):
        errors.append("participants: require exactly one each of P01, P02, P03, P04, P05")

    required_strings = (
        "session_datetime", "build_id", "os_display", "prior_cad_cam",
        "prior_hobby_cnc", "moderator",
    )
    required_facts = (
        "back_to_project_visibly_labeled", "preview_only_became_member",
        "wrong_project_asset_or_output", "layout_preset_opened",
        "unrelated_panel_tab_opened",
    )
    required_task_strings = (
        "start_timestamp", "end_timestamp", "project_answer", "design_answer",
        "membership_answer", "next_action_answer", "observation_timecode",
    )
    required_task_bools = (
        "success", "accidental_mutation", "project_correct", "design_correct",
        "membership_correct", "state_probe_pass",
    )
    required_task_counts = ("actions", "backtracks", "workspace_tab_hunts")

    valid_participants: list[dict[str, Any]] = []
    for index, participant in enumerate(participants):
        path = f"participants[{index}]"
        if not isinstance(participant, dict):
            errors.append(f"{path}: expected an object")
            continue
        pid = participant.get("id", f"index-{index}")
        path = str(pid)
        if participant.get("run_status") != "COMPLETE":
            errors.append(f"{path}.run_status: expected COMPLETE")
        for field in (
            "eligible", "consent", "first_exposure_to_build",
            "not_routine_professional_cad_cam", "mouse_keyboard_unassisted",
            "did_not_observe_prior_session", "fresh_test_profile",
            "fixture_only_library", "guided_workshop", "virtual_cnc_only",
        ):
            if participant.get(field) is not True:
                errors.append(f"{path}.{field}: expected true")
        for field in required_strings:
            value = participant.get(field)
            if not isinstance(value, str) or not value.strip():
                errors.append(f"{path}.{field}: required non-empty string")
        session_datetime = participant.get("session_datetime")
        if (isinstance(session_datetime, str) and session_datetime.strip() and
                not _is_timezone_aware_iso(session_datetime)):
            errors.append(
                f"{path}.session_datetime: required timezone-aware ISO datetime")
        scale = participant.get("ui_scale")
        if not _is_number(scale) or scale <= 0:
            errors.append(f"{path}.ui_scale: required positive finite number")
        diameter = participant.get("manual_ball_nose_diameter_mm")
        if not _is_number(diameter) or not math.isclose(diameter, 3.175, abs_tol=1e-9):
            errors.append(f"{path}.manual_ball_nose_diameter_mm: expected 3.175")
        for field in required_facts:
            if not _is_bool(participant.get(field)):
                errors.append(f"{path}.{field}: required boolean")
        for field in ("back_to_project_activations", "continue_activations"):
            if not _is_nonnegative_int(participant.get(field)):
                errors.append(f"{path}.{field}: required non-negative integer")
        for field in ("reopened_project", "reopened_selected_design"):
            value = participant.get(field)
            if not isinstance(value, str) or not value.strip():
                errors.append(f"{path}.{field}: required non-empty string")

        tasks = participant.get("tasks")
        if not isinstance(tasks, dict) or set(tasks) != set(TASK_IDS):
            errors.append(f"{path}.tasks: require exactly T1 through T7")
            continue
        all_codes: list[str] = []
        total_hunts = 0
        for task_id in TASK_IDS:
            task = tasks.get(task_id)
            task_path = f"{path}.{task_id}"
            if not isinstance(task, dict):
                errors.append(f"{task_path}: expected an object")
                continue
            for field in required_task_strings:
                if not isinstance(task.get(field), str) or not task[field].strip():
                    errors.append(f"{task_path}.{field}: required non-empty string")
            for field in required_task_bools:
                if not _is_bool(task.get(field)):
                    errors.append(f"{task_path}.{field}: required boolean")
            for field in required_task_counts:
                if not _is_nonnegative_int(task.get(field)):
                    errors.append(f"{task_path}.{field}: required non-negative integer")
            elapsed = task.get("elapsed_seconds")
            if not _is_number(elapsed) or elapsed < 0:
                errors.append(f"{task_path}.elapsed_seconds: required non-negative finite number")
            start_time = _parse_timecode(task.get("start_timestamp"))
            end_time = _parse_timecode(task.get("end_timestamp"))
            observation_time = _parse_timecode(task.get("observation_timecode"))
            for field, parsed_time in (
                    ("start_timestamp", start_time),
                    ("end_timestamp", end_time),
                    ("observation_timecode", observation_time)):
                value = task.get(field)
                if isinstance(value, str) and value.strip() and parsed_time is None:
                    errors.append(
                        f"{task_path}.{field}: expected MM:SS.mmm or HH:MM:SS.mmm")
            if start_time is not None and end_time is not None:
                if end_time < start_time:
                    errors.append(f"{task_path}: end_timestamp precedes start_timestamp")
                elif (_is_number(elapsed) and elapsed >= 0 and
                      not math.isclose(end_time - start_time, elapsed,
                                       rel_tol=0.0, abs_tol=0.05)):
                    errors.append(
                        f"{task_path}.elapsed_seconds: inconsistent with task timecodes")
            if (end_time is not None and observation_time is not None and
                    observation_time < end_time):
                errors.append(
                    f"{task_path}.observation_timecode: must be at or after end_timestamp")
            for field in ("project_response_seconds", "design_response_seconds",
                          "membership_response_seconds"):
                value = task.get(field)
                if not _is_number(value) or value < 0:
                    errors.append(f"{task_path}.{field}: required non-negative finite number")
            seq = task.get("seq")
            if not isinstance(seq, int) or isinstance(seq, bool) or not 1 <= seq <= 7:
                errors.append(f"{task_path}.seq: required integer from 1 through 7")
            assistance = task.get("assistance")
            if assistance not in ASSISTANCE:
                errors.append(f"{task_path}.assistance: expected NONE, REPEAT, or RESCUE")
            elif assistance == "RESCUE":
                errors.append(f"{task_path}.assistance: RESCUE navigation coaching is not scoreable")
            codes = task.get("error_codes")
            if not isinstance(codes, list) or any(not isinstance(code, str) for code in codes):
                errors.append(f"{task_path}.error_codes: required string array")
                codes = []
            elif len(codes) != len(set(codes)):
                errors.append(f"{task_path}.error_codes: duplicate codes are invalid")
            unknown = set(codes) - ERROR_CODES
            if unknown:
                errors.append(f"{task_path}.error_codes: unknown {sorted(unknown)}")
            all_codes.extend(codes)
            if _is_nonnegative_int(task.get("workspace_tab_hunts")):
                total_hunts += task["workspace_tab_hunts"]

            membership_code = task.get("membership_answer_code")
            if membership_code not in MEMBERSHIP_ANSWER_CODES:
                errors.append(
                    f"{task_path}.membership_answer_code: expected MEMBER, "
                    "NOT_MEMBER, or UNRECOGNIZED")
            project_answer = task.get("project_answer")
            design_answer = task.get("design_answer")
            membership_answer = task.get("membership_answer")
            raw_answers_valid = (
                isinstance(project_answer, str) and bool(project_answer.strip()) and
                isinstance(design_answer, str) and bool(design_answer.strip()) and
                isinstance(membership_answer, str) and bool(membership_answer.strip()) and
                membership_code in MEMBERSHIP_ANSWER_CODES)
            normalized_membership_code = (
                _membership_answer_code(membership_answer)
                if isinstance(membership_answer, str) and membership_answer.strip()
                else None)
            if (membership_code in MEMBERSHIP_ANSWER_CODES and
                    normalized_membership_code is not None and
                    membership_code != normalized_membership_code):
                errors.append(
                    f"{task_path}.membership_answer_code: contradicts the "
                    "recorded verbatim answer")
            project_matches = (
                raw_answers_valid and
                _normalize_answer(project_answer) == _normalize_answer(EXPECTED_PROJECT))
            design_matches = (
                raw_answers_valid and
                _normalize_answer(design_answer) ==
                _normalize_answer(EXPECTED_DESIGNS[task_id]))
            membership_matches = (
                raw_answers_valid and normalized_membership_code == "MEMBER" and
                membership_code == "MEMBER")
            for field, matches in (
                    ("project_correct", project_matches),
                    ("design_correct", design_matches),
                    ("membership_correct", membership_matches)):
                if _is_bool(task.get(field)) and raw_answers_valid and task[field] != matches:
                    errors.append(
                        f"{task_path}.{field}: contradicts the recorded canonical answer")

            probe_fields_valid = raw_answers_valid and all(
                _is_bool(task.get(field)) for field in (
                    "project_correct", "design_correct", "membership_correct")) and all(
                _is_number(task.get(field)) and task[field] >= 0 for field in (
                    "project_response_seconds", "design_response_seconds",
                    "membership_response_seconds"))
            if probe_fields_valid and _is_bool(task.get("state_probe_pass")):
                computed_probe = (
                    project_matches and design_matches and membership_matches and
                    task["project_correct"] and task["design_correct"] and
                    task["membership_correct"] and task["project_response_seconds"] <= 5 and
                    task["design_response_seconds"] <= 5 and
                    task["membership_response_seconds"] <= 5)
                if task["state_probe_pass"] != computed_probe:
                    errors.append(f"{task_path}.state_probe_pass: does not match answers/times")
                has_probe_error = "E-05" in codes
                if has_probe_error != (not computed_probe):
                    requirement = "required" if not computed_probe else "not allowed"
                    errors.append(
                        f"{task_path}.error_codes: E-05 is {requirement} for the "
                        "recorded state-probe result")

        if _is_bool(participant.get("preview_only_became_member")):
            if participant["preview_only_became_member"] != ("CE-02" in all_codes):
                errors.append(f"{path}: preview membership fact and CE-02 disagree")
        if _is_bool(participant.get("wrong_project_asset_or_output")):
            wrong = any(code in {"CE-01", "CE-03", "CE-06"} for code in all_codes)
            if participant["wrong_project_asset_or_output"] != wrong:
                errors.append(f"{path}: wrong-project fact and CE-01/CE-03/CE-06 disagree")
        if (participant.get("layout_preset_opened") is True or
                participant.get("unrelated_panel_tab_opened") is True) and total_hunts == 0:
            errors.append(f"{path}: context-recovery fact requires a recorded workspace/tab hunt")
        if (_is_nonnegative_int(participant.get("back_to_project_activations")) and
                isinstance(tasks.get("T3"), dict) and
                _is_nonnegative_int(tasks["T3"].get("actions")) and
                participant["back_to_project_activations"] > tasks["T3"]["actions"]):
            errors.append(f"{path}: Back to Project activations exceed all T3 actions")
        if (_is_nonnegative_int(participant.get("continue_activations")) and
                isinstance(tasks.get("T5"), dict) and
                _is_nonnegative_int(tasks["T5"].get("actions")) and
                participant["continue_activations"] > tasks["T5"]["actions"]):
            errors.append(f"{path}: Continue activations exceed all T5 actions")
        valid_participants.append(participant)
    comparable_configs = [
        (p.get("build_id"), p.get("os_display"), p.get("ui_scale"))
        for p in valid_participants
        if isinstance(p.get("build_id"), str) and isinstance(p.get("os_display"), str) and
        _is_number(p.get("ui_scale"))
    ]
    if len(comparable_configs) == 5 and len(set(comparable_configs)) != 1:
        errors.append("participants: build_id, os_display, and ui_scale must match for P01-P05")
    return valid_participants, errors


def score_data(data: Any) -> Report:
    participants, errors = _validate(data)
    if errors:
        return Report(tuple(errors), ())

    tasks = [participant["tasks"][task] for participant in participants for task in TASK_IDS]
    codes = [code for task in tasks for code in task["error_codes"]]
    t1_success = sum(participant["tasks"]["T1"]["success"] for participant in participants)
    t2_success = sum(participant["tasks"]["T2"]["success"] for participant in participants)
    preview_success = sum(
        participant["tasks"]["T3"]["success"] and
        not any("CE-02" in task["error_codes"] for task in participant["tasks"].values())
        for participant in participants)
    ownership_count = sum(code in OWNERSHIP_CODES for code in codes)
    state_count = sum(task["state_probe_pass"] for task in tasks)
    start_values = [
        participant["tasks"]["T1"]["elapsed_seconds"]
        if participant["tasks"]["T1"]["success"] else math.inf
        for participant in participants]
    add_values = [
        participant["tasks"]["T2"]["elapsed_seconds"]
        if participant["tasks"]["T2"]["success"] else math.inf
        for participant in participants]
    start_median = statistics.median(start_values)
    add_median = statistics.median(add_values)
    back_values = [p["back_to_project_activations"] for p in participants]
    visible_labels = sum(p["back_to_project_visibly_labeled"] for p in participants)
    continue_values = [p["continue_activations"] for p in participants]
    hunt_count = sum(task["workspace_tab_hunts"] for task in tasks)
    seq_total = sum(task["seq"] for task in tasks)
    success_count = sum(task["success"] for task in tasks)
    other_critical = sum(code in OTHER_CRITICAL_CODES for code in codes)
    mutation_count = sum(task["accidental_mutation"] for task in tasks)
    restored_count = sum(
        p["reopened_project"].strip() == "River Sign" and
        p["reopened_selected_design"].strip() == "Primary"
        for p in participants)

    start_raw = ", ".join(_format_number(value) for value in start_values)
    add_raw = ", ".join(_format_number(value) for value in add_values)
    gates = (
        Gate("Full canonical completion", success_count == 35,
             f"success={success_count}/35", "= 35/35 (protocol prerequisite)"),
        Gate("Other critical safety errors", other_critical == 0,
             f"count(CE-04,CE-05)={other_critical}", "= 0 (protocol prerequisite)"),
        Gate("Accidental mutation", mutation_count == 0,
             f"count={mutation_count}/35 tasks", "= 0 (protocol prerequisite)"),
        Gate("Restored identity", restored_count == 5,
             f"River Sign + Primary={restored_count}/5", "= 5/5 (protocol prerequisite)"),
        Gate("Named project from Library", t1_success == 5,
             f"sum(success(T1))={t1_success}/5", "= 5/5"),
        Gate("Add second design", t2_success == 5,
             f"sum(success(T2))={t2_success}/5", "= 5/5"),
        Gate("Isolated preview and return", preview_success == 5,
             f"sum(success(T3) and not CE-02)={preview_success}/5", "= 5/5"),
        Gate("Wrong ownership", ownership_count == 0,
             f"count(CE-01,CE-02,CE-03,CE-06)={ownership_count}", "= 0"),
        Gate("State identification", state_count / 35 >= 0.90,
             f"sum(state_probe_pass)={state_count}/35 ratio={state_count / 35:.6f}",
             ">= 0.90 (at least 32/35)"),
        Gate("Start-project time", start_median < 60,
             f"values=[{start_raw}] median={_format_number(start_median)} seconds", "< 60 seconds"),
        Gate("Add-design time", add_median < 30,
             f"values=[{add_raw}] median={_format_number(add_median)} seconds", "< 30 seconds"),
        Gate("Visible return", all(value == 1 for value in back_values) and visible_labels == 5,
             f"activations={back_values} exact_one={sum(value == 1 for value in back_values)}/5 "
             f"visible_labels={visible_labels}/5",
             "exactly 1 for every participant and visibly labeled for 5/5"),
        Gate("Continue active operation", max(continue_values) <= 2,
             f"activations={continue_values} max={max(continue_values)}", "max <= 2"),
        Gate("Context recovery hunting", hunt_count == 0,
             f"sum(workspace_tab_hunts)={hunt_count}/35 task cells", "= 0"),
        Gate("Ease", seq_total / 35 >= 5.5,
             f"sum(SEQ)={seq_total}/35 mean={seq_total / 35:.6f}", ">= 5.5/7"),
    )
    return Report((), gates)


def exit_code(report: Report) -> int:
    if report.errors:
        return 2
    return 0 if report.passed else 1


def print_report(report: Report, source: Path) -> None:
    print(f"River Sign novice-study score: {source}")
    if report.errors:
        print("INPUT: INVALID / NOT EVALUABLE")
        for error in report.errors:
            print(f"  ERROR: {error}")
        print("OVERALL: NOT PASSING (exit 2)")
        return
    print("INPUT: COMPLETE AND SCOREABLE")
    print("Prompt repetition (REPEAT) is protocol-permitted; RESCUE is rejected.")
    for gate in report.gates:
        status = "PASS" if gate.passed else "FAIL"
        print(f"[{status}] {gate.name}: {gate.raw}; required {gate.condition}")
    print(f"OVERALL: {'PASS' if report.passed else 'FAIL'} (exit {exit_code(report)})")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=Path, help="completed River Sign study JSON")
    args = parser.parse_args(argv)
    try:
        data = json.loads(args.results.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"River Sign novice-study score: {args.results}")
        print(f"INPUT: INVALID / NOT EVALUABLE\n  ERROR: {error}\nOVERALL: NOT PASSING (exit 2)")
        return 2
    report = score_data(data)
    print_report(report, args.results)
    return exit_code(report)


if __name__ == "__main__":
    sys.exit(main())
