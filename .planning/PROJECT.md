# Digital Workshop

## What This Is

A C++17 desktop application for 3D model management, G-code analysis, 2D cut optimization, CNC control, and project costing — built for woodworkers who run CNC machines. Uses SDL2, Dear ImGui, OpenGL 3.3, and SQLite.

## Core Value

A woodworker can go from selecting a piece of wood and a cutting tool to safely running a CNC job with optimized feeds and speeds — all without leaving the application.

## Requirements

### Validated

- v0.1.x: CNC Controller Suite (8 phases, 8 feature areas — shipped 2026-02-25)
- v0.2.0: Sender Feature Parity (5 phases, safety/controls/niceties/extended/TCP — shipped 2026-02-27)
- v0.3.0: Direct Carve (6 phases, heightmap through streaming — completed 2026-02-28)
- v0.4.0: Shared Materials & Project Costing (7 phases, 27 requirements — completed 2026-03-05)

### Active

See REQUIREMENTS.md, PROJECT-CENTERED-WORKSHOP-PLAN.md, and PROJECT-CENTERED-WORKSHOP-SESSIONS.md for v0.7.0 scope.

### Out of Scope

- Accounting integration (QuickBooks, etc.) — desktop app, not an accounting tool
- Multi-currency support — USD-only for now
- Cloud-based price updates — no cloud sync
- Purchase order generation — receipt/order view covers the output need

## Current Milestone: v0.7.0 Project-Centered Workshop

**Goal:** Make Project the persistent working context for an inexperienced CNC hobbyist, make Design Library an explicit source/picker, and create a deliberate safety boundary between preparation and machine execution.

**Target features:**
- One authoritative ProjectSession and selection origin
- Home, persistent project shell, and contextual Design Library picker
- One Project Plan tree with readiness, blockers, and next action
- Project-pinned Prepare Carve and immutable Run CNC handoff
- Opt-in Guided experience with the existing Advanced Workbench preserved until human acceptance
- Targeted modularization and de-monolith work in every touched subsystem

## Context

- 524 production C++ files under a non-increasing source-size ratchet
- Build: CMake application/settings targets plus isolated workflow-policy test targets
- 1,587 full-binary tests and 1,812 sequential CTest cases passing
- 72/72 responsive workflow captures pass independent original-image review
  across 1366 x 768 and 4K at 100%, 150%, and 200% UI scale
- Existing project open items provide hierarchy, status, intent, and snapshot data
- ProjectSession owns active project/context truth; Library preview, Project Plan, preparation, and Run consume explicit typed boundaries
- The pre-existing thumbnail, viewport-navigation, and Direct Carve work was preserved and recorded before implementation
- Direct Carve shell is 647 lines with preparation, Machine Setup, review, run, sync, and navigation behavior in focused owned units

## Constraints

- **Tech stack**: C++17, SDL2, ImGui, OpenGL 3.3, SQLite — no changes
- **Quality**: No hardcoded values in visual/GUI code
- **File size**: New/substantially rewritten files target 500 lines; 750-line milestone ceiling
- **Tests**: All tests must continue to pass after each change
- **Rendering**: Single Camera class, single Renderer class — no new rendering abstractions
- **Compatibility**: External .nc files must still work standalone (no model required)
- **Modularity**: New business behavior uses render-independent modules, immutable snapshots, explicit intents/commands, and direct tests
- **Safety**: Active streaming has highest transition priority and locks project/run identity
- **Migration**: Custom layouts and existing project/library data survive; Guided stays opt-in until acceptance
- **Scope**: Refactor touched workflow seams; do not turn the milestone into a repository-wide rewrite

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Project is context, not a peer panel mode | Beginners should not choose which subsystem owns current work | Session authority applied in Phase 41; visible shell continues in Phases 42-44 |
| Library is a catalog/picker | Reusable source assets must be distinct from project-specific intent | Applied in Phase 43 |
| Guided and Advanced share ProjectSession | Preserve expert tools without creating two data models | Applied; Advanced remains default pending human gate |
| Prepare and Run use immutable handoff | Preparation cannot silently mutate a live machine job | Applied in Phases 45-46 |
| Module extraction accompanies behavior changes | Prevent the redesign from growing existing monoliths | Required in every phase |
| File target 500, ceiling 750 | Keep future sessions reviewable and ownership visible | Required at every session gate |
| Phase numbering continues from 40 | v0.6.0 ended at Phase 39 | Applied |
| Back to Project explicitly navigates to Project | The visible action must be deterministic; picker cancel/restore is a distinct Library command | Applied in Session 05 |
| Context bar reports area while Project Plan reports stage | Route and preparation state stay truthful without duplicate policy | Applied; PCW-02 complete |
| Resume identity lives outside general preferences | Stable project/item IDs need atomic validation and repair without reviving legacy paths | Applied in Session 06 |
| Home owns project entry; Library owns design choice | New/Open/Recent and reusable-design browsing are different decisions for a beginner | Applied in Phases 42-43 |
| Picker return is acknowledged, not fire-and-forget | Delayed callbacks must not restore a prior project/item over newer navigation | Applied in Session 07 |
| Library visibility belongs to workflow state | A saved layout must not reopen or hide a picker without its purpose/return contract | Applied in Session 08 |
| Guided default is a human release decision | Automation cannot prove novice comprehension or no-coaching task success | Held pending the exact P01–P05 protocol |

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition** (via `/gsd:transition`):
1. Requirements invalidated? → Move to Out of Scope with reason
2. Requirements validated? → Move to Validated with phase reference
3. New requirements emerged? → Add to Active
4. Decisions to log? → Add to Key Decisions
5. "What This Is" still accurate? → Update if drifted

**After each milestone** (via `/gsd:complete-milestone`):
1. Full review of all sections
2. Core Value check — still the right priority?
3. Audit Out of Scope — reasons still valid?
4. Update Context with current state

---
*Last updated: 2026-07-10 after Session 18 engineering validation and handoff*
