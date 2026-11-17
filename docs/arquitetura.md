# Arquitetura

Rede de sensores para o monitoramento contínuo e não invasivo de idosos em **Instituições de Longa
Permanência (ILPIs)**. O cenário de referência é a ILPI Butantã (~60 residentes, turno noturno com poucos
cuidadores). Nesta fase, o escopo é o quarto de um residente: portas, presença, temperatura e uma pulseira que
detecta quedas.

Este arquivo descreve como o sistema é organizado. Os formatos de mensagem ficam em
[`protocolo.md`](protocolo.md), as metas em [`requisitos.md`](requisitos.md), a ordem de implementação em
[`roadmap.md`](roadmap.md), o que já foi feito e verificado em [`estado.md`](estado.md) e o histórico das
decisões em [`decisoes.md`](decisoes.md).

## Visão geral

```
Pulseira ──ESP-NOW──▶ Hub do quarto ──MQTT──▶ Mosquitto ─┬─▶ Telegraf ─▶ InfluxDB ─▶ Grafana
(detecta a queda)     (regras de tempo, alertas,          ├─▶ notificador ─▶ Telegram (cuidador)
                       LED de status)                     └─▶ futuro: backend + banco ─ nuvem via bridge
                          └── CAN ◀──▶ módulos da sala
```

Princípios:

1. **Processamento na borda** (diretriz da disciplina): a queda é detectada na pulseira e as regras de tempo
   rodam no hub, que publica os alertas. A central só armazena, mostra e notifica.
2. **O MQTT é o contrato:** painéis, banco, notificador e, no futuro, backend e nuvem consomem os mesmos
   tópicos. Trocar a central não muda as placas.
3. **Funciona sem internet:** rede local, broker local e hora por um servidor NTP local. A nuvem entra por um
   bridge, sem tirar o funcionamento local.

## Por que estes enlaces

**Pulseira → hub: ESP-NOW.**

| Critério | ESP-NOW → hub (escolhido) | Wi-Fi direto → broker | BLE → hub |
|---|---|---|---|
| Latência do alerta | milissegundos, sem conexão | boa se já associado; segundos se precisar reconectar | depende de conexão ou anúncio |
| Bateria | rádio ligado só para enviar | precisa manter associação | baixa, mas a pilha é pesada no ESP32 |
| Funciona sem a rede Wi-Fi | sim (alarme local no hub) | não | sim |
| Cobertura | raio do hub | onde o Wi-Fi chega | raio do hub, menor |
| Complexidade | média: confiabilidade e tradução por nossa conta | baixa | alta; no hub disputa o rádio com o Wi-Fi |

O ponto fraco do ESP-NOW é a cobertura. Se o teste mostrar áreas fora do alcance (corredor, áreas comuns), as
saídas são mais hubs ou o Wi-Fi como reserva da queda. A reserva já existe como opção da pulseira (MQTT
direto, desligado por padrão por causa da bateria; [`protocolo.md`](protocolo.md) §1).

**Hub → central: MQTT.** Vários consumidores ao mesmo tempo, estado retido para quem conecta depois, Last
Will para saber que o hub caiu e QoS por criticidade.

**CAN.** Exigência da disciplina: integra o hub aos módulos da sala. Não é caminho da central. A pulseira também
tem CAN, mas só em bancada, para o requisito e para os testes; no produto ela não tem transceptor.

## Dispositivos

Dois nomes valem para todo o projeto: o **hub** (hub do quarto) é a placa fixa em cada quarto; a **pulseira** é o
dispositivo que o residente usa no pulso. No código C, em inglês, são `room`/`hub` e `gadget`/`wearable`.

| Dispositivo | Pasta | Placa | Papel | Rádio |
|---|---|---|---|---|
| Hub do quarto (gateway) | `firmware/room/` | ESP32-S3 DevKitC-1 | sensores do quarto, regras, alertas, recepção ESP-NOW, cliente MQTT, nó CAN | STA associado com `WIFI_PS_NONE` + ESP-NOW + TWAI |
| Pulseira (pulseira) | `firmware/gadget/` | ESP32-S3 DevKitC-1 | aquisição inercial, detecção de queda, envio ESP-NOW; opcionais: MQTT direto e CAN de bancada | STA sem associação + ESP-NOW (com o MQTT opcional, STA associado) |
| Central | `interface/` | Raspberry Pi 4 (PC no desenvolvimento) | broker, histórico, painéis, notificação | rede local |

Nenhuma placa usa SoftAP nem AP+STA: o ESP-NOW funciona sobre a interface STA.

### Hub do quarto — `firmware/room/`

ESP-IDF 6.0.1, alvo `esp32s3`, `MINIMAL_BUILD` ligado. As duas placas são, no primeiro projeto, **a mesma
PCB** com firmwares diferentes; o mapa de pinos está em [`hardware/README.md`](../hardware/README.md) e, no
código, em `firmware/common/pcb/include/pcb_pins.h`, verificado na compilação (sem pino repetido, proibido ou
reservado). O `components/board/include/board_pins.h` de cada placa só escolhe o que ela usa. O mapa é proposta
para o esquemático, a confirmar no layout.

| Periférico | Função | Pino (proposta) |
|---|---|---|
| HC-SR501 (PIR, 5 V) | presença; 60 s de aquecimento antes da primeira leitura | GPIO8 |
| MC-38 (reed switch) × N | portas, listadas em `BOARD_DOORS`, cada uma com papel `saida`, `banheiro` ou `outra` | GPIO11 (quarto), GPIO12 (banheiro); 9 e 10 para as portas 3 e 4; 39–42 na expansão |
| LM35DZ + LM358 (G = 5,5, 5 V e GND, RC 10 kΩ/1 µF na entrada do ADC) | temperatura: `adc_cali` em mV, depois a curva `T = (V_mV − 144,739) / 51,3335`; 16 amostras a cada 30 s | GPIO4 (ADC1_CH3) |
| Transceptor CAN | barramento da sala | TX GPIO13, RX GPIO14 |
| Buzzer | **retirado em 2026-10-23**: a queda aparece no LED e chega ao cuidador pela central | — (GPIO21 livre) |
| LED WS2812 da placa | status: verde 1 Hz normal, amarelo 1 Hz sem broker, vermelho 4 Hz com alerta aberto | GPIO48 (v1.0) / GPIO38 (v1.1) |

Configuração por `idf.py menuconfig` (fica no `sdkconfig`, fora do git), em quatro menus. Os três primeiros
vêm dos componentes comuns e são os mesmas na pulseira:
- **Network (Wi-Fi):** Wi-Fi, servidor SNTP e fuso;
- **MQTT:** broker, usuário, senha, site e quarto;
- **CAN bus (TWAI):** modo de bancada;
- **Room hub: ESP-NOW:** MAC da pulseira e chaves.

Responsabilidades:

1. sensores locais → CAN e MQTT;
2. regras de tempo → `alerta` (seção Regras);
3. ESP-NOW da pulseira → MQTT e alertas ([`protocolo.md`](protocolo.md) §4);
4. LED local de status e de alerta, que funciona mesmo sem a rede;
5. hora por SNTP;
6. parâmetros por `config/desejada`, guardados em NVS;
7. Last Will no tópico `status`.

Componentes ESP-IDF próprios do hub (os comuns estão em [Componentes comuns](#componentes-comuns)):

| Componente | Responsabilidade |
|---|---|
| `board` | mapa de pinos e tabela de portas (`BOARD_DOORS`, até 8: uma por nibble CAN) |
| `sensors` | barramento de eventos com filtro de glitch e debounce por estabilização (`sensor_events`) + drivers do PIR, das portas e do LM35 |
| `rules` | motor de regras em C puro, testado no PC (`test_host/`) |
| `hub_config` | config em JSON (`config/desejada` e `config/aplicada`), validação e NVS |
| `espnow_link` | recepção ESP-NOW, validação e descarte de reenvios |

Fluxo:

```
ISR do GPIO ─fila─▶ sensors_task ─(50 ms sem bordas: lê o nível)─┐
lm35_task (30 s) ────────────────────────────────────────────────┼─▶ fila do hub ─▶ hub_task ─┬─▶ fila CAN ─▶ can_tx
espnow_task (pulseira) ──────────────────────────────────────────┤                            ├─▶ MQTT (estados)
MQTT config/desejada ────────────────────────────────────────────┘                            └─▶ regras ─▶ alerta, LED
ISR do TWAI ─fila─▶ can_rx ─▶ log (portas de outros grupos, quadros da pulseira em bancada)
```

As ISRs e os callbacks de rádio só enfileiram. Uma única task, `hub_task`, roda as regras e publica; por isso o
motor de regras não precisa de travas. As tasks `sensors_task` e `hub_task` são vigiadas pelo watchdog de
30 s.

### Regras (no hub)

Valores padrão; todos ajustáveis por `config/desejada` ([`protocolo.md`](protocolo.md) §1). Cada alerta é um
episódio: publicado uma vez como `ativo` e encerrado como `resolvido` ou `cancelado`, sem repetição.

| `tipo` | Dispara quando | Resolve quando | Padrão |
|---|---|---|---|
| `queda` | chega `FALL` da pulseira (LED em alerta) | `FALL_CANCEL` → `cancelado` | — |
| `saida_noturna` | a porta `saida` abre no período noturno e passam N min sem presença no quarto | a presença volta | 22h–06h, 15 min |
| `banheiro_prolongado` | a porta `banheiro` fecha e passam N min sem presença no quarto e sem a `saida` abrir | a porta do banheiro abre | 40 min |
| `inatividade` | no período diurno, N min sem presença e sem a `saida` abrir | a presença volta | 120 min (calibrar) |
| `temperatura` | fora de 18–28 °C por 10 min contínuos | dentro da faixa, com histerese de 0,5 °C, por 10 min | — |
| `pulseira_offline` | 90 s sem mensagens da pulseira | a próxima mensagem | — |
| `bateria_baixa` | bateria da pulseira < 3,5 V | bateria > 3,6 V | — |

Sem hora válida, as regras com janela de horário ficam suspensas e `status` informa `"hora":false`. A
inatividade só vale de dia porque, à noite, o residente dormindo não aciona o PIR.

**Sensor não instalado:** um PIR solto lê "ninguém" e uma porta solta lê "aberta", e nada no sinal os distingue
de um caso real. Por isso o hub tem o menu "Room hub: sensors" para declarar o que está instalado:
- sem PIR, a inatividade e a saída noturna (que depende de "sem presença") ficam suspensas;
- uma porta ausente não é lida, então não arma regra nenhuma;
- sem LM35 (ou com ele solto, lendo fora de 0–57 °C), a regra de temperatura não dispara.

Cada peça ausente (sensor, CAN, Wi-Fi, broker) dá um aviso único no log, não um erro por tentativa
([`firmware/room/README.md`](../firmware/room/README.md)).

### Pulseira — `firmware/gadget/`

Componentes próprios: `mpu6050`, `fall_pipeline` (pipeline de queda; C puro, testado no PC contra o Python),
`signal_proc` (buffer circular, momentos com ESP-DSP e atividade; testado no PC), `espnow_tx` e `battery`, além
dos comuns. Pinos, da mesma PCB do hub:
I²C SDA/SCL 1/2 (o par que o ULP também aceita), INT do MPU 17, botão 18, CAN
13/14 (só bancada); ver [`hardware/README.md`](../hardware/README.md). Configuração em `idf.py menuconfig` →
Wearable, mais os menus comuns Network (Wi-Fi), MQTT e CAN bus (TWAI) quando os opcionais estão ligados.

- **Aquisição:** MPU-6050 por I²C, ±8 g, filtro digital de 44 Hz, 200 Hz temporizados pelo próprio sensor
  (`SMPLRT_DIV`) e lidos do FIFO a cada 50 ms, no núcleo 1. O *light sleep* entre as leituras ainda não foi
  feito: depende de medir o consumo em placa.
- **Detecção de queda — pipeline com modelo (desde 2026-11-11):** o mesmo desenvolvido e comparado no PC
  ([`modelo/`](../modelo/README.md)), amostra a amostra no núcleo 1:
  1. passa-baixa de 20 Hz nos 6 eixos (um `dsps_biquad_f32` do ESP-DSP por eixo);
  2. **gatilho:** um pico de \|a\| filtrado ≥ L<sub>on</sub> = 2,0 g, emitido depois de 0,5 s sem um valor maior.
     Correr ou sentar com força também disparam; o gatilho só acorda o classificador (~750 vezes por hora de
     atividade na SisFall);
  3. **janela** de −1,0 s a +0,8 s em volta do pico, tirada de um buffer circular por eixo;
  4. **23 atributos no tempo** (\|a\|, \|ω\|, queda livre, o corpo depois do impacto, inclinação, correlação entre
     eixos), com as mesmas definições de `modelo/queda/atributos.py`;
  5. **classificador:** Random Forest de 60 árvores (padrão) ou regressão logística, escolhido no `menuconfig`;
  6. **decisão:** queda quando P(queda) ≥ o limiar do modelo, com cooldown de 30 s. O custo em ciclos é medido
     dentro de uma seção crítica e vai para o log.

  O modelo (`fall_model.c`) é **gerado** por `modelo/experimentos/09_exportar_pulseira.py` e foi treinado na
  SisFall (cintura, ±8 g): é **provisório até retreinar com os dados do pulso** ([roadmap](roadmap.md), fase 8).
  No modo de coleta, cada janela vai para a serial com os atributos e P (`FEAT:`).
- **Comunicação** ([`protocolo.md`](protocolo.md) §3): varredura de canal, heartbeat a cada 30 s com bateria
  e atividade, `FALL` (com o pico e a P(queda) do modelo) com reenvio até o ACK.
- **MQTT (opcional, desligado por padrão):** a pulseira se associa ao Wi-Fi do hub e publica a própria
  telemetria; se o hub não confirmar uma queda, publica o alerta ela mesma, com o mesmo `id`
  ([`protocolo.md`](protocolo.md) §1). Custa bateria, por isso é opção.
- **CAN (opcional, só bancada):** quedas (com a P do modelo) e heartbeats também como quadros `0x0FD`/`0x0FE`, para cumprir o
  requisito da disciplina e ajudar nos testes. O produto não tem transceptor.
- **Interação:** botão "estou bem" e LED. A pulseira envia `FALL` na hora, e o LED fica em alerta durante a
  janela de cancelamento (15 s); se o botão for apertado nesse tempo, envia `FALL_CANCEL`. Não há buzzer
  (retirado em 2026-10-23). Em teste, o botão pode
  simular uma queda (`menuconfig`).
- **Energia:** no protótipo, power bank na entrada USB-C da placa, sem medição de bateria; a pulseira informa
  "bateria desconhecida" (`0xFFFF`). O firmware já sabe ler um divisor no ADC1 (`GADGET_BATTERY_SENSE`), para a
  placa própria da pulseira com LiPo.
- **Falha do sensor (desde 2026-11-11):** se o MPU-6050 não responde, no boot ou durante o uso, a pulseira
  continua rodando:
  - LED vermelho lento (falha);
  - procura o sensor a cada 5 s;
  - **não manda heartbeat**, para não parecer saudável sem detectar quedas. O hub acaba declarando-o offline,
    como antes, quando ele reiniciava em laço.
- **Sem hub (MAC vazio):** modo de bancada e de demonstração. O ESP-NOW não é ligado, e as quedas vão para o log
  serial (e para o MQTT e o CAN, se ligados). É o que usa a demonstração com uma placa só
  ([`firmware/gadget/README.md`](../firmware/gadget/README.md)).

### Central

**Agora**, tudo em Docker Compose (`interface/docker-compose.yml`), no PC ou no Raspberry Pi; instruções em
[`interface/README.md`](../interface/README.md):

| Peça | Função |
|---|---|
| Mosquitto | broker com usuário/senha e ACL (`hub`, `pulseira`, `telegraf`, `notificador`, `painel`) |
| Telegraf | grava todos os tópicos MQTT no InfluxDB, sem código |
| InfluxDB | séries temporais, retenção de 30 dias |
| Grafana | painel "Quartos", com seletor de quarto: estado atual, histórico e alertas; versionado em JSON |
| Notificador (Python) | assina `idoso/+/+/alerta` e envia ao Telegram do cuidador |
| Simulador (Python) | faz o papel de um hub, para testar a central sem hardware |

Ainda falta um servidor NTP local, para os hubs terem hora quando a ILPI estiver sem internet. A demonstração
de 18/09 (painel HTML e formato antigo) fica em `interface/legado/`, só como histórico.

**Futuro** ([roadmap](roadmap.md)):
- backend + PostgreSQL: residentes, quartos, dispositivos, cuidadores e alertas com ciclo de vida;
- nuvem via bridge do Mosquitto com TLS;
- app ou web do cuidador;
- OTA;
- modelo preditivo de queda.

## Componentes comuns

Ficam em `firmware/common/` e entram nas duas placas por `EXTRA_COMPONENT_DIRS`; cada placa passa os seus pinos
e tópicos por uma struct de configuração.

| Componente | Responsabilidade |
|---|---|
| `protocol` | o contrato em C: `protocol_mqtt.h`, `protocol_can.h` (nibbles e payloads do grupo `0xF`), `protocol_espnow.h` |
| `rtos_util` | `task_config.h` (prioridades nomeadas, núcleos, pilhas) e `rtos_err.h` |
| `sys_events` | event group do sistema: rede, hora, MQTT e estado do CAN |
| `net` | Wi-Fi STA com ou sem associação, reconexão com recuo exponencial, SNTP |
| `mqtt_link` | cliente MQTT 3.1.1 sobre `espressif/mqtt`, com Last Will e outbox; tópico do Last Will e assinatura informados pela placa |
| `can` | transporte TWAI com fila de TX, task dona do periférico, recuperação de *bus-off*, filtro de hardware e diretório de IDs (`can_ids.h`) |
| `indicators` | LED WS2812 (e um buzzer, se a placa tiver; a PCB atual não tem), numa task própria |

## Tasks, núcleos e recursos compartilhados

Seguem o slide de RTOS (`aulas/Semestre 1/ESP32 - RTOS.pdf`): prioridades nomeadas em `task_config.h`
(`PRIO_CRITICA` 20, `PRIO_ALTA` 15, `PRIO_MEDIA` 10, `PRIO_BAIXA` 5, `PRIO_MINIMA` 2), nunca acima de 20 (a
task do Wi-Fi roda em 23), e toda task fixada num núcleo com `xTaskCreatePinnedToCore`. O núcleo 0 fica com o
rádio e tudo o que fala com ele; o núcleo 1, com o tempo real.

| Placa | Task | Núcleo | Prioridade | Função |
|---|---|---|---|---|
| hub | `sensors_task` | 1 | ALTA | estabiliza e lê portas e PIR |
| hub | `lm35_task` | 1 | BAIXA | temperatura a cada 30 s |
| hub | `hub_task` | 0 | MEDIA | regras, publicação, LED |
| hub | `espnow_task` | 0 | MEDIA | valida e encaminha as mensagens da pulseira |
| as duas | `can_rx`, `can_tx` | 0 | MEDIA | recepção e despacho; transmissão e estado do nó |
| as duas | `indicators` | 0 | MINIMA | LED |
| pulseira | `sampling` | 1 | ALTA | FIFO do MPU-6050 (e as novas tentativas sem ele), pipeline de queda |
| pulseira | `comm` | 0 | MEDIA | ESP-NOW, MQTT e CAN opcionais, botão, heartbeat |

Como os recursos são compartilhados:

- **Filas, sempre por valor:** ISR → `sensors_task`; sensores, ESP-NOW e MQTT → `hub_task`; qualquer task →
  `can_tx`; ISR do TWAI → `can_rx`; `sampling`, botão e MQTT → `comm`. Quem produz nunca toca o estado de quem
  consome.
- **Event group (`sys_events`):** rede, hora, MQTT e CAN. Cada bit tem um único escritor.
- **Notificação de task:** o callback de estado do TWAI só avisa a `can_tx`, que lê o estado ela mesma; uma
  notificação nunca se perde, ao contrário de uma fila cheia.
- **Mutex** (com herança de prioridade): só a atividade da pulseira, entre `sampling` e `comm`, e só pelo tempo
  de somar um lote.
- **Seção crítica (`portMUX`):** o estado pedido ao LED e ao buzzer (um prazo de 64 bits não é escrito numa
  instrução só), e a contagem de ciclos dos atributos.
- **Contra deadlock:** nenhuma task segura duas travas ao mesmo tempo, e as tasks vigiadas pelo watchdog só
  esperam com timeout.

## Estado atual

O que já foi feito, e com que nível de verificação, está em [`estado.md`](estado.md); a ordem e os critérios
de pronto, em [`roadmap.md`](roadmap.md).
