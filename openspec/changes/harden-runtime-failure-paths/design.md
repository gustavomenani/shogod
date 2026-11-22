## Approach
The audit follows loader and runtime startup, input dispatch, SDK reads, fire decisions, rendering, binds, profile storage, and shutdown. It assumes one game process and one menu. Third-party library internals and unexecuted runtime behavior are outside the code-review evidence.

Use exact native entry prefixes for the installed client, preserve visibility results when queries fail, and require a valid result for edge jump. Stage profile output in the destination directory before replacing the old file. Keep history keyed to the pawn handle rather than the reusable controller slot alone.

## Remaining audit findings
- Unpinned unload has no proof that rendering or input callbacks have returned. The pinned path requires a process restart. A robust unload handshake is open.
- WndProc feeds the same ImGui context used by Present. A cross-thread event queue is needed if they run on different threads.
- Wall depth is a heuristic and minimum damage still uses base damage. The shot write and local cooldown do not confirm server hit registration.
- Silent Aim, isolated anti-aim, live chams, third person model visibility, and the soak test still require build-specific runtime evidence.


## Verified during the audit

| Area | Result |
| --- | --- |
| SDK and input | REA evidence `ev_d57769ffc3d6861a29cbac0a07150390413b9532225c2045982c22071066c023` confirms the native prefixes for input update RVA `0xcfd7c0`, shape initialization RVA `0x203570`, and trace RVA `0xa15360`. The resolver now gates the actual slot used by the input hook. |
| Line of sight | A failed or out-of-range trace no longer implies clear space. Requiring a clear ray removes the previous 2 percent distance-dependent cover tolerance. Updates and camera writes require an alive local pawn. |
| Loader | A timed-out load leaves the remote path alive because thread ownership is unresolved. Exit checks no longer label the 32-bit return value as a full module address. |
| Profiles | Nested keys and newer version numbers survive known setting changes. Writes flush a sibling temporary file before replacing the destination. The suite tests both a nested profile round trip and successful replacement. |
| ESP history | Slot 64 works. The renderer rejects history for any pawn handle other than the one currently in the frame. |
| DX11 | Both backend failure paths destroy the partial ImGui context. A lost device tears down the backend and a removed device is rejected during restart. Swapchain replacement and threaded WndProc behavior require live verification. |
| UI and binds | Existing tests cover focus-loss and rendering independent health, armor, icon, and ammo fields. They do not drive the native WndProc or game swapchain. |
| Build and tests | Release x64 compiled. All 51 tests passed. The current DLL was not run in a live match during this audit. |
