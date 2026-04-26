#pragma once

#include <string>
#include <string_view>

namespace dw::file_dialog {

// Convert app glob filters ("*.stl;*.obj") into the comma-separated extension
// list expected by nativefiledialog-extended ("stl,obj").
std::string toNativeFilterSpec(std::string_view extensions);

} // namespace dw::file_dialog
