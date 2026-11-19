#include "engine/module.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstring>

namespace shogod::module {

info find(std::string_view name) noexcept {
    const HMODULE handle = GetModuleHandleA(name.data());
    if (handle == nullptr) return {};

    const auto* base = reinterpret_cast<const std::uint8_t*>(handle);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return {};

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return {};

    info out;
    out.base = reinterpret_cast<std::uintptr_t>(base);
    out.size = nt->OptionalHeader.SizeOfImage;
    return out;
}

}  // namespace shogod::module
