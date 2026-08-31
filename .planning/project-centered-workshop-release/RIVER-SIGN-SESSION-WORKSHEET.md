# River Sign Session Worksheet

Use one copy for each attempt. Preserve unsuccessful and technically invalid
attempts; never erase or overwrite an earlier worksheet.

## Attempt identity

| Field | Value |
|---|---|
| Cohort ID | |
| `id` — cohort slot (`P01`–`P05`) | |
| Attempt evidence ID (`P01-A01`, `P01-A02`, ...) | |
| `run_status` (`NOT RUN/COMPLETE/TECHNICAL INVALID/WITHDRAWN`) | `NOT RUN` |
| `session_datetime` (timezone-aware ISO, for example `2026-07-11T14:05:00-07:00`) | |
| `build_id` | |
| Binary SHA-256 | |
| `os_display` (`1366x768`) | |
| `ui_scale` (`1.0`) | |
| `moderator` | |
| Note taker (evidence-only) | |
| Optional recording consent (`Y/N`) | |
| Recording filename, or `NONE` | |

## Consent, eligibility, and setup fields

These names map directly to the result JSON unless marked evidence-only.

| Field | Required value | Recorded value |
|---|---:|---:|
| `consent` | `true` | |
| `eligible` | `true` | |
| `first_exposure_to_build` | `true` | |
| `not_routine_professional_cad_cam` | `true` | |
| `mouse_keyboard_unassisted` | `true` | |
| `did_not_observe_prior_session` | `true` | |
| `prior_cad_cam` | non-empty text | |
| `prior_hobby_cnc` | non-empty text | |
| `fresh_test_profile` | `true` | |
| `fixture_only_library` | `true` | |
| `guided_workshop` | `true` | |
| `virtual_cnc_only` | `true` | |
| `manual_ball_nose_diameter_mm` (record after T6; never preselect) | `3.175` | |
| Physical CNC interfaces absent (evidence-only) | `true` | |
| Window/work area (evidence-only) | `1366x768` | |
| Effective UI scale (evidence-only) | `1.0` | |
| Fixture label `Primary` verified (evidence-only) | `true` | |
| Fixture label `Alternate` verified (evidence-only) | `true` | |
| Fixture label `Preview Only` verified (evidence-only) | `true` | |
| No River Sign project exists (evidence-only) | `true` | |
| Guided fixture-only Library is initial screen (evidence-only) | `true` | |

Preflight report path: `_______________________________________________________`

Moderator preflight initials/date: `___________________________________________`<br>
Note-taker or verifier initials/date: `________________________________________`

## Task data

Use one continuous monotonic or recording timeline. Every `start_timestamp`,
`end_timestamp`, and `observation_timecode` must be exactly `MM:SS.mmm` or
`HH:MM:SS.mmm`; minutes and seconds must each be below 60. Observation time must
be at or after task end. `elapsed_seconds` must equal end minus start within
0.05 seconds. Fill every cell for every completed scored attempt.
`project_correct`, `design_correct`, `membership_correct`, and
`state_probe_pass` are checked against the fixed answer key after the session.

| JSON field | T1 | T2 | T3 | T4 | T5 | T6 | T7 |
|---|---|---|---|---|---|---|---|
| `start_timestamp` | | | | | | | |
| `end_timestamp` | | | | | | | |
| `elapsed_seconds` | | | | | | | |
| `success` (`true/false`) | | | | | | | |
| `actions` | | | | | | | |
| `backtracks` | | | | | | | |
| `workspace_tab_hunts` | | | | | | | |
| `assistance` (`NONE/REPEAT/RESCUE`) | | | | | | | |
| `error_codes` (JSON array) | | | | | | | |
| `accidental_mutation` (`true/false`) | | | | | | | |
| `project_answer` | | | | | | | |
| `project_correct` (`true/false`) | | | | | | | |
| `project_response_seconds` | | | | | | | |
| `design_answer` | | | | | | | |
| `design_correct` (`true/false`) | | | | | | | |
| `design_response_seconds` | | | | | | | |
| `membership_answer` | | | | | | | |
| `membership_answer_code` (`MEMBER/NOT_MEMBER/UNRECOGNIZED`) | | | | | | | |
| `membership_correct` (`true/false`) | | | | | | | |
| `membership_response_seconds` | | | | | | | |
| `state_probe_pass` (`true/false`) | | | | | | | |
| `next_action_answer` | | | | | | | |
| `seq` (`1`–`7`) | | | | | | | |
| `observation_timecode` | | | | | | | |

### Deterministic membership-answer coding

Preserve the participant's answer verbatim. For coding only, casefold it,
change a curly apostrophe to a straight apostrophe, replace each run of
characters other than ASCII letters, digits, or apostrophes with one space,
and collapse whitespace. Then use exactly this vocabulary:

| Code | Exact normalized answers |
|---|---|
| `MEMBER` | `yes`; `yeah`; `yep`; `member`; `a member`; `it is a member`; `it's a member`; `belongs`; `it belongs`; `yes it belongs`; `it belongs to the project`; `it belongs to river sign`; `part of the project`; `it is part of the project`; `it's part of the project`; `part of river sign`; `it is part of river sign`; `it's part of river sign` |
| `NOT_MEMBER` | `no`; `nope`; `no it is not`; `no it isn't`; `not a member`; `it is not a member`; `it's not a member`; `it isn't a member`; `does not belong`; `doesn't belong`; `it does not belong`; `it doesn't belong`; `no it does not belong`; `no it doesn't belong`; `it does not belong to the project`; `it doesn't belong to the project`; `not part of the project`; `it is not part of the project`; `it's not part of the project`; `it isn't part of the project`; `not part of river sign`; `it is not part of river sign`; `it's not part of river sign`; `it isn't part of river sign` |
| `UNRECOGNIZED` | Every other non-empty normalized response. Record it as an incorrect answer, not a technical invalid. |

After applying the fixed answer key, `E-05` must appear in `error_codes`
exactly when `state_probe_pass` is `false`. A missing or spurious `E-05`, or a
code that contradicts the normalized verbatim answer, is invalid transcription.

## Participant-level result fields

| JSON field | Recorded value |
|---|---:|
| `back_to_project_activations` | |
| `back_to_project_visibly_labeled` (`true/false`) | |
| `continue_activations` | |
| `preview_only_became_member` (`true/false`) | |
| `wrong_project_asset_or_output` (`true/false`) | |
| `layout_preset_opened` (`true/false`) | |
| `unrelated_panel_tab_opened` (`true/false`) | |
| `reopened_project` | |
| `reopened_selected_design` | |

## Raw action and observation log

Record each click, tap, keyboard activation, and name-entry activity. Pointer
movement and individual typed characters are not actions. Add rows as needed.

| Task | Time/timecode | Action # | Exact visible control or input | Result/state | Backtrack, hunt, assistance, or error code | Observer note |
|---|---:|---:|---|---|---|---|
| | | | | | | |
| | | | | | | |
| | | | | | | |
| | | | | | | |
| | | | | | | |
| | | | | | | |
| | | | | | | |
| | | | | | | |
| | | | | | | |
| | | | | | | |

Exact repeated prompt or rescue hint, when applicable:<br>
`____________________________________________________________________________`<br>
`____________________________________________________________________________`

Technical anomaly/crash details, when applicable:<br>
`____________________________________________________________________________`<br>
`____________________________________________________________________________`

## Closing comments

Keep these separate from scored fields.

Where, if anywhere, did Library and Project feel mixed together?<br>
`____________________________________________________________________________`<br>
`____________________________________________________________________________`

When were you least sure what would happen next?<br>
`____________________________________________________________________________`<br>
`____________________________________________________________________________`

What one label would you change?<br>
`____________________________________________________________________________`<br>
`____________________________________________________________________________`

## Post-session transcription verification

Worksheet/raw-log filename: `__________________________________________________`<br>
Result JSON filename: `_______________________________________________________`<br>

Transcriber: `________________` — Initials/date: `________________`<br>
Independent verifier: `________________`  Initials/date: `________________`

- `[ ]` Every worksheet field was transcribed without inference.
- `[ ]` All 35 task records were compared with the raw log/recording.
- `[ ]` Correctness booleans match the fixed T1–T7 answer key.
- `[ ]` Counts and error codes match the raw log.
- `[ ]` Consent and identifying data are absent from the result JSON.
- `[ ]` The scorer output and SHA-256 manifest were archived.
