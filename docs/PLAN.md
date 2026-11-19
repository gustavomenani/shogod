# SHOGOD — Plano de construção e estado

Atualizado em 2026-11-19. Fonte de verdade do RE: `docs/RE/findings.md`
(anchors, patterns, RVAs e resultados de sonda em jogo).

## 0. Estado atual (o que está feito)

| Área | Estado | Evidência |
|---|---|---|
| Build | Release x64 /O2 compila limpo | `cmake --build build --config Release` |
| Testes | 37 testes verdes (scanner, W2S à mão, ângulos, hull, hitchance, config round-trip, snapshot seqlock, backtrack, alvo, antiaim) | `build/tests/Release/shogod_tests.exe` |
| Injeção | loader injeta/ejecta em `cs2.exe` | `tools/loader`, log `loaded — module base` |
| Present/ResizeBuffers | hook vivo via **vtable privada** (clone + troca só no objeto do jogo) | log `private vtable ... installed`, `Present hook live` |
| WndProc | hook + bloqueio de input com menu aberto | log `WndProc hooked` |
| CreateMove | `vtable[7]` do `CCSGOInput`; dados validados em jogo (`edx=0`, `count=2`, `arr` válido) | log `create_move:` |
| Anchors | 3/3 OK em jogo: entity system, input/CreateMove, schema | log `anchors: 3/3 OK` |
| Menu | 5 abas, sidebar, header vivo, tema, animações, tooltips, fontes; renderiza no CS2 | `shots/game/menu_ingame.png` |
| Config | JSON versionado, clamp com log, chave desconhecida preservada, perfis | `src/config`, testes |
| Math/lógica | W2S, ângulos, hull, hitchance, seleção de alvo, fire gate, backtrack history, antiaim math | `src/features`, testes |
| Preview 3D | pipeline DX11 offscreen + câmera órbita/zoom + grade + overlay de skeleton | `src/ui/preview` |

## 1. O que falta (ordem de execução)

| # | Item | Bloqueia | Detalhe |
|---|---|---|---|
| 1 | **View matrix** | todo o ESP/aim visual | instância do `CViewRender` localizada (RVA `0x256afa0`, vtable `0x1c76ac8`), matriz não encontrada; sonda em `[0x20,0x1d60)` deu negativo (§8.6). Próximo: relaxar filtro p/ coluna-maior, seguir ponteiros do objeto, ou subir pelo slot 17 (`+0x1cd0`) |
| 2 | **Schema runtime** (classe → campo → offset) | health, team, origin, bones, weapon data | `ISchemaSystem` slot 12 (`FindTypeScopeForModule`) provado; falta o lookup de classe e o layout do field entry. Probe de memória (scan de ponteiros para `C_CSPlayerPawn`/`m_iHealth`) já no build |
| 3 | **Entidades + snapshot** | ragebot, ESP, backtrack real | layout derivado: `entitySystem+0x10` → 64 chunks × 512 × stride `0x70`, entity em `+0x00`, serial `+0x10`. Falta o código do iterador e publicar `frame_snapshot` |
| 4 | **Pawn/controller, bones, weapon vdata** | aim points, skeleton, dano | em cima de #2 e #3 |
| 5 | **Ragebot in-game** | — | lógica pura pronta; falta aim points nos bones, auto-fire/stop/crouch escrevendo `CUserCmd`, penetração (trace + weapon data) |
| 6 | **Antiaim/resolver in-game** | — | math pronta; falta escrever yaw/pitch no cmd e o resolver por histórico com log |
| 7 | **Visuals** | — | ESP (box/health/armor/name/weapon/ammo/flags/distance/skeleton/glow), chams, world (bomb, nades, no-flash/smoke/scope), hitmarker, tracer |
| 8 | **Preview ligado ao jogo** | — | trocar template por bones reais e aplicar o mesmo `cham_material` dos chams |
| 9 | **Misc** | — | bhop/strafe/edge/ladder/slide, knife-bot, auto-accept/pistol, fake-duck, third-person, free-look, hit log, watermark |
| 10 | **Wiring audit** | regra do master prompt | hoje há controles sem consumidor; cada item do menu tem que nomear a função que o usa |
| 11 | **Docs restantes** | entrega | `docs/DETECTION.md`, `docs/USAGE.md`, `docs/REPORT.md` |
| 12 | **Gates 3–5** | entrega | ver §5 |

## 2. Slices — estado real

| # | Slice | Owns | Estado |
|---|---|---|---|
| S1 | foundation | `CMakeLists.txt`, `vendor/`, `brand.hpp`, `engine/log.*`, `tests/` | **feito** |
| S2 | math | `features/math/*` | **feito** (testes) |
| S3 | scanner/signatures | `engine/pattern.*`, `sdk/signatures.*` | **feito** (log OK/FAIL por anchor) |
| S4 | RE | `docs/RE/*` | **feito até onde a build permite**; gaps declarados (§3) |
| S5 | sdk | `sdk/interfaces/schema/entity/snapshot` | **parcial**: snapshot + resolve de anchors + probes; falta schema lookup e iterador |
| S6 | engine/hooks | `engine/hooks.*`, `loader.*`, `runtime.*` | **feito in-game**; unload não verificado (gate 4) |
| S7 | ragebot | `features/rage/*` | **parcial**: lógica pura; sem integração |
| S8 | aaa | `features/aaa/*` | **parcial**: math; sem escrita no cmd |
| S9 | backtrack/pen | `features/backtrack.*` | **parcial**: histórico; penetração não iniciada |
| S10 | visuals | `features/esp/*`, `chams/*` | **não iniciado** |
| S11 | preview | `ui/preview/*` | **parcial**: pipeline DX11 + câmera + overlay; falta dados reais |
| S12 | misc | `features/misc/*` | **não iniciado** |
| S13 | config | `config/*` | **feito** |
| S14 | menu shell | `ui/menu.*`, `ui/style.*` | **feito** (in-game 1080p) |
| S15 | menu pages | `ui/pages/*` | **feito na UI**; wiring audit pendente (#10) |
| S16 | harness | `tests/*` | **feito** (37 verdes) |
| S17 | integração | build + gates 2–5 | **parcial**: gate 2 parcial; 3–5 pendentes |

## 3. Anchors (RE desta build — detalhe em `docs/RE/findings.md`)

| Anchor | Resolve | Validação | Status |
|---|---|---|---|
| `CGameEntitySystem` | rip @+9 → global | ponteiro + vtable `base+0x1c29090` | **OK em jogo** |
| `CCSGOInput` / `CreateMove` | rip @0 → global; vtable slot 7 | vtable `base+0x1c97330`, slot 7 `base+0xb781e0`; `edx=0`, `count≤150` | **OK em jogo** |
| `SchemaSystem_001` | CreateInterface | vtable dentro de `schemasystem.dll`; slot 12 = FindTypeScopeForModule | **OK em jogo**; lookup de classe pendente |
| `CViewRender` | `base+0x256afa0` (in-place) | vtable `base+0x1c76ac8` | **OK em jogo**; view matrix pendente |
| view matrix | membro/ponteiro do `CViewRender` | 16 floats finitos, muda com a câmera | **pendente** (sonda negativa §8.6) |
| bone array | `m_modelState+0x80`, count `+0x8c` | layout provado; binding de classe via schema | **parcial** |
| `GlobalVars` | rip global | `interval_per_tick` em (0, 0.1] | **não derivado** |
| trace | `CGamePhysicsQueryInterface::TraceShape` | fração 0..1 num trace de teste | **não derivado** (`CGameTraceManager` não existe nesta build) |
| weapon vdata | schema `CCSWeaponBaseVData` | dano/alcance > 0 | **pendente** |

Offset inventado é proibido. Anchor que falha: feature desliga, log `FAIL`, status no menu.

## 4. Contratos de threading

`CreateMove` (game thread) publica, `Present` (render thread) lê. Snapshot fixo
64 players × 32 bones, ponteiro atômico, `seq` ímpar = frame descartado.
Zero heap por frame após init. Nenhum mutex em `Present`/`CreateMove`.

## 5. Gates

| Gate | Estado |
|---|---|
| 1 — unidade | **verde** (37 testes) |
| 2 — init offline com bots | **parcial**: anchors 3/3, Present/CreateMove vivos; falta "feature set reportado" (features não integradas) |
| 3 — 10 min in-game (ESP 1080p/1440p, input, config, preview) | **não executado** (depende de #1–#4) |
| 4 — soak 2h, reinjeção, unload em ordem inversa | **não executado** |
| 5 — screenshots (abas, preview com chams+skeleton, ESP, hit log) | **parcial**: menu in-game ok; faltam preview com dados reais, ESP e hit log |

Gate não executado é reportado como não executado.

## 6. Riscos conhecidos

- `client.dll` de 39 MB: Ghidra estoura o deadline do REA; RE feito com rizin + capstone e re-lido no binário.
- Vtable compartilhada do `IDXGISwapChain` não pode ser patchada (mata o `dxgi`); usar vtable privada (feito).
- Assinatura quebrada por patch da Valve: re-derivar com o playbook do `cs2-hvh-build`, nunca copiar offset.
- Features detectáveis por plugin de servidor comunitário têm toggle e entrada em `docs/DETECTION.md`.
- VAC fora de escopo; alvo é servidor comunitário HvH.
