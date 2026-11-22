## 1. Máquina de Estados de BunnyHop e Latch por Tick (`command_number`)

- [x] 1.1 Atualizar `step_bhop` em `src/features/rage/target.cpp` para que transições de tick (`!on_ground`, `release_next`, `!was_on_ground`) ocorram apenas quando `command_number != s.last_cmd`, impedindo auto-cancelamento nos sub-frames 1..N do mesmo `command_number`.
- [x] 1.2 Adicionar `tick_input_latch` em `src/features/createmove.hpp` e travar o estado físico inicial do tick (`on_ground`, `move_type`, `velocity`, `speed_2d`, `jump_held`, `raw_held`, `pre_aa_fwd`, `pre_aa_left`) no início de `run_createmove` (`src/features/createmove.cpp`).

## 2. Limpeza de `CInButtonState` e Estabilização de Auto-Strafe + Anti-Aim Aéreo

- [x] 2.1 Atualizar `sync_move_held_buttons` em `src/features/createmove.hpp` para limpar `held`, `toggle` e `scroll` em `k_move_button_mask`.
- [x] 2.2 Implementar `step_runtime_strafe_sign` em `src/features/createmove.hpp` com guiagem por delta de yaw da câmera, teclas `A`/`D` e sustentação mínima de lóbulo (`k_min_strafe_lobe_ticks = 4`).
- [x] 2.3 Desativar `auto_adjust` quando `!on_ground` e manter `at_targets = false` / `freestanding = false` durante `want_autostrafe` aéreo em `src/features/createmove.cpp`.
- [x] 2.4 Suprimir pré-frenagem `AutoStop` quando `bhop_active && jump_held && (!on_ground || do_jump)` e `!shot_qualified` em `src/features/createmove.cpp`.

## 3. Testes de Regressão e Verificação

- [x] 3.1 Adicionar testes unitários em `tests/test_features.cpp` cobrindo idempotência sub-frame de `step_bhop`, limpeza de `toggle`/`scroll` em `sync_move_held_buttons` e `step_runtime_strafe_sign`.
- [x] 3.2 Adicionar teste E2E em `tests/test_e2e_hvh.cpp` validando ausência de snaps de 45° no ar com `auto_adjust` + `at_targets` + `auto_strafe` e sub-frames a 300 Hz.
- [x] 3.3 Compilar `shogod` e `shogod_tests` em Release e validar 100% da suíte.
