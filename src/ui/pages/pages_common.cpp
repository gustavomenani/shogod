#include "ui/pages/pages.hpp"

namespace shogod::ui::pages {

bool begin_columns(const char* id) {
    return ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchSame);
}

void end_columns() { ImGui::EndTable(); }

}  // namespace shogod::ui::pages
