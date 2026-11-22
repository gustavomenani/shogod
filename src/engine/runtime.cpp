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
    sdk::publisher snap_pub;
    sdk::anchors anchors{};
    sdk::netvars netvars{};
    features::backtrack_history backtrack;
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

config::store& cfg() { return g_state->cfg; }
ui::menu_state& menu() { return g_state->menu; }
ui::status& live() { return g_state->live; }
sdk::publisher& snapshot_publisher() { return g_state->snap_pub; }
const sdk::anchors& anchors() { return g_state->anchors; }
void set_anchors(const sdk::anchors& a) {
    if (g_state) g_state->anchors = a;
}
const sdk::netvars& netvars() { return g_state->netvars; }
void set_netvars(const sdk::netvars& nv) {
    if (g_state) {
        g_state->netvars = nv;
        g_state->live.no_flash_available = nv.cs_pawn_flash_overlay_alpha > 0;
    }
}
features::backtrack_history& backtrack() { return g_state->backtrack; }

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

    if (const auto snap = g_state->snap_pub.acquire()) {
        g_state->live.in_game = snap->in_game;
        g_state->live.tick = snap->tick;
        g_state->live.players = snap->count;
        g_state->live.view_matrix_ready = snap->view_matrix.usable_projection();
    }
}

void set_anchor_summary(int ok, int total, std::string detail) {
    if (!g_state) return;
    g_state->live.anchors_ok = ok;
    g_state->live.anchors_total = total;
    g_state->live.anchor_detail = std::move(detail);
}

}  // namespace shogod::runtime
