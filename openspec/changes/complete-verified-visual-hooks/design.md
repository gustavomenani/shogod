## Context
The installed client.dll SHA-1 is `4E16EA32EA9485A25A67933ACD5A8B4870D855D1` and its image size is `0x298d000`. Reverie supplied recognition patterns. The offsets below are RVAs derived from the installed file.

## Goals / Non-Goals
- Goal: Add camera and smoke toggles using checked callback slots.
- Goal: Keep camera wall collision and native input angles independent.
- Non-goal for this implementation: expose controls backed by unverified command or render contracts.

## Decisions
- SetupView is `0xb929b0` and appears in slot 15 of ClientModeShared (`0x1c61cf0`) and ClientModeCSNormal (`0x1c994a8`). The corrected vector bases are origin `+0x4a0` and angles `+0x4b8`. See the REA evidence in `audit-rage-and-camera-runtime/design.md`.
- The scope callback at `0x8dd900` is reached by direct calls at `0xce83da` and `0x11d838c`. It writes visibility at output offset `+4`. Use pinned MinHook v1.3.4 for the direct entry.
- The smoke descriptor callback is `0xd18be0` in CSmokeVolumeSceneObjectDesc slot 1, at `0x1ca1b40`. The original is called while the option is disabled.
- Position the third-person camera using the validated view angles and cap its distance using the existing physics query. If the view callback is on a different thread from input or the trace fails, keep the original camera.
- Input slot 4 at `0xcfd7c0` receives frame time in XMM2 and the active bit in R9B. Preserve both through the existing detour.
- Keep the DLL mapped until game exit when render callbacks are installed, since active and pending dispatch cannot be proven quiescent by restoring the slots.

## Risks / Trade-offs
- Static evidence and unit tests do not prove the in-game callback and rendering behavior. The local model and camera collision must be verified independently.
- Silent Aim needs the live serialized attack history and camera separation verified.


## Remaining evidence
The installed client.dll input vtable slot 5 points to `0xb5ba30`. Reverie uses slot 5 and obtains a separate `0x98` serialized command from the controller's command manager. SHOGOD uses slot 4 and the `0x440` input record. A live capture must correlate the command numbers and identify the attack history entry and the camera angle consumer before changing either. This also leaves camera-isolated anti-aim unverified.

Reverie generates chams at the AnimatableSceneObjectDesc primitive callback and applies per-pass material overrides. The primitive layout, entity ownership, and material lifetime still need verification for the installed build.
