#pragma once

namespace shogod::sdk {

// One-shot diagnostic: resolves the client.dll type scope and logs what the
// low vtable slots return for a known class name. Used to derive the schema
// method/field layout; not part of the shipping feature path.
void probe_schema_once();

}  // namespace shogod::sdk
