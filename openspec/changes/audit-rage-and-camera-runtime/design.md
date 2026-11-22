## Evidence
Installed client.dll SHA-256 is `9e0fd6aa19a8cb491c45ae5e9ce3d13716892a42c578e0c4911cf9c3ffaa67ee`.

- REA `ev_9382de91ea0bd8b3dac04f3cfd60b5259bc533ee758ecaa6759285c359a94f1a`: SetupView writes origin XY at `+0x4a0`, origin Z at `+0x4a8`, angles XY at `+0x4b8`, and angle roll at `+0x4c0`. The two old vector bases pointed at their last components.
- REA `ev_f077a93826465edec2ba687f9517dd652b869247bc8f5a746a68852bcfa28b4e`: movement inputs at CUserCmd offsets `+0x18` and `+0x1c` are individually clamped by the builder to `[-1,1]`.
- Runtime log at 20:59 records the visual hooks installed. At 21:00, the rifle produced several attack writes and its next-primary tick advanced. Other exposed shots fell below the configured hitchance with the old maximum-cone model.
- Reverie's SpreadCache is a reference for sampling separate radial components. Its RNG stream and hitbox layout remain independent of this implementation.

## Decisions
Use each validated movement component unchanged. Expose threshold, braking budget, and strict wait in each weapon profile. Preserve legacy defaults. Use a deterministic approximation of separate radial samples when both accuracy reads succeed.

Log changes to weapon cooldown separately from button writes. The camera reads the corrected vectors. If rendering runs off the input thread, queue a trace and accept only a recent matching result. Unsupported or stale views retain the engine camera.

## Remaining evidence
Live damage and hit confirmation, exact hitboxes and shot RNG, material penetration, scope and smoke behavior, third-person local model, Silent Aim serialized commands, and chams material lifetime need separate confirmation.

## Audit ledger

| Path | Evidence | Remaining proof |
| --- | --- | --- |
| Input builder and movement | REA confirms forward/side float offsets and normalized range. Previous logs show slot 4 runs in a match. | Measure a moving player's speed after the new braking commands. |
| Shot selection | Logs show visible candidates rejected by modeled hitchance and separate accepted weapon cooldown changes after some requests. | Correlate attack requests with local shot events and hit confirmation. |
| Live weapon accuracy | Existing log records live getters resolving and returning plausible values. The new sampler uses independent radial offsets. | Validate native RNG and exact hitbox geometry by recording real shot distributions. |
| Camera | REA identifies the two vector bases. The previous startup log confirms vtable hook installation. | Observe the corrected callback placing the view behind the player and clearing walls. Verify the local model. |
| ESP and removals | ESP projection and config behavior are covered by the existing suite. Smoke and scope callbacks resolved in the previous run. | Verify visibility and draw results in CS2. |
| Serialized fire, anti-aim, and chams | Reverie shows separate command history and render material paths. | Resolve the live shot command, camera separation, and per-pass material ownership before enabling. |
