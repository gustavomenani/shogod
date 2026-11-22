#pragma once

// Hook surface: Present / ResizeBuffers from the swapchain vtable, WndProc via
// SetWindowLongPtrW. Installed from a thread the DLL spawns, never inside
// DllMain's loader lock. Hooks remain mapped until process exit.

namespace shogod::hooks {

bool install();

}  // namespace shogod::hooks
