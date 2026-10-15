#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../database/database.h"
#include "../database/material_repository.h"
#include "../types.h"
#include "material.h"

namespace dw {

// MaterialManager coordinates the materials subsystem:
//   - Seeding built-in defaults into the database on first run
//   - Importing .dwmat archives (copy to app-managed dir, extract metadata, insert to DB)
//   - Exporting .dwmat archives (copy managed archive, or create metadata-only archive)
//   - CRUD delegation to MaterialRepository
//   - Material assignment on models (material_id column in models table)
class MaterialManager {
  public:
    explicit MaterialManager(Database& db);

    // Seed default materials if the database is empty (idempotent: only runs when count == 0)
    void seedDefaults();

    // Import a .dwmat file into the app-managed materials directory.
    // Copies the file to getMaterialsDir(), extracts metadata via MaterialArchive::load(),
    // inserts a MaterialRecord into the database, and returns the new material ID.
    // Returns nullopt on failure (invalid archive, copy failure, DB insert failure).
    std::optional<i64> importMaterial(const Path& dwmatPath);

    // Export a material to a .dwmat file at the given output path.
    // If the material has an archivePath: copies the managed archive to outputPath.
    // If the material has no archive (default species): creates a metadata-only .dwmat.
    // Returns true on success.
    bool exportMaterial(i64 materialId, const Path& outputPath);

    // --- Read operations (delegate to repository) ---

    std::vector<MaterialRecord> getAllMaterials();
    std::vector<MaterialRecord> getMaterialsByCategory(MaterialCategory category);
    std::optional<MaterialRecord> getMaterial(i64 id);

    // --- Write operations ---

    // Insert a new material record and return the new ID
    std::optional<i64> addMaterial(const MaterialRecord& record);

    // Update all editable fields of a material record
    bool updateMaterial(const MaterialRecord& record);

    // Remove a material. Bundled materials are hidden instead of deleted.
    // User materials are deleted from the DB and their .dwmat file is removed.
    bool removeMaterial(i64 id);

    // Hide/unhide a bundled material
    bool setMaterialHidden(i64 id, bool hidden);

    // --- Material-to-model assignment ---

    // Set material_id on the models table row (persists across restarts)
    bool assignMaterialToModel(i64 materialId, i64 modelId);

    // Clear the material assignment for a model (sets material_id to NULL)
    bool clearMaterialAssignment(i64 modelId);

    // Get the material assigned to a model (queries models table then fetches MaterialRecord)
    std::optional<MaterialRecord> getModelMaterial(i64 modelId);

  private:
    // Generate a unique filename in getMaterialsDir() to avoid collisions.
    // If "material.dwmat" exists, tries "material_1.dwmat", "material_2.dwmat", etc.
    Path uniqueArchivePath(const std::string& originalFilename) const;

    Database& m_db;
    MaterialRepository m_repo;
};

} // namespace dw
