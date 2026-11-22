## Why

Durante saltos e sequências de BunnyHop com Auto-Strafe e Anti-Aim ativos, o personagem apresenta quatro falhas acopladas observadas em runtime (`shogod-20261121.log`) e confirmadas via disassembly de `client.dll`:

1. **Auto-cancelamento de BunnyHop a 300 Hz por re-predição sub-frame**: `CCSGOInput::vtable[4]` (`0xcfd870`) executa `CreateMove[5]` (`0xb5baa0`) uma vez por render frame (~275–300 Hz, 4 a 5 sub-frames por tick de 64 Hz) reutilizando o mesmo `cmd->command_number` e rodando Client Prediction (`0xdef610`) ao final de cada sub-frame. No Sub-frame 0 do tick de pouso `N`, `local_pawn` está com `FL_ONGROUND = 1`; `step_bhop` emite `do_jump = true` e a predição do Sub-frame 0 simula o pulo in-place no `local_pawn`, limpando `FL_ONGROUND = 0`. No Sub-frame 1 (3,3 ms depois, ainda no mesmo `command_number = N`), `run_createmove` relê `FL_ONGROUND = 0` e chama `step_bhop(N, false, true)`. Como `if (!on_ground)` em `step_bhop` não verificava `command_number != s.last_cmd`, o Sub-frame 1 cancela o pulo (`cmd_jump = false`), fazendo a predição devolver o pawn ao chão no Sub-frame 1, pular de novo no Sub-frame 2 e cancelar no Sub-frame 3. Isso gera vibração vertical a 300 Hz e trava o BunnyHop no mesmo lugar.
2. **Bits de transição `toggle`/`scroll` sujos a 64 Hz nos botões `W/A/S/D`**: `SendMove_inner` (`0xb626c9`) copia `cmd->button_state.held` para `CCSGOInput::prev_held` (`[this + 0x250]`). Quando o movement fix do Anti-Aim ou o Auto-Strafe rotaciona `cmd->button_state.held`, o próximo tick em `CreateMove[5]` (`0xb5c5b0`) detecta `prev_held != physical_held` e seta `toggle` (`buttonstate2`) com flags de transição `UP_DOWN`/`DOWN_UP`. Como `sync_move_held_buttons` limpava apenas `held`, o AnimGraph2 recebia eventos de início/fim de tecla em `W/A/S/D` 64 vezes por segundo, tremendo as pernas e o tronco do boneco.
3. **Snaps de 45 graus e giros no ar por `auto_adjust` (+36°), `at_targets` e inversão de `strafe_sign` a 64 Hz**:
   - `auto_adjust` (`+36.0°`, calibrado para torção de coluna em pé no chão) permanecia ativo no ar, deslocando `rel = yaw_delta(ref_yaw, steer_yaw)` de `±90°` (centro do setor octogonal) para `-126° / +54°` (a apenas `13.5°` da borda de setor `±22.5°`), causando snaps de **45° em 1 tick** (`137.0° -> 104.7°` no log).
   - Com inimigos vivos, `at_targets` no ar ancorava `ref_yaw` ao inimigo e trocava entre `vis_yaw` e `any_yaw` durante o salto, girando o modelo no ar.
   - `strafe_sign` invertia `+1 <-> -1` a cada tick (64 Hz) porque 1 tick de aceleração aérea gira `vel_yaw` em `6.84° > 2.0°`, invertendo `leftmove` e `IN_MOVELEFT`/`IN_MOVERIGHT` 64 vezes por segundo.
4. **Frenagem de `AutoStop` durante BunnyHop ativo**: Com `auto_stop_flags = 7` (`in_air`) e `penetration = true`, o RageBot acionava `AutoStop` (`stop=1`) no ar e no tick de pouso mesmo com `Space` pressionado e hitchance aéreo insuficiente (`8.2% < 73%`), zerando a velocidade horizontal do BunnyHop.

## What Changes

- **Travamento (`latch`) de estado físico por `command_number` em `CreateMove`**:
  - `step_bhop` só transiciona estados (`!on_ground`, `release_next`, `was_on_ground`) quando `command_number != s.last_cmd`, garantindo que um pulo engatilhado no Sub-frame 0 jamais seja cancelado nos Sub-frames 1..4 do mesmo `command_number`.
  - `createmove_state` passa a armazenar `tick_input_latch`, fixando `on_ground`, `move_type`, `velocity`, `speed_2d`, `jump_held`, `raw_held` e wish move do início do `command_number` durante todos os sub-frames de predição daquele tick.
- **Saneamento completo de `CInButtonState` em `sync_move_held_buttons`**:
  - Limpa `held`, `toggle` e `scroll` para `k_move_button_mask` (`IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT`) antes de gravar os bits correspondentes ao vetor final `(forwardmove, leftmove)`.
- **Coordenação Anti-Aim + Auto-Strafe no ar e direção de strafe guiada por mouse/teclado**:
  - Desativa `auto_adjust` quando `!on_ground` e mantém `ref_yaw` relativo à câmera (`at_targets = false`, `freestanding = false`) enquanto `want_autostrafe` estiver ativo no ar, mantendo `rel` centrado em `±90°` (`tilt ≈ 0°`, a `22.5°` de qualquer borda de setor).
  - Adiciona controle de direção de strafe em runtime guiado por rotação do mouse (`camera_yaw_delta`), teclas `A`/`D` e sustentação mínima de lóbulo (`k_min_strafe_lobe_ticks = 4`) para eliminar vibração de 64 Hz em `leftmove`.
- **Supressão de pré-frenagem `AutoStop` durante BunnyHop não qualificado para disparo**:
  - Enquanto `bhop_active && jump_held && (!on_ground || do_jump)` estiver ativo, o `AutoStop` só freia se o tiro já estiver qualificado (`shot_qualified`), preservando o momentum do BunnyHop.

## Capabilities

### New Capabilities
- `air-movement-and-bhop`: Estabilidade de BunnyHop sub-frame-safe, limpeza de bits de transição de movimento em `CInButtonState`, coordenação de Anti-Aim aéreo com Auto-Strafe sem snaps de setor e preservação de momentum contra pré-frenagem de `AutoStop`.

### Modified Capabilities

## Impact

- `src/features/rage/target.cpp` (`step_bhop`)
- `src/features/createmove.hpp` (`tick_input_latch`, `sync_move_held_buttons`, `step_runtime_strafe_sign`, `createmove_state`)
- `src/features/createmove.cpp` (`run_createmove`)
- `src/engine/hooks.cpp` (reset de `tick_latch` em perda de foco/menu)
- `tests/test_features.cpp` e `tests/test_e2e_hvh.cpp` (testes unitários e E2E de regressão)
