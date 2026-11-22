# Tasks — movement and visibility polish

- [x] 1.1 Mover o cálculo de LOS para `collect_frame` (game thread) e expor no snapshot
- [x] 1.2 Ragebot e ESP consumirem o mesmo flag `visible`
- [x] 2.1 ESP: `visible_only`, caixa esmaecida para ocultos, setas fora de tela com distância
- [x] 2.2 Contadores `visible`/`arrows` no log de ESP
- [x] 3.1 Backtrack: escolher a pose mais antiga dentro da janela (rewind real)
- [x] 3.2 Atualizar o teste de backtrack para a nova semântica
- [x] 4.1 Auto-pistol com cadência de `m_flCycleTime` (schema) em vez de tick alternado
- [x] 4.2 Config + menu para `edge_jump`, com sonda de chão pelo trace
- [x] 5.1 Build Release + 41 testes verdes
- [x] 5.2 Verificação em partida: `cycle=0.170`, `enemies/visible/projected` no log
- [x] 6.1 Wallbang: filtro que ignora o alvo (vtable própria) + medição de espessura por dois traces
- [x] 6.2 Controle "Wallbang" + limite de espessura por arma no menu
- [x] 6.3 Verificação em partida: `wallbang: near=0.111 far=0.099 thickness=2913.9 allow=0` (nega parede grossa, sem crash)
