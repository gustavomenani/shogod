// shogod :: DLL entry — the loader's landing point.
#include "brand.hpp"
#include "engine/hooks.hpp"
#include "engine/log.hpp"
#include "engine/runtime.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

HMODULE g_module = nullptr;

DWORD WINAPI bootstrap(LPVOID) {
    shogod::log::info("{} loaded — module base {:p}", shogod::brand::version_tag(),
                      static_cast<void*>(g_module));
    shogod::runtime::init();
    if (!shogod::hooks::install()) {
        shogod::log::error("bootstrap: hook installation failed — menu will not appear");
    }
    return 0;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH: {
            g_module = instance;
            DisableThreadLibraryCalls(instance);
            shogod::log::init();
            const HANDLE thread = CreateThread(nullptr, 0, &bootstrap, nullptr, 0, nullptr);
            if (thread != nullptr) CloseHandle(thread);
            return TRUE;
        }
        case DLL_PROCESS_DETACH:
            if (g_module != nullptr) {
                shogod::log::info("{} unloading", shogod::brand::version_tag());
                shogod::hooks::uninstall();
                shogod::runtime::shutdown();
                g_module = nullptr;
            }
            shogod::log::shutdown();
            return TRUE;
        default:
            return TRUE;
    }
}
