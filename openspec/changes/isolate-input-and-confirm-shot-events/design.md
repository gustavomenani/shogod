## Approach
Keep native cursor capture on the window thread. Put only copied input values in a bounded queue. The render thread owns every ImGui event and context access. A menu close, focus loss, or queue overflow clears queued state.

Acquire a process-lifetime module pin before callbacks are published. Save the default profile on menu close rather than calling hooks, filesystem code, and waits under the loader lock during process termination.

Use a build-validated event manager and compare an event's shooter or attacker controller with the current local controller. A local weapon_fire event means a shot occurred. A local-attacker player_hurt event supplies observed health damage. Neither event by itself attributes a hit to a particular automated command if manual fire was also possible.

See tasks for live event registration, target collision, and penetration verification.


## Build evidence and limits
- The screenshot at tick 2988 and the running session log show the third-person camera and ESP rendering. Nearby log lines show attack writes and a subsequent weapon cooldown advance. They do not establish damage for that shot.
- REA evidence `ev_382caf3067e99ef122406c276978b1fc87f93cff2f41049091d830757213a6b8` confirms the event manager cell at client.dll RVA `0x221ab70`. The manager registration entry is `0x9c5360`. The event controller getter reads the hash key by address. The runtime compares the expected RTTI vtables and callback slots before invoking them.
- Direct-hit estimates use range falloff, hitgroup, and available armor and helmet data. They still approximate server-side damage rules.
- The provided Reverie source identifies the filter, trace setup, player clipping, segment sorting and rebuild, bullet simulation, and impact decoding routines. Their signatures each match once in the installed client with SHA-1 `4E16EA32EA9485A25A67933ACD5A8B4870D855D1`. The constructor at RVA `0x87b110` and destructor at `0x87be80` establish vector storage and cleanup. REA evidence `ev_c76e2b3062f78c1cb6e14f20f4768fee3a394142871f6835fada35c5e0026ef4` shows the constructor and simulation at RVA `0x881a20`, including the write to each impact's residual damage field. The decoder at RVA `0x881b40` writes the entity pointer and hitbox pointer. The hitgroup offset and filter values are from the provided reference. The runtime checks entry bytes and requires an impact on the selected pawn after a penetration segment. Hitgroup and armor scaling are applied to this residual damage before comparing Min Damage. Live material and server damage comparison remains pending.
- The hitchance calculation uses 256 spread seeds. All 1,024 unit draws match the `CUniformRandomStreamImpl< CThreadNullMutex >::RandomFloat` export of tier0.dll with SHA-256 `0BCD160DFA72D9F97FBF85095D59BAAD18FCA61F20D840E83A5BD9F7635D5C32`. Target volumes are spheres around the sampled bones. For wallbangs, hitchance measures target intersection while damage comes from the center ray.
- The camera cache stores traced clearance. The render callback applies recent clearance along the current view ray with a shorter distance when the eye position or view has moved. Native physics stays on the game thread.
- The separate Release x64 build passed 52 tests. Loader --unload was exercised and exited without touching a running process. Native event subscription, damage attribution, and menu focus transitions need an offline pass with this build.
