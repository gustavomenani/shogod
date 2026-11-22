## Why
The window and render threads both touch ImGui, unload can free live code, and the current logs cannot prove whether the local player hit anyone.

## What Changes
- Queue Win32 menu input for the render thread.
- Keep callbacks mapped until process exit and make the loader report that lifecycle.
- Resolve and report local weapon-fire and player-hurt events for the audited build.

## Capabilities
### New Capabilities
- `runtime-input-and-hit-events`: Render-owned UI input and local shot telemetry.

### Modified Capabilities
None.

## Impact
Window messages, render input, DLL lifetime, event SDK, and logs.
