## Why
The runtime has failure paths that can misclassify players, leak resources, or corrupt profile files. The existing tests primarily exercise pure functions.

## What Changes
- Gate native calls on verified identities and stop treating trace failures as successful traces.
- Recover from DX11 initialization or device loss without carrying stale resources.
- Preserve nested profile fields and atomically replace saved files.
- Reject historical poses belonging to a different entity in the same slot.

## Capabilities
### New Capabilities
- `runtime-failure-safety`: Fail-closed queries, recoverable rendering, and durable profile writes.

### Modified Capabilities
None.

## Impact
SDK trace initialization, visual rendering, pose history, and configuration persistence.
