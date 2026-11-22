## Why
Feature keybinds are stored but have no shared runtime state. ESP weapon ammo depends on name rendering, and narrow health bars can be clipped at the viewport edge.

## What Changes
- Wire hold and toggle binds through one state machine.
- Give weapon icon, name, and ammo separate selections.
- Keep health bars on screen and count the drawn ESP elements.

## Capabilities
### New Capabilities
- `feature-binds-and-esp-elements`: Independent gameplay binds and overlay elements.

### Modified Capabilities
None.

## Impact
Input, settings, config persistence, ESP collection and rendering.
