#pragma once

#include "toolpath_types.h"
#include "../cnc/machine_units.h"

#include <string>

namespace dw {
namespace carve {

// Export toolpath to G-code file
// Returns true on success, false if file could not be written
bool exportGcode(const std::string& path,
                 const MultiPassToolpath& toolpath,
                 const ToolpathConfig& config,
                 const std::string& modelName,
                 const std::string& toolName,
                 cnc::SendUnits units = cnc::SendUnits::Millimeters);

// Generate G-code string (for testing without filesystem)
std::string generateGcode(const MultiPassToolpath& toolpath,
                          const ToolpathConfig& config,
                          const std::string& modelName,
                          const std::string& toolName,
                          cnc::SendUnits units = cnc::SendUnits::Millimeters);

} // namespace carve
} // namespace dw
