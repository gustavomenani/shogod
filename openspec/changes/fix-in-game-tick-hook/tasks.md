# Tasks — in-game tick pipeline

## 1. Hook point
- [x] 1.1 Instrumentar `client.dll` com Frida e provar que `vtable[7]` (`0xb781e0`) não roda em partida (0 hits / 7 s)
- [x] 1.2 Provar que `vtable[4]` (`0xcfd7c0`) roda por tick e deixa `count 0→1` com ângulos válidos
- [x] 1.3 Trocar o hook para `vtable[4]` pós-original e remover o hook de `vtable[7]` do install/uninstall
- [x] 1.4 Confirmar em partida: `cm: total` cresce, `players>0`, `bones=32`, `wep=<arma>`

## 2. Snapshot e bones
- [x] 2.1 Ler contagem de bones de `m_modelState+0x94` (capacidade `+0x90`) e manter stride `0x20`
- [x] 2.2 Diagnóstico de falha (`ms_ptr/ms_cnt/ms_stage`) no log de 2 s
- [x] 2.3 Mapa de bones CS2 verificado em dois modelos (`src/sdk/bones.hpp`)

## 3. Comando
- [x] 3.1 `cmd+0x00` = máscara held (fórmula do builder), `+0x08` pressed, `+0x10` released
- [x] 3.2 bhop limpa `IN_JUMP` em `+0x00` (verificado: `btn0 2 → btn0_after 0`)
- [x] 3.3 auto-stop/crouch/scope só quando o disparo é concedido (`will_fire`)
- [x] 3.4 `forwardmove/sidemove` em `+0x18/+0x1c`

## 4. Visibilidade (auto-wall)
- [x] 4.1 Derivar `CGamePhysicsQueryInterface::TraceShape` (ctx `0x221ab18`, fn `0xa15360`)
- [x] 4.2 Descobrir o filtro mínimo: vtable `0x1ad05d8` + `+0x08 = 0x18000` + `+0x30 = 0x0F00FFFF00000000`
- [x] 4.3 Implementar `sdk::trace_blocked` e usar como gate do ragebot (self-test: raio para baixo bloqueia)
- [x] 4.4 Verificar em partida: candidatos atrás de parede viram `blocked`, `picked=-1`

## 5. Verificação
- [x] 5.1 Build Release x64 limpo e 41 testes verdes
- [x] 5.2 Disparo ao vivo: `rage: picked fire=1` + `cmd_buttons=0x1` + munição caindo (13→12)
- [x] 5.3 Screenshot final com ESP + skeleton em inimigo visível (`shots/verify/crop1.png`)
