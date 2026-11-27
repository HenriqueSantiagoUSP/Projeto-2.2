# Projeto 3, Q3: roteiro da demonstração da comunicação sem fio (24/11)

**O que mostrar:** o caminho inteiro de uma queda, sem a PCB, com duas DevKits na protoboard:

```
pulseira (MPU-6050) ──ESP-NOW──► hub (PIR) ──Wi-Fi + MQTT──► central (Mosquitto → Telegraf → InfluxDB → Grafana; notificador)
```

- **A pulseira** só tem o MPU-6050. Ela detecta a queda no próprio ESP32 e manda ao hub por ESP-NOW, além de um
  heartbeat a cada 30 s.
- **O hub** só tem o PIR. Ele roda as regras e publica por MQTT. Sem transceptor na protoboard, o CAN dá um aviso
  e fica de fora. Os
  sensores que faltam (LM35, portas) estão declarados ausentes no firmware: um aviso de cada no boot, e nenhum
  alerta falso.

Os passos de cada placa estão nos READMEs:
- [`firmware/room/README.md`](../../../../firmware/room/README.md), "Demonstração: hub só com o PIR";
- [`firmware/gadget/README.md`](../../../../firmware/gadget/README.md), "Demonstração com o hub";
- [`interface/README.md`](../../../../interface/README.md), seção 4.

## Material

- Duas ESP32-S3-DevKitC-1 e dois cabos USB de dados.
- **Pulseira:** MPU-6050 em 3V3/GND, SDA no GPIO 1 e SCL no GPIO 2. Opcional: um botão (ou um fio) do GPIO 18 ao
  GND.
- **Hub:** HC-SR501 com VCC no 5V, GND e OUT no GPIO 8. Ajuste o potenciômetro de tempo no mínimo (~3 s) e o
  jumper em "repetível", para a presença responder rápido.
- PC com o Docker Desktop e o ESP-IDF. Um power bank, se a pulseira for andar solta.
- Uma almofada para a queda.

## Hoje: preparação e ensaio (uma vez)

1. **MACs:** com cada placa na USB, no PowerShell do ESP-IDF, rode
   `esptool.py --port COMx read-mac`. Anote os dois.
2. **Rede:** ligue o "Hotspot móvel" do Windows (Configurações → Rede e Internet), na banda de **2,4 GHz**, com
   nome e senha. O PC fica em `192.168.137.1`. Se ele tiver internet, o hub acerta o relógio, o que só é preciso
   para o passo opcional da inatividade.
3. **Firewall** (uma vez, como administrador): a regra das portas 1883/9001 do `interface/README.md`, seção 4.
4. **Central:** abra o Docker Desktop e, em `interface/`, rode `docker compose up -d`.
5. **Hub:**
   - no `menuconfig` do perfil demo, preencha:
     - o Wi-Fi do hotspot;
     - `mqtt://192.168.137.1:1883`, usuário `hub`, senha `MQTT_HUB_PASSWORD` do `.env`;
     - o MAC da pulseira;
     - PMK e LMK (16 caracteres);
     - a versão da DevKit;
   - grave e abra o monitor.
6. **Pulseira:** no `menuconfig` do build `build/dupla`, coloque o MAC do hub e as mesmas PMK e LMK, e grave.
7. **Ensaio completo:** a sequência de "Na sala", do início ao fim, conferindo cada linha esperada.
   - Tire um *print* do monitor do hub com `PING`, `heartbeat` e `FALL`. É a foto `serial_espnow` que falta no
     relatório final.
   - Grave uma sessão da pulseira com `demo_pulseira.py --gravar ensaio.txt` (plano B).
8. **Depois do ensaio:** os dados ficam no InfluxDB. Na sala, use o intervalo dos **últimos 15 minutos** no
   Grafana, para não misturar com o ensaio.

## Na sala

### Montagem (antes de chamar o professor)

1. Ligue o hotspot e abra o Docker Desktop. A central sobe sozinha se já estava rodando; senão, rode
   `docker compose up -d`.
2. **Ligue o hub e abra o monitor dele** (`idf.py -B build/demo … -p COMx monitor`). Espere:
   - `hub MAC …` e `paired with …, encrypted`;
   - `connected, ip 192.168.137.x, channel N`, com **N de 1 a 11**;
   - `connected to mqtt://192.168.137.1:1883`;
   - os avisos esperados: `temperature sensor not fitted` e `door … not fitted`. Eles são declarados, não são
     erro;
   - o aquecimento do PIR, que leva 60 s.
3. **Ligue a pulseira** em até 90 s, senão o hub abre o alerta `pulseira_offline`, que se resolve sozinho quando
   ela aparece. No hub:
   - `PING from <id>: the wearable found the hub (rssi … dBm)`;
   - `heartbeat from <id>: rssi … dBm, activity … mg`, a cada 30 s.
4. **Grafana:** abra
   `http://localhost:3000/d/quartos?var-quarto=quarto101&from=now-15m&to=now&refresh=5s`, com o usuário e a
   senha do Grafana no `.env`. Devem aparecer:
   - **Hub** e **Pulseira** "Online";
   - temperatura e portas como "sem leitura", porque não estão instaladas.

   Antes de o hub conectar, todo o topo fica em "sem leitura": o painel só mostra o que chegou na conexão atual
   do hub, e nada da rodada do simulador nem do ensaio de hoje.
5. **Tela:** o monitor do hub de um lado e o Grafana do outro. Opcional: a pulseira na USB, com o
   `demo_pulseira.py --porta COMy` **sem** `--mqtt-host`, mostrando o sinal ao vivo.

### Sequência (~8 min)

| # | Faça | Aparece | Diga |
|---|---|---|---|
| 1 | Aponte o monitor do hub | `heartbeat from <id>: rssi …` a cada 30 s | ESP-NOW: sem roteador nem conexão, quadros curtos direto ao MAC do hub, criptografados (PMK + LMK) e confirmados pelo rádio. A pulseira achou o canal do hub sozinha, mandando PING em cada canal até um ser confirmado. |
| 2 | Passe a mão no PIR | `presence detected`; a linha do tempo no Grafana | O hub publica por MQTT (QoS 1, retido). Ele também espelha a presença no CAN, mas o quadro só volta com um transceptor ligado, que a protoboard não tem. |
| 3 | Solte a pulseira de pé sobre a almofada e deixe-a deitada | `FALL from <id>, a2 = …, P = … %` e `alert queda ativo [queda-…]`; uma linha nova em "Alertas" em até ~15 s | A detecção roda na pulseira (gatilho → janela → 23 atributos → Random Forest). Pelo ar vão só 13 bytes: tipo, número de sequência, tempo, pico e P. A queda fica guardada e é reenviada até o hub confirmar, e o hub descarta a repetida. |
| 4 | `docker compose logs -f notificador` | a mensagem que iria ao Telegram | Sem o bot configurado, o notificador só imprime. O bot entra na entrega final. |
| 5 | Com botão: dois toques em até 15 s depois de uma queda | `FALL_CANCEL from <id>` e o alerta `cancelado` | O "estou bem" do residente fecha o episódio. |
| 6 | Com botão: segure 3 s | `HELP from <id>` e o alerta `ajuda` | Pedido de ajuda pelo mesmo caminho. |
| 7 | Desligue a pulseira | em 90 s, o alerta `pulseira_offline` | As regras de tempo rodam no hub: três heartbeats perdidos. Ao religar, ela volta. |

**Opcional, com o relógio do hub acertado** (`clock set by SNTP` no log do hub): a regra de inatividade do hub com o
PIR.
- Publique `inatividade_min` = 1:

  ```powershell
  docker compose exec mosquitto mosquitto_pub -u painel -P <MQTT_PAINEL_PASSWORD> -r -q 1 -t idoso/butanta/quarto101/config/desejada -m '{\"inatividade_min\":1}'
  ```

- Fique 1 min fora da vista do PIR: sai o alerta `inatividade`. Passe a mão, e ele é resolvido.
- **Depois, volte a 120** com o mesmo comando. A configuração fica retida no broker e gravada na memória do hub.

## Planos B

- **A rede ou o broker falham na sala:** o ESP-NOW não depende deles. O monitor do hub continua mostrando o
  heartbeat, a queda e o alerta, e o hub reconecta sozinho quando a rede volta.
- **O modelo não dispara na frente da plateia:** repita a queda com a placa de pé, deixando-a parada depois.
  Ou mostre a sessão do ensaio com `demo_pulseira.py --reproduzir ensaio.txt`.
- **A pulseira não acha o hub:** veja a tabela de problemas do [`firmware/room/README.md`](../../../../firmware/room/README.md).
  O mais comum: chaves ou MAC diferentes, ou a rede no canal 12 ou 13.
- **O Grafana fica vazio:** o quarto no seletor ou o intervalo de tempo.

## Depois da apresentação

- Se usou a inatividade, volte `inatividade_min` a 120.
- Marque em [`docs/pendencias.md`](../../../../docs/pendencias.md) o que foi visto na placa: os itens "Hub gravado
  e conferido", "Pulseira gravada e conferida" e a descoberta de canal.
