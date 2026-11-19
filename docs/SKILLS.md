# SKILLS — o que foi usado, fonte e validação

| Skill | Autoridade | Como foi usada | Validada contra |
|---|---|---|---|
| `cs2-hvh-build` | arquitetura, slices, gates | árvore de módulos, contrato de slice no `PLAN.md`, regras de signature table, snapshot, wiring audit | este repo: `docs/PLAN.md`, `src/engine/*`, `src/sdk/snapshot.*` |
| `cs2-internal-rea` | RE e engine de CS2 | loop de REA para anchors; padrão de hook Present/Resize/WndProc; regra de schema em runtime; performance (zero heap por frame) | `docs/RE/findings.md` + `anchors.json` derivados desta build |
| `dx11-imgui-render` | render, menu, preview | 8/13 da vtable, estado salvo/restaurado, offscreen RT, armadilhas de versão do ImGui | `src/engine/render/dx11.cpp`, host com captura PNG |
| `impeccable` | direção visual | paleta, hierarquia, densidade, tooltips, temas | screenshots em `shots/` |
| `windows-internals` | loader e detecção | injeção LoadLibrary, WndProc, input blocking | `tools/loader`, `docs/DETECTION.md` |
| `cpp-testing` | harness | gtest + CTest, testes determinísticos | `tests/`, 37 testes |
| `verification-before-completion` | gate | gate 1 executado, demais reportados como não executados | `docs/REPORT.md` |
| `requesting-code-review` | revisão | revisão por área antes do release | pendente por área (registrado no relatório) |

## Lacunas tratadas

- **Layout do schema system em runtime**: sem skill dedicada; o fluxo
  `SchemaSystem_001 → TypeScope(module) → DeclaredClass → fields` foi derivado
  do build atual com RTTI + xrefs (ver `docs/RE/findings.md`).
- **Preview 3D com material do jogo**: a regra de "um material, dois
  consumidores" veio do `dx11-imgui-render`; a ligação com `cham_material` foi
  implementada em `src/ui/preview` + `src/features/chams`.

Skills de apoio carregadas quando a tarefa pediu: `codegraph` (navegação do
repo), `systematic-debugging` (captura PNG com pitch errado), `idapython`/
`re-x64dbg` (não necessárias — rizin + capstone cobriram o RE).
