# SHOGOD — superfícies de detecção

O alvo é servidor comunitário HvH. Plugins de servidor (e anticheats
comunitários) não veem a DLL; veem **comportamento e rede**. Esta lista mapeia
o que cada módulo expõe e como o toggle `misc.block_server_detected`
(`docs/PLAN.md`) deve agir.

## O que o servidor consegue observar

| Observável | Origem | Módulo que expõe | Mitigação no produto |
|---|---|---|---|
| Yaw/pitch fora do envelope do cliente | `CUserCmd` replicado | antiaim (desync/jitter/fake-static) | toggle por modo; `fake_static` e jitter com amplitude limitada; `on_shot` desliga o offset no tick do disparo |
| Cadência de ticks irregulares | fake lag / choke | ragebot (fake lag) | limite de choke configurável; desligar com `block_server_detected` |
| Headshot consistente em alvos não visíveis | hit registration | ragebot (aim points + penetration) | safe point e min-damage elevados; penetração desligável |
| Strafe perfeito / bhop sem perda | `CUserCmd` (movimento) | misc (bhop/auto-strafe) | desligar auto-strafe e edge-jump no modo seguro |
| Aceitação instantânea de partida | eventos de matchmaking | misc (auto-accept) | cosmético para o servidor, mas visível em logs de matchmaking |
| Compras/uso de faca em timing sobre-humano | eventos de dano | misc (knife-bot) | toggle próprio; dano calculado localmente |
| Precisão de granada idêntica ao line-up | trajetória replicada | misc (grenade line-up) | apenas assistência visual; sem alterar o cmd |
| Trace de visibilidade “limpo” demais para o ping | discrepância de hit | ragebot/backtrack | janela de backtrack limitada; validação de histórico no cliente |

## O que o servidor **não** vê

- Nome da DLL, módulos carregados, handles e threads (a menos que um plugin
  rode no cliente — fora do escopo de servidor comunitário).
- Overlay/menu: o Present é desenhado depois do frame do jogo e o input é
  engolido no `WndProc`; nada é replicado.
- Config e logs locais.

## Modo `block_server_detected`

Quando ligado, o produto deve:

1. desligar antiaim (ou reduzir a `off`), fake lag e auto-strafe;
2. subir `min_damage`/`safe_point` do ragebot para o preset safe;
3. desligar knife-bot, auto-pistol e edge-jump;
4. manter apenas ESP, chams e info (não observáveis pelo servidor).

O status no menu deve refletir os módulos parados (mesma tabela de anchors:
feature desligada aparece no header).

## Limites

- VAC e anticheats de cliente estão fora do escopo declarado do projeto; esta
  matriz cobre plugins de servidor comunitário e replay/estatística.
- Qualquer plugin que compare `CUserCmd` com o estado do servidor em tempo real
  enxerga antiaim e fake lag por definição — o toggle reduz, não elimina.
