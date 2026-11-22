## 1. Fundação SDK, Protobuf leve e Anchors dinâmicos
- [x] Implementar `src/sdk/usercmd.hpp` e `src/sdk/usercmd.cpp` (`CUserCommand`, `CInButtonState`, `CUserCommandManager`, `CSGOUserCmdPB`, `CBaseUserCmdPB`, `CSubtickMoveStepPB`, `CSGOInputHistoryEntryPB`, helpers de subtick e botões).
- [x] Atualizar `src/sdk/signatures.hpp/.cpp`, `src/sdk/schema.hpp/.cpp` e `src/sdk/trace.hpp/.cpp` removendo a trava fixa `0x298d000` e resolvendo interfaces (`MaterialSystem`, `SceneSystem`, `Cvar`, `EngineClient`, `Source2Client`, `GlobalVars`, `Physics`, `KV3`) e novos netvars.
- [x] Implementar `src/sdk/hitboxes.hpp/.cpp` e `src/features/math/hitbox_geom.hpp/.cpp` com geometria real de cápsula/OBB (`CHitbox`), leitura de `CModel` / `CHitboxTransformHistory` e geração de multi-points testável em `shogod_core`.

## 2. CreateMove, Movement Fix, AntiAim e Predição de Acurácia
- [x] Migrar hook para `CCSGOInput::vtable[5]` (`CreateMove`) + `SendMove` (`vtable[8]`) preservando `current_move_data.viewangles`.
- [x] Implementar `rotate_movement` (movement fix), bunny hop, auto strafe direcional, edge jump, leg movement (slide walk) e peek assist em `src/features/createmove.cpp`.
- [x] Reativar `AntiAim` em `CreateMove` e desbloquear controles em `src/ui/pages/page_antiaim.cpp`.

## 3. LagCompensation, AutoWall e Ragebot completo
- [x] Implementar `src/features/lagcomp.hpp/.cpp` (`lag_compensation` + `hitbox_transform_scope`).
- [x] Implementar autowall / trace de penetração em `src/sdk/trace.hpp/.cpp` (`trace_bullet_ex`, `trace_wall_damage`) e escala de dano em `src/features/createmove.cpp`.
- [x] Reescrever o pipeline do ragebot em `src/features/createmove.cpp` (multi-point real, `weapon_spread_pattern::chance_multi`, auto-stop, auto-scope, auto-revolver, `compute_aim_punch`, silent aim, `input_history[0]` + `subtick_moves`).

## 4. Chams in-game (ColoredModels), World e Visuals
- [x] Implementar `src/features/chams/chams.hpp/.cpp` (materiais VMAT `Shaded`/`Flat`/`Glow`/`Glossy`, `Ignore-Z` depth override + PVS, enemy/team/local/arms/weapon + scope blend).
- [x] Implementar `src/features/world/world.hpp/.cpp` (modulação de mundo/luz/exposição, view punch, no-smoke/no-scope/no-flash, penetration crosshair).

## 5. Config, UI, Build e Testes
- [x] Atualizar `src/config/config.hpp/.cpp`, `src/ui/pages/page_rage.cpp`, `src/ui/pages/page_visuals.cpp`, `src/ui/pages/page_misc.cpp`, `src/engine/hooks.cpp` e `CMakeLists.txt`.
- [x] Expandir `tests/test_features.cpp` e validar build Release x64 + execução completa de `shogod_tests.exe` (55/55 OK; runtime live no CS2 não executado nesta sessão).
