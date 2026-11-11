# Protocolo — contrato entre placas e interface

**Fonte única da verdade.** Todo tópico, payload, ID CAN ou mensagem ESP-NOW trocada entre o vestível, o hub do
quarto, o barramento CAN e a central é definido aqui. Mudou o contrato? Atualize este arquivo **no mesmo
commit** que altera o código.

Cada item indica o **estado**: *implementado* (existe no código e compila, ainda não verificado em placa),
*testado* (verificado em bancada) ou *proposto* (definido aqui, ainda sem código). O que foi verificado e como
está em [`estado.md`](estado.md).

```
                ESP-NOW (criptografado, ACK + reenvio)       MQTT/TCP 1883               ┌─▶ Telegraf ─▶ InfluxDB ─▶ Grafana
Vestível ─────────────────────────────────────────▶ Hub ─────────────────────▶ Mosquitto ─┼─▶ notificador ─▶ Telegram
                                                     │                                    └─▶ (futuro) backend, nuvem
                                                     └── CAN 250 kbit/s ◀──▶ demais módulos da sala
```

---

## 1. MQTT — interface aberta (hub → broker → consumidores)

Estado: **implementado** no firmware do hub (`firmware/common/mqtt_link`, `firmware/room/main/main.c`), no
vestível como opção desligada por padrão (`firmware/gadget/main/main.c`, abaixo), na central (`interface/`) e no
simulador (`interface/simulador.py`).

### Broker

| Item | Valor |
|---|---|
| Implementação | Eclipse Mosquitto 2.0 (`interface/docker-compose.yml`, `interface/mosquitto/`) |
| Versão do protocolo | MQTT 3.1.1 |
| MQTT/TCP (hub, serviços) | porta `1883`; `8883` com TLS quando houver nuvem |
| MQTT/WebSocket (navegador) | porta `9001` |
| Autenticação | usuário e senha por cliente + ACL por tópico (`interface/mosquitto/acl`); senhas no `interface/.env`, fora do git |
| Usuários | `hub` (publica tudo do quarto, lê `config/desejada`), `vestivel` (publica só `vestivel/<id>/telemetria`, `vestivel/<id>/mqtt` e `alerta`), `telegraf` (lê tudo), `notificador` (lê `alerta`), `painel` (lê tudo, escreve `config/desejada`) |
| Endereço nas placas | `menuconfig` → MQTT (o mesmo menu nas duas); keepalive de 30 s (o Last Will sai até ~45 s após a placa cair) |

### Hierarquia

`idoso/<site>/<quarto>/<assunto>`. `site` é a instituição e `quarto` o cômodo; os dois são configuráveis no
hub. Exemplo: `idoso/butanta/quarto101/porta/banheiro`.

- Tudo de um quarto: `idoso/butanta/quarto101/#`.
- Todos os alertas de todos os quartos: `idoso/+/+/alerta`.

### Campos comuns

Todo payload JSON tem:

| Campo | Tipo | Significado |
|---|---|---|
| `v` | inteiro | versão do formato; começa em `1` |
| `ts` | inteiro | hora UTC em ms desde 1970 (epoch), carimbada pela placa que publica; **omitido** enquanto ela não tem hora (SNTP) |

### Tópicos

| Tópico | QoS | Retido | Payload | Quando |
|---|---|---|---|---|
| `status` | 1 | sim | `{"v":1,"estado":"online","fw":"0.1.0","hora":true,"ts":…}` | o hub conecta, e quando `hora` muda |
| `status` (Last Will) | 1 | sim | `{"v":1,"estado":"offline"}` | o broker publica se o hub cair |
| `porta/<id>` | 1 | sim | `{"v":1,"sensor":"MC-38","estado":"aberta","ts":…}` | a cada mudança |
| `presenca` | 1 | sim | `{"v":1,"sensor":"HC-SR501","estado":"detectada","ts":…}` | a cada mudança |
| `temperatura` | 0 | sim | `{"v":1,"sensor":"LM35DZ","valor":22.5,"ts":…}` | a cada 30 s |
| `vestivel/<id>` | 1 | sim | `{"v":1,"estado":"online","bateria_mV":3900,"rssi":-61,"atividade":0.12,"ts":…}` | a cada heartbeat (30 s) |
| `vestivel/<id>` | 1 | sim | `{"v":1,"estado":"offline","ts":…}` | 90 s sem mensagens do vestível |
| `vestivel/<id>/telemetria` | 0 | não | `{"v":1,"bateria_mV":3900,"atividade":0.05,"canal":6,"hub":true,"ts":…}` | publicado **pelo vestível** com MQTT ligado, a cada heartbeat (30 s) |
| `vestivel/<id>/mqtt` | 1 | sim | `{"v":1,"estado":"online","fw":"0.1.0"}` | o vestível com MQTT ligado conecta ao broker |
| `vestivel/<id>/mqtt` (Last Will) | 1 | sim | `{"v":1,"estado":"offline"}` | o broker publica se o vestível cair |
| `alerta` | 1 | não | ver abaixo | início e fim de cada episódio |
| `config/desejada` | 1 | sim | ver abaixo | a central (ou o operador) publica |
| `config/aplicada` | 1 | sim | ver abaixo | o hub publica ao iniciar e após cada `config/desejada` |

Estados retidos chegam a quem assina depois. `alerta` não é retido: quem precisa do estado dos alertas (banco,
Grafana, backend) o reconstrói pelos episódios.

### Campos específicos

| Campo | Tipo | Onde | Significado |
|---|---|---|---|
| `estado` | string | `porta/<id>` | `"aberta"` / `"fechada"` |
| `estado` | string | `presenca` | `"detectada"` / `"livre"` |
| `estado` | string | `status`, `vestivel/<id>` | `"online"` / `"offline"` |
| `sensor` | string | sensores do hub | modelo da peça: `MC-38`, `HC-SR501`, `LM35DZ` |
| `valor` | número | `temperatura` | °C |
| `fw` | string | `status` | versão do firmware do hub |
| `hora` | booleano | `status` | `true` se o hub sincronizou a hora; sem hora, as regras com janela de horário ficam suspensas |
| `bateria_mV` | inteiro | `vestivel/<id>` | tensão da bateria do vestível |
| `rssi` | inteiro | `vestivel/<id>` | sinal do vestível medido pelo hub, em dBm |
| `atividade` | número | `vestivel/<id>`, `vestivel/<id>/telemetria` | RMS de (\|a\| − 1 g) nos últimos 30 s, em g |
| `canal` | inteiro | `vestivel/<id>/telemetria` | canal de rádio em uso pelo ESP-NOW |
| `hub` | booleano | `vestivel/<id>/telemetria` | `true` se o hub confirmou (ACK) o último heartbeat por ESP-NOW |

`<id>` da porta é o nome configurado (`quarto`, `banheiro`, …). `<id>` do vestível são os 3 últimos bytes do
MAC em hexadecimal (`a1b2c3`).

`vestivel/<id>` é a visão **do hub** sobre o vestível (o que ele ouviu por ESP-NOW); `vestivel/<id>/telemetria`
e `vestivel/<id>/mqtt` são publicados pelo **próprio vestível**. A ACL impede o vestível de escrever no primeiro.

### Vestível com MQTT (opcional)

Desligado por padrão (`menuconfig` → Wearable → *Also join the Wi-Fi and talk to the central over MQTT*). O
caminho principal continua sendo ESP-NOW → hub → MQTT; o enlace direto serve para telemetria e como **reserva da
queda**:

- o vestível se associa ao mesmo Wi-Fi do hub, então o ESP-NOW já está no canal certo e a varredura de canais
  não é usada (§3);
- se o hub não confirmar o `FALL` nem depois de procurá-lo de novo, o vestível publica ele mesmo o `alerta` de
  `queda`, com o **mesmo `id`** que o hub usaria (`<vestível>-<seq>`) e `"via":"vestivel"` no `detalhe`. Se o
  hub também tiver recebido, o notificador descarta a cópia (mesmo `id` + `estado`);
- o "estou bem" segue o mesmo caminho: se a queda saiu pelo MQTT, ou se o `FALL_CANCEL` não for confirmado, o
  vestível publica o `cancelado` com o mesmo `id`.

### `alerta`

```json
{"v":1,"id":"a1b2c3-42","tipo":"queda","estado":"ativo","origem":"vestivel/a1b2c3","detalhe":{"a2":7.88,"prob":0.97},"ts":1792497600000}
```

| Campo | Significado |
|---|---|
| `id` | identifica o episódio; o mesmo `id` aparece no `ativo` e no `resolvido`/`cancelado` correspondente |
| `tipo` | ver tabela abaixo |
| `estado` | `"ativo"`, `"resolvido"` (a condição acabou) ou `"cancelado"` (o residente cancelou no vestível) |
| `origem` | o que gerou: `vestivel/<id>`, `porta/<id>`, `presenca`, `temperatura` |
| `detalhe` | objeto com dados do tipo (abaixo) |

| `tipo` | `detalhe` | Regra |
|---|---|---|
| `queda` | `{"a2": 7.88, "prob": 0.97}`: `a2` é o \|a\|² filtrado no pico do gatilho (g²); `prob` é a P(queda) do classificador do vestível (0–1, não calibrada; 0 numa queda simulada pelo botão); `"via":"vestivel"` quando publicado pelo próprio vestível | [`arquitetura.md`](arquitetura.md), seção Regras |
| `saida_noturna` | `{"porta":"quarto","min_sem_presenca":15}` | idem |
| `banheiro_prolongado` | `{"porta":"banheiro","min":40}` | idem |
| `inatividade` | `{"min":120}` | idem |
| `temperatura` | `{"valor":29.1,"limite":28}` | idem |
| `vestivel_offline` | `{"vestivel":"a1b2c3","ultimo_contato_s":90}` | idem |
| `bateria_baixa` | `{"vestivel":"a1b2c3","bateria_mV":3480}` | idem |

Formato do `id`: para `queda`, `<vestível>-<seq da mensagem FALL>`; para os demais, `<tipo>-<ts de início em s>`.
Quem consome descarta `id` + `estado` repetidos: com QoS 1, a mesma mensagem pode chegar duas vezes.

### `config/desejada` e `config/aplicada`

Parâmetros das regras do hub, ajustáveis sem recompilar. O hub valida o que recebe, grava em NVS e publica o
resultado em `config/aplicada`. Uma configuração inválida é recusada inteira, e a anterior continua valendo.
No boot, o hub usa a NVS ou, se ela estiver vazia, os valores padrão.

```json
{
  "v": 1,
  "noite": {"inicio": "22:00", "fim": "06:00"},
  "portas": {"quarto": "saida", "banheiro": "banheiro"},
  "saida_noturna_min": 15,
  "banheiro_min": 40,
  "inatividade_min": 120,
  "temperatura": {"min": 18, "max": 28, "histerese": 0.5, "persistencia_min": 10},
  "vestivel_offline_s": 90,
  "bateria_baixa_mV": 3500
}
```

- `portas` associa o `<id>` de cada porta ao seu papel: `saida`, `banheiro` ou `outra`. Os GPIOs de cada
  porta ficam no firmware; o papel fica aqui.
- `config/aplicada` repete os parâmetros em uso e acrescenta `"origem":"nvs"|"padrao"|"mqtt"`. Em caso de
  recusa, traz `"erro":"<motivo>"`.
- Perfil "demo": a mesma estrutura com tempos curtos (ex.: `saida_noturna_min: 2`, `banheiro_min: 3`).

### Formato antigo (só histórico)

`interface/legado/` guarda a demonstração de 18/09/2026, que usava outro formato: hierarquia
`idoso/quarto101/…` sem `site`, tópico próprio `queda`, `presenca` como evento, uma única `porta`, `ts` em
uptime e broker anônimo. Nada do sistema atual fala esse formato.

---

## 2. CAN — barramento da sala (hub ↔ demais módulos)

Estado: **implementado** nas duas placas. Transporte comum em `firmware/common/can/` (`can.h`, `can_ids.h`,
`can_ids.c`); IDs e payloads do grupo `0xF` em `firmware/common/protocol/include/protocol_can.h`. O hub envia
presença, temperatura e portas. O vestível só usa CAN **em bancada**, para cumprir o requisito da disciplina e
ajudar nos testes (`menuconfig` → Wearable → *CAN bus*, desligado por padrão): no produto ele fala com o hub por
ESP-NOW e não tem transceptor.

| Item | Valor |
|---|---|
| Controlador | TWAI do ESP32-S3; TX `GPIO13` / RX `GPIO14` nas duas placas (a PCB é a mesma; [`hardware/README.md`](../hardware/README.md)) |
| Taxa | 250 kbit/s |
| Quadro | CAN clássico, ID padrão de 11 bits, até 8 bytes de dados |
| Filtro | de hardware: só IDs `0x000`–`0x0FF` (formato `0x0GS`) chegam ao firmware |
| Erros | retransmissão até haver ACK (`fail_retry_cnt = -1`); em *bus-off*, nova tentativa de recuperação (`twai_node_recover`) após 1 s; quadros que não saem são descartados com log |
| Modo de bancada | `menuconfig` → CAN bus (TWAI) → *Bench mode* (autoconfirmação + loopback), ligado por padrão. Desligue no barramento real da sala |

### Formato do ID: `0x0GS`

- `G` — nibble do **grupo** (qual nó está falando)
- `S` — nibble do **sensor** dentro do grupo, escolhido por cada grupo

Toda a identidade está no ID; os 8 bytes de dados são só payload. Lembrete: no CAN, ID menor = maior prioridade.

| Grupo | `G` | Observação |
|---|---|---|
| `CAN_GROUP_1` | `0xF` | **este projeto** (hub do quarto e, em bancada, o vestível) |
| `CAN_GROUP_2` | `0xD` | outro módulo da sala |
| `CAN_GROUP_3` | `0xA` | outro módulo da sala |
| `CAN_GROUP_4` | `0xB` | outro módulo da sala |
| `CAN_GROUP_5` | `0xC` | outro módulo da sala |

### Mensagens deste projeto (grupo `0xF`)

| ID | Placa | Sensor | Tipo | Payload |
|---|---|---|---|---|
| `0x0F1` | hub | HC-SR501 | presença | 1 byte: `0` = livre, `1` = presença |
| `0x0F2` | hub | LM35 | temperatura | 2 bytes: `int16` little endian, em 1/100 °C |
| `0x0F3` … `0x0FA` | hub | portas, na ordem de `BOARD_DOORS` (`board_pins.h`): `0x0F3` = quarto, `0x0F4` = banheiro; **até 8 portas** | contato | 1 byte: `0` = fechada, `1` = aberta |
| `0x0FD` | vestível (bancada) | MPU-6050 | queda | 3 bytes: `int16` little endian, \|a\|² no pico do gatilho em 1/100 g², + `uint8` P(queda) do classificador em % |
| `0x0FE` | vestível (bancada) | — | heartbeat | 4 bytes: `uint16` bateria em mV (`0xFFFF` = não medida) + `uint16` atividade em mg, little endian |

`0x0F0`, `0x0FB`, `0x0FC` e `0x0FF` estão livres. Do hub, cada mudança de estado de porta ou presença e cada
leitura de temperatura (30 s) vira um quadro. Do vestível, cada queda detectada e cada heartbeat (30 s). O hub
registra no log os quadros do vestível, mas **não** os passa às regras: o alerta de queda depende do `seq` do
ESP-NOW (§3).

### Tipos de payload (comuns a todos os grupos)

| Tipo (`can_sensor_kind_t`) | Bytes | Codificação |
|---|---|---|
| `CAN_SENSOR_KIND_PRESENCE` | 1 | `0` = livre, `1` = presença |
| `CAN_SENSOR_KIND_CONTACT` | 1 | `0` = fechado, `1` = aberto |
| `CAN_SENSOR_KIND_TEMPERATURE` | 2 | `int16` little endian, em 1/100 °C |
| `CAN_SENSOR_KIND_FALL` | 3 | `int16` little endian, pico de \|a\|² em 1/100 g², + `uint8` P(queda) em % |
| `CAN_SENSOR_KIND_WEARABLE` | 4 | `uint16` bateria (mV) + `uint16` atividade (mg), little endian |

Para registrar outro grupo no diretório do barramento, siga o roteiro no topo de `can_ids.c` e acrescente a
linha correspondente nas tabelas acima.

---

## 3. ESP-NOW — vestível → hub

Estado: **implementado**. A definição em C está em `firmware/common/protocol/include/protocol_espnow.h`,
compilada pelas duas placas; o vestível envia (`firmware/gadget/components/espnow_tx`) e o hub recebe
(`firmware/room/components/espnow_link`).

### Enlace

| Item | Valor |
|---|---|
| Sentido | vestível envia, hub recebe; sempre unicast |
| Rádio do hub | STA associado ao Wi-Fi da central, com `esp_wifi_set_ps(WIFI_PS_NONE)`: com economia de energia, o hub dorme entre beacons e perde pacotes |
| Rádio do vestível | STA sem associação, ligado só para o ESP-NOW (padrão); com o MQTT do vestível ligado (§1), associado ao mesmo Wi-Fi do hub |
| Canal | o da rede Wi-Fi do hub; sem MQTT, o vestível o descobre por varredura (abaixo); com MQTT, já está nele |
| Endereçamento | MAC do hub configurado no vestível |
| Criptografia | ligada: PMK comum + LMK do par; chaves fora do repositório |
| Tamanho | ≤ 13 bytes por mensagem (o limite do ESP-NOW é 250) |

### Formato

Estrutura empacotada (`packed`), little endian. Cabeçalho de 8 bytes em toda mensagem:

| Campo | Tipo | Significado |
|---|---|---|
| `version` | `uint8` | versão do formato, começa em `1`; o hub descarta versão desconhecida |
| `type` | `uint8` | tipo da mensagem (tabela abaixo) |
| `seq` | `uint16` | contador do vestível, +1 a cada mensagem nova; um reenvio repete o `seq` |
| `uptime_ms` | `uint32` | uptime do vestível |

| `type` | Valor | Payload após o cabeçalho | Quando |
|---|---|---|---|
| `PING` | `0x01` | — | varredura de canal |
| `HEARTBEAT` | `0x02` | `uint16 battery_mv`, `uint16 activity_mg` | a cada 30 s |
| `FALL` | `0x03` | `float a2_peak` (\|a\|² filtrado no pico do gatilho, g²), `uint8 prob_pct` (P(queda) do classificador, %; 0 = simulada pelo botão) | o classificador do vestível decidiu queda (fora do cooldown de 30 s) |
| `FALL_CANCEL` | `0x04` | `uint16 fall_seq` (o `seq` da queda cancelada) | botão "estou bem" |
| `CONFIG` | `0x05` | reservado (hub → vestível) | fase futura |

`activity_mg` é o RMS de (\|a\| − 1 g) desde o heartbeat anterior, em mg; o hub o publica como `atividade`, em
g. `battery_mv = 0xFFFF` significa bateria não medida: o hub publica `vestivel/<id>` sem `bateria_mV` e não
aplica a regra `bateria_baixa`. O hub descarta, com log, mensagem de `type` desconhecido ou de tamanho
diferente do esperado para o tipo.

### Confiabilidade

- Uma mensagem só conta como entregue quando o callback de envio do ESP-NOW retorna `ESP_NOW_SEND_SUCCESS`
  (ACK de camada MAC do hub).
- `FALL` e `FALL_CANCEL`: até 10 tentativas, 50 ms entre elas, mesmo `seq`. Se todas falharem, o vestível
  refaz a varredura de canal e tenta de novo. Se ainda assim falhar e o MQTT do vestível estiver ligado, ele
  publica o alerta direto no broker (§1, *Vestível com MQTT*).
- `HEARTBEAT` e `PING`: sem reenvio.
- Duplicatas: o hub guarda o último (`seq`, `uptime_ms`) de cada vestível e descarta a mensagem que repete os
  dois. Um reenvio repete o quadro inteiro, uptime incluído; comparar o uptime evita confundir com reenvio a
  primeira mensagem de um vestível que reiniciou e recomeçou o `seq`.
- Valores de tentativas e intervalos são iniciais; ajustar nos testes.

### Varredura de canal

1. No boot, e depois de 3 falhas seguidas de envio, o vestível manda `PING` ao hub nos canais 1 a 13.
2. O primeiro canal que receber ACK é guardado na memória RTC e usado dali em diante.
3. Se nenhum canal responder, o vestível espera 30 s e recomeça; enquanto isso, o hub o declara offline.
4. Com o MQTT do vestível ligado não há varredura: o canal é o do access point, que é o mesmo do hub, e o
   `PING` só confirma que o hub ouve.

### Vivacidade

- O vestível manda `HEARTBEAT` a cada 30 s.
- Qualquer mensagem do vestível conta como sinal de vida. Após **90 s** sem nenhuma, o hub publica
  `vestivel/<id>` com `"estado":"offline"` e abre o alerta `vestivel_offline`.

---

## 4. Hub: entradas → tópicos e alertas

Estado: **implementado** (`firmware/room/main/main.c` e `components/rules`). O hub publica no MQTT o que lê
dos sensores locais e o que recebe por ESP-NOW, carimbando `ts` com a própria hora, e roda as regras que geram
`alerta`.

| Entrada | Publica | Também |
|---|---|---|
| HC-SR501 local (o mesmo estado enviado em `0x0F1`) | `presenca` | alimenta as regras de presença |
| Cada porta (o mesmo estado enviado em `0x0F3`, `0x0F4`, …) | `porta/<id>` | alimenta as regras de porta |
| LM35 local | `temperatura` | regra `temperatura` |
| ESP-NOW `FALL` | `alerta` `queda` / `ativo` | LED em alerta |
| ESP-NOW `FALL_CANCEL` | `alerta` `queda` / `cancelado` | LED volta ao normal |
| ESP-NOW `HEARTBEAT` | `vestivel/<id>` online | regra `bateria_baixa` |
| 90 s sem mensagens do vestível | `vestivel/<id>` offline | `alerta` `vestivel_offline` |
| Conexão ao broker / Last Will | `status` | — |
| `config/desejada` | `config/aplicada` | grava em NVS |

Mensagens CAN de outros grupos ainda não têm tópico MQTT definido. Os quadros do vestível em bancada (`0x0FD`,
`0x0FE`) só vão para o log do hub.
