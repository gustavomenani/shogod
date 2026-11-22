## Context

No CS2 (`client.dll`), o pipeline de input executa `CCSGOInput::vtable[4]` (`0xcfd870`) a cada render frame (~275–300 Hz) e `CCSGOInput::SendMove` (`vtable[8]`, `0xce1d90` -> `0xb62640`) apenas na fronteira do tick de rede (64 Hz):

1. Em `CreateMove[5]` (`0xb5baa0`), quando `cmd->next_prediction_reason == 2` (`0xb5bbde`), o jogo reutiliza o mesmo slot `CUserCmd` e o mesmo `cmd->command_number` do sub-frame anterior, reconstruindo `base_cmd` a partir do estado atual de teclado/mouse e rodando `ClientPrediction` (`0xdef610`) ao final do hook.
2. `ClientPrediction` altera `local_pawn->m_fFlags` (`FL_ONGROUND`), `m_vecAbsVelocity` e `m_vOldOrigin` in-place após o Sub-frame 0.
3. Em `SendMove_inner` (`0xb626c9`), o jogo grava `this->prev_held = cmd->button_state.held` (`[this + 0x250]`). No próximo `CreateMove[5]` (`0xb5c5b0`), a tabela de transição `0x2227fb0` compara `prev_held` (modificado pelo movement fix/autostrafe) com `physical_held` (`[this + 0x258]`) e liga bits de transição em `cmd->button_state.toggle` (`buttonstate2`).

## Goals / Non-Goals

**Goals:**
- Garantir idempotência do estado de entrada física (`on_ground`, `velocity`, `jump_held`, `raw_held`, wish move) e da máquina de estados `step_bhop` ao longo de todos os sub-frames (~300 Hz) de um mesmo `command_number`.
- Eliminar flags espúrias de transição (`toggle` e `scroll`) nos botões `IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT` quando `sync_move_held_buttons` reescreve `cmd->button_state`.
- Eliminar snaps de 45 graus e rotações espúrias no ar mantendo `ref_yaw` relativo à câmera sem `auto_adjust` aéreo e estabilizando a troca de lóbulo do `strafe_sign`.
- Impedir que o `AutoStop` trave o BunnyHop quando o jogador mantém `Space` pressionado e o hitchance aéreo ainda não qualifica um disparo.

**Non-Goals:**
- Alterar a assinatura ou o contrato matemático puro de `compute_autostrafe` ou `compute_sector_matched_autostrafe` utilizados pelos testes unitários de física existentes.

## Decisions

### D1: Idempotência por `command_number` em `step_bhop` e `tick_input_latch` em `createmove_state`
- **Decisão**:
  1. Em `step_bhop(bhop_state& s, int32_t command_number, bool on_ground, bool jump_held)`, todas as transições de tick (`!on_ground`, `s.release_next`, `!s.was_on_ground`) só podem executar quando `command_number != s.last_cmd`. Se `command_number == s.last_cmd`, `step_bhop` preserva `s.cmd_jump` já decidido no início do tick (permitindo apenas promover `s.cmd_jump = true` caso `on_ground && !s.was_on_ground`, nunca rebaixar `true -> false` dentro do mesmo `command_number`).
  2. Em `createmove_state`, adicionar `tick_input_latch` indexado por `command_number`. No primeiro sub-frame de um novo `command_number`, `run_createmove` captura `on_ground`, `move_type`, `has_velocity`, `velocity`, `speed_2d`, `jump_held`, `raw_held`, `pre_aa_fwd` e `pre_aa_left` antes que a predição do Sub-frame 0 altere o pawn, e reutiliza esses valores nos Sub-frames 1..N do mesmo `command_number`.
- **Racional**: Impede que a simulação preditiva do Sub-frame 0 (`FL_ONGROUND: 1 -> 0`, `vel.z: 0 -> +301.99`) invalide a decisão de pulo nos Sub-frames 1..4 antes de `SendMove` enviar o comando ao servidor.

### D2: Limpeza de `held`, `toggle` e `scroll` em `sync_move_held_buttons`
- **Decisão**: Em `sync_move_held_buttons`, limpar `cmd->button_state.held &= ~k_move_button_mask`, `cmd->button_state.toggle &= ~k_move_button_mask` e `cmd->button_state.scroll &= ~k_move_button_mask` antes de aplicar `move_buttons_for_axes(fwd, left)`.
- **Racional**: Remove os eventos falsos de `UP_DOWN` e `DOWN_UP` gerados pela divergência entre `CCSGOInput::prev_held` (pós-rotação AA/Auto-Strafe) e `CCSGOInput::physical_held`, estabilizando o AnimGraph2.

### D3: Preservação Integral do Anti-Aim Aéreo (`auto_adjust`, `at_targets`, `freestanding`) e `compute_runtime_air_strafe` com Histerese de Setor
- **Decisão**:
  1. Em `run_createmove`, manter `aa_cfg = cfg.anti_aim; aa_cfg.enabled = true;` tanto no chão quanto no ar (`!on_ground`), preservando `auto_adjust` (`+36°` rifles/snipers, `+18°` pistolas/Zeus), `at_targets` e `freestanding` em todas as situações aéreas para manter a cabeça centralizada.
  2. Em `compute_runtime_air_strafe` (`src/features/createmove.hpp`), ancorar `ref_yaw` diretamente ao `aa_angles.yaw` calculado por `apply_antiaim_full` e utilizar o wish real do jogador (`strafe_f_wish`, `strafe_l_wish` + `camera_view.yaw`).
  3. Estabilizar a seleção do setor base de 45° (`state.strafe_base_sector`) com histerese de `27.5°` centrada em `wish_yaw` quando `|delta| < 10.0°`. Como `+90°` e `-90°` são múltiplos exatos de `45°` (`±2 * 45°`), ambos os lados do strafe compartilham a mesma grade de setores, eliminando snaps de `45°` na fronteira de `(k + 0.5) * 45°` em todos os `360°` de `ref_yaw`.

### D4: Resiliência do BunnyHop a `ForceButtonState` (`0xa62520`) e `sv_jump_spam_penalty_time` (`0x8bf800`)
- **Decisão**:
  1. Em `run_createmove`, travar `state.bhop_space_latched` enquanto `VK_SPACE` permanecer fisicamente pressionado após o início de um pulo/movimento em jogo, impedindo que `CCSPlayer_MovementServices::FinishMove` / `ResetJumpButton` -> `ForceButtonState(IN_JUMP)` (`client.dll` `0xa62520`) zere `jump_held` durante cadeias contínuas de BunnyHop.
  2. Em `tick_latch`, promover `state.tick_latch.on_ground = true` caso `raw_on_ground` transicione `false -> true` nos Sub-frames 1..4 do mesmo `command_number`, sem jamais rebaixar `true -> false`.
  3. Remover `add_subtick_move_step` duplicado para `k_in_jump` em `run_createmove`, mantendo apenas `strip_subtick_buttons(base_cmd, sdk::k_in_jump)` e `cmd->button_state.set_button_state(sdk::k_in_jump, up_down / up)` para evitar múltiplos splits em `0x8dba30` que disparam `sv_jump_spam_penalty_time` (`0.015625s` em `0x8bf913`).

## Risks / Trade-offs

- **[Risco]** O jogador soltar `Space` no meio de um sub-frame do mesmo tick -> **[Mitigação]** Se `raw_jump_held` cair para `false` em qualquer sub-frame, o latch atualiza `jump_held = false` imediatamente para respeitar a liberação do botão.
- **[Risco]** Compatibilidade com testes existentes de `compute_autostrafe` e `compute_sector_matched_autostrafe` -> **[Mitigação]** As funções matemáticas puras em `target.cpp` mantêm seu comportamento exato; a histerese temporal de lóbulo e guiagem por câmera atuam em `step_runtime_strafe_sign` dentro de `run_createmove`.
