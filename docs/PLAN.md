# shogod — Status da Arquitetura e Plano de Engenharia

## 1. Status Atual dos Subsistemas

Todos os subsistemas principais estão implementados, verificados contra o binário atual do CS2 (`client.dll` / `scenesystem.dll`) e cobertos pela suíte `shogod_tests.exe`:

| Subsistema | Status | Detalhes Técnicos |
|---|---|---|
| **Pattern Scanner & SchemaSystem** | Completo (4/4 anchors) | `dwEntityList`, `dwViewMatrix`, `dwLocalPlayerController` e `dwCSGOInput` resolvidos dinamicamente + netvars via `SchemaSystem_001`. |
| **CUserCommand & Subtick Protobuf** | Completo | Manipulação zero-dependency de `CUserCmd` (`0x98`), `CBaseUserCmdPB`, `CSGOInputHistoryEntryPB`, `CSubtickMoveStep` e `CInButtonState`. |
| **CreateMove & Isolamento de Câmera** | Completo | Hooks em `CCSGOInput::vtable[4]` e `SendMove` (`vtable[8]`), com `tick_input_latch` por `command_number` (~300 Hz sub-frame-safe). |
| **RageBot, AutoWall & LagComp** | Completo | 19 hitboxes reais via `CModel`, multipoint, hitchance analítico por cone de spread, AutoWall multi-superfície, avaliação lazy de hitchance por slot e `LagCompensation` com extrapolação condicional. |
| **Anti-Aim & Movement Fix** | Completo | Pitch (`Down`/`Up`/`Zero`), Yaw (`Backward`/`Jitter`/`Random`), `At-Targets`, `Freestanding` com histerese, `Auto-Adjust` terrestre e difusão de erro 2D (`ΣΔ`) para movimento terrestre quantizado. |
| **BunnyHop & Auto-Strafe Aéreo** | Completo | `step_bhop` idempotente a sub-frames de predição, `compute_runtime_air_strafe` alinhado à velocidade sem snaps de `45°`, estabilização de lóbulo (`4` ticks) + guiagem por mouse e limpeza de `toggle`/`scroll` em `CInButtonState`. |
| **Visuals, Chams & World Modulation** | Completo | ESP completo (Box, HP, Armor, Skeleton 32B, Weapon Icons, Offscreen, Sound ESP, Backtrack Ghost, Penetration Crosshair), `SceneSystem` Chams (Enemy/Team/Local/Arms/Weapon + XQZ) e World/Light Modulation. |
| **UI ImGui (6 Abas) & Configs** | Completo | `RageBot`, `Anti-Aim`, `Visuals`, `Misc`, `Configs`, `Settings` com overrides por arma e persistência JSON em `%APPDATA%\shogod\configs\`. |

---

## 2. Histórico de Mudanças OpenSpec (`openspec/changes/`)

1. **`fix-hvh-core-bugs`**:
   - Correção de reutilização de `CSubtickMoveStep` (preservação do pool de `RepeatedPtrField` e limpeza de `has_bits` `0x60`), ordem estrita fraca de seleção de hitbox com avaliação lazy de hitchance, extrapolação apenas em lag real (`tick_gap >= 2`), gating de `AutoStop` e isolamento de `Anti-Aim`.
2. **`fix-air-movement-and-bhop`**:
   - Correção do auto-cancelamento de BunnyHop a 300 Hz após Client Prediction no Sub-frame 0 (`step_bhop` + `tick_input_latch`), limpeza de bits de transição `toggle`/`scroll` em `sync_move_held_buttons`, eliminação de snaps de `45°` e vibração de 64 Hz no Auto-Strafe aéreo (`compute_runtime_air_strafe`) e supressão de pré-frenagem `AutoStop` durante BunnyHop não qualificado para tiro.

---

## 3. Fluxo para Novas Evoluções

1. Criar mudança em `openspec/changes/<change-id>/` (`proposal.md`, `design.md`, `specs/`, `tasks.md`).
2. Implementar alterações mantendo funções matemáticas e de decisão testáveis nos headers/módulos de `shogod_core`.
3. Adicionar testes unitários em `tests/test_features.cpp` e cenários E2E em `tests/test_e2e_hvh.cpp`.
4. Validar build `Release` e execução 100% verde de `shogod_tests.exe` antes do commit.
