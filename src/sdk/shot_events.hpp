#pragma once

#include <cstddef>
#include <cstdint>

namespace shogod::sdk {

void update_shot_events(std::uintptr_t manager_cell, std::uintptr_t client_base,
                        std::size_t client_size, std::uintptr_t local_controller) noexcept;

}  // namespace shogod::sdk
