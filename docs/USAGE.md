# SHOGOD — uso

## Build

```
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

Saídas em `build/Release/`:

| Arquivo | Papel |
|---|---|
| `shogod.dll` | cheat (injetado no `cs2.exe`) |
| `shogod_loader.exe` | injeta/ejecta a DLL |
| `shogod_host.exe` | host standalone do menu (sem jogo), `--capture <dir>` gera PNGs |
| `tests/Release/shogod_tests.exe` | suite de unidade (37 testes) |

`fonts/` precisa ficar ao lado da DLL (o `shogod.dll` carrega `fonts/Inter-Medium.ttf`).

## Injeção

Alvo: servidor comunitário HvH / partida offline com bots. **Não** é para VAC.

```
shogod_loader.exe                 # acha cs2.exe e injeta build\Release\shogod.dll
shogod_loader.exe --wait 60       # espera o processo subir
shogod_loader.exe --unload        # FreeLibrary remoto (restaura hooks no detach)
shogod_loader.exe --dll <path> --process <nome.exe>
```

- Injeção padrão: `CreateRemoteThread(LoadLibraryW)` com o caminho da DLL.
- Manual-map não faz parte do pacote; o loader e o `DllMain` assumem carga por
  `LoadLibrary`, e o unload depende de `DllMain(DLL_PROCESS_DETACH)` para
  remover hooks em ordem inversa. Quem for adicionar manual-map precisa
  implementar: mapeamento de seções/relocs/TLS, resolver imports, chamar
  `DllMain` com `DLL_PROCESS_ATTACH`, e um export explícito de unload — porque
  sem `FreeLibrary` o detach não acontece sozinho.

## Operação

- `INSERT` abre/fecha o menu (configurável em Config → Settings; salvo no JSON).
- Com o menu aberto, o `WndProc` engole mouse/teclado do jogo; ao fechar, o
  input volta na mesma sessão.
- Configs: `%APPDATA%\shogod\configs\<nome>.json` (salvar/carregar na aba Config).
- Logs: `%APPDATA%\shogod\logs\shogod-YYYYMMDD.log` (prefixo `[shogod]`), com
  `OK`/`FAIL` por anchor no init.

## Diagnóstico rápido

| Sintoma | Causa provável / ação |
|---|---|
| Menu não aparece, jogo continua | log sem `private vtable ... installed`: swapchain não encontrado (janela errada) ou `Present` não foi chamado ainda |
| Jogo fecha ao injetar | log mostra até onde foi; se parar em `hooks: bootstrap`, olhar o `.mdmp` mais novo em `game/bin/win64` |
| `anchors: x/3 OK` | ler a linha do anchor que falhou; feature dependente fica desligada |
| ESP desalinhado | view matrix/projeção; ver `docs/RE/findings.md` §8.6 (ainda pendente) |
| Texto do menu gigante/pequeno | `ui_scale` do host; no jogo a fonte é carregada do diretório da DLL |

## Estado de features

Features em jogo (ESP, ragebot, antiaim, misc) dependem de SDK (view matrix +
schema). O que está verificado hoje: injeção, hooks, menu, config e o
`CreateMove` com dados válidos. Ver `docs/PLAN.md` §1 para a ordem pendente.
