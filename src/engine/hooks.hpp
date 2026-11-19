#pragma once

// Hook surface: Present / ResizeBuffers from the swapchain vtable, WndProc via
// SetWindowLongPtrW. Installed from a thread the DLL spawns, never inside
// DllMain's loader lock. Uninstall walks the same list in reverse.

namespace shogod::hooks {

bool install();
void uninstall();

}  // namespace shogod::hooks
