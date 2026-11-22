# Fix in-game tick pipeline for ESP and ragebot

## Why

In a live match no feature ran even though every anchor resolved and the unit tests
were green. Frida instrumentation on the shipped `client.dll` proved the installed
hook point (`CCSGOInput::vtable[7]`, `0xb781e0`) is never entered during gameplay
(0 hits over 7 s), while `CCSGOInput::vtable[4]` (`0xcfd7c0`) runs every tick and
leaves the new `CUserCmd` in `[input+0xb58]`. Because the frame snapshot was only
filled from the dead hook, `Present` always saw zero players and a zero view
matrix, so ESP drew nothing and the ragebot never received a command.

A second defect surfaced once the pipeline ran: the bone array is at
`m_modelState+0x80` but the bone count lives at `+0x94` (capacity at `+0x90`),
not at `+0x88` as the previous RE notes assumed. Reading `+0x88` returned a
pointer interpreted as a count, so skeletons were empty.

## What Changes

- Hook `CCSGOInput::vtable[4]` instead of `vtable[7]`, and run the feature
  pipeline **after** the original returns, so the ragebot/antiaim own the command
  the game is about to consume.
- Read bone count/capacity from `m_modelState+0x94` / `+0x90`, keeping stride
  `0x20` and the `{pos.xyz, scale, quat}` layout.
- Keep throttled diagnostics: entity collection counters, command angles
  before/after, ragebot candidate decisions and raw matrix dump when unusable.
- Remove the `vtable[7]` hook (dead in match) from install/uninstall.

## Impact

- Affected specs: `in-game-execution`, `frame-snapshot`, `diagnostics`
- Affected code: `src/engine/hooks.cpp`, `src/sdk/entities.cpp/.hpp`,
  `src/features/createmove.cpp`, `docs/PLAN.md`, `docs/RE/findings.md`
- Behaviour: ESP boxes/skeleton/weapon data and ragebot commands act on the live
  tick; menus and injection paths unchanged.
