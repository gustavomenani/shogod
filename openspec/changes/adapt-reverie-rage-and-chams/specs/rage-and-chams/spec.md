## ADDED Requirements

### Requirement: Subtick CreateMove and Isolated Camera
The system SHALL hook `CCSGOInput::CreateMove` (`vtable[5]`) and `SendMove` so command angles, `input_history`, `subtick_moves`, and `CInButtonState` masks are written atomically without mutating the local camera when Silent Aim or Anti-Aim is active.

#### Scenario: Silent aim shot isolates camera
- **WHEN** the ragebot fires with `silent_aim` enabled
- **THEN** `input_history[0].view_angles`, `attack1_start_history_index`, and `base_cmd.viewangles` SHALL target the enemy while `SendMove` restores `current_move_data.viewangles`.

### Requirement: Real Hitbox Scan, Lag Compensation, AutoWall, and Recoil Compensation
The ragebot SHALL scan real `CHitbox` capsule/box geometry across valid `LagCompensation` records, evaluate penetration via `AutoWall`, verify multi-hitbox `Hitchance`, and subtract `ComputeAimPunch` when recoil removal is enabled.

#### Scenario: Multi-point and penetration scan
- **WHEN** the center of an enabled hitbox is occluded or fails minimum damage
- **THEN** the scanner SHALL evaluate capsule/box multi-points and penetrated wallbang damage before discarding the target.

#### Scenario: Full-auto and revolver support
- **WHEN** an automatic rifle or `WEAPON_REVOLVER` is active and ready to fire
- **THEN** full-auto weapons SHALL fire on consecutive ready ticks without artificial 1-tick release lockouts, and `WEAPON_REVOLVER` SHALL handle cock/ready timing.

### Requirement: In-Game Colored Models (Chams) and World Visuals
The rendering subsystem SHALL hook `SceneSystem` and `RenderSystemDX11` to apply custom `CMaterial2` passes (`Shaded`, `Flat`, `Glow`, `Glossy`) with `Ignore-Z` depth inversion and PVS bypass for Enemy, Friendly, Local, Arms, and Weapon categories, alongside world modulation and removals.

#### Scenario: Enemy occluded chams
- **WHEN** `enemy_occluded` chams are enabled
- **THEN** occluded enemy PVS clusters SHALL bypass culling and render the occluded pass with inverted depth comparison before the visible pass.
