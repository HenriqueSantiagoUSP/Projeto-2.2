# firmware/room — hub do quarto

ESP32-S3 fixo no quarto, alimentado pela tomada. Ele:
- lê as portas, a presença (PIR) e a temperatura (LM35);
- roda as regras de alerta;
- recebe a pulseira por ESP-NOW;
- publica tudo por MQTT;
- conversa com os módulos da sala por CAN.

O desenho está em [`docs/arquitetura.md`](../../docs/arquitetura.md), as mensagens em
[`docs/protocolo.md`](../../docs/protocolo.md) e o que já foi verificado em [`docs/estado.md`](../../docs/estado.md).
O código segue os slides de [`aulas/`](../../aulas/README.md) e a documentação do ESP-IDF v6.0.1: tasks com
prioridade nomeada e núcleo fixo, ISR que só enfileira, filtro de glitch nas entradas, ADC calibrado, CAN com
recuperação de *bus-off*.

## Configurar

Tudo o que muda de uma instalação para outra está em `idf.py menuconfig`. Fica no `sdkconfig`, que não vai
para o git. Os três primeiros menus vêm de `firmware/common/` e são os mesmas na pulseira:

| Menu | O quê |
|---|---|
| Network (Wi-Fi) | SSID e senha do Wi-Fi; servidor SNTP; fuso (padrão: Brasília) |
| MQTT | URI do broker, usuário `hub` e a senha do `interface/.env`, site e quarto do tópico |
| CAN bus (TWAI) | modo de bancada (desligue no barramento real da sala) |
| Room hub: ESP-NOW | MAC da pulseira pareada, PMK e LMK (as mesmas da pulseira) |
| Room hub: sensors | o que está instalado: PIR, LM35 e as portas ausentes (abaixo) |
| PCB | versão da DevKitC-1 (LED de status no GPIO48 na v1.0, no GPIO38 na v1.1) |

Sem SSID, o hub fica só com ESP-NOW num canal fixo, o que basta para testar a pulseira sem roteador. O MAC do
hub, que a pulseira precisa, aparece no log de boot (`hub MAC aa:bb:…`). Antes de gravar, ele também sai com
`python -m esptool --port COMx read-mac`.

### Sem alguma peça

O hub continua funcionando sem qualquer peça. Cada peça ausente dá **um** aviso (W) quando é percebida e uma
linha (I) quando volta, e as novas tentativas ficam em silêncio:

| Situação | O que acontece |
|---|---|
| Wi-Fi fora do ar ou senha errada | aviso uma vez, com o motivo; novas tentativas a cada até 60 s. O MQTT só começa depois que a rede sobe |
| Broker fora do ar | aviso uma vez por queda de conexão (usuário/senha recusados são erro: configuração errada). As mensagens QoS 1 esperam no outbox |
| CAN sem barramento (modo bancada desligado) | aviso uma vez ("CAN bus not connected"); novas tentativas a cada até 60 s |
| LM35 solto | aviso uma vez ("temperature sensor not connected"); nada é publicado, e a regra de temperatura não dispara |
| Pulseira configurada e ausente | o alerta `pulseira_offline` depois de 90 s, de propósito. Na bancada sem pulseira, deixe o MAC vazio |

**PIR e portas soltos não dão para detectar:** um PIR solto lê "ninguém" e, de dia, gera o alerta de
inatividade; uma porta solta lê "aberta" e, à noite, gera o de saída noturna. Declare-os em **Room hub:
sensors**:
- `HC-SR501 presence sensor (PIR) fitted` desligado: sem presença, e as regras de inatividade e de saída noturna
  ficam suspensas;
- `LM35 temperature sensor fitted` desligado: sem temperatura;
- `Doors not fitted`: os ids de `BOARD_DOORS` que não estão instalados, separados por vírgula (ex.: `banheiro`).

Peça declarada ausente não é lida, publicada nem enviada ao CAN, e o boot avisa uma vez. As outras portas mantêm
o índice e o id CAN.

Pinos: a PCB é a mesma da pulseira, e o mapa está em `firmware/common/pcb/include/pcb_pins.h`
([`hardware/README.md`](../../hardware/README.md)); o `components/board/include/board_pins.h` escolhe o que o hub
usa. Para outra porta (a placa tem 4 entradas e mais 4 na expansão), acrescente uma linha em
`BOARD_DOORS` (até 8: uma por nibble CAN). Os parâmetros das regras (tempos, faixa de temperatura, papel de cada
porta) **não** ficam aqui: chegam por MQTT em `config/desejada` e são guardados em NVS (`interface/README.md`,
seção 5).

## Demonstração: hub só com o PIR

Para mostrar a pulseira chegando ao hub por ESP-NOW e o hub publicando na central, com uma DevKit que só tem o PIR
(HC-SR501: VCC no 5V, GND e OUT no GPIO 8). O roteiro completo da apresentação do Q3 está em
[`relatorios/Semestre 2/Projeto 3/Q3/roteiro-demonstracao.md`](<../../relatorios/Semestre 2/Projeto 3/Q3/roteiro-demonstracao.md>).

1. **Perfil** [`sdkconfig.demo.defaults`](sdkconfig.demo.defaults):
   - o PIR fica ligado;
   - o LM35 e as duas portas ficam declarados ausentes (um aviso de cada no boot, e nada mais);
   - o CAN fica em modo bancada: sem transceptor, cada mudança de presença volta como quadro CAN no log.

   Ele fica numa pasta de build própria, sem mexer no `sdkconfig` do dia a dia.
2. **`menuconfig` nessa mesma pasta:**

   ```powershell
   idf.py -B build/demo -D SDKCONFIG=build/demo/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.demo.defaults" menuconfig
   ```

   Preencha:
   - **Network (Wi-Fi):** SSID e senha de uma rede de 2,4 GHz. O hotspot do próprio PC é o mais seguro, porque o
     PC fica sempre em `192.168.137.1`;
   - **MQTT:** `mqtt://<IP do PC>:1883`, usuário `hub`, senha = `MQTT_HUB_PASSWORD` do `interface/.env`;
   - **Room hub: ESP-NOW:**
     - o MAC da pulseira (`wearable …, MAC …` no boot dela, ou `esptool read-mac`);
     - PMK e LMK de 16 caracteres, as mesmas da pulseira;
   - **PCB:** a versão da DevKit, para o LED.
3. **Gravar e abrir o monitor**, com o mesmo `-B build/demo`:

   ```powershell
   idf.py -B build/demo -D SDKCONFIG=build/demo/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.demo.defaults" -p COMx build flash monitor
   ```

   No boot devem aparecer:
   - `hub MAC …`;
   - `paired with …, encrypted`;
   - `connected, ip …, channel N`;
   - `connected to mqtt://…`;
   - os avisos esperados: `temperature sensor not fitted` e `door … not fitted`.

   O PIR começa a valer depois de 60 s de aquecimento.
4. **Com a pulseira ligada** (perfil em [`firmware/gadget/README.md`](../gadget/README.md), "Demonstração com o hub")
   o hub registra:
   - `PING from <id>: the wearable found the hub`, quando ela acha o canal;
   - `heartbeat from <id>: rssi … dBm` a cada 30 s;
   - `FALL from <id>`, `HELP from <id>` e o `alert …` de cada um.

| Sintoma | Causa provável |
|---|---|
| A pulseira diz `hub found on channel N`, mas o hub não mostra nada | PMK ou LMK diferentes nas duas placas, ou o MAC da pulseira errado no hub: o rádio do hub pode confirmar o quadro e descartá-lo sem avisar |
| `frames from … ignored: only the paired wearable is accepted` | o MAC da pulseira no `menuconfig` do hub não é o dela |
| A pulseira diz `hub not found on any channel` | hub desligado, MAC do hub errado na pulseira, ou a rede no canal 12 ou 13: sem o país configurado, a pulseira só procura do 1 ao 11. O canal sai na linha `connected, ip …, channel N` do hub |
| `broker refused the connection` | usuário ou senha do broker |
| `broker … not reachable` | IP errado na URI, a central parada, ou o firewall do PC (`interface/README.md`, seção 4) |

## Componentes

Próprios do hub, em `components/`: `board`, `sensors`, `rules`, `hub_config`, `espnow_link`. Comuns às duas
placas, em [`firmware/common/`](../common/README.md): `pcb`, `protocol`, `rtos_util`, `sys_events`, `net`,
`mqtt_link`, `can`, `indicators`.

| Task | Núcleo | Prioridade | Função |
|---|---|---|---|
| `sensors_task` | 1 | ALTA | estabiliza e lê portas e PIR |
| `lm35_task` | 1 | BAIXA | temperatura a cada 60 s; a `hub_task` só a publica quando muda 0,2 °C ou a cada 5 min |
| `hub_task` | 0 | MEDIA | regras, publicação, LED (só ela publica e chama as regras) |
| `espnow_task` | 0 | MEDIA | valida as mensagens da pulseira |
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
