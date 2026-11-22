## ADDED Requirements

### Requirement: Move the correct camera fields
The view hook SHALL read the audited angle vector and write only the audited position vector.

#### Scenario: The view thread differs from the physics thread
- **WHEN** third person is requested off the input thread
- **THEN** the renderer uses a recent obstruction trace produced by the input thread or preserves the native camera.

### Requirement: Model weapon accuracy components
The fire gate SHALL sample spread and inaccuracy separately when both are available.

#### Scenario: Spread and inaccuracy both contribute
- **WHEN** live weapon accuracy fields are valid
- **THEN** hitchance uses the combined sample offsets produced by the two separate components.

### Requirement: Per-weapon Auto Stop options
A weapon profile SHALL expose Auto Stop, target speed, braking ticks, and strict wait.

#### Scenario: Strict wait is enabled
- **WHEN** the braking budget expires while speed remains above the configured target
- **THEN** the weapon keeps braking and the automatic attack stays gated.
