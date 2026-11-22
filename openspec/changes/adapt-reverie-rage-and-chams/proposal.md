## Why
O ragebot atual do SHOGOD falha ao atirar e calcular dano/visibilidade porque opera sobre `CCSGOInput::vtable[4]` (`ExtendedMoveData` cru) sem `input_history`, sem `subtick_moves`, sem compensação de recoil, sem engine prediction/accuracy penalty, com scan limitado a um bone por grupo usando esferas fixas e com travas rígidas de `k_recorded_image_size = 0x298d000`. Além disso, `player_chams` e `anti_aim` existem apenas no preview/UI sem pipeline in-game.

## What Changes
- Migrar o tick de comando para `CreateMove` (`CCSGOInput::vtable[5]`) + `SendMove` com acesso estruturado a `CUserCommand`, `CInButtonState` e campos protobuf (`CBaseUserCmdPB`, `CSGOInputHistoryEntryPB`, `CSubtickMoveStep`) sem dependência de CRT incompatível.
- Substituir travas fixas de build (`0x298d000`) por resolução dinâmica via patterns validados no binário instalado (`client.dll`, `scenesystem.dll`, `rendersystemdx11.dll`, `materialsystem2.dll`, `particles.dll`, `engine2.dll`).
- Adaptar do `reverie src` os subsistemas de hitbox real (`CHitbox` / `CHitboxTransformHistory`), `LagCompensation`, `AutoWall`, `Ragebot` (multi-point real, hitchance multi-hitbox, auto-stop, auto-scope, auto-revolver, silent aim, remove recoil) e `AntiAim` com movement fix (`Movement::Rotation`).
- Adaptar o pipeline de `ColoredModels` (Chams in-game com materiais `Shaded`, `Flat`, `Glow`, `Glossy`, `Ignore-Z` via override de depth stencil + PVS, overlay e transparência em scope) e modulação/remoções de `World`.
- Atualizar `config_v1`, páginas da UI (`Rage`, `AntiAim`, `Visuals`, `Misc`) e suíte de testes unitários em `tests/`.

## Capabilities
### New Capabilities
- `rage-and-chams`: Pipeline completo de `CreateMove` subtick, `Ragebot` com lagcomp/autowall/hitbox real, `AntiAim` com câmera isolada, `Chams` in-game via `SceneSystem`/`MaterialSystem2` e modulação/remoções de `World`.

### Modified Capabilities
None.

## Impact
`CMakeLists.txt`, `src/config/*`, `src/sdk/*`, `src/engine/hooks.cpp`, `src/features/*`, `src/ui/pages/*`, `tests/*`.
