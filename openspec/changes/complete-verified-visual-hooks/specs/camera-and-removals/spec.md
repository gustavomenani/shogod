## ADDED Requirements

### Requirement: Independently selectable visual controls
The menu SHALL allow third-person, smoke removal, and scope overlay removal to be enabled independently when their hooks are available.

#### Scenario: Enable only smoke removal
- **WHEN** the smoke hook is available and only smoke removal is enabled
- **THEN** the smoke callback suppresses smoke rendering and the original view and scope overlay stay active

#### Scenario: A hook cannot be verified
- **WHEN** a callback slot fails validation or installation
- **THEN** its control is visibly unavailable while the other verified controls remain usable

### Requirement: Preserve input update arguments
The existing detour SHALL pass the native slot, frame time, and active flag to the original input update.

#### Scenario: The game updates input
- **WHEN** the detour is called with a nonzero frame time
- **THEN** the original receives that time in the floating-point argument register and the active flag in the original byte argument

### Requirement: Collision-aware camera
Third person SHALL change the render camera origin only for a valid local view with a successful obstruction trace.

#### Scenario: A wall is behind the player
- **WHEN** third person is enabled and the obstruction trace hits a wall before the requested camera position
- **THEN** the camera stops in front of the hit without changing view angles

#### Scenario: The trace fails
- **WHEN** the obstruction trace cannot run or fails
- **THEN** the camera origin stays at the original position

### Requirement: Keep active callback code mapped
The module SHALL remain mapped until game exit when the visual callbacks are installed and callback quiescence cannot be proven.

#### Scenario: The user requests unload
- **WHEN** the loader requests unload and the DLL remains mapped
- **THEN** the loader reports that it remains loaded
