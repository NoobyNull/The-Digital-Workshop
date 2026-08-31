#include "project_plan.h"
#include "project_plan_detail.h"

#include <algorithm>

namespace dw::project_plan {
using namespace detail;

ProjectPlanBuilder::ProjectPlanBuilder(ProjectPlanBuilderOptions options)
    : m_options(options) {}

ProjectPlan ProjectPlanBuilder::build(const ProjectPlanInput& input) const {
    ProjectPlan plan;
    plan.project = input.project;
    plan.projectName = input.projectName;
    plan.stages = makeStages();
    if (!m_options.enabled) {
        plan.status = BuildStatus::Disabled;
        setAction(plan.nextAction, NextActionKind::None, StageId::DesignAndSize,
                  "Project Plan disabled", "The Guided Project Plan is disabled.");
        return plan;
    }
    if (!input.project.valid()) {
        plan.status = BuildStatus::InvalidProject;
        setAction(plan.nextAction, NextActionKind::None, StageId::DesignAndSize,
                  "No project", "Open or create a project to build its plan.");
        return plan;
    }
    plan.status = BuildStatus::Ready;

    auto snapshots = input.items;
    std::sort(snapshots.begin(), snapshots.end(), [](const ItemSnapshot& lhs,
                                                     const ItemSnapshot& rhs) {
        if (lhs.ref.item.value != rhs.ref.item.value)
            return lhs.ref.item.value < rhs.ref.item.value;
        if (lhs.ref.project.value != rhs.ref.project.value)
            return lhs.ref.project.value < rhs.ref.project.value;
        if (lhs.kind != rhs.kind) return lhs.kind < rhs.kind;
        return lhs.label < rhs.label;
    });

    std::vector<WorkingItem> items;
    for (const auto& item : snapshots) {
        const auto stage = stageFor(item.kind);
        if (!item.ref.valid() || item.ref.project != input.project) {
            plan.diagnostics.push_back(blocker(stage, BlockerCode::ForeignItem,
                                               "Ignored an invalid or foreign-project item.",
                                               item.ref.valid()
                                                   ? std::optional<ItemRef>(item.ref)
                                                   : std::nullopt));
            continue;
        }
        if (!items.empty() && items.back().item.ref.item == item.ref.item) {
            plan.diagnostics.push_back(blocker(stage, BlockerCode::DuplicateItem,
                                               "Ignored a duplicate project item ID.", item.ref));
            continue;
        }
        items.push_back({item, std::nullopt, {}});
    }

    for (std::size_t index = 0; index < items.size(); ++index) {
        if (!items[index].item.parent) continue;
        const auto parent = findItem(items, *items[index].item.parent);
        const bool selfParent = parent && *parent == index;
        if (!parent || selfParent) {
            auto issue = blocker(stageFor(items[index].item.kind),
                                 selfParent ? BlockerCode::ParentCycle
                                            : BlockerCode::MissingParent,
                                 selfParent ? "An item cannot be its own parent."
                                            : "The item's parent is missing.",
                                 items[index].item.ref);
            items[index].blockers.push_back(issue);
            plan.diagnostics.push_back(issue);
            items[index].item.parent.reset();
            continue;
        }
        items[index].parent = parent;
    }

    for (std::size_t start = 0; start < items.size(); ++start) {
        std::vector<std::size_t> path;
        auto current = std::optional<std::size_t>(start);
        while (current) {
            const auto repeated = std::find(path.begin(), path.end(), *current);
            if (repeated != path.end()) {
                const auto breakAt = *std::min_element(repeated, path.end(), [&](auto lhs, auto rhs) {
                    return items[lhs].item.ref.item.value < items[rhs].item.ref.item.value;
                });
                for (auto it = repeated; it != path.end(); ++it) {
                    auto issue = blocker(stageFor(items[*it].item.kind),
                                         BlockerCode::ParentCycle,
                                         "A parent cycle was broken to preserve this item.",
                                         items[*it].item.ref);
                    items[*it].blockers.push_back(issue);
                    plan.diagnostics.push_back(issue);
                }
                items[breakAt].parent.reset();
                items[breakAt].item.parent.reset();
                break;
            }
            path.push_back(*current);
            current = items[*current].parent;
        }
    }

    plan.nodes.reserve(items.size());
    for (const auto& working : items) {
        PlanNode node;
        node.item = working.item;
        node.primaryStage = stageFor(node.item.kind);
        node.role = !usable(node.item.state) || !working.blockers.empty()
                        ? NodeRole::RepairItem
                        : (actionable(node.item.kind) ? NodeRole::ActivateItem
                                                     : NodeRole::Informational);
        node.blockers = working.blockers;
        plan.nodes.push_back(std::move(node));
    }
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (items[index].parent) {
            plan.nodes[*items[index].parent].children.push_back(items[index].item.ref.item);
        } else {
            plan.roots.push_back(items[index].item.ref.item);
        }
        plan.stages[stageIndex(stageFor(items[index].item.kind))]
            .contributingItems.push_back(items[index].item.ref.item);
    }

    bool liveRunValid = false;
    bool runMismatch = false;
    bool interrupted = false;
    std::optional<std::size_t> branch;
    if (input.liveRun) {
        const auto job = findItem(items, input.liveRun->job.item);
        const auto program = findItem(items, input.liveRun->program.item);
        const auto operation = input.liveRun->operation
                                   ? findItem(items, input.liveRun->operation->item)
                                   : std::nullopt;
        const auto programOperation = program
                                          ? ancestorOfKind(items, *program,
                                                           ItemKind::Operation)
                                          : std::nullopt;
        const bool operationMatches = input.liveRun->operation
                                          ? input.liveRun->operation->project == input.project &&
                                                operation &&
                                                items[*operation].item.kind == ItemKind::Operation &&
                                                programOperation == operation
                                          : !programOperation;
        const bool identitiesMatch = input.liveRun->job.project == input.project &&
                                     input.liveRun->program.project == input.project && job &&
                                     program && items[*job].item.kind == ItemKind::Job &&
                                     items[*program].item.kind == ItemKind::GCode &&
                                     ancestorOfKind(items, *job, ItemKind::GCode) == program &&
                                     operationMatches;
        interrupted = identitiesMatch && input.liveRun->state == RunState::Interrupted &&
                      items[*job].item.state == ItemState::Stale;
        liveRunValid = identitiesMatch && !interrupted &&
                       input.liveRun->state != RunState::Interrupted &&
                       items[*job].item.state == ItemState::Sent;
        runMismatch = !identitiesMatch || (!interrupted && !liveRunValid);
        if (identitiesMatch) branch = operation ? operation : program;
    }
    if (!branch && input.liveOperation &&
        input.liveOperation->operation.project == input.project) {
        const auto candidate = findItem(items, input.liveOperation->operation.item);
        if (candidate && items[*candidate].item.kind == ItemKind::Operation) branch = candidate;
    }
    if (!branch) branch = focusedBranch(input, items);
    if (!branch) {
        for (std::size_t index = 0; index < items.size(); ++index) {
            if (items[index].item.kind == ItemKind::Operation &&
                !completedJobBelow(items, index)) {
                branch = index;
                break;
            }
        }
    }
    if (!branch) {
        auto operation = std::find_if(items.begin(), items.end(), [](const WorkingItem& item) {
            return item.item.kind == ItemKind::Operation;
        });
        if (operation != items.end()) branch = static_cast<std::size_t>(operation - items.begin());
    }
    if (!branch) {
        auto gcode = std::find_if(items.begin(), items.end(), [](const WorkingItem& item) {
            return item.item.kind == ItemKind::GCode;
        });
        if (gcode != items.end()) branch = static_cast<std::size_t>(gcode - items.begin());
    }

    const bool standaloneGcode = branch && items[*branch].item.kind == ItemKind::GCode &&
                                 !ancestorOfKind(items, *branch, ItemKind::Operation);
    std::optional<std::size_t> model;
    if (branch) model = ancestorOfKind(items, *branch, ItemKind::Model);
    if (!model) {
        auto found = std::find_if(items.begin(), items.end(), [](const WorkingItem& item) {
            return item.item.kind == ItemKind::Model && usable(item.item.state);
        });
        if (found != items.end()) model = static_cast<std::size_t>(found - items.begin());
    }

    OperationFacts facts;
    if (branch && items[*branch].item.kind == ItemKind::Operation) {
        facts.operation = items[*branch].item.ref;
        const auto stored = std::find_if(input.operations.begin(), input.operations.end(),
                                         [&](const OperationFacts& candidate) {
                                             return candidate.operation == facts.operation;
                                         });
        if (stored != input.operations.end()) facts = *stored;
        if (input.liveOperation) {
            if (input.liveOperation->operation == facts.operation) {
                facts = mergeFacts(facts, *input.liveOperation);
            } else if (input.liveOperation->operation.project == input.project) {
                plan.diagnostics.push_back(blocker(
                    StageId::CarvePreview, BlockerCode::SnapshotMismatch,
                    "Live preparation facts do not match the selected operation.",
                    input.liveOperation->operation));
            }
        }
    }
    if (model && usable(items[*model].item.state) &&
        facts.modelLoaded == Evidence::Unknown)
        facts.modelLoaded = Evidence::Satisfied;
    if (branch && items[*branch].item.kind == ItemKind::Operation) {
        for (std::size_t index = 0; index < items.size(); ++index) {
            if (!descendsFrom(items, index, *branch) || !usable(items[index].item.state)) continue;
            if (items[index].item.kind == ItemKind::Tool &&
                facts.finishingToolSelected == Evidence::Unknown)
                facts.finishingToolSelected = Evidence::Satisfied;
            if (items[index].item.kind == ItemKind::GCode &&
                facts.toolpathGenerated == Evidence::Unknown)
                facts.toolpathGenerated = Evidence::Satisfied;
            if (items[index].item.kind == ItemKind::Zeroing &&
                items[index].item.state == ItemState::Ready &&
                facts.zeroVerified == Evidence::Unknown)
                facts.zeroVerified = Evidence::Satisfied;
        }
    }

    const bool branchComplete = branch && completedJobBelow(items, *branch);
    plan.branchComplete = branchComplete;
    const bool hasUsableModel = model.has_value();
    const bool hasLiveSendableProgram =
        branch && items[*branch].item.kind == ItemKind::Operation &&
        input.liveOperation && input.liveOperation->operation == facts.operation &&
        input.liveOperation->toolpathGenerated == Evidence::Satisfied &&
        input.liveOperation->toolpathFresh == Evidence::Satisfied;
    const bool hasUsableGcode =
        hasLiveSendableProgram ||
        (branch && items[*branch].item.kind == ItemKind::GCode
             ? usable(items[*branch].item.state)
             : std::any_of(items.begin(), items.end(), [&](const WorkingItem& item) {
                   const auto index =
                       static_cast<std::size_t>(&item - items.data());
                   return item.item.kind == ItemKind::GCode &&
                          usable(item.item.state) && branch &&
                          descendsFrom(items, index, *branch);
               }));

    if (standaloneGcode) {
        plan.stages[0].state = StageState::NotApplicable;
        plan.stages[1].state = StageState::NotApplicable;
        const bool hasTool = std::any_of(items.begin(), items.end(), [&](const WorkingItem& item) {
            const auto index = static_cast<std::size_t>(&item - items.data());
            return item.item.kind == ItemKind::Tool && usable(item.item.state) &&
                   descendsFrom(items, index, *branch);
        });
        plan.stages[2].state = hasTool ? StageState::Complete : StageState::Available;
        if (!hasTool)
            plan.stages[2].blockers.push_back(blocker(
                StageId::ChooseTool, BlockerCode::VerificationRequired,
                "Verify the tool required by the imported G-code."));
        plan.stages[3].state = hasUsableGcode ? StageState::Complete : StageState::NeedsAttention;
        plan.stages[4].state = hasUsableGcode ? StageState::Available : StageState::Locked;
        plan.stages[4].blockers.push_back(blocker(
            StageId::MachineSetup, BlockerCode::VerificationRequired,
            "Verify the machine, work zero, safe Z, and outline before running."));
        plan.stages[5].state = StageState::Locked;
    } else {
        auto& design = plan.stages[0];
        if (!hasUsableModel) {
            design.state = StageState::Available;
            design.blockers.push_back(blocker(StageId::DesignAndSize,
                                              BlockerCode::MissingRequirement,
                                              "Choose a model for this project."));
        } else {
            requireEvidence(design, facts.modelLoaded, "that the design is loaded");
            requireEvidence(design, facts.modelFitsBlank, "that the design fits the blank");
            requireEvidence(design, facts.modelFitsMachine, "that the design fits the machine");
            design.state = evidenceComplete(design) ? StageState::Complete : StageState::Available;
        }

        auto& material = plan.stages[1];
        if (design.state == StageState::Complete) {
            requireEvidence(material, facts.materialSelected, "the material selection");
            requireEvidence(material, facts.blankSpecified, "the blank dimensions");
            material.state = evidenceComplete(material) ? StageState::Complete
                                                        : StageState::Available;
        }
        auto& tool = plan.stages[2];
        if (material.state == StageState::Complete) {
            requireEvidence(tool, facts.finishingToolSelected, "the finishing tool");
            requireEvidence(tool, facts.toolSetupConfirmed, "the tool setup");
            tool.state = evidenceComplete(tool) ? StageState::Complete : StageState::Available;
        }
        auto& preview = plan.stages[3];
        if (tool.state == StageState::Complete) {
            requireEvidence(preview, facts.toolpathGenerated, "the carve preview");
            requireEvidence(preview, facts.toolpathFresh,
                            "that the preview matches the current settings");
            preview.state = evidenceComplete(preview) ? StageState::Complete
                                                      : StageState::Available;
        }
        auto& machine = plan.stages[4];
        if (preview.state == StageState::Complete) {
            requireEvidence(machine, facts.machineConnected, "the CNC connection");
            requireEvidence(machine, facts.machineIdle, "that the CNC is idle");
            requireEvidence(machine, facts.machineAlarmClear, "that alarms are clear");
            requireEvidence(machine, facts.machineProfileConfigured, "the machine profile");
            requireEvidence(machine, facts.machineHomedOrSkipped, "homing or an explicit skip");
            requireEvidence(machine, facts.limitSwitchesClear, "that limit switches are clear");
            requireEvidence(machine, facts.safeZVerified, "safe Z");
            requireEvidence(machine, facts.zeroVerified, "the work zero");
            requireEvidence(machine, facts.outlineCompletedOrSkipped,
                            "the outline check or an explicit skip");
            machine.state = evidenceComplete(machine) ? StageState::Complete
                                                      : StageState::Available;
        }
        auto& review = plan.stages[5];
        if (machine.state == StageState::Complete) {
            review.state = StageState::Available;
            if (!hasUsableGcode)
                review.blockers.push_back(blocker(StageId::ReviewAndRun,
                                                  BlockerCode::MissingRequirement,
                                                  "Create a sendable G-code program."));
            requireEvidence(review, facts.finalConfirmed, "the final safety review");
        }
    }

    if (branchComplete || liveRunValid) {
        for (std::size_t index = 0; index < plan.stages.size(); ++index)
            plan.stages[index].blockers.clear();
        for (std::size_t index = 0; index < 5; ++index)
            plan.stages[index].state = StageState::Complete;
        plan.stages[5].state = branchComplete ? StageState::Complete : StageState::InProgress;
    }

    std::optional<ItemRef> repairTarget;
    StageId repairStage = StageId::ReviewAndRun;
    bool repairTargetMissing = false;
    for (std::size_t index = 0; index < items.size(); ++index) {
        auto& node = plan.nodes[index];
        for (const auto& issue : items[index].blockers)
            plan.stages[stageIndex(node.primaryStage)].blockers.push_back(issue);
        if (items[index].item.state == ItemState::Stale ||
            items[index].item.state == ItemState::Missing) {
            auto issue = blocker(node.primaryStage,
                                 items[index].item.state == ItemState::Missing
                                     ? BlockerCode::MissingItem
                                     : BlockerCode::StaleItem,
                                 items[index].item.state == ItemState::Missing
                                     ? "This project item is missing."
                                     : "This project item changed and must be refreshed.",
                                 items[index].item.ref);
            node.blockers.push_back(issue);
            plan.stages[stageIndex(node.primaryStage)].blockers.push_back(issue);
        }
        const bool candidateMissing = items[index].item.state == ItemState::Missing;
        const bool betterRepair =
            !repairTarget || stageIndex(node.primaryStage) < stageIndex(repairStage) ||
            (node.primaryStage == repairStage && candidateMissing && !repairTargetMissing) ||
            (node.primaryStage == repairStage && candidateMissing == repairTargetMissing &&
             items[index].item.ref.item.value < repairTarget->item.value);
        if (!liveRunValid && gatesProgress(items[index].item.kind) &&
            (!usable(items[index].item.state) || !items[index].blockers.empty()) &&
            betterRepair) {
            repairTarget = items[index].item.ref;
            repairStage = node.primaryStage;
            repairTargetMissing = candidateMissing;
        }
    }
    if (repairTarget) {
        plan.stages[stageIndex(repairStage)].state = StageState::NeedsAttention;
        for (std::size_t index = stageIndex(repairStage) + 1; index < plan.stages.size(); ++index)
            if (plan.stages[index].state != StageState::Complete)
                plan.stages[index].state = StageState::Locked;
    }

    const auto sentJob = std::find_if(items.begin(), items.end(), [](const WorkingItem& item) {
        return item.item.kind == ItemKind::Job && item.item.state == ItemState::Sent;
    });
    const bool sentJobExists = sentJob != items.end();
    const bool anotherSentJob = liveRunValid && std::any_of(
        items.begin(), items.end(), [&](const WorkingItem& item) {
            return item.item.kind == ItemKind::Job &&
                   item.item.state == ItemState::Sent &&
                   item.item.ref != input.liveRun->job;
        });
    runMismatch = runMismatch || (sentJobExists && !liveRunValid) || anotherSentJob;
    if (runMismatch) {
        auto issue = blocker(StageId::ReviewAndRun, BlockerCode::RunStateMismatch,
                             "The saved job and live machine run do not agree.");
        plan.stages[5].state = StageState::NeedsAttention;
        plan.stages[5].blockers.push_back(issue);
        plan.diagnostics.push_back(issue);
    }

    const auto branchRef = branch ? std::optional<ItemRef>(items[*branch].item.ref) : std::nullopt;
    const auto modelRef = model ? std::optional<ItemRef>(items[*model].item.ref) : branchRef;
    if (runMismatch) {
        setAction(plan.nextAction, NextActionKind::ReconcileRunState, StageId::ReviewAndRun,
                  "Check the interrupted job",
                  "Reconcile the saved job with the machine before continuing.",
                  input.liveRun ? std::optional<ItemRef>(input.liveRun->job)
                                : (sentJobExists
                                       ? std::optional<ItemRef>(sentJob->item.ref)
                                       : branchRef));
    } else if (liveRunValid) {
        setAction(plan.nextAction, NextActionKind::MonitorRun, StageId::ReviewAndRun,
                  "Return to running job", "A machine run is active and has priority.",
                  input.liveRun->job);
    } else if (repairTarget) {
        setAction(plan.nextAction, NextActionKind::RepairItem, repairStage,
                  "Repair project item",
                  "Repair the earliest project item that blocks safe progress.", repairTarget);
    } else if (branchComplete) {
        setAction(plan.nextAction, NextActionKind::None, StageId::ReviewAndRun,
                  "Project branch complete", "The completed run is available in project history.");
    } else if (interrupted) {
        setAction(plan.nextAction, NextActionKind::ReviewInterruptedRun, StageId::ReviewAndRun,
                  "Review interrupted run", "Review the interruption before preparing another run.",
                  input.liveRun->job);
    } else if (!hasUsableModel && !standaloneGcode) {
        setAction(plan.nextAction, NextActionKind::AddDesignFromLibrary,
                  StageId::DesignAndSize, "Choose a model",
                  "Choose a reusable model from the Design Library.");
    } else if (plan.stages[0].state == StageState::Available) {
        setAction(plan.nextAction, NextActionKind::OpenDesignAndSize, StageId::DesignAndSize,
                  "Check design and size", "Fit the design to the blank and machine travel.", modelRef);
    } else if (plan.stages[1].state == StageState::Available) {
        setAction(plan.nextAction, NextActionKind::OpenMaterialAndBlank,
                  StageId::MaterialAndBlank, "Choose material and blank",
                  "Select the material before choosing a tool.", branchRef ? branchRef : modelRef);
    } else if (plan.stages[2].state == StageState::Available) {
        setAction(plan.nextAction, NextActionKind::OpenToolSelection, StageId::ChooseTool,
                  "Choose or verify the tool", "Choose the cutter required for this work.", branchRef);
    } else if (plan.stages[3].state == StageState::Available || !hasUsableGcode) {
        setAction(plan.nextAction, NextActionKind::OpenCarvePreview, StageId::CarvePreview,
                  "Build the carve preview",
                  "Generate a current preview and sendable program before machine setup.", branchRef);
    } else if (plan.stages[4].state == StageState::Available) {
        setAction(plan.nextAction, NextActionKind::OpenMachineSetup, StageId::MachineSetup,
                  "Set up the machine",
                  "Connect, home, zero, and verify the outline before review.", branchRef);
    } else {
        setAction(plan.nextAction, NextActionKind::OpenReviewAndRun, StageId::ReviewAndRun,
                  "Review and run", "Review the final safety summary before starting.", branchRef);
    }

    return plan;
}

} // namespace dw::project_plan
