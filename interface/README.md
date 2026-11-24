# interface — central de monitoramento

A central recebe o que os hubs dos quartos publicam, guarda o histórico, mostra tudo no Grafana e avisa o
cuidador pelo Telegram. As regras e os alertas **não** rodam aqui: eles saem prontos dos hubs (ESP32), e a
central só armazena, mostra e notifica ([`docs/arquitetura.md`](../docs/arquitetura.md)). O formato de cada
mensagem está em [`docs/protocolo.md`](../docs/protocolo.md).

```
hubs (ou simulador.py) ──MQTT 1883──▶ Mosquitto ─┬─▶ Telegraf ─▶ InfluxDB (30 dias) ─▶ Grafana :3000
                                                 └─▶ notificador ─▶ Telegram do cuidador
```

| Peça | Pasta / arquivo | Função |
|---|---|---|
| Mosquitto | `mosquitto/` | broker com usuário e senha por cliente e ACL por tópico |
| Telegraf | `telegraf/telegraf.conf` | grava todos os tópicos `idoso/#` no InfluxDB, sem código |
| InfluxDB | (`docker-compose.yml`) | séries temporais, retenção de 30 dias |
| Grafana | `grafana/` | painel "Quartos": estado atual e histórico, com seletor de quarto |
| Notificador | `notificador/` | assina `idoso/+/+/alerta` e envia ao Telegram |
| Simulador | `simulador.py` | faz o papel de um hub, para testar sem hardware |
| Demonstração | `demo_pulseira.py` | uma placa só pela USB: o modelo de queda ao vivo e, opcionalmente, no papel do hub (seção 2) |
| Legado | `legado/` | a demonstração de 18/09/2026 (painel HTML, formato antigo), só como histórico |

## 1. Subir a central

Requer Docker (Docker Desktop no Windows; no Raspberry Pi, `docker` + `docker compose`).

```powershell
cd interface
copy .env.example .env      # depois troque TODAS as senhas no .env
docker compose up -d
docker compose ps           # os 5 serviços devem aparecer "running"
```

| Endereço | O quê |
|---|---|
| `localhost:1883` | MQTT (hubs, simulador) |
| `localhost:9001` | MQTT sobre WebSocket |
| http://localhost:3000 | Grafana (usuário e senha `GRAFANA_ADMIN_*` do `.env`) |
| http://localhost:8086 | InfluxDB (usuário e senha `INFLUX_ADMIN_*` do `.env`) |

Os usuários do broker são fixos (`hub`, `pulseira`, `telegraf`, `notificador`, `painel`), porque o arquivo
`mosquitto/acl` se refere a eles; só as senhas vêm do `.env`. Trocou uma senha? `docker compose restart mosquitto`.
O usuário `pulseira` só existe para o MQTT opcional da pulseira e só pode publicar a própria telemetria e o alerta.

Para apagar todo o histórico e começar do zero: `docker compose down -v`.

## 2. Testar sem hardware

```powershell
pip install paho-mqtt        # ou use o .venv desta pasta
python simulador.py --senha <MQTT_HUB_PASSWORD do .env> --intervalo-temp 5
```

O simulador publica estados, sinais de vida da pulseira e, sob comando, alertas. Como o hub, ele mede a
temperatura a cada 60 s mas só a publica quando muda 0,2 °C ou a cada 5 min, e manda o resumo da pulseira a cada
5 min (`--intervalo-temp` e `--intervalo-pulseira` mudam os intervalos; em `--intervalo-temp 5` o gráfico
anda mais depressa):

| Tecla + ENTER | Efeito |
|---|---|
| (só ENTER) | queda |
| `a` | pedido de ajuda (o botão da pulseira segurado) |
| `c` | cancela a última queda ou o último pedido de ajuda ("estou bem", os dois toques) |
| `s` / `b` / `t` | saída noturna / banheiro prolongado / temperatura alta |
| `r` | resolve os alertas abertos |
| `o` | pulseira sem sinal (de novo: volta) |
| `q` | encerra |

Abra o Grafana (painel **Quartos**) e acompanhe; os alertas aparecem na tabela e nos logs do notificador
(`docker compose logs -f notificador`). Um segundo quarto: rode outro simulador com `--quarto quarto102`.

### Demonstração com uma placa só

`demo_pulseira.py` mostra o modelo de queda funcionando com uma ESP e o MPU-6050 ligados pela USB, sem hub.

1. **Grave na placa o perfil de demonstração** ([`firmware/gadget/README.md`](../firmware/gadget/README.md), seção
   "Demonstração com uma placa só").
2. **Instale as dependências**, uma vez:

   ```powershell
   .venv\Scripts\pip install -r requirements-demo.txt     # pyserial, paho-mqtt, matplotlib
   ```
3. **Escolha um modo:**
   - só a janela:

     ```powershell
     .venv\Scripts\python demo_pulseira.py --porta COM5
     ```
   - janela e central, com o programa no papel do hub:

     ```powershell
     .venv\Scripts\python demo_pulseira.py --porta COM5 --mqtt-host <IP desta máquina> --senha <MQTT_HUB_PASSWORD>
     ```

A janela mostra:
- o \|a\| filtrado rolando, com a linha do gatilho;
- cada disparo com a P(queda) da placa:
  - azul: não é queda;
  - laranja: queda;
  - laranja vazado: "já avisada". A placa achou queda, mas o disparo veio logo depois de uma queda avisada,
    dentro da espera (5 s no perfil de demonstração, `CONFIG_GADGET_FALL_COOLDOWN_S`), e ela não avisa duas
    vezes a mesma queda. Para ver cada queda avisada, espere uns 5 s entre uma e outra;
- uma faixa grande "QUEDA DETECTADA" ou "PEDIDO DE AJUDA" (o botão da placa segurado por 3 s);
- os atributos da última janela (pico, queda livre, quietude, inclinação);
- os avisos da placa.

Com a central, o botão da placa também chega a ela: segurar 3 s publica o alerta `ajuda`, e dois toques publicam o
`cancelado` do último alerta.

Teclas: `c` = "estou bem" (publica o `cancelado` do último alerta, como os dois toques); `q` = sai.

Ao abrir a porta, o programa reinicia a placa, para ler no boot o id da pulseira e o modelo (`--sem-reset`
desliga isso).

Com a central, o programa publica no contrato do hub ([`docs/protocolo.md`](../docs/protocolo.md)):
- `status` online, com `"fw":"demo-usb"`;
- `pulseira/<id>` com a atividade: o primeiro logo, e depois um resumo a cada 5 min, como o hub;
- o `alerta` `queda` com `a2` e `prob`, a cada queda que a placa decide. Ele aparece na tabela de alertas do
  Grafana (coluna P(queda)) e, com o bot configurado, no Telegram.

Lembre do serviço Mosquitto do Windows: use o IP da máquina, não `localhost` (seção 7).

Para ensaiar sem a placa, `--gravar sessao.log` guarda o que chega da serial, e `--reproduzir sessao.log` toca a
sessão de novo no mesmo ritmo.

Se o link USB piscar (no Windows aparece `GetOverlappedResult failed`), o título da janela mostra "PERDIDA,
reabrindo…" e a leitura volta sozinha quando a porta reaparece, sem resetar a placa.

## 3. Telegram

1. Crie um bot com o [@BotFather](https://t.me/BotFather) e copie o token.
2. Mande qualquer mensagem ao bot e descubra o `chat_id` em
   `https://api.telegram.org/bot<token>/getUpdates`.
3. Preencha `TELEGRAM_TOKEN` e `TELEGRAM_CHAT_ID` no `.env` e rode `docker compose up -d notificador`.

Sem token, o notificador só imprime nos logs o que enviaria.

## 4. Ligar um hub de verdade

No firmware do hub (`idf.py menuconfig` em `firmware/room`):

- **MQTT:** URI `mqtt://<IP desta máquina>:1883`, usuário `hub`, senha = `MQTT_HUB_PASSWORD`, além de site e
  quarto.
- **Network (Wi-Fi):** SSID e senha do Wi-Fi.

A pulseira normalmente não fala com a central: manda tudo ao hub por ESP-NOW. Com a opção *Also join the
Wi-Fi and talk to the central over MQTT* (`menuconfig` → Wearable, em `firmware/gadget`), ela usa os mesmos
dois menus, com usuário `pulseira` e senha `MQTT_PULSEIRA_PASSWORD`, e o mesmo site e quarto do hub. Aí
publica `pulseira/<id>/telemetria` (medição `pulseira_telemetria` no InfluxDB) e, se o hub não confirmar uma
queda, o próprio alerta, que o notificador marca como "enviado pela própria pulseira".

Libere as portas 1883 e 9001 no firewall se o hub estiver em outra máquina:

```powershell
New-NetFirewallRule -DisplayName "Mosquitto MQTT" -Direction Inbound -Protocol TCP -LocalPort 1883,9001 -Action Allow
```

**Rede para uma demonstração:** o IP da máquina vai compilado na URI do hub, então convém uma rede em que ele não
mude. O "Hotspot móvel" do Windows (Configurações → Rede e Internet) deixa o PC sempre em `192.168.137.1`:
- escolha a banda de **2,4 GHz**, porque o ESP32 não usa 5 GHz;
- confira o canal na linha `connected, ip …, channel N` do hub: a pulseira só procura do 1 ao 11.

Para ver o hub chegando ao broker, sem o Grafana:

```powershell
docker compose exec mosquitto mosquitto_sub -u painel -P <MQTT_PAINEL_PASSWORD> -v -t "idoso/#"
```

Dentro do container não há o problema do serviço Mosquitto do Windows. Os passos da demonstração de duas placas
estão em [`firmware/room/README.md`](../firmware/room/README.md), "Demonstração: hub só com o PIR".

## 5. Ajustar as regras de um quarto

As regras rodam no hub, e os parâmetros mudam sem regravar a placa: publique em `config/desejada` com o
usuário `painel`. A mensagem é retida, então o hub a recebe mesmo se estiver desligado agora. O hub responde em
`config/aplicada`, com os valores em uso ou com o erro que fez recusar a configuração.

```bash
mosquitto_pub -h localhost -u painel -P <MQTT_PAINEL_PASSWORD> -r -q 1 \
  -t idoso/butanta/quarto101/config/desejada -m '{"saida_noturna_min":2,"banheiro_min":3}'
```

No PowerShell 5.1, escape as aspas internas com `\"`.

## 6. Testes

```powershell
python -m unittest discover -s notificador     # lógica do notificador
python -m unittest test_demo_pulseira          # leitura da serial, filtro e alerta do demo_pulseira.py
```

## 7. Problemas comuns

| Sintoma | Causa provável | Solução |
|---|---|---|
| O simulador conecta, mas nada chega ao Grafana nem ao notificador | o **serviço Mosquitto do Windows** escuta em `127.0.0.1:1883` e atende `localhost` antes do Docker | use `--host <IP da máquina>` (`ipconfig`), ou pare o serviço como administrador: `net stop mosquitto` e `Set-Service mosquitto -StartupType Manual` |
| `Connection Refused: not authorised` | senha errada, ou usuário diferente de `hub`/`pulseira`/`telegraf`/`notificador`/`painel` | confira o `.env`; trocou a senha? `docker compose restart mosquitto` |
| `docker compose up` reclama de `MQTT_PULSEIRA_PASSWORD` | `.env` criado antes desse usuário existir | acrescente `MQTT_PULSEIRA_PASSWORD=<senha>` ao `.env` (modelo em `.env.example`) |
| Mosquitto reinicia sem parar, com `set: illegal option -` no log | `mosquitto/entrypoint.sh` salvo com fim de linha CRLF | converta para LF (o `.gitattributes` já força LF nos `*.sh` do git) |
| `docker compose up` reclama de variável | `.env` ausente ou incompleto | `copy .env.example .env` e preencha |
| Painel do Grafana vazio | ainda não chegou nenhuma mensagem, ou o quarto escolhido no topo não é o do hub | rode o simulador e escolha o quarto no seletor |
| Topo do painel em "sem leitura", com o histórico embaixo | o hub está offline: presença, portas, pulseira e temperatura do topo só valem para o que chegou na conexão atual do hub (e a temperatura, nos últimos 15 min) | ligue o hub (ou o simulador); os estados voltam assim que ele conecta, porque ele republica todos ao conectar |
| Um tópico chega ao broker (`mosquitto_sub` mostra), mas não ao InfluxDB | o consumidor do Telegraf usa sessão persistente: o broker guarda as assinaturas antigas, e o Telegraf não assina os tópicos novos | ao mudar os `topics` de um consumidor no `telegraf.conf`, troque também o `client_id` dele e `docker compose restart telegraf` |
| Hub não conecta | IP, usuário ou senha errados no `menuconfig`, ou firewall | veja o log do hub e `docker compose logs mosquitto`; libere a porta 1883 |
