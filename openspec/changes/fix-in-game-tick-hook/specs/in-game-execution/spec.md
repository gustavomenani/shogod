## ADDED Requirements

### Requirement: Per-tick feature execution
The cheat SHALL run its feature pipeline from the game's per-tick input update
(`CCSGOInput::vtable[4]`), after the original has built the newest `CUserCmd`, so
that feature writes land in the command the tick consumes.

#### Scenario: Live match
- **WHEN** the player is in a live match and the cheat is loaded
- **THEN** the tick hook MUST be invoked continuously and report `count > 0`
- **AND** the frame snapshot MUST publish a non-zero player count

#### Scenario: Front-end
- **WHEN** the game is in the main menu
- **THEN** the tick pipeline MUST NOT be required to run and MUST NOT crash the game

### Requirement: Player snapshot completeness
The snapshot SHALL carry world-space bone transforms for every alive pawn, read
from `m_modelState+0x80` with the count at `+0x94` and stride `0x20`.

#### Scenario: Pawn with a skeleton
- **WHEN** a pawn's scene node resolves and its bone array is readable
- **THEN** the snapshot MUST expose up to 32 bone positions
- **AND** the ESP skeleton MUST connect only verified CS2 bone indices

### Requirement: Line-of-sight gating
The ragebot MUST only consider targets whose ray from the local eye to the aim
point is unobstructed, using the game's physics query.

#### Scenario: Enemy behind geometry
- **WHEN** every enemy candidate is occluded by map geometry
- **THEN** the ragebot MUST NOT pick a target
- **AND** it MUST NOT apply auto-stop, crouch, scope or attack state

#### Scenario: Clear line of sight
- **WHEN** a candidate's ray reaches the aim point without hitting geometry
- **THEN** the candidate MAY be selected and fired at, subject to hitchance and damage gates

### Requirement: Authoritative input masks
Feature code SHALL write the held button mask to `CUserCmd+0x00` and the pressed
mask to `+0x08`, matching the engine's own builder.

#### Scenario: Bunny hop
- **WHEN** bunny hop is enabled and the jump button is held while airborne
- **THEN** the held mask in the built command MUST have the jump bit cleared
