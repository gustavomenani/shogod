#pragma once

// Signature table. Every row carries the pattern found on the current build,
// the resolve kind, and the runtime read-back that proves the anchor. A failed
// anchor turns its features off and is reported to the menu; it never aborts
// init.

#include <cstdint>
#include <string>

namespace shogod::sdk {

struct anchors {
    std::uintptr_t entity_system = 0;   // CGameEntitySystem*
    std::uintptr_t input = 0;           // CCSGOInput*
    std::uintptr_t schema = 0;          // ISchemaSystem*
    std::uintptr_t view_render = 0;     // CViewRender* (unresolved -> 0)

    bool entities_ok = false;
    bool input_ok = false;
    bool schema_ok = false;

    int ok_count() const noexcept {
        return (entities_ok ? 1 : 0) + (input_ok ? 1 : 0) + (schema_ok ? 1 : 0);
    }
    static constexpr int total_count = 3;

    std::string summary;
};

// Resolves every anchor for the loaded client.dll. Logs OK/FAIL per row.
const anchors& resolve();

}  // namespace shogod::sdk
