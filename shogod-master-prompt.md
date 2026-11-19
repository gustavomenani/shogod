# PROJETO: SHOGOD — cheat interno para CS2 (HvH)

## 0. MISSÃO

Construa, do zero, o **Shogod**: um cheat interno para Counter-Strike 2
(`game/csgo/bin/win64/client.dll`), voltado a HvH em servidores comunitários.
Referência de produto: Neverlose, Gamesense, Fatality — menu customizado,
módulos completos, zero placeholder. O resultado é um repositório compilando,
verificado em partida offline com bots e apresentado com evidência de teste.

Shogod não é protótipo. Feature que aparece no menu existe e funciona no jogo, ou
não aparece. Nenhum "MVP", nenhum TODO, nenhum stub, nenhuma opção decorativa.

### Identidade fixa do produto (usar exatamente)

| Onde | Valor |
|---|---|
| Repositório / solução | `shogod` |
| DLL | `shogod.dll` |
| Namespace C++ | `shogod::` |
| Header do menu | `SHOGOD` |
| Watermark in-game | `shogod` |
| Versão | `shogod vX.Y.Z` |
| Configs | `%APPDATA%\shogod\configs\*.json` |
| Logs | `%APPDATA%\shogod\logs\shogod-YYYYMMDD.log` |
| Prefixo de log | `[shogod]` |

Toda string visível sai do mesmo lugar (`brand.hpp`), sem repetição espalhada.

---

## 1. PLANO ANTES DE CÓDIGO (obrigatório)

1. Inspecione o ambiente: repo existente, solução VS, SDK vendorizado, build de
   referência do jogo. Liste o que existe antes de assumir qualquer coisa.
2. Abra `todo` com fases; cada módulo vira item rastreável.
3. Escreva `docs/PLAN.md` com:
   - árvore de módulos e dependência unidirecional
     (`engine → sdk → features → ui → config`, conforme `cs2-hvh-build`);
   - tabela de slices: dono exclusivo de arquivos, contratos consumidos e
     expostos, critério de aceite observável;
   - tabela de anchors (nome, pattern, resolve `rip`/`abs`/`vtable`, RVA,
     readback de validação) — preenchida por RE desta build, nunca copiada;
   - ordem de execução, o que roda em paralelo, quem é o dono da integração.
4. Só então implemente. Plano mudou → atualiza o arquivo e explica em uma linha.

## 2. SKILLS — LEIA ANTES DE USAR, UMA AUTORIDADE POR ÁREA

**Núcleo (obrigatórias, ler primeiro):**
- `skill://cs2-internal-rea` — anchors em `client.dll`, schema runtime, hooks,
  performance, verificação pós-patch. Autoridade em RE e engine de CS2.
- `skill://cs2-hvh-build` — módulos, contrato de slice, snapshot/threading,
  signature table, config, wiring audit, gates, playbook de patch. Autoridade
  em arquitetura e definição de pronto.
- `skill://dx11-imgui-render` — Present/ResizeBuffers, restauração de estado,
  render target offscreen, paridade de material, armadilhas do ImGui, device
  loss. Autoridade em render, menu e preview 3D.

**Apoio (carregar só quando a tarefa pede):**
- `cpp-core-guidelines` — review de C++ e decisões de ownership/RAII. Único
  padrão de C++ do projeto (não usar `cpp-coding-standards` em paralelo).
- `cpp-testing` — harness gtest/CTest, sanitizers, flaky.
- `windows-internals` — loader, ordem de carga de DLL, modelo de token,
  superfícies de detecção. Para o loader e o `docs/DETECTION.md`.
- `memory-scanning` / `cheat-engine-cli` — confirmar valor/estrutura em runtime
  antes de virar hipótese (scan de HP, arma, view matrix). Nunca substitui RE.
- `idapython` e `re-x64dbg` — quando o REA não bastar: script de IDA ou
  breakpoint em `x64dbg` para confirmar fluxo. Fallback, não caminho principal.
- `windbg-diagnostic-method` — dump de crash da DLL injetada; análise de
  access violation com evidência.
- `code-obfuscation-deobfuscation` / `unpacking-protected-binaries` — só se
  alguma proteção aparecer no caminho; `client.dll` não é protegida.
- `systematic-debugging` — qualquer bug: reprodução antes do fix.
- `verification-before-completion` — nada é "pronto" sem prova executada.
- `requesting-code-review` — review independente por área antes do merge.
- `subagent-driven-development` / `dispatching-parallel-agents` — orquestração.
- `impeccable` — direção visual do menu (hierarquia, densidade, tipografia).

**Lacuna de skill:** se faltar procedimento (ex.: resource system de modelo,
antiaim math, heurística de resolver), use `find-skills` e, se não houver,
`web_search` para localizar skill ou documentação oficial. Skill da internet
entra como procedimento a validar contra a build, nunca como código cego.
Registre tudo em `docs/SKILLS.md`: o que usou, fonte, e o que validou.

## 3. ESCOPO FUNCIONAL — TUDO ENTRA

### 3.1 RageBot

- Seleção de alvo: FOV por arma, distância, prioridade (distância / menor HP /
  ameaça / cycling), ignore-limbs opcional.
- Aim points: head, neck, chest, stomach, pelvis, legs + multi-point com
  percentual de safe point e escala de hull.
- Auto-fire com hitchance spread-aware, min-damage por arma, auto-scope,
  auto-stop/auto-crouch, delay por tick.
- Penetration: dano através de parede, escolha de ponto penetrável, limite
  configurável.
- Backtrack: janela em ticks/latência, validação de histórico — não atirar em
  posição reconstruída inválida; chams de backtrack ligados ao mesmo histórico.
- Antiaim: yaw/pitch base (desync, jitter, random, fake-static), condicionais
  por estado (standing/moving/air, fake-walk, on-shot), integrado ao fake-lag.
- Resolver: heurísticas por histórico de ângulo e desync, por arma/estado, com
  log de qual heurística disparou por alvo em modo debug.
- Perfis "safe" e "rage" trocando min-damage/hitchance/pontos.

### 3.2 Visuals

- ESP: box (2D/3D/corner), health com gradiente, armor, name, weapon, ammo,
  flags (scoped, defusing, flashed, reloading, kit), distance, skeleton
  (bones do `m_modelState`), glow, chams (player, viewmodel, arms, weapon;
  flags ignore-z/wireframe/flat/overlay), backtrack ghost, hitmarker, damage
  indicator direcional, bullet tracer, spectator list.
- World: bomb timer/site, grenade helper com predição de trajetória
  (smoke/molotov/flash), projectile ESP, dropped weapons, radar hack, remoção
  de flash/smoke/scope overlay, world modulation.
- **PREVIEW 3D (obrigatório):** player model em target DX11 offscreen exibido
  por `ImGui::Image`, conforme `dx11-imgui-render`:
  - órbita/zoom por drag e scroll, reset por duplo-clique;
  - fundo neutro com grade, iluminação simples, contato/sombra;
  - os MESMOS `cham_material` do jogo aplicados ao modelo, reagindo em tempo
    real a cor, alpha, flags, wireframe e ignore-z;
  - overlays ligados ao estado real: skeleton, pontos de aim, hull de
    multi-point — projetados pela mesma câmera do mesh;
  - um único sistema de material, dois consumidores. Sem lógica duplicada.

### 3.3 Misc

- Movement: bhop com strafe assist, auto-strafe, edge-jump, fast-ladder, slide.
- Utilities: knife-bot com dano calculado, auto-accept, auto-pistol, grenade
  line-up, fake-duck, third-person com modelo carregado, free-look.
- Info: match info, watermark `shogod`, lista de keybinds, hit log com
  dano/hitgroup/resolver.
- Toggles para desligar features que plugins de servidor comunitário detectam;
  documentados em `docs/DETECTION.md`.

### 3.4 Config e Menu

- Config: structs versionadas, JSON, import/export, preservação de chave
  desconhecida, migração, clamp com log, perfis por estilo de jogo.
- Menu: ImGui estilizado do zero. Sidebar (RageBot, Visuals, Misc, Config,
  Settings), header `SHOGOD` com status vivo (FPS, tick, jogadores, saúde dos
  anchors), transição animada de aba, abertura/fechamento animados, tooltip em
  todo controle não óbvio, fontes de arquivo versionado, tema claro/escuro com
  acento configurável, nada cortado em 1080p.
- Input: menu aberto bloqueia input do jogo no `WndProc`; keybinds por feature
  (toggle/hold) salvos no config.

### 3.5 Engine (base)

- Injeção: DLL loader próprio; manual-map opcional documentado; unload limpo
  restaurando hooks em ordem inversa.
- Hooks: `Present` + `ResizeBuffers` (dummy swapchain, ImGui DX11),
  `CCSGOInput::CreateMove`, `WndProc` via `SetWindowLongPtrW`.
- SDK: interfaces via `CreateInterface`; schema system runtime
  (`SchemaSystem_001`) para todo netvar — offset hardcoded proibido onde o
  schema resolve; bone array `m_modelState + 0x80` count `+0x8c`; view matrix;
  `CGameTraceManager` para visibilidade/trigger.
- Signature table com resolve explícito, validação no init, log OK/FAIL por
  anchor; anchor falhou → feature desliga e aparece no status do menu.

## 4. PADRÃO DE ENGENHARIA (ANTI-BUG)

- C++20, MSVC, x64, Release `/O2`, libs vendorizadas e pinadas.
- Zero heap por frame após init; snapshot fixo de 64 players × 32 bones; um
  scan de entidade por frame; bones uma vez por frame; nada de mutex em
  `Present`/`CreateMove`; publicação por atomic pointer/seqlock; rejeitar
  matriz não-finita e `w <= 0.001`.
- Validar ponteiro de entidade antes de cada deref (mapa muda, player sai).
- `ResizeBuffers` libera view própria antes de chamar original.
- Todo acesso ao jogo passa por camada de validação (nulo, range, tipo).
  Crash = bug bloqueante.
- **Wiring audit**: cada item do menu nomeia a função que o consome. Item sem
  consumidor ou consumidor sem item = slice não terminada.
- Logging com níveis; modo debug expõe anchors e decisão do resolver.

## 5. ORQUESTRAÇÃO — 20+ SUBAGENTS, CONTRATO PRIMEIRO

- Slices com dono exclusivo de arquivo e contrato preenchido (formato em
  `cs2-hvh-build`): objetivo, arquivos, interfaces, aceite, proibições.
- Slices independentes em paralelo no mesmo batch; só sequencial o que tem
  dependência dura. Um único dono de integração faz o build completo e a suite.
- Seis slices, no mínimo, com revisão independente: ragebot, antiaim/resolver,
  render/preview, menu, engine/hooks, config.
- Subagent travado não vira fatia faltando: redivida, mude a estratégia,
  derive o anchor por outro caminho.

## 6. GATES DE ACEITE (nesta ordem)

1. **Unidade** — scanner em buffer sintético com match conhecido; W2S contra
   matriz calculada à mão; normalização de ângulo; hull 0%/100%; config
   round-trip com chave desconhecida.
2. **Init** — injetar em partida offline com bots; os anchors logam OK;
   feature set reportado. FAIL explicado no log.
3. **In-game** — 10 minutos offline: ESP alinhado em 1080p e 1440p; sem crash;
   menu abre/fecha sem vazar input; config salva e recarrega; preview 3D reage
   aos sliders de material.
4. **Soak** — 2 horas, reinjeção repetida, unload restaura hooks; nenhum módulo
   listado após unload manual-map.
5. **Prova visual** — screenshots: 5 abas do menu, preview com chams + skeleton,
   ESP em jogo, hit log.

Gate que não rodou é reportado como não rodou. Nunca como aprovado.

## 7. ENTREGÁVEIS

1. Repositório `shogod` compilando em Release x64 sem erro.
2. `docs/PLAN.md`, `docs/RE/findings.md` (anchors + signatures + build
   identity), `docs/SKILLS.md`, `docs/DETECTION.md`, `docs/USAGE.md`.
3. Signature table validada com log de init.
4. Screenshots do gate 5.
5. Relatório final: o que funciona com evidência, o que foi desligado por
   anchor e por quê (com log), e como re-derivar após patch da Valve.

## 8. REGRAS DURAS

- Não pare até compilar, testar e provar. Resposta parcial não é entrega.
- Não invente offset. Todo número vem de RE desta build, com signature e local
  de verificação registrados.
- Não existe "depois eu ligo essa opção". Ou está ligada em código até o efeito
  no jogo, ou não existe no menu.
- Antes de dizer pronto: rode o gate e mostre o resultado. Só então declare
  conclusão.
