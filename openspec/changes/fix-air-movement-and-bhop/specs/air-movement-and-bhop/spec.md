# Specification: Air Movement, BunnyHop & Auto-Strafe Stability

## Requirements

### Requirement: Sub-Frame Idempotent BunnyHop State Machine
O sistema `step_bhop` e o pipeline `run_createmove` SHALL operar de forma idempotente ao longo de todos os sub-frames de renderização (~300 Hz) que compartilham o mesmo `cmd->command_number`, impedindo que a predição local do Sub-frame 0 (`FL_ONGROUND: 1 -> 0`) cancele o salto nos Sub-frames 1..N antes do envio em `SendMove`.

#### Scenario: Pouso no Sub-frame 0 seguido de re-predição aérea no Sub-frame 1 do mesmo command_number
- **WHEN** `step_bhop` recebe `command_number = N` com `on_ground = true` e `jump_held = true` no Sub-frame 0 (após estar no ar no tick `N - 1`) e retorna `do_jump = true`
- **AND** no Sub-frame 1 do mesmo `command_number = N` a função é chamada novamente com `on_ground = false` devido à predição local do Sub-frame 0
- **THEN** `step_bhop` MUST preservar `do_jump = true` e `cmd_jump = true` durante todos os sub-frames de `command_number = N`

### Requirement: Clean Movement Transition Flags in CInButtonState
A função `sync_move_held_buttons` SHALL limpar os bits de movimento (`IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT`) não apenas em `cmd->button_state.held`, mas também em `cmd->button_state.toggle` e `cmd->button_state.scroll`.

#### Scenario: Reescrita de botões de movimento após rotação de Anti-Aim ou Auto-Strafe
- **WHEN** `cmd->button_state.toggle` ou `cmd->button_state.scroll` contêm bits de transição residuais em `k_move_button_mask` gerados pela diferença entre `CCSGOInput::prev_held` e o teclado físico
- **THEN** `sync_move_held_buttons` MUST zerar todos os bits de `k_move_button_mask` em `toggle` e `scroll` e refletir o vetor `(forwardmove, leftmove)` apenas em `held`

### Requirement: Airborne Anti-Aim & Auto-Strafe Yaw Continuity
Enquanto o jogador estiver no ar (`!on_ground`) com Anti-Aim e Auto-Strafe ativos, `run_createmove` SHALL desativar o offset de torção terrestre (`auto_adjust`) e manter a referência de yaw do Anti-Aim relativa à câmera, evitando cruzamentos de fronteira octogonal de 45 graus e giros por troca de alvo no ar.

#### Scenario: Salto contínuo com Anti-Aim Backward, Auto-Adjust e Auto-Strafe ativos
- **WHEN** o jogador salta e permanece no ar (`!on_ground`) com `anti_aim.enabled = true`, `anti_aim.auto_adjust = true`, `anti_aim.at_targets = true` e `rage.auto_strafe = true`
- **THEN** `run_createmove` MUST suprimir `auto_adjust` e `at_targets` enquanto `!on_ground`, mantendo `base_cmd->viewangles->y` sem saltos de 45 graus entre ticks aéreos consecutivos

### Requirement: BunnyHop Momentum Preservation Against Unqualified AutoStop
Enquanto o jogador mantiver o BunnyHop ativo no ar ou no tick de salto (`bhop_active && jump_held && (!on_ground || do_jump)`), `run_createmove` SHALL suprimir a pré-frenagem do `AutoStop` a menos que o disparo já esteja qualificado (`shot_qualified == true`).

#### Scenario: BunnyHop ativo com inimigo penetrável e hitchance aéreo abaixo do limiar
- **WHEN** o jogador está em BunnyHop ativo (`bhop_active && jump_held`) e o RageBot encontra um alvo com dano acima de `min_damage` porém com `hitchance < target_hc` (`shot_qualified == false`)
- **THEN** `run_createmove` MUST NOT acionar `compute_autostop`, preservando o vetor de movimento do BunnyHop/Auto-Strafe
