// shogod :: DLL entry — the loader's landing point.
#include "brand.hpp"
#include "engine/hooks.hpp"
#include "engine/log.hpp"
#include "engine/module.hpp"
#include "engine/runtime.hpp"
#include "sdk/schema.hpp"
#include "sdk/signatures.hpp"
#include "sdk/trace.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

HMODULE g_module = nullptr;

DWORD WINAPI bootstrap(LPVOID) {
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&bootstrap), &pinned)) return 1;
    shogod::log::init();
    shogod::log::info("{} loaded — module base {:p}", shogod::brand::version_tag(),
                      static_cast<void*>(g_module));
    shogod::runtime::init();

    const auto& anchors = shogod::sdk::resolve();
    shogod::runtime::set_anchors(anchors);
    std::string detail = anchors.summary;
    if (anchors.game_event_manager_cell == 0) detail += "shot events: anchor unavailable; ";
    if (anchors.schema_ok) {
        shogod::sdk::netvars nv{};
        if (shogod::sdk::init_schema(anchors.schema, nv)) {
            shogod::runtime::set_netvars(nv);
        } else {
            detail += "schema: netvar lookup failed; ";
        }
    }
    {
        const auto client = shogod::module::find("client.dll");
        if (!shogod::sdk::init_trace(client.base, client.size)) detail += "trace: query unavailable; ";
    }
    shogod::runtime::set_anchor_summary(anchors.ok_count(), shogod::sdk::anchors::total_count,
                                        detail.empty() ? "all resolved" : detail);

    if (!shogod::hooks::install()) {
        shogod::log::error("bootstrap: hook installation failed — menu will not appear");
    }
    return 0;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    g_module = instance;
    DisableThreadLibraryCalls(instance);
    const HANDLE thread = CreateThread(nullptr, 0, &bootstrap, nullptr, 0, nullptr);
    if (thread == nullptr) return FALSE;
    CloseHandle(thread);
    return TRUE;
}
