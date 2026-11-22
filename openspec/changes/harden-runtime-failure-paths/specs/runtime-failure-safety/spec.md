## ADDED Requirements

### Requirement: Unknown traces remain unknown
Callers SHALL require a successful geometry query before making trace-dependent decisions.

#### Scenario: The physics context is absent
- **WHEN** the trace call cannot run
- **THEN** it does not imply clear geometry and edge jump does not fire.

### Requirement: Preserve profile data
Saving SHALL stage a complete file before replacing the destination and preserve unknown nested keys.

#### Scenario: The staging write fails
- **WHEN** a complete replacement cannot be written
- **THEN** the existing profile remains accessible.

### Requirement: Pose identity survives slot changes
Pose history SHALL only render records for the current pawn occupying the tracked slot.

#### Scenario: The pawn handle changes
- **WHEN** a player slot is reused
- **THEN** a history lookup does not return a pose from the previous pawn.
