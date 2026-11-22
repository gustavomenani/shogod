## ADDED Requirements

### Requirement: Predictable keybinds
Feature binds SHALL provide hold and toggle modes that respect focus and menu capture.

#### Scenario: Focus returns with the key held
- **WHEN** the game regains input focus while a bind key is already down
- **THEN** the bind remains unchanged until a separate press.

### Requirement: Independent ESP elements
Weapon icon, weapon name, and ammo SHALL be independently selectable.

#### Scenario: Ammo only
- **WHEN** only ammo is enabled and the snapshot has a valid ammo value
- **THEN** the draw list contains the ammo label.
