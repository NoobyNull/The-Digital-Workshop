#pragma once

#include <string>
#include <vector>

#include "../types.h"
#include "material.h"

namespace dw {

// Result of a material generation request
struct GenerateResult {
    bool success = false;
    std::string error;
    Path dwmatPath;        // path to created .dwmat archive
    MaterialRecord record; // parsed properties
};

// Generates materials via LM Studio (texture image + CNC properties).
// All methods are blocking — call from a worker thread.
class LMStudioMaterialService {
  public:
    GenerateResult generate(const std::string& prompt, const std::string& endpoint);

  private:
    // Fetch CNC properties JSON from LM Studio
    std::string fetchProperties(const std::string& prompt, const std::string& endpoint);

    // Parse LM Studio JSON response into a MaterialRecord
    MaterialRecord parseProperties(const std::string& json, const std::string& name);
};

} // namespace dw
