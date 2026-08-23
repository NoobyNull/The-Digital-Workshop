#include "selective_tool_importer.h"

#include <map>
#include <set>
#include <unordered_map>

#include "supplier_tool_catalog.h"
#include "tool_database.h"

namespace dw {
namespace {

SelectiveToolImportResult failed(SelectiveToolImportError error, std::string message) {
    SelectiveToolImportResult result;
    result.error = error;
    result.message = std::move(message);
    return result;
}

bool sameCuttingData(const VtdbCuttingData& a, const VtdbCuttingData& b) {
    return a.rate_units == b.rate_units && a.feed_rate == b.feed_rate
        && a.plunge_rate == b.plunge_rate && a.spindle_speed == b.spindle_speed
        && a.spindle_dir == b.spindle_dir && a.stepdown == b.stepdown
        && a.stepover == b.stepover && a.clear_stepover == b.clear_stepover
        && a.thread_depth == b.thread_depth && a.thread_step_in == b.thread_step_in
        && a.laser_power == b.laser_power && a.laser_passes == b.laser_passes
        && a.laser_burn_rate == b.laser_burn_rate && a.line_width == b.line_width
        && a.length_units == b.length_units && a.tool_number == b.tool_number
        && a.laser_kerf == b.laser_kerf && a.notes == b.notes;
}

std::optional<VtdbToolEntity> findEntityById(Database& db, const std::string& id) {
    auto query = db.prepare(
        "SELECT id, material_id, machine_id, tool_geometry_id, tool_cutting_data_id "
        "FROM tool_entity WHERE id=?");
    if (!query.isValid() || !query.bindText(1, id) || !query.step())
        return std::nullopt;
    VtdbToolEntity value;
    value.id = query.getText(0);
    value.material_id = query.isNull(1) ? "" : query.getText(1);
    value.machine_id = query.isNull(2) ? "" : query.getText(2);
    value.tool_geometry_id = query.getText(3);
    value.tool_cutting_data_id = query.getText(4);
    return value;
}

bool sameEntity(const VtdbToolEntity& a, const VtdbToolEntity& b) {
    return a.id == b.id && a.material_id == b.material_id
        && a.machine_id == b.machine_id && a.tool_geometry_id == b.tool_geometry_id
        && a.tool_cutting_data_id == b.tool_cutting_data_id;
}

std::optional<VtdbTreeEntry> findTreeEntryById(Database& db, const std::string& id) {
    auto query = db.prepare(
        "SELECT id, parent_group_id, sibling_order, tool_geometry_id, name, notes, expanded "
        "FROM tool_tree_entry WHERE id=?");
    if (!query.isValid() || !query.bindText(1, id) || !query.step())
        return std::nullopt;
    VtdbTreeEntry value;
    value.id = query.getText(0);
    value.parent_group_id = query.isNull(1) ? "" : query.getText(1);
    value.sibling_order = static_cast<int>(query.getInt(2));
    value.tool_geometry_id = query.isNull(3) ? "" : query.getText(3);
    value.name = query.isNull(4) ? "" : query.getText(4);
    value.notes = query.getText(5);
    value.expanded = static_cast<int>(query.getInt(6));
    return value;
}

std::optional<VtdbMachine> findMachineByName(ToolDatabase& db, const std::string& name) {
    for (const auto& machine : db.findAllMachines()) {
        if (machine.name == name) return machine;
    }
    return std::nullopt;
}

std::string treeKey(const std::string& parent, const std::string& name) {
    return parent + "\x1f" + name;
}

} // namespace

SelectiveToolImportResult SelectiveToolImporter::copySelected(
    const SupplierToolCatalog& source,
    ToolDatabase& destination,
    const std::vector<std::string>& geometryIds) {
    if (!source.isOpen())
        return failed(SelectiveToolImportError::CatalogNotOpen,
                      "Open a supplier tool database before copying tools.");
    if (geometryIds.empty())
        return failed(SelectiveToolImportError::EmptySelection,
                      "Select at least one supplier tool to copy.");

    std::vector<std::string> selected;
    std::set<std::string> seen;
    for (const auto& id : geometryIds) {
        if (seen.insert(id).second) selected.push_back(id);
    }
    for (const auto& id : selected) {
        if (!source.findBundle(id))
            return failed(SelectiveToolImportError::UnknownTool,
                          "The selected supplier tool no longer exists: " + id);
    }

    Transaction transaction(destination.database());
    if (!transaction.started())
        return failed(SelectiveToolImportError::DestinationWriteFailed,
                      "Could not start a local tool database transaction.");

    SelectiveToolImportResult result;
    std::unordered_map<std::string, std::string> materialIds;
    std::unordered_map<std::string, std::string> machineIds;
    std::unordered_map<std::string, std::string> groupIds;
    std::unordered_map<std::string, VtdbTreeEntry> treeEntriesById;
    std::map<std::string, std::string> groupsByPath;
    for (const auto& entry : destination.getAllTreeEntries()) {
        treeEntriesById.emplace(entry.id, entry);
        if (entry.tool_geometry_id.empty())
            groupsByPath.emplace(treeKey(entry.parent_group_id, entry.name), entry.id);
    }

    const auto conflict = [](const std::string& message) {
        return failed(SelectiveToolImportError::IdentityConflict, message);
    };
    const auto writeFailure = [](const std::string& message) {
        return failed(SelectiveToolImportError::DestinationWriteFailed, message);
    };

    for (const auto& geometryId : selected) {
        const auto* bundle = source.findBundle(geometryId);
        if (!bundle || bundle->geometry.id.empty() || bundle->leaf.id.empty()) {
            return failed(SelectiveToolImportError::BrokenSourceGraph,
                          "The selected supplier tool has an incomplete graph: " + geometryId);
        }

        if (destination.findGeometryById(geometryId)) {
            result.items.push_back(
                {geometryId, geometryId, ToolCopyDisposition::AlreadyPresent});
            ++result.alreadyPresentCount;
            continue;
        }

        for (const auto& material : bundle->materials) {
            if (materialIds.count(material.id)) continue;
            if (auto byId = destination.findMaterialById(material.id)) {
                if (byId->name != material.name)
                    return conflict("A local material uses supplier UUID " + material.id);
                materialIds[material.id] = material.id;
                continue;
            }
            if (auto byName = destination.findMaterialByName(material.name)) {
                materialIds[material.id] = byName->id;
                continue;
            }
            if (!destination.insertMaterial(material)
                || !destination.findMaterialById(material.id)) {
                return writeFailure("Could not copy material: " + material.name);
            }
            materialIds[material.id] = material.id;
        }

        for (const auto& machine : bundle->machines) {
            if (machineIds.count(machine.id)) continue;
            if (auto byId = destination.findMachineById(machine.id)) {
                if (byId->name != machine.name)
                    return conflict("A local machine uses supplier UUID " + machine.id);
                machineIds[machine.id] = machine.id;
                continue;
            }
            if (auto byName = findMachineByName(destination, machine.name)) {
                machineIds[machine.id] = byName->id;
                continue;
            }
            if (!destination.insertMachine(machine)
                || !destination.findMachineById(machine.id)) {
                return writeFailure("Could not copy machine dependency: " + machine.name);
            }
            machineIds[machine.id] = machine.id;
        }

        for (const auto& cutting : bundle->cuttingData) {
            if (auto existing = destination.findCuttingDataById(cutting.id)) {
                if (!sameCuttingData(*existing, cutting))
                    return conflict("A local cutting profile uses supplier UUID " + cutting.id);
                continue;
            }
            if (!destination.insertCuttingData(cutting)
                || !destination.findCuttingDataById(cutting.id)) {
                return writeFailure("Could not copy a supplier cutting profile.");
            }
        }

        if (!destination.insertGeometry(bundle->geometry)
            || !destination.findGeometryById(bundle->geometry.id)) {
            return writeFailure("Could not copy supplier tool: " + geometryId);
        }

        for (const auto& sourceGroup : bundle->ancestors) {
            std::string localParent;
            if (!sourceGroup.parent_group_id.empty()) {
                auto parent = groupIds.find(sourceGroup.parent_group_id);
                if (parent == groupIds.end()) {
                    return failed(SelectiveToolImportError::BrokenSourceGraph,
                                  "Supplier category ancestors are out of order.");
                }
                localParent = parent->second;
            }

            auto mapped = groupIds.find(sourceGroup.id);
            if (mapped != groupIds.end()) continue;
            auto existingId = treeEntriesById.find(sourceGroup.id);
            if (existingId != treeEntriesById.end()) {
                const auto& existing = existingId->second;
                if (!existing.tool_geometry_id.empty() || existing.name != sourceGroup.name
                    || existing.parent_group_id != localParent) {
                    return conflict("A local tool category uses supplier UUID " + sourceGroup.id);
                }
                groupIds[sourceGroup.id] = existing.id;
                continue;
            }
            auto samePath = groupsByPath.find(treeKey(localParent, sourceGroup.name));
            if (samePath != groupsByPath.end()) {
                groupIds[sourceGroup.id] = samePath->second;
                continue;
            }

            VtdbTreeEntry local = sourceGroup;
            local.parent_group_id = localParent;
            if (!destination.insertTreeEntry(local))
                return writeFailure("Could not copy supplier category: " + local.name);
            auto inserted = findTreeEntryById(destination.database(), local.id);
            if (!inserted)
                return writeFailure("Could not verify supplier category: " + local.name);
            treeEntriesById[local.id] = *inserted;
            groupsByPath[treeKey(localParent, local.name)] = local.id;
            groupIds[sourceGroup.id] = local.id;
        }

        VtdbTreeEntry leaf = bundle->leaf;
        if (!leaf.parent_group_id.empty()) {
            auto parent = groupIds.find(leaf.parent_group_id);
            if (parent == groupIds.end()) {
                return failed(SelectiveToolImportError::BrokenSourceGraph,
                              "The supplier tool has no usable category parent.");
            }
            leaf.parent_group_id = parent->second;
        }
        if (auto existing = findTreeEntryById(destination.database(), leaf.id)) {
            if (existing->tool_geometry_id != leaf.tool_geometry_id
                || existing->parent_group_id != leaf.parent_group_id) {
                return conflict("A local library entry uses supplier UUID " + leaf.id);
            }
        } else if (!destination.insertTreeEntry(leaf)
                   || !findTreeEntryById(destination.database(), leaf.id)) {
            return writeFailure("Could not add the supplier tool to the local library tree.");
        }

        for (const auto& sourceEntity : bundle->entities) {
            VtdbToolEntity entity = sourceEntity;
            if (!entity.material_id.empty()) {
                auto mapped = materialIds.find(entity.material_id);
                if (mapped == materialIds.end()) {
                    return failed(SelectiveToolImportError::BrokenSourceGraph,
                                  "The supplier tool references an unknown material.");
                }
                entity.material_id = mapped->second;
            }
            if (!entity.machine_id.empty()) {
                auto mapped = machineIds.find(entity.machine_id);
                if (mapped == machineIds.end()) {
                    return failed(SelectiveToolImportError::BrokenSourceGraph,
                                  "The supplier tool references an unknown machine.");
                }
                entity.machine_id = mapped->second;
            }
            if (auto existing = findEntityById(destination.database(), entity.id)) {
                if (!sameEntity(*existing, entity))
                    return conflict("A local tool relationship uses supplier UUID " + entity.id);
                continue;
            }
            if (!destination.insertEntity(entity))
                return writeFailure("Could not copy a supplier feeds-and-speeds relationship.");
            auto inserted = findEntityById(destination.database(), entity.id);
            if (!inserted || !sameEntity(*inserted, entity))
                return writeFailure("Could not verify a copied tool relationship.");
        }

        result.items.push_back({geometryId, geometryId, ToolCopyDisposition::Copied});
        ++result.copiedCount;
    }

    if (!transaction.commit())
        return failed(SelectiveToolImportError::CommitFailed,
                      "Could not commit the selected tools to the local database.");
    return result;
}

} // namespace dw
