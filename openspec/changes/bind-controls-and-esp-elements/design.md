## Decisions
Use one foreground input sample per render frame. Suppress new bind transitions while unfocused or while the menu is open. Keep toggle state separate from persisted settings and require a release when focus returns with the key held.

Use original vector silhouettes keyed by weapon family, with distinct shapes for common weapon names. Missing ammo or weapon data does not fabricate a displayed value. Clamp the health bar to the drawable viewport and give elements independent toggles.

## Verification
Exercise state transitions, migration of existing profiles, and draw-list submission for standalone weapon elements. Live ESP rendering remains an offline acceptance check.

The renderer publishes each config and active bind mask together for game-thread consumers. The menu key is reserved and keyboard-only. Unbound features use their existing enable settings.
