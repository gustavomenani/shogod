#pragma once

// Single source of truth for every user-visible string. Nothing else in the
// tree spells the product name, the menu header, the version or the paths.

#include <string_view>

namespace shogod::brand {

inline constexpr std::string_view k_name = "shogod";
inline constexpr std::string_view k_menu_header = "SHOGOD";
inline constexpr std::string_view k_version = "0.1.0";
inline constexpr std::string_view k_log_prefix = "[shogod]";

inline constexpr std::string_view k_appdata_folder = "shogod";
inline constexpr std::string_view k_configs_subdir = "configs";
inline constexpr std::string_view k_logs_subdir = "logs";

// "shogod vX.Y.Z"
std::string_view version_tag() noexcept;

}  // namespace shogod::brand
