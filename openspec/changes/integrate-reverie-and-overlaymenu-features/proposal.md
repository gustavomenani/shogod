# Integrate Reverie & OverlayMenu Features

## Summary
Adapted and integrated key capabilities from Reverie and OverlayMenu (Neverlose) into Shogod:
1. **Subtick BunnyHop & Jitter-Free Auto-Strafe**: `build_subtick_autostrafe_plan`, `compute_next_jump_frac`, `strip_subtick_analog_moves`, CS2 `CModernJump` / `sv_*` CVar integration (`src/sdk/cvar.hpp`), locking `base_cmd->viewangles->y` with zero yaw jitter.
2. **Interactive 3D SAS Preview + "Manage elements" Drawer & Synced ESP Layout**: shared `esp_layout::compute_layout` / `build_layout_with_ghost` (`src/features/esp/esp_layout.hpp`), hover/drag/reorder + right-click settings popup + animated bottom drawer (`src/ui/preview/preview.*`), mirrored 1:1 in `src/features/esp/esp.cpp`.
3. **Hitmarker & Dual-Backend Hitsounds**: crosshair fading hitmarker + WinMM 44.1 kHz embedded WAV PCMs (OverlayMenu) + CS2 `PlayVsnd` (`soundsystem.dll`, Reverie), per-situation Body/Head Hit/Kill config & preview (`src/features/audio/hitsounds.*`, `src/sdk/shot_events.*`, `src/ui/pages/page_misc.cpp`).
4. **Live Spectator List HUD**: observer target collection (`src/sdk/entities.cpp`, `src/sdk/snapshot.hpp`) and Neverlose-style Spectator window stacked cleanly above Keybinds (`src/features/esp/esp.cpp`).
