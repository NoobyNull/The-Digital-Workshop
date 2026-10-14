# Milestones: Digital Workshop

## Completed

### v0.1.x CNC Controller Suite (Phases 1-8)
**Shipped:** 2026-02-25
**Scope:** Fix foundation, status display, manual control, tool integration, job streaming, job safety, firmware settings, macros.

### v0.2.0 Sender Feature Parity (Phases 9-13)
**Shipped:** 2026-02-27
**Scope:** Safety guards, core sender controls, niceties, extended features, TCP/IP transport.

### v0.3.0 Direct Carve (Phases 14-19)
**Completed:** 2026-02-28
**Scope:** Heightmap engine, model analysis, tool recommendation, toolpath generation, guided workflow, streaming integration.

### v0.4.0 Shared Materials & Project Costing (Phases 20-26)
**Completed:** 2026-03-05
**Scope:** Data foundation & rename, material stock sizes, consumables & live pricing, CLO material integration, project costing engine, CLO-to-costing pipeline, estimate & order views.
**Stats:** 7 phases, 27 requirements, 16 plans, 931 tests passing.

### v0.5.0 Codebase Cleanup & Simplification (Phases 27-30)
**Completed:** 2026-04-18
**Scope:** Bug fixes, monolithic function splits, duplicate code consolidation, hardcoded UI fixes, code quality improvements.

### v0.5.5 Unified 3D Viewport (Phases 31-35)
**Completed:** 2026-04-18 after tracker reconciliation
**Scope:** Merge G-code preview rendering into ViewportPanel. Core toolpath rendering, viewport toolbar & toggles, model-toolpath alignment, simulation playback, GCodePanel rendering elimination.
**Stats:** 5 phases, 15 requirements.

### v0.6.0 Technical Debt Cleanup (Phases 36-39)
**Completed:** 2026-03-29
**Scope:** Rendering correctness, coordinate-space consolidation, GL state safety, and CNC connection initialization deduplication.

## Active

### v0.8.0 PureCutCNC CAM Integration
**Started:** 2026-08-23
**Status:** Phases 1–2 complete. Scope reset 2026-10-14 to library management, CAM, and the sender; remaining phases revised in `.planning/WORKSHOP-SCOPE.md`.

## Frozen

### v0.7.0 Project-Centered Workshop (Phases 40-48)
**Started:** 2026-07-10
**Status:** Frozen as an engineering candidate. The Guided-default human acceptance gate was retired, and the Project Plan and Guided experience were removed in the 2026-10-14 scope reset.
**Scope:** Persistent project context, contextual Design Library, one Project Plan, modular Prepare Carve, protected Run CNC, layout migration, Advanced compatibility, and targeted de-monolith work.
**Stats:** 9 phases, 18 bounded sessions, 43 requirements.
**Master plan:** `.planning/PROJECT-CENTERED-WORKSHOP-PLAN.md`
**Execution sessions:** `.planning/PROJECT-CENTERED-WORKSHOP-SESSIONS.md`
**Release evidence:** `.planning/PROJECT-CENTERED-WORKSHOP-RELEASE-VALIDATION.md`
**Completion audit:** `.planning/project-centered-workshop-release/COMPLETION-AUDIT.md`
**UX matrix audit:** `.planning/project-centered-workshop-release/UX-MATRIX-AUDIT.md`
**Human protocol:** `.planning/PROJECT-CENTERED-WORKSHOP-USABILITY.md`

---
*Last phase completed: v0.8.0 Phase 2 — Sidecar*
*Next: v0.8.0 Phase 3 as revised in `.planning/WORKSHOP-SCOPE.md`*
