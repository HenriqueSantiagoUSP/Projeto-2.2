# firmware/gadget — vestível

Pulseira com ESP32-S3 e acelerômetro MPU-6050 (I²C). Detecta a queda na própria placa e avisa o hub do quarto
por ESP-NOW. O desenho está em [`docs/arquitetura.md`](../../docs/arquitetura.md) (seção Vestível), as
mensagens em [`docs/protocolo.md`](../../docs/protocolo.md) e o que já foi verificado em
[`docs/estado.md`](../../docs/estado.md). O código segue os slides de [`aulas/`](../../aulas/README.md) e a
documentação do ESP-IDF v6.0.1.

## O que faz

- **Aquisição:** ±8 g e ±500 °/s, filtro de 44 Hz, 200 Hz temporizados pelo próprio MPU-6050 e lidos do FIFO
  a cada 50 ms (núcleo 1).
- **Detecção de queda (`components/fall_pipeline`):** o pipeline desenvolvido no PC
  ([`modelo/`](../../modelo/README.md)), amostra a amostra:
  1. passa-baixa de 20 Hz nos 6 eixos (um `dsps_biquad_f32` por eixo);
  2. gatilho: um pico de \|a\| filtrado ≥ 2,0 g, emitido depois de 0,5 s sem um valor maior;
  3. janela de −1,0 s a +0,8 s em volta do pico (360 amostras por eixo, de um buffer circular por eixo);
  4. 23 atributos no tempo, os mesmos de `modelo/queda/atributos.py`;
  5. classificador: Random Forest de 60 árvores (padrão) ou regressão logística;
  6. queda quando P(queda) ≥ o limiar do modelo (0,73 no Random Forest); depois, 30 s de cooldown.

  O custo dos passos 4 a 6 é medido em ciclos de CPU e vai para o log em cada disparo.
- **O modelo é gerado, não escrito à mão:** `fall_model.h`/`fall_model.c` saem de
  `modelo/experimentos/09_exportar_vestivel.py`, junto com os vetores de teste de paridade. Ele foi treinado na
  SisFall (cintura, ±8 g) e é **provisório até retreinar com os dados do pulso**.
- **Modo de coleta:** imprime cada amostra bruta em CSV e, a cada disparo do gatilho, uma linha `FEAT:` com os 23
  atributos, P(queda) e o custo em ciclos. Serve para formar o conjunto de dados do modelo
  ([`docs/coleta-de-dados.md`](../../docs/coleta-de-dados.md)).
- **ESP-NOW:** varredura de canal, heartbeat a cada 30 s com bateria e atividade, `FALL` (com o pico e a
  P(queda)) com reenvio até o ACK, `FALL_CANCEL` pelo botão "estou bem" dentro da janela de 15 s em que o LED fica em alerta (não há buzzer).
- **MQTT (opcional):** associa ao Wi-Fi do hub, publica `vestivel/<id>/telemetria` e, se o hub não confirmar
  uma queda, publica o alerta ele mesmo ([`docs/protocolo.md`](../../docs/protocolo.md) §1).
- **CAN (opcional, só bancada):** quedas (pico e P) e heartbeats como quadros `0x0FD`/`0x0FE`, para o
  requisito da disciplina e para testes. O vestível de verdade não tem transceptor.
- **Ainda não:** *light sleep* (depende de medir o consumo em placa).

## Configurar

`idf.py menuconfig` → **Wearable**:

| Opção | O quê |
|---|---|
| MAC of the room hub | aparece no log de boot do hub |
| Encrypt / PMK / LMK | iguais às do hub |
| Fall trigger | L<sub>on</sub> do gatilho em centésimos de g (200 = 2,0 g, o valor do treino). Mudar afasta as janelas das do treino: revise com os dados do pulso e retreine |
| Fall classifier | Random Forest (padrão) ou regressão logística |
| Cooldown, cancel window | 30 s e 15 s por padrão |
| Button simulates a fall | só para testes: o botão gera uma queda e permite testar todo o caminho até o Telegram |
| Data collection mode | liga a saída CSV e as linhas `FEAT:` (uma por disparo do gatilho) |
| Battery divider | **desligado por padrão**: a PCB do protótipo não tem divisor (o vestível roda com power bank), e a bateria vai como "desconhecida". Ligue só numa placa com LiPo e divisor no GPIO 5 |
| Also join the Wi-Fi and talk to the central over MQTT | liga o MQTT direto (padrão: desligado). Preencha também **Network (Wi-Fi)** e **MQTT** (usuário `vestivel`, senha `MQTT_VESTIVEL_PASSWORD` do `interface/.env`, mesmo site e quarto do hub) |
| CAN bus | liga o CAN de bancada (padrão: desligado). Modo de bancada em **CAN bus (TWAI)** |

O id do vestível (3 últimos bytes do MAC) e o MAC completo, que o hub precisa, aparecem no log de boot. Pinos:
`firmware/common/pcb/include/pcb_pins.h`, o mapa da PCB que o vestível divide com o hub
([`hardware/README.md`](../../hardware/README.md)); `components/board/include/board_pins.h` escolhe o que o
vestível usa. Em `menuconfig` → PCB, a versão da DevKit (LED no GPIO48 ou no GPIO38).

## Componentes e tasks

Próprios, em `components/`: `board`, `mpu6050`, `fall_pipeline` e `signal_proc` (os dois dependem de
`espressif/esp-dsp`, baixado do registro no primeiro build), `espnow_tx`, `battery`. Comuns, em
[`firmware/common/`](../common/README.md): `pcb`, `protocol`, `rtos_util`, `sys_events`, `net`, `mqtt_link`, `can`,
`indicators`.

| Task | Núcleo | Prioridade | Função |
|---|---|---|---|
| `sampling` | 1 | ALTA | FIFO do MPU-6050, pipeline de queda (filtro, gatilho, janela, atributos, classificador) |
| `comm` | 0 | MEDIA | ESP-NOW, MQTT e CAN opcionais, botão, heartbeat |
| `can_rx`, `can_tx` | 0 | MEDIA | só com o CAN ligado |
| `indicators` | 0 | MINIMA | LED |

`sampling` só conversa com `comm` por fila; a atividade acumulada é o único estado dividido entre as duas,
protegida por um mutex que fica preso só o tempo de somar um lote.

## Compilar, gravar e testar

```powershell
. C:\Espressif\tools\Microsoft.v6.0.1.PowerShell_profile.ps1
idf.py build
idf.py -p COMx flash monitor
```

Testes no PC (Git Bash, gcc do MSYS2 no PATH):

```bash
sh components/fall_pipeline/test_host/run_tests.sh   # gatilho, filtro, janela e paridade com o Python
sh components/signal_proc/test_host/run_tests.sh     # buffer circular, momentos, atividade
```
