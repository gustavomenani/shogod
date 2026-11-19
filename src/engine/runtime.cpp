#include "engine/runtime.hpp"

#include "brand.hpp"
#include "engine/log.hpp"

#include <chrono>
#include <memory>

namespace shogod::runtime {
namespace {

struct state {
    config::store cfg;
    ui::menu_state menu;
    ui::status live;
    std::chrono::steady_clock::time_point frame_start = std::chrono::steady_clock::now();
    int frames = 0;
    std::chrono::steady_clock::time_point window_start = std::chrono::steady_clock::now();
};

std::unique_ptr<state> g_state;

}  // namespace

void init() {
    if (g_state) return;
    g_state = std::make_unique<state>();
    g_state->cfg.load(config::store::default_dir() / "default.json");
    g_state->live.config_name = "default";
    log::info("runtime: {} ready", brand::version_tag());
}

void shutdown() {
    if (!g_state) return;
    g_state->cfg.save(config::store::default_dir() / "default.json");
    g_state.reset();
}

config::store& cfg() { return g_state->cfg; }
 ui::menu_state& menu() { return g_state->menu; }
ui::status& live() { return g_state->live; }

void frame_tick() {
    if (!g_state) return;
    ++g_state->frames;
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration<float>(now - g_state->window_start).count();
    if (elapsed >= 0.5f) {
        g_state->live.fps = static_cast<float>(g_state->frames) / elapsed;
        g_state->frames = 0;
        g_state->window_start = now;
    }
}

void set_anchor_summary(int ok, int total, std::string detail) {
    if (!g_state) return;
    g_state->live.anchors_ok = ok;
    g_state->live.anchors_total = total;
    g_state->live.anchor_detail = std::move(detail);
}

}  // namespace shogod::runtime
