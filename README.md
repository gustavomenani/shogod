# shogod

Suite interna `x64` em `C++20` para Counter-Strike 2 (`Windows 10/11`, `MSVC`, `CMake`, `DirectX 11`), estruturada com desacoplamento lock-free entre Game Thread e Render Thread, manipulação protobuf zero-dependency de `CUserCommand` e desenvolvimento orientado por especificações (**OpenSpec**).

---

## Arquitetura Principal

| Camada | Módulos | Responsabilidade |
|---|---|---|
| **Core & Config** | `src/core/`, `src/config/` | Logging assíncrono (`spdlog`), crash handler VEH, tipos matemáticos (`vec3`, `angles`, `view_matrix`), perfis JSON (`default.json` e por arma) e máquina de estados de keybinds (`Hold` / `Toggle`). |
| **SDK & Engine** | `src/sdk/`, `src/engine/` | Resolução dinâmica via `SchemaSystem_001`, pattern scanning resiliente (4 anchors em `client.dll`), `CUserCommand` / `CSubtickMoveStep` protobuf zero-dependency, Engine Trace / AutoWall, hooks DX11 + `CCSGOInput` + `SceneSystem` e `triple_buffer<frame_snapshot>`. |
| **Features (Game Thread)** | `src/features/` | `run_createmove` com `tick_input_latch` sub-frame-safe (~300 Hz vs 64 Hz), `LagCompensation`, `RageBot` (19 hitboxes, multipoint, hitchance analítico, AutoStop), `Anti-Aim` (Freestanding, At-Targets, Auto-Adjust, ΣΔ Ground Move) e `Movement` (BunnyHop, Auto-Strafe alinhado à velocidade, Edge Jump, Peek Assist). |
| **Visuals & UI (Render Thread)** | `src/features/visuals/`, `src/ui/` | ESP completo, `SceneSystem` Chams (Enemy/Team/Local/Arms/Weapon + XQZ), modulação de mundo/iluminação e menu ImGui de 6 abas (`RageBot`, `Anti-Aim`, `Visuals`, `Misc`, `Configs`, `Settings`). |
| **Tests & OpenSpec** | `tests/`, `openspec/` | Suíte GoogleTest unitária (`test_features.cpp`) e E2E (`test_e2e_hvh.cpp`) com simulação física de CS2 a 64 Hz / 300 Hz, além das especificações em `openspec/changes/`. |

---

## Build & Testes (Windows x64 / MSVC)

### 1. Configurar e Compilar (Release)

```powershell
cmake -B build -A x64
cmake --build build --config Release
```

Artefatos gerados em `build/Release/`:
- `build/Release/shogod.dll` — Módulo interno principal
- `build/Release/shogod_loader.exe` — Injetor CLI (`--process cs2.exe --dll build/Release/shogod.dll`)
- `build/tests/Release/shogod_tests.exe` — Suíte de testes unitários e E2E

### 2. Executar a Suíte de Testes

```powershell
.\build\tests\Release\shogod_tests.exe
```

---

## Diretórios de Runtime (`%APPDATA%\shogod\`)

- **Configurações**: `%APPDATA%\shogod\configs\` (`default.json` carregado automaticamente na inicialização e salvo ao fechar o menu)
- **Logs**: `%APPDATA%\shogod\logs\shogod-YYYYMMDD.log`
- **Crash Dumps**: `%APPDATA%\shogod\crashes\`

---

## Documentação Adicional

- [`shogod-master-prompt.md`](shogod-master-prompt.md) — Especificação técnica mestre da arquitetura
- [`docs/PLAN.md`](docs/PLAN.md) — Status atual dos módulos e histórico de evolução
- [`docs/USAGE.md`](docs/USAGE.md) — Guia detalhado de compilação, injeção, atalhos e solução de problemas
- [`docs/RE/findings.md`](docs/RE/findings.md) — Offsets, vtables e estruturas verificadas em `client.dll` e `scenesystem.dll`
