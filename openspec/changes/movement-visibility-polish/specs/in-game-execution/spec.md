## ADDED Requirements

### Requirement: Shared line-of-sight flag
Line of sight SHALL be resolved once per frame on the game thread and published
on the frame snapshot, so every consumer reads the same visibility state.

#### Scenario: ESP and ragebot agree
- **WHEN** an enemy is occluded from the local eye
- **THEN** the snapshot marks that player not visible
- **AND** the ragebot rejects the candidate and the ESP dims or hides it

### Requirement: Backtrack targets the oldest pose in the window
Backtrack SHALL select the oldest recorded pose that still falls inside the
configured window, not the newest.

#### Scenario: Three poses inside the window
- **WHEN** poses at t, t+100ms and t+200ms exist and the window is 200ms
- **THEN** the target pose is the oldest one still inside the window

### Requirement: Movement assists are explicit
Edge jump SHALL only press jump when the ground probe 24 units ahead reports no
floor, and only while the option is enabled.

#### Scenario: Running off a ledge
- **WHEN** edge jump is enabled, the player is grounded and moving, and the probe
  ahead finds no floor
- **THEN** the command carries the jump bit

### Requirement: Apply Min Damage to penetration shots
The ragebot SHALL require a native simulated impact on the selected pawn after
penetration. It SHALL compare positive residual damage, scaled by hitgroup and
armor, with the configured Min Damage.

#### Scenario: Damage meets the threshold
- **WHEN** a simulated impact after penetration meets Min Damage
- **THEN** the candidate is eligible if the remaining firing conditions pass

#### Scenario: Damage falls short
- **WHEN** the native query cannot confirm an impact or the damage is below Min Damage
- **THEN** the ragebot withholds the automated shot
