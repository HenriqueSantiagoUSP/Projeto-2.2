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
| MAC of the room hub | aparece no log de boot do hub. **Vazio = sem hub** (bancada ou demonstração): o ESP-NOW não é iniciado, e as quedas vão para o log serial (e para o MQTT e o CAN, se ligados) |
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

## Sem alguma peça

Nada do que é opcional derruba o vestível. Cada peça ausente dá **um** aviso (W) quando é percebida e uma linha
(I) quando volta, e as novas tentativas ficam em silêncio:

| Situação | O que acontece |
|---|---|
| Sem hub (MAC vazio) | modo sem hub: não liga o ESP-NOW; o LED fica em "ok" |
| Hub configurado, fora do alcance | aviso uma vez; a queda segue pelo MQTT, se ligado. Uma queda que não chega a lugar nenhum é o único erro (E) |
| MPU-6050 ausente no boot ou solto durante o uso | não reinicia: aviso uma vez, LED vermelho lento (falha), procura o sensor a cada 5 s e **não manda heartbeat**, então o hub o declara offline. Ao voltar, o pipeline recomeça do zero |
| Wi-Fi fora do ar ou senha errada (com MQTT) | aviso uma vez, com o motivo ("network not found", "password refused?"); novas tentativas a cada até 60 s |
| Broker fora do ar | aviso uma vez por queda de conexão; usuário/senha recusados são erro (configuração errada) |
| CAN sem barramento (modo bancada desligado) | aviso uma vez ("CAN bus not connected"); novas tentativas a cada até 60 s; os quadros são descartados em silêncio |

## Demonstração com uma placa só

Para mostrar o modelo funcionando só com a ESP e o MPU-6050, ligada ao computador pela USB, sem hub:

1. **Ligação:** MPU-6050 em 3V3 e GND, SDA no GPIO 1, SCL no GPIO 2 (o INT não é usado). Qualquer uma das duas
   USB da DevKit serve: o console sai pela UART e também pela USB nativa.
2. **Gravar o perfil de demonstração** ([`sdkconfig.demo.defaults`](sdkconfig.demo.defaults)). Ele deixa o MAC do
   hub vazio, liga o modo de coleta, a espera entre quedas cai para 5 s e o console sobe para 921600 baud. Fica
   numa pasta de build própria, sem mexer no `sdkconfig` do dia a dia:

   ```powershell
   idf.py -B build/demo -D SDKCONFIG=build/demo/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.demo.defaults" -p COMx build flash
   ```

   Se mudar o `sdkconfig.demo.defaults`, apague `build/demo/sdkconfig` antes: os defaults só preenchem o que o
   `sdkconfig` ainda não tem.
3. **No computador**, rode o [`interface/demo_vestivel.py`](../../interface/README.md#demonstração-com-uma-placa-só).
   Ele mostra o sinal e as decisões ao vivo e, se quiser, repassa as quedas à central. Para só olhar o log:
   `idf.py -B build/demo -p COMx monitor`. No boot devem aparecer `+-8 g, +-500 dps`,
   `fall pipeline: trigger 2.00 g, random forest…` e o aviso `no hub configured … stand-alone`.

**Na frente do professor:**
1. **Andar ou balançar a placa:** o gatilho dispara, mas a P(queda) fica perto de 0 e não há alarme.
2. **Simular uma queda:** segure a placa de pé, solte-a sobre uma almofada e deixe-a deitada. Isso dá queda livre,
   impacto, corpo parado e a gravidade em outro eixo: P(queda) alta e "QUEDA DETECTADA".

O modelo foi treinado com o sensor na **cintura** (SisFall), então movimentos de mão podem dar resultados
diferentes dos de uma pessoa caindo.

## Componentes e tasks

Próprios, em `components/`: `board`, `mpu6050`, `fall_pipeline` e `signal_proc` (os dois dependem de
`espressif/esp-dsp`, baixado do registro no primeiro build), `espnow_tx`, `battery`. Comuns, em
[`firmware/common/`](../common/README.md): `pcb`, `protocol`, `rtos_util`, `sys_events`, `net`, `mqtt_link`, `can`,
`indicators`.

| Task | Núcleo | Prioridade | Função |
|---|---|---|---|
| `sampling` | 1 | ALTA | FIFO do MPU-6050 (e as novas tentativas quando ele falta), pipeline de queda (filtro, gatilho, janela, atributos, classificador) |
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
