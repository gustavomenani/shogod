## Why
SHOGOD needs camera, smoke and scope controls. Silent Aim still needs its serialized shot command path verified.

## What Changes
- Add independently selectable third-person, no-smoke, and no-scope controls when their callbacks resolve.
- Preserve the native input update arguments and record the evidence required for Silent Aim and live acceptance.

## Capabilities
### New Capabilities
- `camera-and-removals`: Build-gated third-person camera and smoke and scope rendering controls.

### Modified Capabilities
None.

## Impact
Visual settings, client module resolution, vtable hooks, and loader unload reporting.
