# Movement and visibility polish

## Why

Round one made the tick pipeline run, but three behaviours were still crude:
line of sight was traced inside the ragebot only (the ESP could not use it),
backtrack returned the newest pose inside the window (a no-op, since that is the
current position), and the auto-pistol toggled the trigger every other tick
instead of the weapon's own cadence. Movement had no ledge assist.

## What Changes

- Compute line of sight once per frame in `collect_frame` and publish it on the
  snapshot; the ragebot and the ESP consume the same flag.
- ESP: `visible_only` filter, dimmed boxes for occluded players, off-screen
  arrows with distance for visible enemies.
- Backtrack selects the oldest pose inside the window (the furthest legitimate
  rewind) instead of the newest.
- Auto-pistol holds the trigger one tick and releases for the weapon's
  `m_flCycleTime`, resolved from `CCSWeaponBaseVData`.
- New movement option `edge_jump`: probes the ground 24 units ahead with the
  physics query and jumps when it is missing.

## Impact

- Specs: `in-game-execution`
- Code: `src/sdk/{entities,schema,trace}.*`, `src/features/{createmove,esp,backtrack}.*`,
  `src/config/*`, `src/ui/pages/page_{visuals,misc}.cpp`, `tests/test_features.cpp`
