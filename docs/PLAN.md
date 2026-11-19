# SHOGOD — Plano de construção

Estado do ambiente (inspeção inicial, 2026-11-19):

| Item | Valor |
|---|---|
| Repo | `C:\Users\User\Documents\shogod` (apenas o master prompt no início) |
| Jogo | `C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive` |
| Build | `game/csgo/bin/win64/client.dll` 39.141.528 B, sha1 `4E16EA32EA9485A25A67933ACD5A8B4870D855D1` (csgo.signatures) |
| Módulos | `client.dll`, `server.dll`, `host.dll`, `matchmaking.dll` em `game/csgo/bin/win64`; `schemasystem.dll`, `tier0.dll` em `game/bin/win64` |
| MSVC | VS 18 Community, MSVC 14.51.36231, SDK 10.0.26100.0 |
| CMake | VS-bundled (`Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe`), generator `Visual Studio 18 2026` |
| CMake/Ninja standalone | ausentes — o build usa o CMake do VS |
| Rede | disponível (vendor clonado no setup, pins gravados em `vendor/PINS.txt`) |

## 1. Árvore de módulos (dependência unidirecional)

```
engine/    hooks, loader, log, pattern, signatures   -> sdk/
sdk/       interfaces, schema, entity, snapshot, trace -> features/
features/  math, rage, aaa, backtrack, pen, esp, chams, misc -> ui/
ui/        menu, style, pages, preview                -> config/
config/    serialize, migrate, profiles
brand.hpp  strings visíveis (fonte única)
```

Regras: `features/` nunca inclui `ui/`; `sdk/` nunca inclui `features/`; `engine/` não
conhece features. Violação = review-blocking.

## 2. Slices (dono exclusivo de arquivos)

| # | Slice | Owns | Consome | Expõe | Aceite observável |
|---|---|---|---|---|---|
| S1 | foundation | `CMakeLists.txt`, `vendor/`, `src/brand.hpp`, `src/engine/log.*`, `tests/CMakeLists.txt` | — | `log::`, `brand::`, build targets | `cmake --build` Release x64 limpo; teste de log |
| S2 | math | `src/features/math/*` | — | `vec3`, `mat4`, `angles`, `w2s`, `hull`, `hitchance` | teste unidade: W2S matriz à mão, normalização ±180, hull 0/100% |
| S3 | scanner | `src/engine/pattern.*`, `src/engine/signatures.*` | S1 log | `pattern::find`, `signatures::resolve_all` | teste: match conhecido em buffer sintético; log OK/FAIL por anchor |
| S4 | RE | `docs/RE/*` | REA | `anchors.json` (name, pattern, resolve, rva, validation) | findings com evidence ID por anchor, pattern único |
| S5 | sdk | `src/sdk/interfaces.*`, `schema.*`, `entity.*`, `snapshot.*`, `trace.*` | S3, S4 | `interfaces::`, `schema::offset`, `entity_view`, `frame_snapshot` | compila; offsets resolvidos por nome; snapshot fixo sem alocação |
| S6 | engine/hooks | `src/engine/hooks.*`, `loader.*`, `input.*` | S3, S5 | `hooks::install`, `hooks::uninstall` (ordem inversa) | init loga anchors; unload restaura ponteiros |
| S7 | ragebot | `src/features/rage/*` | S2, S5 | `rage::run(frame)` | alvo escolhido por prioridade, hitchance e min-damage aplicados |
| S8 | aaa | `src/features/aaa/*` | S2, S5 | `aaa::run(cmd)`, `resolver::log` | yaw/pitch aplicados por estado; log da heurística |
| S9 | backtrack/pen | `src/features/backtrack.*`, `pen.*` | S2, S5 | `backtrack::history`, `pen::damage` | histórico validado; ponto penetrável escolhido |
| S10 | visuals | `src/features/esp/*`, `chams/*` | S2, S5, S6 | `esp::draw`, `chams::materials` | ESP alinhado 1080p/1440p; chams com flags |
| S11 | preview | `src/ui/preview/*` | S10 (mesmo `cham_material`) | `preview::draw` | órbita/zoom, reagindo a cor/alpha/wireframe |
| S12 | misc | `src/features/misc/*` | S2, S5 | `misc::run` | bhop/auto-accept/third-person com efeito real |
| S13 | config | `src/config/*` | S1 | `config::load/save/migrate` | round-trip com chave desconhecida preservada |
| S14 | menu shell | `src/ui/menu.*`, `style.*` | S13 | `ui::draw` | 5 abas, header com status, sem clipping em 1080p |
| S15 | menu pages | `src/ui/pages/*` | S14, S13 | páginas | wiring audit: todo item nomeia consumidor |
| S16 | harness | `tests/*` | S1–S13 | gtest/CTest | gate 1 verde |
| S17 | integração | build completo, suite, gates 2–5 | todas | relatório | `docs/REPORT.md` com evidência |

Ordem: S1 → (S2, S3, S4 em paralelo) → S5 → (S6, S13 em paralelo) →
(S7, S8, S9, S10, S12 em paralelo) → S11 → (S14, S15) → S16 → S17.
Dono de integração: S17, nunca paralelo ao que integra.

## 3. Anchors (preenchido por RE desta build — ver `docs/RE/findings.md`)

| Anchor | Resolve | Validação no init | Status |
|---|---|---|---|
| `CGameEntitySystem` | rip/abs → object | ponteiro não nulo + vtable de classe esperada | RE |
| `view_matrix` | rip global | 16 floats finitos, base do frame | RE |
| `CCSGOInput::CreateMove` | vtable/pattern | leitura do input global + cmd consistente | RE |
| `CGameTraceManager` | rip → object | trace de teste retorna fração 0..1 | RE |
| `GlobalVars` | rip global | `interval_per_tick` em (0, 0.1] | RE |
| bone array | `m_modelState + 0x80`, count `+0x8c` | bone_count em (0, 32] | RE |
| `SchemaSystem_001` | CreateInterface export | TypeScope("client.dll") resolve classe conhecida | RE |
| weapon vdata | schema por nome | dano/alcance > 0 | RE |

Offset inventado é proibido. Anchor que falha: feature desliga, log `FAIL`, status no menu.

## 4. Contratos de threading

`CreateMove` (game thread) publica, `Present` (render thread) lê. Snapshot fixo
64 players × 32 bones, ponteiro atômico, `seq` ímpar = frame descartado.
Zero heap por frame após init. Nenhum mutex em `Present`/`CreateMove`.

## 5. Gates

1. Unidade (scanner em buffer sintético, W2S à mão, ângulos, hull 0/100, config round-trip com chave desconhecida).
2. Init em partida offline com bots: anchors `OK`, feature set no log.
3. 10 min in-game: ESP alinhado 1080p e 1440p, sem crash, menu sem vazar input, config persiste, preview reage.
4. Soak 2 h, reinjeção, unload restaura hooks em ordem inversa.
5. Screenshots: 5 abas, preview com chams+skeleton, ESP, hit log.

Gate não executado é reportado como não executado.

## 6. Riscos conhecidos

- `client.dll` de 39 MB: auto-análise do Ghidra leva minutos; RE roda em sessão única.
- Assinatura quebrada por patch da Valve: playbook em `cs2-hvh-build` (re-derivar, nunca copiar).
- HvH em servidor comunitário: features detectáveis por plugin têm toggle e entrada em `docs/DETECTION.md`.
- Anticheat (VAC) fora de escopo de bypass; o alvo do produto é servidor comunitário HvH.
