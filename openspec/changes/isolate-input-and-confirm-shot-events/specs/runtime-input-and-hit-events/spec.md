## ADDED Requirements

### Requirement: Only the renderer feeds ImGui
WndProc SHALL enqueue copied input values and SHALL keep native mouse capture on its owner thread.

#### Scenario: Focus is lost before rendering the next frame
- **WHEN** the menu loses focus
- **THEN** the renderer clears queued input and resets pressed keys and buttons.

### Requirement: Distinguish requests, shots and damage
The runtime SHALL report observed local shot and damage events separately from input writes.

#### Scenario: A shot misses
- **WHEN** the local player receives a weapon-fire event and no corresponding damage has been observed
- **THEN** the log reports the weapon-fire event without claiming a hit.
