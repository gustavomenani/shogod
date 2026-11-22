# shogod — Guia de Compilação, Uso e Operação

## 1. Pré-requisitos

- **SO**: Windows 10 ou Windows 11 x64
- **Toolchain**: Visual Studio 2022 / Build Tools com workload *Desktop development with C++* (MSVC v143/v145 x64 + CMake)
- **Target**: Counter-Strike 2 (`cs2.exe`, DirectX 11)

---

## 2. Compilação e Testes

```powershell
# 1. Gerar solução CMake x64
cmake -B build -A x64

# 2. Compilar todos os alvos em Release
cmake --build build --config Release

# 3. Executar a suíte de testes unitários e E2E
.\build\tests\Release\shogod_tests.exe
```

Artefatos compilados:
- `build\Release\shogod.dll`
- `build\Release\shogod_loader.exe`
- `build\tests\Release\shogod_tests.exe`

---

## 3. Injeção e Controles em Jogo

Para uso em ambiente de teste local, inicie o CS2 com `-insecure`:

```powershell
.\build\Release\shogod_loader.exe --process cs2.exe --dll .\build\Release\shogod.dll
```

### Atalhos Principais
- **`INSERT`** (configurável na aba `Settings`): Abre/fecha o menu ImGui (salva `default.json` automaticamente ao fechar).
- **`END`**: Descarrega (`unload`) o módulo com restauração limpa de todos os hooks (`MinHook`), `WndProc` e recursos D3D11.

### Estrutura das 6 Abas do Menu
1. **RageBot**: Ativação global, Silent Aim, FOV, prioridade de alvo, Backtrack (`ms`), Auto-Wall e configuração detalhada por grupo de arma ou arma específica (`Min Damage`, `Hitchance`, `Multipoint`, `Safe Point`, `Hitboxes`, `Auto-Stop`, `Auto-Scope`, `Delay Shot`).
2. **Anti-Aim**: Pitch (`Off`, `Down`, `Up`, `Zero`), Yaw (`Off`, `Backward`, `Jitter`, `Random`), `Yaw Offset`, `Jitter Range`, `At Targets`, `Freestanding`, `Auto-Adjust`, `On Move`, `On Air`, `Safe Manual Fire`.
3. **Visuals**: ESP de inimigos (Box, Health, Armor, Name, Weapon Icon/Text, Ammo, Flags, Skeleton, Head Dot, Offscreen Arrows, Sound ESP, Backtrack Ghost), Chams (`Enemy Visible`, `Enemy XQZ`, `Team`, `Local`, `Arms`, `Weapon`, `Scope Blend`), World/Light Modulation, Third-Person, Camera FOV, Removals (`No Flash`, `No Smoke`, `No Scope`, `Remove View Punch`) e Penetration Crosshair.
4. **Misc**: `BunnyHop` (com bind `Hold`/`Toggle`), `Auto-Strafe` (com bind `Hold`/`Toggle`), `Edge Jump`, `Peek Assist`, `Slide Walk` (`Leg Movement`), `Auto-Pistol` e `Safe Mode`.
5. **Configs**: Gerenciamento completo de perfis JSON (`Create`, `Load`, `Save`, `Duplicate`, `Delete`, `Import/Export Clipboard`).
6. **Settings**: Keybind do menu, escala de DPI, cor de destaque (`Accent`) e telemetria dos hooks/anchors.

---

## 4. Diretórios de Configuração e Diagnóstico

Todos os arquivos de execução ficam em `%APPDATA%\shogod\`:
- `%APPDATA%\shogod\configs\default.json`
- `%APPDATA%\shogod\logs\shogod-YYYYMMDD.log`
- `%APPDATA%\shogod\crashes\`
