# Design — in-game tick pipeline

## Context

`client.dll` build `sha1 4E16EA32…` (see `docs/RE/findings.md`). The cheat resolves
four anchors by pattern at load, hooks `Present` through a private swapchain vtable
copy and hooks `CCSGOInput` virtually.

Evidence gathered in a live offline match (`-insecure`, dust2, bots):

| Probe | Result |
|---|---|
| `client.dll+0xb781e0` (`vtable[7]`) | 0 hits / 7 s |
| `client.dll+0xb782c0` (dispatch thunk to `[input]+0x38`) | 0 hits / 7 s |
| `client.dll+0xb723c0` (command builder) | ~100 hits / s |
| `client.dll+0xcfd7c0` (`vtable[4]`) | per tick; `count 0 → 1`, angles valid |
| matrix `0x255ce50` | live `worldToProjection`; rows 0/1 = clip xy, row 3 = w |

Backtrace of the builder: `vtable[4] → 0xb723c0`. So `vtable[4]` is the per-tick
input update that produces the `CUserCmd`; `vtable[7]` is only reached in the
front-end.

## Goals / Non-Goals

**Goals**
- Features act on the command the server will receive, in match, every tick.
- Snapshot publishing stays lock-free for `Present`.
- Failures are visible in the log instead of silent.

**Non-Goals**
- Trace/`TraceShape`, wallbang, chams (separate changes).
- Deriving a second input instance; the static `CCSGOInput` is the live one.

## Decisions

1. **Hook `vtable[4]` post-original.** Call the trampoline result first, then run
   `collect_frame` + `run_createmove` + `publish`. Modifying the command after the
   engine built it means the ragebot/antiaim writes survive into the tick.
2. **Keep the hook virtual, not inline.** The vtable is private to the class
   (`0x1c97330`); patching slot 4 avoids code-byte relocation and is reversible on
   unload.
3. **Bone count at `+0x94`.** Dump of `m_modelState` shows `+0x80` heap pointer to
   `{pos,scale,quat}` stride `0x20`, `+0x90 = 0x6f` (capacity 111),
   `+0x94 = 0x5e` (count 94). `+0x88` is a second array pointer (transforms).
4. **Diagnostics stay throttled to 2 s.** They are part of the delivery until the
   remaining features (trace, chams) land.

## Risks / Trade-offs

- `vtable[4]` runs more than once per tick in some frames; the pipeline is
  idempotent per frame (snapshot rewrite + command rewrite), so repeated calls are
  harmless but waste cycles. If measured overhead matters, gate on the command
  number.
- The bone layout is build-specific. A `client.dll` patch can move `+0x90/+0x94`;
  the load-time anchor check does not cover bones, so a stale offset degrades to
  `bones=0` and is reported in the log.

## Migration Plan

1. Ship the hook change and bone offset fix.
2. Verify in a live bot match: `players>0`, `bones=32`, `boxes>0`.
3. Re-verify ragebot writes (`exit != entry`) with `auto_fire` enabled.
4. Update `docs/PLAN.md` anchors table and `docs/RE/findings.md`.

## Open Questions

- Does `vtable[4]` run before or after prediction in every frame? Post-original
  writes demonstrably change the command; if a later stage overwrites angles, gate
  on the command number and write again.
