# shogod — Master Architecture & Engineering Specification (CS2 x64 Internal)

> Especificação mestre atualizada da arquitetura, subsistemas, contratos de thread-safety e fluxo spec-driven (OpenSpec) do `shogod` (`Windows 10/11 x64`, `C++20`, `MSVC`, `CMake`, `DirectX 11`).

---

## 1. Visão Geral e Princípios de Arquitetura

O `shogod` é uma DLL interna `x64` (`shogod.dll`) e um injetor nativo (`shogod_loader.exe`) para Counter-Strike 2 (`cs2.exe`), projetados com:

- **Desacoplamento estrito entre Game Thread e Render Thread**:
  - A **Game Thread** (`CCSGOInput::vtable[4]` / `SendMove` `vtable[8]`) coleta o `frame_snapshot`, executa `LagCompensation`, `BunnyHop`, `Auto-Strafe`, `Anti-Aim`, `Movement Fix`, `AutoWall` e `RageBot`, e publica o snapshot em um buffer triplo lock-free (`sdk::triple_buffer<sdk::frame_snapshot>`).
  - A **Render Thread** (`IDXGISwapChain::Present` / `ResizeBuffers`) consome exclusivamente o `frame_snapshot` publicado para desenhar o ESP e a UI ImGui de 6 abas, sem jamais dereferenciar ponteiros brutos de entidades do jogo.
- **Zero-Dependency Protobuf `CUserCommand`**:
  - Manipulação direta de `CUserCmd` (`0x98` bytes), `CBaseUserCmdPB`, `CSGOInputHistoryEntryPB`, `CSubtickMoveStep` (`RepeatedPtrField`) e `CInButtonState` sem dependência de `libprotobuf`, preservando o pool da arena do motor ao remover ou reordenar subticks.
- **Idempotência Sub-Frame (~300 Hz vs 64 Hz)**:
  - `CCSGOInput::vtable[4]` roda uma vez por render frame (~275–300 Hz, 4 a 5 sub-frames por tick de 64 Hz) sobre o mesmo `cmd->command_number`, invocando Client Prediction ao final de cada sub-frame.
  - O pipeline `run_createmove` trava (`tick_input_latch`) o estado físico inicial do tick (`on_ground`, `move_type`, `velocity`, `speed_2d`, `jump_held`, `raw_held`, wish move) na transição de `command_number`, impedindo que a predição local do Sub-frame 0 cancele pulos de BunnyHop ou rotacione botões múltiplas vezes nos Sub-frames 1..4.
- **Isolamento de Câmera Local (`SendMove` Hook)**:
  - O hook em `CCSGOInput::SendMove` (`vtable[8]`) preserva os `viewangles` reais da câmera no cliente enquanto transmite os ângulos de `Anti-Aim` e `Silent Aim` nos comandos enviados ao servidor.

---

## 2. Mapa de Diretórios do Repositório

```text
shogod/
├── CMakeLists.txt                  # Build system (shogod_core, shogod_ui, shogod, shogod_loader, shogod_tests)
├── README.md                       # Guia rápido de build, arquitetura e uso
├── AGENT.MD                        # Convenções operacionais para agentes no repositório
├── shogod-master-prompt.md         # Especificação mestre atualizada do projeto
├── docs/
│   ├── PLAN.md                     # Roadmap e status atual dos subsistemas
│   ├── USAGE.md                    # Manual de compilação, injeção, configuração e testes
│   └── RE/
│       └── findings.md             #Offsets, vtables e estruturas verificadas em client.dll / scenesystem.dll
├── openspec/
│   ├── config.yaml                 # Configuração do fluxo spec-driven (OpenSpec)
│   └── changes/                    # Mudanças especificadas (proposal, design, specs, tasks)
├── loader/
│   └── main.cpp                    # Injetor CLI (shogod_loader.exe)
├── src/
│   ├── main.cpp                    # Entrypoint DllMain e thread de bootstrap/unload
│   ├── core/                       # Logger spdlog, crash handler VEH, math (vec3, angles, view_matrix)
│   ├── config/                     # Schema config_v1, serialização JSON (nlohmann), bind_state e perfis por arma
│   ├── engine/                     # Hooks D3D11/WndProc/CCSGOInput/SceneSystem, renderer DX11 e estado runtime
│   ├── sdk/                        # SchemaSystem netvars, pattern scanner, CUserCommand protobuf, trace/autowall
│   ├── features/
│   │   ├── createmove.hpp/.cpp     # Orquestrador de tick: Latch, Bhop, AutoStrafe, AA, Movement Fix, RageBot
│   │   ├── lagcomp.hpp/.cpp        # Histórico de LagCompensation por pawn/handle e extrapolação wall-clamped
│   │   ├── backtrack.hpp           # Histórico visual para Backtrack Ghost
│   │   ├── aaa/antiaim.hpp/.cpp    # Anti-Aim (Backward, Jitter, Random, At-Targets, Freestanding, Auto-Adjust)
│   │   ├── rage/target.hpp/.cpp    # Seleção de alvo, AutoStop, AutoStrafe, BunnyHop e Shot Delay
│   │   ├── math/                   # Geometria de cápsulas/hitboxes (19 hitboxes), multipoint e hitchance analítico
│   │   └── visuals/                # ESP 2D/Skeleton/Offscreen, SceneSystem Chams e World/Light Modulation
│   └── ui/                         # Menu ImGui bespoke (6 abas), tema, animações e widgets customizados
├── tests/
│   ├── test_features.cpp           # Suíte unitária (Math, Config, Binds, Hitbox, Hitchance, AA, Rage, Movement)
│   ├── test_e2e_hvh.cpp            # Suíte E2E de simulação HvH (64 Hz / 300 Hz sub-frame)
│   └── fixtures/                   # Fixtures determinísticas de física CS2, arena de hitboxes e configs
└── third_party/
    ├── imgui/                      # Dear ImGui v1.91.x (Win32 + DX11)
    └── minhook/                    # MinHook x64
```

---

## 3. Subsistemas e Contratos Técnicos

### 3.1 SDK, SchemaSystem e Pattern Scanning (`src/sdk/`)
- **Resolução Dinâmica via `SchemaSystem_001`**: Todos os netvars (`m_iHealth`, `m_iTeamNum`, `m_vOldOrigin`, `m_vecViewOffset`, `m_fFlags`, `m_vecAbsVelocity`, `m_pGameSceneNode`, `m_modelState`, `m_pWeaponServices`, `m_AttributeManager`, `m_iItemDefinitionIndex`, `m_nNextPrimaryAttackTick`, `m_flNextAttack`, `m_bIsScoped`, `m_bResumeZoom`, `m_aimPunchCache`) são resolvidos dinamicamente em tempo de carga.
- **4 Anchors Principais em `client.dll`**:
  1. `dwEntityList` (`CGameEntitySystem`)
  2. `dwViewMatrix` (`4x4` float world-to-clip matrix)
  3. `dwLocalPlayerController` (`CCSPlayerController`)
  4. `dwCSGOInput` (`CCSGOInput`)
- **Engine Trace & AutoWall (`src/sdk/trace.cpp`)**:
  - Usa o TraceManager nativo de `client.dll` + `CCSWeaponBaseVData` (`m_flDamage`, `m_flPenetration`, `m_flRange`, `m_flRangeModifier`, `m_flHeadshotMultiplier`, `m_flArmorRatio`) para simular penetração multi-superfície e calcular dano real com armadura/capacete.

### 3.2 Movimentação Aérea, BunnyHop e Auto-Strafe (`src/features/createmove.*`, `src/features/rage/target.*`)
- **BunnyHop Sub-Frame-Safe (`step_bhop` + `tick_input_latch`)**:
  - `step_bhop` só avança transições de tick (`!on_ground`, `release_next`, `!was_on_ground`) quando `command_number != s.last_cmd`, garantindo que o pulo engatilhado no Sub-frame 0 permaneça ativo até o `SendMove`.
- **Auto-Strafe Alinhado à Velocidade com Estabilização de Lóbulo (`compute_runtime_air_strafe`)**:
  - Enquanto no ar (`!on_ground`), desativa `auto_adjust` (`+36°` terrestre) e `at_targets`/`freestanding`, ancorando `ref_yaw` à direção contínua de velocidade (`vel_yaw + 180°` com Anti-Aim ativo ou `vel_yaw` sem Anti-Aim).
  - Isso mantém `rel = yaw_delta(ref_yaw, steer_yaw)` exatamente em `±90°` (`tilt == 0°`), eliminando snaps de `45°` entre setores octogonais.
  - `step_runtime_strafe_sign` segue imediatamente a rotação do mouse (`|cam_delta| >= 0.25°`) ou teclas direcionais (`A`/`D`/`S`), e sustenta cada lóbulo reto por no mínimo `k_min_strafe_lobe_ticks = 4` ticks para eliminar vibração de 64 Hz no AnimGraph2.
- **Limpeza Completa de `CInButtonState` (`sync_move_held_buttons`)**:
  - Limpa `held`, `toggle` e `scroll` em `k_move_button_mask` (`IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT`) antes de gravar os bits do vetor final `(forwardmove, leftmove)`.

### 3.3 RageBot, LagCompensation e Hitboxes (`src/features/createmove.*`, `src/features/lagcomp.*`)
- **Extrapolação Condicional (`compute_extrapolation_gap_ms`)**:
  - Só extrapola quando `tick_gap >= 2` (choke/lag real), mantendo `0 ms` contra alvos atualizados (`tick_gap <= 1`).
- **Ordem Estrita Fraca de Hitboxes (`better_rage_shot_point` + `update_rage_shot_slot`)**:
  - Avaliação lazy de hitchance por slot (`best_head` e `best_body`), garantindo que pontos de borda/multipoint substituam o centro quando o centro falha no limiar de hitchance.
  - Respeita `prefer_body_aim`, `force_body_aim`, `ignore_limbs` (separando `pelvis` de `arms`/`legs`) e letalidade.
- **Coordenação `AutoStop` + `BunnyHop`**:
  - Enquanto o jogador mantém BunnyHop ativo no ar ou no tick de pulo (`bhop_active && jump_held && (!on_ground || bhop_do_jump)`), a pré-frenagem do `AutoStop` é suprimida até que o tiro esteja qualificado (`shot_qualified`).

### 3.4 Anti-Aim e Movement Fix (`src/features/aaa/antiaim.*`)
- **Modos de Pitch/Yaw**: Pitch (`Off`, `Down +89°`, `Up -89°`, `Zero 0°`), Yaw (`Off`, `Backward 180°`, `Jitter`, `Random`), `At Targets`, `Freestanding` com histerese e probes dentro do hull de `16u`, e `Auto-Adjust` (`+36°` rifle / `+18°` pistola no chão).
- **Quantized Ground Move (`compute_quantized_ground_move`)**:
  - Difusão de erro 2D (`ΣΔ`) sobre os 8 setores quantizados de `SetupMove` (`roundf(forwardmove)`, `roundf(leftmove)` em `{-1, 0, +1}`), preservando a trajetória exata em qualquer yaw de Anti-Aim.

### 3.5 Visuals, SceneSystem Chams e UI ImGui (`src/features/visuals/`, `src/ui/`)
- **ESP**: Bounding box (`Off`, `Full`, `Corners`), barra de vida com gradiente e texto condicional, barra de armadura, skeleton (bone matrix stride `32B`), nome, arma (ícone/texto), munição, flags (`SCOPED`, `FLASHED`, `DEFUSING`), offscreen arrows, sound rings, backtrack ghost e penetration crosshair.
- **Chams & World Modulation**: Hooks em `scenesystem.dll` para materiais customizados (Enemy Visible/XQZ, Team, Local, Arms, Weapon, Scope Blend) e modulação de mundo/luz/exposição.
- **Menu ImGui (6 Abas)**: `RageBot`, `Anti-Aim`, `Visuals`, `Misc`, `Configs`, `Settings`.

---

## 4. Fluxo de Desenvolvimento Spec-Driven (OpenSpec)

Toda alteração estrutural ou correção de subsistema segue o fluxo OpenSpec em `openspec/changes/<nome-da-mudanca>/`:
1. `proposal.md`: Diagnóstico com evidências (`client.dll` RVAs, logs, reprodução) e escopo da mudança.
2. `design.md`: Decisões técnicas, invariantes matemáticos e trade-offs.
3. `specs/<capability>/spec.md`: Requisitos formais (`SHALL`/`MUST`) e cenários (`WHEN`/`THEN`).
4. `tasks.md`: Checklist rastreável de implementação e verificação via `shogod_tests.exe`.
