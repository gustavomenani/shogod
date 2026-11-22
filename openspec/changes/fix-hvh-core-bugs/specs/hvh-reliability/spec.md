## ADDED Requirements

### Requirement: Reliable BunnyHop, Edge Jump Arbitration, and Sector-Matched AutoStrafe
The movement subsystem SHALL latch `BunnyHop` per `cmd->command_number`, skip `edge_jump` while `bhop` is active with `jump_held`, steer `AutoStrafe` toward the AirAccelerate optimum (`k_air_dot_target = 0.0f` above `24.4 u/s`) in 45-degree sector-matched space after `AntiAim` (`<= 22.5 deg` tilt), and advance `auto_pistol` and `peek_assist` timers once per `command_number`.

#### Scenario: BunnyHopping while strafing with A or D and Anti-Aim active
- **WHEN** `Bunny hop`, `Auto strafe`, and `Anti-Aim` are enabled and the user holds Space + `A` or `D` across 300 Hz render sub-frames
- **THEN** `step_bhop` presses `k_in_jump` at `when = 0.0f` on landing and releases 1 tick on ground to re-arm without `edge_jump` conflict, `compute_sector_matched_autostrafe` latches `strafe_sign` per `command_number` with zero model spin, and `clear_subtick_view_deltas` plus `strip_subtick_buttons` synchronize subtick state.

#### Scenario: Fire-tick movement re-solve and command-number-latched timers
- **WHEN** `will_fire`, `on_shot`, `auto_pistol`, or `peek_assist` runs across 300 Hz render sub-frames
- **THEN** `resolve_fire_tick_movement` preserves `strafe_out` in air or solves `compute_quantized_ground_move` on ground via `rotate_move_inputs` and `sync_move_held_buttons`, while `step_auto_pistol` and `step_peek_assist_stall` step strictly once per `command_number`.

### Requirement: Anti-Aim Persistence, Strict Independence, Quantized Ground Move, Auto Adjust, and Freestanding
The `Anti-Aim` system SHALL be gated by `bound_feature::anti_aim`, persist across `k_in_attack`, suppress when `!on_move` and `speed_2d > 5.0f || has_move_input`, keep `at_targets`, `base_yaw` (`off` without hidden `+180`), `auto_adjust`, `freestanding`, and `yaw_offset` strictly independent, and solve ground movement via `compute_quantized_ground_move` preserving arbitrary `aa_angles.yaw`.

#### Scenario: Enemy centering, Mouse 1 held, and straight WASD ground movement
- **WHEN** `bound_feature::anti_aim` is active with arbitrary `aa_angles.yaw`, `At targets`, `Auto adjust`, or `Freestanding`
- **THEN** `apply_antiaim_full` seeds `jitter`/`random` deterministically on `cmd->command_number`, applies `pose_head_yaw_offset` across modes, traces `freestanding_head_probe` from `freestanding_trace_target` without enemy-pawn self-hits, and `compute_quantized_ground_move` emits exact `{-1, 0, +1}` inputs with zero lateral drift.

### Requirement: Clean Integer Sliders, HP+ Min Damage, and Active Misc/Events Wiring
The UI SHALL snap integer sliders to exact integer steps (`1, 2, 3...`), display `Min damage` `1..100` and `HP+1..HP+20` (`101..120`), wire `Slide walk` (`leg_movement`), `Anti-Aim`, and `Peek assist` binds, balance `RageBot` cards without vertical clipping, validate `IGameEventManager2` dynamically within `client.dll` bounds, and render the posed CT SAS 3D Preview with M4A1-S.

#### Scenario: HP+ Min Damage, balanced UI cards, Slide Walk, and 3D Preview
- **WHEN** `Min damage` is set above `100` (`101..120`), `Slide walk` is enabled while walking, and the `Visuals` tab is open
- **THEN** the slider pill renders `HP+1..HP+20`, `RageBot` renders all cards without vertical clipping at `1000x650`, `Slide walk` alternates move bits without hurting speed, and `Preview` renders the posed operator holding the M4A1-S with live Chams/glow and ESP toggles.

### Requirement: Ragebot Hitgroup Isolation, Lagcomp, and Fire-Gate Reliability
The Ragebot and Lag Compensation subsystems SHALL honor `prefer_body_aim` with post-`eval_hc` early breaks, isolate `arms` (`5`) from `pelvis` (`3`) and honor `ignore_limbs`, detect on-shot `lagcomp` records via `last_shot_time` retaining up to 3 distinct records `[newest, shooting, oldest]` within `sv_maxunlag`, deduplicate 300 Hz `backtrack_history` pushes with `!in_game` reset, and perform exact OBB `box` intersection plus atomic `hitboxes.cpp` Fallback #2.

#### Scenario: Low-hitchance lethal head falls back to qualified body shot with on-shot lagcomp
- **WHEN** a lethal head has spread hitchance below `req_hc`, a body hitbox satisfies `min_damage` and `req_hc`, or an enemy shot is recorded via `last_shot_time`
- **THEN** `createmove` evaluates `chance_multi` with isolated torso groups, `safe_point`, and `strict_hitchance`, selects the qualified body hitbox without discarding the middle `shooting` lagcomp record, and guards `RepeatedPtrField` capacity/`nullptr` and `0x60u` `has_bits`.

### Requirement: Extrapolation Alignment, AutoStop Firing Hold, and Weapon Fire-Gate Hygiene
The Ragebot execution pipeline SHALL extrapolate strictly on lag gaps (`tick_gap >= 2`) with unshifted `render_tick_count`, hold `AutoStop` on firing ticks honoring `auto_stop_speed`/`auto_stop_ticks`/`auto_stop_wait` and `is_between_shots_window`, honor `remove_recoil`, check `cs_pawn_resume_zoom` and unscoped hitchance in `auto_scope`, clear `k_in_attack2` on primary `auto_revolver` shots, and advance `cycle_cursor` only when firing.

#### Scenario: AutoStop wait, AutoScope resume zoom, Auto-Revolver primary fire, and scan caching
- **WHEN** `auto_stop_wait` is enabled on Scout/AWP, `auto_revolver` fires primary, or `run_createmove` repeats on the same `command_number`
- **THEN** fire is held until `speed_2d <= auto_stop_speed` while keeping braking active on the firing tick, `auto_scope` respects `resume_zoom` and unscoped `hc >= req_hc`, `k_in_attack2` and `attack2_start_history_index` clear cleanly on `will_fire`, `active_wep.range`/`imp.hitgroup`/helmet/shotgun/Zeus rules apply, and `rage_scan_cache` avoids redundant full scans.
