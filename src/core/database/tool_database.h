#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../cnc/cnc_tool.h"
#include "../types.h"
#include "database.h"

namespace dw {

// Separate SQLite database for CNC tools in Vectric .vtdb format.
// The file IS a valid .vtdb — same schema, same format — so it can be opened
// directly by Vectric software (Aspire, VCarve) for bidirectional sharing.
class ToolDatabase {
  public:
    ToolDatabase() = default;
    ~ToolDatabase() = default;

    ToolDatabase(const ToolDatabase&) = delete;
    ToolDatabase& operator=(const ToolDatabase&) = delete;

    // Open or create the .vtdb file
    bool open(const Path& path);

    // Create all 10 Vectric tables with exact DDL matching real .vtdb format
    static bool initializeSchema(Database& db);

    // --- Machine CRUD ---
    bool insertMachine(const VtdbMachine& m);
    bool updateMachine(const VtdbMachine& m);
    std::vector<VtdbMachine> findAllMachines();
    std::optional<VtdbMachine> findMachineById(const std::string& id);

    // --- Material CRUD ---
    bool insertMaterial(const VtdbMaterial& m);
    std::vector<VtdbMaterial> findAllMaterials();
    std::optional<VtdbMaterial> findMaterialById(const std::string& id);
    std::optional<VtdbMaterial> findMaterialByName(const std::string& name);

    // --- Tool Geometry CRUD ---
    bool insertGeometry(const VtdbToolGeometry& g);
    std::optional<VtdbToolGeometry> findGeometryById(const std::string& id);
    std::vector<VtdbToolGeometry> findAllGeometries();
    bool updateGeometry(const VtdbToolGeometry& g);
    bool removeGeometry(const std::string& id);

    // --- Cutting Data CRUD ---
    bool insertCuttingData(const VtdbCuttingData& cd);
    std::optional<VtdbCuttingData> findCuttingDataById(const std::string& id);
    bool updateCuttingData(const VtdbCuttingData& cd);
    bool removeCuttingData(const std::string& id);

    // --- Tool Entity (junction) ---
    bool insertEntity(const VtdbToolEntity& e);
    std::vector<VtdbToolEntity> findEntitiesForGeometry(const std::string& geomId);
    std::vector<VtdbToolEntity> findEntitiesForMaterial(const std::string& materialId);
    bool removeEntity(const std::string& id);

    // --- Tree Entry ---
    bool insertTreeEntry(const VtdbTreeEntry& te);
    std::vector<VtdbTreeEntry> findChildrenOf(const std::string& parentId);
    std::vector<VtdbTreeEntry> findRootEntries();
    std::vector<VtdbTreeEntry> getAllTreeEntries();
    bool updateTreeEntry(const VtdbTreeEntry& te);
    bool removeTreeEntry(const std::string& id);

    // --- Name Format ---
    struct NameFormat {
        std::string id;
        int tool_type = 0;
        std::string format;
    };
    std::vector<NameFormat> findAllNameFormats();

    // --- High-level ---
    std::optional<VtdbToolView> getToolView(const std::string& geomId,
                                             const std::string& materialId,
                                             const std::string& machineId);

    // Import all rows from an external .vtdb file (matched by UUID, no duplicates).
    // Returns number of geometries imported, or -1 on error.
    int importFromVtdb(const Path& externalPath);

    Database& database() { return m_db; }

  private:
    static bool migrateSchema(Database& db);
    Database m_db;
};

} // namespace dw
