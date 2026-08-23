#pragma once

#include <array>
#include <functional>
#include <string>

#include "modules/design_library/library_picker_presentation.h"

namespace dw::design_library {

struct LibraryPickerViewCallbacks {
    std::function<LibraryActionResult()> preview;
    std::function<LibraryActionResult(std::string projectName)> primary;
    std::function<LibraryActionResult()> cancel;
};

// ImGui rendering owned by the Design Library module. It translates button
// presses only; project creation, membership, navigation, and persistence stay
// in the application executor behind the callbacks.
class LibraryPickerView final {
  public:
    void render(const LibraryPickerPresentation& presentation,
                const LibraryPickerViewCallbacks& callbacks);
    void reset();

  private:
    void beginProjectNamePrompt(const LibraryPickerPresentation& presentation);
    void renderProjectNamePrompt(const LibraryPickerPresentation& presentation,
                                 const LibraryPickerViewCallbacks& callbacks);

    std::array<char, 256> m_projectName{};
    std::string m_modalError;
    bool m_openNamePrompt = false;
    bool m_namePromptActive = false;
    bool m_submitPending = false;
};

} // namespace dw::design_library
