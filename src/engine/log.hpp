#pragma once

// Leveled file logger. One file per day under %APPDATA%\shogod\logs.
// Thread-safe (the only place a mutex is allowed); never called from Present
// or CreateMove hot paths except on state transitions.

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace shogod::log {

enum class level { trace, debug, info, warn, error };

// Opens the daily log file and creates the folder tree. Safe to call twice.
void init();
void shutdown();

void write(level lvl, std::string_view msg);

// printf-style convenience: builds the line with std::format.
template <typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    write(level::info, std::format(fmt, std::forward<Args>(args)...));
}
template <typename... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    write(level::warn, std::format(fmt, std::forward<Args>(args)...));
}
template <typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    write(level::error, std::format(fmt, std::forward<Args>(args)...));
}
template <typename... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    write(level::debug, std::format(fmt, std::forward<Args>(args)...));
}

// Exposed for tests and for the menu status line.
std::string log_path();

}  // namespace shogod::log
