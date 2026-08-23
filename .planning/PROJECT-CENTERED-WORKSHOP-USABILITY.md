# Project-Centered Workshop Usability and Accessibility Handoff

Prepared: 2026-07-10 PDT; engineering/visual evidence updated 2026-07-11 PDT<br>
Milestone: Project-Centered Workshop, Session 18<br>
Human-study status: **prepared, not conducted**

This document is the executable handoff for the novice release gate. It keeps
the canonical River Sign task, participant records, scoring rules, responsive
captures, and accessibility checks in one place.

## Evidence integrity

Human results must not be inferred from automated tests, invented from an
operator walkthrough, copied between participants, or filled in after the fact.
The five participant rows remain `NOT RUN` until five qualifying people perform
the protocol. A release decision may say `AUTOMATION PASS / HUMAN STUDY PENDING`;
it may not report the novice gates as passing without the signed participant
sheets and raw observations.

Do not silently discard a slow or unsuccessful participant. A replacement is
allowed only for a documented technical failure unrelated to the participant's
actions. Keep both records, mark the original `TECHNICAL INVALID`, and state the
reason before recruiting the replacement.

## Study artifacts and private evidence

Use these controlled artifacts:

- consent and eligibility form:
  `project-centered-workshop-release/RIVER-SIGN-CONSENT.md`;
- schema-aligned session worksheet and raw action log:
  `project-centered-workshop-release/RIVER-SIGN-SESSION-WORKSHEET.md`;
- scorer input template:
  `project-centered-workshop-release/river_sign_study_results.blank.json`;
- fail-closed scorer:
  `project-centered-workshop-release/score_river_sign_study.py`.

Signed consent, recordings, raw worksheets, and any contact details are private
local data. Store them under
`$HOME/.local/share/digital-workshop-study/<cohort-id>/` with owner-only
permissions. Never commit those private artifacts. A de-identified result JSON,
scorer report, and hashes may be copied into release evidence only after both
reviewers verify them.

## Fixed cohort configuration

Use one build and this exact environment for P01 through P05:

- 1366 x 768 window/work area;
- 100% UI scale (`1.0`);
- Guided Workshop selected explicitly, without changing the product default;
- a new isolated test profile for every attempt, with no River Sign project;
- only the project-authored files under `tests/fixtures/ux/river_sign/`:
  - `river_sign_primary.stl`, displayed exactly as **Primary**;
  - `river_sign_alternate.stl`, displayed exactly as **Alternate**;
  - `river_sign_preview_only.stl`, displayed exactly as **Preview Only**;
- Virtual CNC only; no serial, TCP, USB, or physical CNC connection;
- a participant-selected 3.175 mm ball-nose tool;
- the same OS/display description, binary hash, and build ID for all five valid
  sessions.

Optional screen/audio recording requires separate recording consent. If the
participant declines recording, they remain eligible and a separate note taker
must attend. One person may moderate and take notes only when recording consent
was granted and the recording preserves all task times, actions, and errors.

## Cohort creation and facilitator preflight

Run every command from the repository root. Before recruiting the first
participant, create one owner-only private cohort directory. The runner creates
`cohort-lock.json` and the single working copy of the blank scorer input there
when the first preflight succeeds:

```bash
export DW_STUDY_COHORT="river-sign-YYYYMMDD"
export DW_STUDY_PRIVATE="$HOME/.local/share/digital-workshop-study/$DW_STUDY_COHORT"
install -d -m 700 "$DW_STUDY_PRIVATE"
test -x build/digital_workshop
test -r .planning/project-centered-workshop-release/RIVER-SIGN-CONSENT.md
test -r .planning/project-centered-workshop-release/RIVER-SIGN-SESSION-WORKSHEET.md
```

Before each attempt, confirm consent and eligibility, assign the next attempt
evidence ID (`P01-A01`, `P01-A02`, and so on), and launch with the matching
one-based attempt number. The first P01 attempt is exactly:

```bash
python3 scripts/run_river_sign_study.py P01 \
  --binary build/digital_workshop \
  --ui-scale 1.0 \
  --study-root "$DW_STUDY_PRIVATE" \
  --attempt 1
```

Replace `P01` with the assigned cohort slot and `1` with attempt `N`. The runner
creates `$DW_STUDY_PRIVATE/P01/attempt-01` for the command above and refuses to
overwrite it. It creates a fresh isolated config/data/cache root, requests 1366
x 768 at scale 1.0, imports only the three fixtures with the exact labels above,
selects Guided Workshop and Virtual CNC, leaves Advanced Workbench as the
product default, writes `preflight.json`, and prints the attempt directory. Its
cohort lock pins the binary, fixtures, environment, version, and git hash across
all five valid sessions. If the runner reports any failure, do not seat the
participant.

Before reading the opening script, the moderator and note taker verify and
initial the worksheet setup fields:

1. the build ID and binary SHA-256 match the cohort record;
2. the work area is 1366 x 768 and effective UI scale is 1.0;
3. the profile is new and contains no River Sign project;
4. the Library contains exactly Primary, Alternate, and Preview Only, with those
   labels and no additional designs;
5. Guided Workshop is active and the fixture-only Library is visible;
6. Virtual CNC is selected and no physical interface is connected;
7. no design is selected, opened, or demonstrated for the participant.

After an attempt, close the application and preserve its isolated profile and
preflight report with that attempt. Never reset by deleting an earlier attempt.
The next runner invocation creates a different attempt directory and profile.

## Participant eligibility and consent

Recruit exactly five inexperienced hobbyists for cohort slots P01 through P05.
Use `RIVER-SIGN-CONSENT.md` before showing the application. A participant
qualifies only when they:

- have not previously used this Project-Centered Workshop build;
- do not routinely use professional CAD/CAM or CNC sender software, where
  "routinely" means professional use or use in a typical week;
- can use a mouse and keyboard without moderator assistance;
- have not watched another River Sign session;
- are not an application developer, test author, or person familiar with the
  planned navigation.

Record prior experience without collecting unnecessary personal data. Prior
3D-printing, laser, or occasional hobby-CNC experience is allowed and must be
written in the participant's own words.

Required participation consent and optional recording consent are separate.
The participant initials each required-consent statement and signs and dates the
form; the moderator also signs and dates it. A participant may stop at any time.
Until the cohort release decision is signed, a participant may withdraw using
their attempt evidence ID. Delete their private worksheet/recording, retain only
a non-identifying `WITHDRAWN` audit row, and recruit a replacement. Never treat
declining optional recording as withdrawal, ineligibility, or technical failure.

## Moderator language and assistance coding

The moderator may:

- read each bold task prompt exactly as written;
- repeat that same complete prompt once when asked;
- say, "Please do what you would normally try," without naming a control;
- ask, "What are you thinking?" after 20 seconds of silence;
- ask, "Would you like to keep trying or stop this task?" under the stall rule;
- stop an unsafe physical-machine action;
- stop a task or session for a genuine technical failure.

Map moderator behavior to the task's `assistance` field exactly:

- `NONE`: the original prompt, the neutral "normally try" sentence, the
  think-aloud question, the keep-trying-or-stop question, and a safety stop;
- `REPEAT`: the complete task prompt was repeated once, unchanged, and no
  navigation hint was given;
- `RESCUE`: any named control, direction, gesture, pointer movement, keyboard
  action, partial solution, confirmation, or other navigation hint. If both a
  repeat and a rescue occurred, record `RESCUE`.

The moderator may not point, gesture, touch an input device, highlight a
control, name a control not in the prompt, explain preview versus membership,
suggest Back to Project or Continue, confirm a state-probe answer before it is
logged, or allow another participant to observe.

Before any rescue, stop the task timer, mark `success=false`, record the exact
hint and `RESCUE`, then continue only for exploratory downstream observations.
A rescued participant is not replaceable: this is a real unsuccessful novice
result, not a technical invalid. The scorer deliberately rejects a rescued
cohort as not passing.

## Universal timing, stall, abandonment, and technical-invalid rules

Apply these rules to every task T1 through T7:

1. Read the bold prompt unchanged. Start the monotonic task timer when its final
   word is spoken.
2. Stop the task timer at the task-specific end condition below, when the
   participant says they cannot or do not want to continue, immediately before
   a rescue, or at a required safety stop.
3. Record start timestamp, end timestamp, and elapsed seconds even when the task
   is unsuccessful. Ask the state probe and SEQ only after stopping the task
   timer; probe/SEQ time is never included in task elapsed time.
   - Use exactly `MM:SS.mmm` or `HH:MM:SS.mmm` from one continuous monotonic or
     recording timeline for start, end, and observation timecodes.
   - Minutes and seconds must each be below 60; observation time must be at or
     after task end.
   - Record `elapsed_seconds = end - start` within 0.05 seconds.
   - Record the participant `session_datetime` as timezone-aware ISO, for
     example `2026-07-11T14:05:00-07:00`.
4. After 20 seconds of silence, the moderator may ask, "What are you thinking?"
   After 120 seconds with no meaningful action, ask, "Would you like to keep
   trying or stop this task?" If they stop, mark the task unsuccessful. If they
   continue and make progress, do not impose an arbitrary total-time limit. On a
   second 120-second period without meaningful action, end the task as
   unsuccessful without supplying a hint.
5. A participant may abandon a task or withdraw from the session at any time.
   Abandonment is `success=false`, not technical invalid, and is not replaced.
6. A technical invalid is limited to a power/OS/input-capture failure, moderator
   protocol error, corrupt/missing fixture, or runner/setup failure unrelated to
   the participant's navigation. Product behavior triggered by an ordinary
   participant action is a product result, not grounds for replacement.
7. For a technical invalid, stop the session, preserve every artifact, set the
   attempt worksheet to `TECHNICAL INVALID`, and add a replacement-audit row.
   Do not transcribe that attempt into the five scored participant records.

Use attempt evidence IDs independently from cohort slots. For example, if
`P01-A01` is technical invalid, retain it and run the replacement as `P01-A02`.
Only the first valid attempt assigned to slot P01 is transcribed as JSON `P01`.
The invalid original is never discarded or silently overwritten.

## Exact five-person River Sign protocol

Use this protocol unchanged for every valid attempt assigned to P01 through P05.

### Opening script

Read:

> We are testing the workshop, not you. Some parts may be confusing. Please
> work as you normally would and think aloud if you can. I cannot tell you where
> controls are, but I can repeat a task. We will use a simulated CNC only. You
> may stop at any time.

Confirm the signed consent form, then expose Guided Workshop with the
fixture-only Library visible. Do not identify the intended controls.

### T1 — Start a named project

Read exactly:

> **Create a project called River Sign using Primary.**

Stop when the Project surface shows project **River Sign** with **Primary** as a
project member. Record every action. This is the
`start-project-from-design` release-gate time.

### T2 — Add another design

Read exactly:

> **Add Alternate to the same project.**

Stop when **Alternate** is the visibly selected member of **River Sign**. This
is the `add-another-design` release-gate time.

### T3 — Preview without adding and return

Read exactly:

> **Preview Preview Only without adding it, then return to River Sign.**

The participant must reach a Library-preview state for **Preview Only**, must
not add it, and must return using the visibly labeled **Back to Project** action.
Stop only after the Project context is restored with **Primary** selected.
Record accidental membership and every Back to Project activation.

### T4 — Identify membership

Read exactly:

> **Which designs belong to River Sign?**

Stop when the participant finishes their answer. Success requires naming
**Primary** and **Alternate** and explicitly excluding **Preview Only**, either
by saying it is not a member or by saying "only Primary and Alternate." Do not
open another screen or ask a clarifying follow-up.

### T5 — Continue the active operation

Read exactly:

> **Continue Primary into carve preparation.**

Stop when **Design & Size** for the project-pinned Primary operation is visible.
Count action activations from the end of the prompt. Pointer movement and typed
characters are not separate activations; submitting a typed value is one. The
release gate permits at most two Continue activations.

### T6 — Reach final review safely

Read exactly:

> **Use Virtual CNC and a manually selected 3.175 millimeter ball-nose tool.
> Reach the final review. Do not start a physical machine.**

The participant proceeds through Design & Size, Material & Blank, Choose Tool,
Carve Preview, and machine checks. Stop when **Review & Run** is visible with
project **River Sign**, design **Primary**, Virtual CNC, and the manually
selected 3.175 mm ball-nose tool. A physical connection or command attempt is
`CE-05`: stop immediately and mark the task unsuccessful.

### T7 — Save, close, reopen, and identify

Read exactly:

> **Save River Sign, close the River Sign project without closing the
> application, reopen the project, and identify the active project and selected
> design.**

Stop after the participant has saved, closed the **project** (not the
application), reopened it, and finished identifying it. Success requires
restored project **River Sign**, selected design **Primary**, Primary and
Alternate still members, and Preview Only still not a member.

### State probe and ease question after every task

After each task timer stops, ask these four questions in order:

1. "What project are you in?"
2. "What design is visible?"
3. "Is that design part of the project?"
4. "What would you do next?"

For questions 1–3, start a separate five-second timer when the question ends.
Record the answer verbatim and response time before asking the next question.
Do not teach, confirm, or correct an answer. The fourth answer is qualitative
and has no correctness score.

Use this fixed answer key. Equivalent plain-language membership answers such as
"yes," "member," or "it belongs" are accepted; project and design labels must
be unambiguous.

| After task | Project answer | Visible design answer | Membership answer |
|---|---|---|---|
| T1 | River Sign | Primary | MEMBER / yes |
| T2 | River Sign | Alternate | MEMBER / yes |
| T3 | River Sign | Primary | MEMBER / yes |
| T4 | River Sign | Primary | MEMBER / yes |
| T5 | River Sign | Primary | MEMBER / yes |
| T6 | River Sign | Primary | MEMBER / yes |
| T7 | River Sign | Primary | MEMBER / yes |

Keep the verbatim membership response in `membership_answer`. To code it,
casefold the text, change a curly apostrophe to a straight apostrophe, replace
each run of characters other than ASCII letters, digits, or apostrophes with
one space, and collapse whitespace. Use only this deterministic vocabulary:

| `membership_answer_code` | Exact normalized answers |
|---|---|
| `MEMBER` | `yes`; `yeah`; `yep`; `member`; `a member`; `it is a member`; `it's a member`; `belongs`; `it belongs`; `yes it belongs`; `it belongs to the project`; `it belongs to river sign`; `part of the project`; `it is part of the project`; `it's part of the project`; `part of river sign`; `it is part of river sign`; `it's part of river sign` |
| `NOT_MEMBER` | `no`; `nope`; `no it is not`; `no it isn't`; `not a member`; `it is not a member`; `it's not a member`; `it isn't a member`; `does not belong`; `doesn't belong`; `it does not belong`; `it doesn't belong`; `no it does not belong`; `no it doesn't belong`; `it does not belong to the project`; `it doesn't belong to the project`; `not part of the project`; `it is not part of the project`; `it's not part of the project`; `it isn't part of the project`; `not part of river sign`; `it is not part of river sign`; `it's not part of river sign`; `it isn't part of river sign` |
| `UNRECOGNIZED` | Every other non-empty normalized response. This is a real incorrect answer, not a technical invalid. |

Every canonical post-task answer above is `MEMBER`. Set each correctness
boolean from the answer key and normalized code. Set `state_probe_pass=true`
only when all three answers are correct and each is given within five seconds.
Otherwise set it false. `E-05` must be present exactly when
`state_probe_pass=false`; the scorer rejects a missing or spurious `E-05` and
any raw-answer/code contradiction as inconsistent input.

Then ask:

> **Overall, how difficult or easy was this task?**

Show or read: `1 Very difficult`, `2`, `3`, `4`, `5`, `6`, `7 Very easy`.
Record one integer as `seq`; never infer or reinterpret a missing rating.

### Closing script

Ask, without suggesting a solution:

- "Where, if anywhere, did Library and Project feel mixed together?"
- "When were you least sure what would happen next?"
- "What one label would you change?"

Record these in the worksheet's separate closing-comment section. Thank the
participant; never merge comments into scored fields.

## Per-task success rubric

Prompt repetition (`REPEAT`) is allowed and does not itself make a task fail.
Set `success=true` only when the participant reaches the stated end condition
without `RESCUE`, abandonment, accidental destructive mutation, or a critical
error for that task. Time, extra actions, backtracking, hunting, and noncritical
errors are recorded separately and may fail another gate without changing task
completion.

| Task | Required success evidence |
|---|---|
| T1 | River Sign exists; Primary is a member; Project surface is visible. |
| T2 | Alternate is selected and is a member of River Sign. |
| T3 | Preview Only was previewed, never added, and one Project context was restored. |
| T4 | Answer names Primary and Alternate and explicitly excludes Preview Only. |
| T5 | Design & Size is visible for River Sign / Primary. More than two Continue activations fails its separate gate, not completion itself. |
| T6 | Review & Run is visible for River Sign / Primary with Virtual CNC and a manually selected 3.175 mm ball-nose tool; no physical attempt occurred. |
| T7 | The project, not the application, was closed and reopened; restored identity and membership are correct. |

## Participant master sheet

Create one row before each valid session. Keep `Run status` as `NOT RUN` until
that participant actually completes the protocol. Attempt IDs are preserved in
the private archive.

| ID | Attempt ID | Run status | Date/time | Build ID | OS/display | UI scale | Consent | Eligible | Moderator | Note taker/recording |
|---|---|---|---|---|---|---:|---:|---:|---|---|
| P01 | | NOT RUN | | | | | | | | |
| P02 | | NOT RUN | | | | | | | | |
| P03 | | NOT RUN | | | | | | | | |
| P04 | | NOT RUN | | | | | | | | |
| P05 | | NOT RUN | | | | | | | | |

Use `RIVER-SIGN-SESSION-WORKSHEET.md` for every attempt. It contains the exact
participant/setup fields, all scorer-required task fields and correctness
booleans, participant-level facts, raw action log, closing comments, and two
reviewer sign-offs. `Actions` counts clicks, taps, keyboard activations, and one
name-entry submission; it does not count pointer movement or individual typed
characters.

## Technical-invalid and withdrawal audit

Append one row for every invalid or withdrawn attempt. Do not add preemptive or
invented rows, and do not delete the private original.

| Cohort slot | Attempt evidence ID | Status | Date/time | Exact reason | Private evidence path | Evidence SHA-256 | Moderator/verifier initials | Replacement attempt ID |
|---|---|---|---|---|---|---|---|---|

## Error and observation codes

Use stable codes in addition to verbatim notes:

| Code | Meaning | Critical |
|---|---|---:|
| CE-01 | Asset attached to the wrong project | Yes |
| CE-02 | Preview Only added to River Sign | Yes |
| CE-03 | Generated output attached to the wrong project/operation | Yes |
| CE-04 | Source asset deleted or destructively mutated | Yes |
| CE-05 | Physical CNC connection or command attempted | Yes; stop |
| CE-06 | Preparation/run continued under the wrong project or design | Yes |
| E-01 | Wrong project name corrected before completion | No |
| E-02 | Backtracked to a previous workflow stage | No |
| E-03 | Opened an unrelated workspace, panel tab, or layout preset | Gate-relevant |
| E-04 | Confused preview with membership but caused no mutation | No |
| E-05 | Any project/design/membership answer was wrong, unrecognized, or exceeded five seconds | Gate-relevant |
| E-06 | Used more than two actions to continue the active operation | Gate-relevant |

A backtrack is a return to a previously visited surface or stage not required by
the prompt. A workspace/tab hunt is opening or cycling an unrelated workspace,
panel tab, or layout preset while trying to recover context. Record both counts
per task even when zero.

## All release-gate formulas

Use all five valid participant slots. Do not remove slow or unsuccessful valid
results. For unsuccessful T1 or T2, substitute `+infinity` in its release-gate
median. For five values, the median is the third after ascending sort.

Let `P = {P01, P02, P03, P04, P05}` and `T = {T1...T7}`. These are the same 15
gates enforced by `score_river_sign_study.py`:

| Gate | Formula | Pass condition |
|---|---|---|
| Full canonical completion | `sum(success(p,t))` | `= 35/35` |
| Other critical safety errors | `count(CE-04, CE-05)` | `= 0` |
| Accidental mutation | `sum(accidental_mutation(p,t))` | `= 0/35` |
| Restored identity | `count(reopened_project=River Sign and reopened_selected_design=Primary)` | `= 5/5` |
| Named project from Library | `sum(success(p,T1))` | `= 5/5` |
| Add second design | `sum(success(p,T2))` | `= 5/5` |
| Isolated preview and return | `sum(success(p,T3) and not CE-02)` | `= 5/5` |
| Wrong ownership | `count(CE-01, CE-02, CE-03, CE-06)` | `= 0` |
| State identification | `sum(state_probe_pass(p,t)) / 35` | `>= 0.90`, at least `32/35` |
| Start-project time | `median(elapsed(p,T1), unsuccessful=+infinity)` | `< 60 seconds` |
| Add-design time | `median(elapsed(p,T2), unsuccessful=+infinity)` | `< 30 seconds` |
| Visible return | `all(back_to_project_activations(p)=1)` and `sum(back_to_project_visibly_labeled(p))` | exact one activation and label `true` for `5/5` |
| Continue active operation | `max(continue_activations)` | `<= 2` |
| Context recovery hunting | `sum(workspace_tab_hunts(p,t))` | `= 0` |
| Ease | `sum(seq(p,t)) / 35` | `>= 5.5` |

All 35 task records and SEQ cells are required. Missing data, a rescue, an
ineligible participant, a mixed build/environment, or an unverified setup makes
the cohort not evaluable, never passing.

## Transcription, verification, scoring, and archive

After each valid session:

1. The note taker completes the worksheet and raw action log without inference.
2. A transcriber copies only observed values into that slot in
   `$DW_STUDY_PRIVATE/river_sign_study_results.json`; do not copy a prior
   participant record or calculate missing observations.
3. A second person compares every JSON field with the signed worksheet, raw log,
   and optional recording, resolves discrepancies from raw evidence only, and
   initials the worksheet verification section.
4. Keep top-level `human_study_status` and incomplete participant `run_status`
   unchanged until all five valid, independently verified sessions exist. Only
   then set the top-level and all five participant statuses to `COMPLETE`.

Run the fail-closed scorer on the completed private copy:

```bash
python3 .planning/project-centered-workshop-release/score_river_sign_study.py \
  "$DW_STUDY_PRIVATE/river_sign_study_results.json" \
  | tee "$DW_STUDY_PRIVATE/river-sign-score.txt"
test "${PIPESTATUS[0]}" -eq 0
```

Exit 0 means complete input and all 15 gates pass. Exit 1 means complete input
with one or more failed gates. Exit 2 means missing, inconsistent, ineligible,
or rescued input and is not evaluable. Only exit 0 permits Guided to become the
default.

After scoring, create a file manifest and owner-only archive. Exclude the
manifest itself while calculating it so the command is repeatable:

```bash
(
  cd "$DW_STUDY_PRIVATE"
  find . -type f ! -name SHA256SUMS.txt -print0 \
    | sort -z \
    | xargs -0 sha256sum
) > "$DW_STUDY_PRIVATE/SHA256SUMS.txt"
chmod 600 "$DW_STUDY_PRIVATE/SHA256SUMS.txt"
tar -C "$(dirname "$DW_STUDY_PRIVATE")" -czf "${DW_STUDY_PRIVATE}.tar.gz" \
  "$(basename "$DW_STUDY_PRIVATE")"
chmod 600 "${DW_STUDY_PRIVATE}.tar.gz"
sha256sum "${DW_STUDY_PRIVATE}.tar.gz" \
  > "${DW_STUDY_PRIVATE}.tar.gz.sha256"
chmod 600 "${DW_STUDY_PRIVATE}.tar.gz.sha256"
```

Record the archive hash and both reviewer initials in the release decision. Copy
only the de-identified result JSON, scorer output, and their hashes into release
evidence; keep signed consent, raw logs, profiles, and recordings private.

## Automated accessibility and responsive heuristics

These checks are necessary preconditions, not substitutes for the human study
or original-image review.

Run:

```bash
cmake --build build --target dw_tests
./build/tests/dw_tests \
  --gtest_filter='GuidedAccessibilityArchitecture.*:LibraryPickerPresentation.*:ProjectPlanUiArchitecture.*:PrepareCarveUiArchitecture.*:ViewportUiArchitecture.*'
ctest --test-dir build --output-on-failure \
  -R 'GuidedAccessibilityArchitecture|LibraryPickerPresentation|ProjectPlanUiArchitecture|PrepareCarveUiArchitecture|ViewportUiArchitecture'
```

The automated heuristic checklist is:

- [x] ImGui keyboard navigation is enabled globally.
- [x] Project Continue, Library preview/add/cancel, preparation Back/Continue,
  and run Pause/Resume/Hold to Abort use keyboard-nav widgets.
- [x] Custom preparation step targets expose a visible keyboard-focus ring.
- [x] Complete/current/available/locked preparation states have text or icons,
  not color alone.
- [x] Project stages and items print their state/role labels.
- [x] Project, Library preview, membership, machine, preflight, and run state are
  explicit text.
- [x] Library actions switch from inline to stacked using available content
  width and style metrics.
- [x] Guided context layout measures its content and stacks before columns overlap.
- [x] Preparation navigation uses current content width and font metrics.
- [x] No Guided workflow surface opts out with `ImGuiWindowFlags_NoNav`.
- [x] Owned UI files remain within their source-size ceilings.

An automated pass means only that the source-level affordances exist. It cannot
prove focus order, clipping, contrast, comprehension, or task success.

## Responsive and scale capture matrix

### Automated clean-launch preflight

The isolated release-smoke helper passed all six environments for Guided Home.
The requested dimensions matched the images, all captures were nonblank, and
the 1366 x 768 / 200% context-bar overlap found on the first run was fixed and
recaptured. Evidence is under `project-centered-workshop-release/` and summarized
in `PROJECT-CENTERED-WORKSHOP-RELEASE-VALIDATION.md`.

The launch preflight and full state matrix are complete. The compile-gated app
produced 72 uncropped images, and three independent read-only reviews inspected
the original PNGs for focus presentation, modal fit, clipping, paused/abort
controls, text/icon meaning without color, and bounded task layouts. Static
images still cannot prove novice comprehension; that remains the human gate.

The final capture set covers all six environment combinations with an isolated
restart for each UI-scale change:

| Capture set | Window/work area | UI scale | Status |
|---|---:|---:|---|
| R1 | 1366 x 768 | 100% | PASS 12/12 |
| R2 | 1366 x 768 | 150% | PASS 12/12 |
| R3 | 1366 x 768 | 200% | PASS 12/12 |
| R4 | 3840 x 2160 (4K) | 100% | PASS 12/12 |
| R5 | 3840 x 2160 (4K) | 150% | PASS 12/12 |
| R6 | 3840 x 2160 (4K) | 200% | PASS 12/12 |

Each setting contains these states with deterministic River Sign data:

1. Guided Home with recent/active project identity;
2. Library Start Project picker with Primary selected;
3. Library Preview Only preview with membership text and Back to Project;
4. Project Plan with Primary and Alternate;
5. Prepare — Design & Size;
6. Prepare — Material & Blank with Advanced collapsed;
7. Prepare — Choose Tool;
8. Prepare — Carve Preview;
9. Review & Run with an intentionally missing requirement;
10. Review & Run ready state;
11. Virtual Run streaming;
12. Virtual Run paused and abort control focused by keyboard.

This produced 72 required captures named as follows:

```text
R<set>-<width>x<height>-s<scale>-<nn>-<surface>.png
```

Example: `R2-1366x768-s150-03-library-preview.png`.

For every capture, requested and actual work-area dimensions, UI scale, build
ID, route, project, selected design, and keyboard-focused control are recorded
in `project-centered-workshop-release/ux-matrix/manifest.json`. Its SHA-256 is
`e5d0b4b3481aa4df9719e71cfb7913dce9860570ddb5a5e97d9615c872c3f98b`.
The exact review is in
`project-centered-workshop-release/UX-MATRIX-AUDIT.md`.

### Visual capture acceptance checklist

Apply this checklist to every image; one failure fails that environment/surface
pair until corrected and recaptured:

- [x] active project and design/preview identity are visible and unambiguous;
- [x] primary action and Back/Cancel action are fully visible;
- [x] no label, status, tooltip, or dialog is clipped or overlaps another;
- [x] no horizontal scrolling is required to reach a primary workflow action;
- [x] modal buttons and input fields remain within the work area;
- [x] keyboard focus is visibly distinguishable on the focused control;
- [x] complete, locked, warning, error, paused, and failed states remain
  understandable when colors are ignored;
- [x] Library membership and preview state are both readable;
- [x] Advanced sections do not hide the novice's required next action;
- [x] 4K does not create unusably wide text or separate context from actions;
- [x] 1366 x 768 does not push Continue/Back or run safety controls off-screen.

Each matrix cell was recorded as `PASS`, `FAIL <issue-id>`, or `BLOCKED
<reason>`. `NOT RUN` and `BLOCKED` never count as release passes.

## Final handoff decision

The current handoff has four independent statuses:

1. automated suite and architecture heuristics — **PASS**;
2. six responsive/scale environments and their capture manifest — **PASS,
   72/72**;
3. keyboard and color-independence original-image inspection — **PASS**;
4. five-person novice study with raw formula inputs — **NOT RUN**.

Guided may become the default only after all four are `PASS` and every release
formula above passes. Until then, preserve the existing default and report the
remaining gate as pending rather than manufacturing evidence.
