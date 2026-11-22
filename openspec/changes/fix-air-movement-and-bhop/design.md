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

### D3: Anti-Aim Aéreo Camera-Relative e `step_runtime_strafe_sign` com Histerese Temporal e Guiagem por Mouse
- **Decisão**:
  1. Em `run_createmove`, configurar `aa_cfg.auto_adjust = cfg.anti_aim.auto_adjust && on_ground`. Quando `want_autostrafe` estiver ativo (`!on_ground`), forçar `aa_cfg.at_targets = false` e `aa_cfg.freestanding = false`.
  2. Implementar `step_runtime_strafe_sign` em `createmove.hpp` para uso em `run_createmove`:
     - Se o jogador pressiona exclusivamente `A` (`pre_aa_left > 0.1f`) ou `D` (`pre_aa_left < -0.1f`), seguir a direção desejada.
     - Se o jogador move o mouse (`|yaw_delta(last_camera_yaw, camera_yaw)| >= 0.35°`), alinhar `strafe_sign` ao sentido da curva do mouse (`+1` para esquerda, `-1` para direita).
     - Em linha reta (`W` ou sem rotação de câmera), manter cada lóbulo de strafe por no mínimo `k_min_strafe_lobe_ticks = 4` ticks antes de inverter `strafe_sign`, evitando oscilação de 64 Hz em `leftmove`.
- **Racional**: Sem `auto_adjust` (+36°) e sem `at_targets` no ar, `ref_yaw` fica em `camera_yaw + 180°` (ou offset fixo configurado), posicionando `steer_yaw` exatamente no centro do setor (`tilt ≈ 0°`, a `22.5°` das fronteiras), onde `+90°` e `-90°` possuem o mesmo resto módulo `45°` (zero snaps de yaw).

### D4: Supressão de `AutoStop` Pré-Disparo Durante BunnyHop Ativo
- **Decisão**: Em `run_createmove`, calcular `const bool actively_bhopping = bhop_active && jump_held && (!on_ground || do_jump);`. Quando `actively_bhopping` for verdadeiro, não executar pré-frenagem `AutoStop` enquanto `shot_qualified` for falso.
- **Racional**: Permite que o jogador mantenha a cadeia de BunnyHop fluida mesmo quando há inimigos penetráveis no mapa, só parando ou atirando no ar quando o hitchance aéreo realmente qualificar o disparo.

## Risks / Trade-offs

- **[Risco]** O jogador soltar `Space` no meio de um sub-frame do mesmo tick -> **[Mitigação]** Se `raw_jump_held` cair para `false` em qualquer sub-frame, o latch atualiza `jump_held = false` imediatamente para respeitar a liberação do botão.
- **[Risco]** Compatibilidade com testes existentes de `compute_autostrafe` e `compute_sector_matched_autostrafe` -> **[Mitigação]** As funções matemáticas puras em `target.cpp` mantêm seu comportamento exato; a histerese temporal de lóbulo e guiagem por câmera atuam em `step_runtime_strafe_sign` dentro de `run_createmove`.
