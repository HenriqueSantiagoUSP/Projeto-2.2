# firmware/room — hub do quarto

ESP32-S3 fixo no quarto, alimentado pela tomada. Ele:
- lê as portas, a presença (PIR) e a temperatura (LM35);
- roda as regras de alerta;
- recebe o vestível por ESP-NOW;
- publica tudo por MQTT;
- conversa com os módulos da sala por CAN.

O desenho está em [`docs/arquitetura.md`](../../docs/arquitetura.md), as mensagens em
[`docs/protocolo.md`](../../docs/protocolo.md) e o que já foi verificado em [`docs/estado.md`](../../docs/estado.md).
O código segue os slides de [`aulas/`](../../aulas/README.md) e a documentação do ESP-IDF v6.0.1: tasks com
prioridade nomeada e núcleo fixo, ISR que só enfileira, filtro de glitch nas entradas, ADC calibrado, CAN com
recuperação de *bus-off*.

## Configurar

Tudo o que muda de uma instalação para outra está em `idf.py menuconfig`. Fica no `sdkconfig`, que não vai
para o git. Os três primeiros menus vêm de `firmware/common/` e são os mesmos no vestível:

| Menu | O quê |
|---|---|
| Network (Wi-Fi) | SSID e senha do Wi-Fi; servidor SNTP; fuso (padrão: Brasília) |
| MQTT | URI do broker, usuário `hub` e a senha do `interface/.env`, site e quarto do tópico |
| CAN bus (TWAI) | modo de bancada (desligue no barramento real da sala) |
| Room hub: ESP-NOW | MAC do vestível pareado, PMK e LMK (as mesmas do vestível) |
| Room hub: sensors | o que está instalado: PIR, LM35 e as portas ausentes (abaixo) |
| PCB | versão da DevKitC-1 (LED de status no GPIO48 na v1.0, no GPIO38 na v1.1) |

Sem SSID, o hub fica só com ESP-NOW num canal fixo, o que basta para testar o vestível sem roteador. O MAC do
hub, que o vestível precisa, aparece no log de boot.

### Sem alguma peça

O hub continua funcionando sem qualquer peça. Cada peça ausente dá **um** aviso (W) quando é percebida e uma
linha (I) quando volta, e as novas tentativas ficam em silêncio:

| Situação | O que acontece |
|---|---|
| Wi-Fi fora do ar ou senha errada | aviso uma vez, com o motivo; novas tentativas a cada até 60 s. O MQTT só começa depois que a rede sobe |
| Broker fora do ar | aviso uma vez por queda de conexão (usuário/senha recusados são erro: configuração errada). As mensagens QoS 1 esperam no outbox |
| CAN sem barramento (modo bancada desligado) | aviso uma vez ("CAN bus not connected"); novas tentativas a cada até 60 s |
| LM35 solto | aviso uma vez ("temperature sensor not connected"); nada é publicado, e a regra de temperatura não dispara |
| Vestível configurado e ausente | o alerta `vestivel_offline` depois de 90 s, de propósito. Na bancada sem vestível, deixe o MAC vazio |

**PIR e portas soltos não dão para detectar:** um PIR solto lê "ninguém" e, de dia, gera o alerta de
inatividade; uma porta solta lê "aberta" e, à noite, gera o de saída noturna. Declare-os em **Room hub:
sensors**:
- `HC-SR501 presence sensor (PIR) fitted` desligado: sem presença, e as regras de inatividade e de saída noturna
  ficam suspensas;
- `LM35 temperature sensor fitted` desligado: sem temperatura;
- `Doors not fitted`: os ids de `BOARD_DOORS` que não estão instalados, separados por vírgula (ex.: `banheiro`).

Peça declarada ausente não é lida, publicada nem enviada ao CAN, e o boot avisa uma vez. As outras portas mantêm
o índice e o id CAN.

Pinos: a PCB é a mesma do vestível, e o mapa está em `firmware/common/pcb/include/pcb_pins.h`
([`hardware/README.md`](../../hardware/README.md)); o `components/board/include/board_pins.h` escolhe o que o hub
usa. Para outra porta (a placa tem 4 entradas e mais 4 na expansão), acrescente uma linha em
`BOARD_DOORS` (até 8: uma por nibble CAN). Os parâmetros das regras (tempos, faixa de temperatura, papel de cada
porta) **não** ficam aqui: chegam por MQTT em `config/desejada` e são guardados em NVS (`interface/README.md`,
seção 5).

## Componentes

Próprios do hub, em `components/`: `board`, `sensors`, `rules`, `hub_config`, `espnow_link`. Comuns às duas
placas, em [`firmware/common/`](../common/README.md): `pcb`, `protocol`, `rtos_util`, `sys_events`, `net`,
`mqtt_link`, `can`, `indicators`.

| Task | Núcleo | Prioridade | Função |
|---|---|---|---|
| `sensors_task` | 1 | ALTA | estabiliza e lê portas e PIR |
| `lm35_task` | 1 | BAIXA | temperatura a cada 30 s |
| `hub_task` | 0 | MEDIA | regras, publicação, LED (só ela publica e chama as regras) |
| `espnow_task` | 0 | MEDIA | valida as mensagens do vestível |
| `can_rx`, `can_tx` | 0 | MEDIA | CAN: recepção; transmissão e estado do nó |
| `indicators` | 0 | MINIMA | LED |

## Compilar e gravar

```powershell
. C:\Espressif\tools\Microsoft.v6.0.1.PowerShell_profile.ps1
idf.py build
idf.py -p COMx flash monitor
```

## Testes no PC

A lógica das regras e a validação da config rodam no PC, sem placa (Git Bash, com o gcc do MSYS2 no PATH):

```bash
sh components/rules/test_host/run_tests.sh
sh components/hub_config/test_host/run_tests.sh   # precisa de um idf.py build antes (baixa o cJSON)
```
