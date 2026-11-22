## Why
The render callbacks install, but third person does not move the view. The ragebot rejects exposed shots under a biased hitchance estimate, and Auto Stop settings are hidden in a multiselect.

## What Changes
- Correct the camera layout using REA disassembly of the installed client.
- Keep camera obstruction traces on the game thread, including split-thread view dispatch.
- Model spread and inaccuracy separately and expose independent Auto Stop controls.
- Record observed, statically verified, and unverified paths.

## Capabilities
### New Capabilities
- `rage-camera-runtime`: Camera validation, shot estimation, and stop controls.

### Modified Capabilities
None.

## Impact
Rage UI/configuration, weapon accuracy SDK, shot estimation, and view callback.
