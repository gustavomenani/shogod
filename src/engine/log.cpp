// shogod :: leveled logger — %APPDATA%\shogod\logs\shogod-YYYYMMDD.log
#include "engine/log.hpp"

#include "brand.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>

namespace shogod::log {
namespace {

std::mutex g_mutex;
std::FILE* g_file = nullptr;
std::string g_path;
bool g_initialised = false;

std::string appdata_root() {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &raw))) {
        return ".";
    }
    std::wstring wide(raw);
    CoTaskMemFree(raw);
    return std::filesystem::path(wide).string();
}

std::string today_stamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &t);
    char buf[16]{};
    std::snprintf(buf, sizeof buf, "%04d%02d%02d", local.tm_year + 1900, local.tm_mon + 1,
                  local.tm_mday);
    return buf;
}

const char* level_name(level lvl) {
    switch (lvl) {
        case level::trace: return "TRACE";
        case level::debug: return "DEBUG";
        case level::info:  return "INFO";
        case level::warn:  return "WARN";
        case level::error: return "ERROR";
    }
    return "INFO";
}

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &t);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()) %
                    1000;
    char buf[32]{};
    std::snprintf(buf, sizeof buf, "%02d:%02d:%02d.%03d", local.tm_hour, local.tm_min,
                  local.tm_sec, static_cast<int>(ms.count()));
    return buf;
}

}  // namespace

void init() {
    std::scoped_lock lock(g_mutex);
    if (g_initialised) return;

    std::error_code ec;
    const std::filesystem::path root = std::filesystem::path(appdata_root()) /
                                       std::string(brand::k_appdata_folder) /
                                       std::string(brand::k_logs_subdir);
    std::filesystem::create_directories(root, ec);

    g_path = (root / ("shogod-" + today_stamp() + ".log")).string();
    (void)fopen_s(&g_file, g_path.c_str(), "ab");
    g_initialised = true;

    if (g_file) {
        const std::string line = std::string(brand::k_log_prefix) + " " +
                                 std::string(brand::version_tag()) + " — log opened";
        std::fprintf(g_file, "%s\n", line.c_str());
        std::fflush(g_file);
    }
}

void shutdown() {
    std::scoped_lock lock(g_mutex);
    if (g_file) {
        std::fclose(g_file);
        g_file = nullptr;
    }
    g_initialised = false;
}

void write(level lvl, std::string_view msg) {
    std::scoped_lock lock(g_mutex);
    const std::string line = std::string(brand::k_log_prefix) + " " + timestamp() + " [" +
                             level_name(lvl) + "] " + std::string(msg) + "\n";
    if (g_file) {
        std::fwrite(line.data(), 1, line.size(), g_file);
        std::fflush(g_file);
    }
#ifdef _DEBUG
    OutputDebugStringA(line.c_str());
#endif
}

std::string log_path() {
    std::scoped_lock lock(g_mutex);
    return g_path;
}

}  // namespace shogod::log
