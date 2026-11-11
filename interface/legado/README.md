# Legado — demonstração de 18/09/2026 (broker + dashboard)

> **Histórico, não é mais o sistema.** Esta pasta reproduz a entrega parcial do S2 P2 exatamente como foi
> apresentada: broker anônimo, formato antigo de mensagens (`idoso/quarto101/<sensor>`, `ts` em uptime) e o
> painel em arquivo único. A central atual está em [`interface/`](../README.md) e segue
> [`docs/protocolo.md`](../../docs/protocolo.md). Rode os comandos abaixo **de dentro de `interface/legado/`**.

Camada de recepção e visualização do **Sistema de Monitoramento Não Invasivo de Idosos** (Escola Politécnica da USP).
O hub do quarto (ESP32-S3) publica os dados de quatro sensores no broker **Eclipse Mosquitto** via Wi-Fi; esta pasta
contém o broker configurado, um painel web que assina esses dados em tempo real e um simulador para testar sem o hardware.
Base teórica: [`Entrega Q1.pdf`](<../../relatorios/Semestre 2/Projeto 1/Entrega Q1.pdf>) e
[`Entrega Q2 Q3.pdf`](<../../relatorios/Semestre 2/Projeto 1/Entrega Q2 Q3.pdf>) (Q1 — interface MQTT; Q3 — rede sem fio).

```
 ESP32-S3 (hub do quarto)  ──MQTT/TCP──▶ :1883 ┐                        ┌─MQTT/WebSocket─▶ :9001 ─▶ dashboard.html (navegador)
 simulador_legado.py (substituto) ──MQTT/TCP──▶ :1883 ┤  Mosquitto (broker)   ┤
                                               ┘  roteia por tópico    └─MQTT/TCP───────▶ :1883 ─▶ mosquitto_sub (terminal, opcional)
```

| Arquivo | Função |
|---|---|
| `mosquitto.conf` | Configuração do broker: MQTT na porta 1883 (ESP32) e MQTT sobre WebSockets na 9001 (navegador), acesso anônimo |
| `dashboard.html` | Painel em arquivo único (HTML + CSS + JS). Abre direto no navegador, sem servidor nem build |
| `simulador_legado.py` | Publica dados simulados dos 4 sensores no lugar do ESP32 (Python + paho-mqtt) |
| `mqtt.min.js` | Cópia local da biblioteca MQTT.js 5.16.0 — plano B usado automaticamente pelo painel quando o CDN não responde (sem internet) |

## Tópicos e payloads

Definidos em [`docs/protocolo.md`](../../docs/protocolo.md) §1 — a fonte única da verdade para tópicos, QoS, retain
e campos JSON. O painel, o simulador e os exemplos das seções 4 e 5 abaixo ainda usam o formato anterior
(`idoso/quarto101/<sensor>`, sem `site`, com tópico próprio de queda); as diferenças estão listadas no fim do §1
do protocolo.

## 1. Instalar o Mosquitto

### Windows 10/11

Opção A — winget:

```powershell
winget install --id EclipseFoundation.Mosquitto -e
```

Opção B — instalador `mosquitto-2.1.2-install-windows-x64.exe` em https://mosquitto.org/download/ (Next, Next, Finish).

Os programas ficam em `C:\Program Files\mosquitto` (`mosquitto.exe`, `mosquitto_pub.exe`, `mosquitto_sub.exe`) e
**não entram no PATH**. Em cada terminal novo, adicione a pasta ao PATH da sessão:

```powershell
$env:Path += ";C:\Program Files\mosquitto"
```

O instalador também registra um **serviço do Windows** chamado `mosquitto` (configuração padrão, sem WebSockets).
Se ele estiver em execução, a porta 1883 fica ocupada e o nosso broker não sobe. Pare-o (terminal como Administrador):

```powershell
net stop mosquitto
```

O serviço volta sozinho a cada reinicialização do Windows. Para que ele fique parado até você iniciá-lo à mão
(recomendado na máquina da apresentação), ainda como Administrador:

```powershell
Set-Service -Name mosquitto -StartupType Manual
```

### Linux (Debian/Ubuntu/Raspberry Pi OS)

```bash
sudo apt update && sudo apt install -y mosquitto mosquitto-clients
```

O pacote sobe automaticamente um serviço com a configuração padrão (só localhost, sem WebSockets). Pare-o antes de
usar o nosso arquivo:

```bash
sudo systemctl stop mosquitto
```

Para o serviço passar a usar esta configuração permanentemente (ex.: no Raspberry Pi da central), copie apenas as
linhas `listener`, `protocol` e `allow_anonymous` para `/etc/mosquitto/conf.d/idoso.conf` e execute
`sudo systemctl restart mosquitto`.

## 2. Executar o broker

Na pasta deste projeto:

```powershell
& "C:\Program Files\mosquitto\mosquitto.exe" -c .\mosquitto.conf
```

```bash
mosquitto -c mosquitto.conf
```

Saída esperada (os dois listeners abertos):

```
2026-09-18T10:40:53: mosquitto version 2.1.2 starting
2026-09-18T10:40:53: Config loaded from mosquitto.conf.
2026-09-18T10:40:53: Bridge support available.
2026-09-18T10:40:53: Persistence support available.
2026-09-18T10:40:53: TLS support available.
2026-09-18T10:40:53: TLS-PSK support available.
2026-09-18T10:40:53: Websockets support available.
2026-09-18T10:40:53: Opening ipv6 listen socket on port 1883.
2026-09-18T10:40:53: Opening ipv4 listen socket on port 1883.
2026-09-18T10:40:53: Opening ipv6 listen socket on port 9001.
2026-09-18T10:40:53: Opening ipv4 listen socket on port 9001.
2026-09-18T10:40:53: mosquitto version 2.1.2 running
```

As duas últimas linhas de "Opening ... port 9001" são o listener WebSocket (o log não distingue o protocolo; a linha
"Websockets support available" confirma que o binário tem o suporte).

Deixe esse terminal aberto: ele mostra cada conexão, assinatura e publicação. Para diagnóstico extra, acrescente `-v`.

**Firewall** (só se o ESP32 ou o navegador estiverem em outra máquina): libere as portas TCP 1883 e 9001. No Windows,
em PowerShell como Administrador:

```powershell
New-NetFirewallRule -DisplayName "Mosquitto MQTT" -Direction Inbound -Protocol TCP -LocalPort 1883,9001 -Action Allow
```

O endereço do broker será configurável no firmware do hub (o relatório usa `mqtt://192.168.0.10:1883` como exemplo).
Descubra o IP do PC com `ipconfig` (Windows) ou `ip addr` (Linux) e configure o firmware — ou fixe o IP do PC — para que
coincidam.

## 3. Abrir o dashboard

1. Dê dois cliques em `dashboard.html` (Chrome, Edge ou Firefox).
2. No topo, confira **host** e **porta** (padrão `localhost` e `9001`) e clique em **Conectar**. A pílula de status passa
   a **Conectado** e o terminal do broker registra `New client connected ... as dashboard-xxxxxx`.
3. Se o navegador estiver em outro computador, informe o IP do PC do broker no campo host, ou abra
   `dashboard.html?host=192.168.0.10&port=9001`.

O painel carrega a biblioteca MQTT.js 5.16.0 de um CDN e, se o CDN não responder (sala sem internet), usa
automaticamente a cópia local `mqtt.min.js` que acompanha esta pasta — basta mantê-la ao lado do `dashboard.html`.
Para atualizar essa cópia:

```powershell
Invoke-WebRequest -Uri https://cdn.jsdelivr.net/npm/mqtt@5.16.0/dist/mqtt.min.js -OutFile mqtt.min.js
```

```bash
curl -L -o mqtt.min.js https://cdn.jsdelivr.net/npm/mqtt@5.16.0/dist/mqtt.min.js
```

O alerta sonoro de queda só toca depois de uma interação com a página (política dos navegadores): o clique em
**Conectar** já libera o áudio. Use **Testar som** para conferir o volume antes da apresentação.

## 4. Teste manual com mosquitto_pub (simulando o ESP32)

Com o broker rodando e o dashboard conectado, abra um segundo terminal e publique uma mensagem por tópico. As aspas
do JSON dependem do shell.

**Linux / macOS / Git Bash**

```bash
mosquitto_pub -h localhost -t idoso/quarto101/temperatura -q 0 -m '{"sensor":"LM35DZ","valor":22.5,"ts":244316}'
mosquitto_pub -h localhost -t idoso/quarto101/porta       -q 1 -m '{"sensor":"MC-38","estado":"aberta","ts":250316}'
mosquitto_pub -h localhost -t idoso/quarto101/presenca    -q 1 -m '{"sensor":"HC-SR501","evento":"presenca","ts":257196}'
mosquitto_pub -h localhost -t idoso/quarto101/queda       -q 1 -m '{"sensor":"MPU-6050","evento":"queda","a2":7.88,"ts":243946}'
```

**Windows PowerShell 5.1** (o padrão do Windows). As aspas internas precisam de `\"`; sem isso o PowerShell as remove
ao chamar o `.exe` e o JSON chega ao broker como `{sensor:LM35DZ,...}`, que o painel não consegue interpretar:

```powershell
mosquitto_pub -h localhost -t idoso/quarto101/temperatura -q 0 -m '{\"sensor\":\"LM35DZ\",\"valor\":22.5,\"ts\":244316}'
mosquitto_pub -h localhost -t idoso/quarto101/porta       -q 1 -m '{\"sensor\":\"MC-38\",\"estado\":\"aberta\",\"ts\":250316}'
mosquitto_pub -h localhost -t idoso/quarto101/presenca    -q 1 -m '{\"sensor\":\"HC-SR501\",\"evento\":\"presenca\",\"ts\":257196}'
mosquitto_pub -h localhost -t idoso/quarto101/queda       -q 1 -m '{\"sensor\":\"MPU-6050\",\"evento\":\"queda\",\"a2\":7.88,\"ts\":243946}'
```

No PowerShell 7.3 ou mais novo, use a forma do Linux (sem `\`).

**Prompt de Comando (cmd.exe)**

```cmd
mosquitto_pub -h localhost -t idoso/quarto101/queda -q 1 -m "{\"sensor\":\"MPU-6050\",\"evento\":\"queda\",\"a2\":7.88,\"ts\":243946}"
```

Resultado esperado: o card correspondente atualiza na hora, a linha aparece no log do painel e, no caso da queda, o
banner vermelho **QUEDA DETECTADA — Quarto 101** surge com o alerta sonoro (clique em **Reconhecer alerta** para
silenciar).

Para ver o tráfego bruto em outro terminal:

```bash
mosquitto_sub -h localhost -t "idoso/quarto101/#" -v
```

## 5. Dados simulados contínuos (simulador_legado.py)

```powershell
pip install paho-mqtt
python simulador_legado.py --intervalo-temp 5
```

- `--intervalo-temp 5` acelera a temperatura (o firmware real publica a cada 30 s); porta e presença variam sozinhas
  em intervalos aleatórios.
- **ENTER** publica uma queda; `--auto-queda 60` publica uma a cada 60 s; `--host` aponta para outro broker.
- Ao conectar, publica `online` (retido) em `idoso/quarto101/status`; ao sair com **Ctrl+C**, publica `offline`.
  Se o processo for morto sem aviso, o próprio broker publica o `offline` (Last Will and Testament).

## 6. Roteiro sugerido para a apresentação

1. Terminal 1: subir o broker (seção 2) e mostrar os dois listeners abertos.
2. Navegador: abrir o dashboard, clicar em Conectar e mostrar a conexão aparecendo no log do broker.
3. Terminal 2: `python simulador_legado.py --intervalo-temp 5` — os cards ganham vida e a pílula "Gateway" fica online.
4. ENTER no simulador — banner vermelho + som — clicar em Reconhecer alerta.
5. Ctrl+C no simulador — "Gateway: offline" (Last Will / retain). Opcional: publicar uma mensagem à mão com
   `mosquitto_pub` (seção 4).

## 7. Problemas comuns

| Sintoma | Causa provável | Solução |
|---|---|---|
| `Error: Address already in use` ao subir o broker | serviço `mosquitto` já ativo na porta 1883 | `net stop mosquitto` (Windows) / `sudo systemctl stop mosquitto` (Linux) |
| Painel preso em "Conectando" / "Reconectando" | broker parado, porta 9001 errada ou firewall | confira o terminal do broker e o host/porta; libere 1883/9001 no firewall |
| Painel avisa que a biblioteca MQTT.js não carregou | sem internet e `mqtt.min.js` não está ao lado do HTML | mantenha (ou baixe de novo) `mqtt.min.js` na mesma pasta (seção 3) |
| O log do painel mostra a mensagem, mas o card não atualiza | JSON inválido — no PowerShell 5.1 faltou `\"` | use a forma da seção 4 correspondente ao seu shell |
| ESP32 não conecta | broker em IP diferente de `192.168.0.10`, redes Wi-Fi distintas ou firewall | `ipconfig` / `ip addr`; mesma rede; regra de firewall |
| Broker recusa a conexão (`not authorised`) | `allow_anonymous` ausente (Mosquitto 2.0 ou mais novo) | rodar com `-c mosquitto.conf` deste projeto |
| Sem som na queda | áudio bloqueado até a primeira interação com a página | clicar em Conectar / Testar som antes |

## 8. Próximos passos

O plano mudou em relação ao relatório: as regras de alerta rodam no hub (ESP32), não no Node-RED, e a central passa a
ser Mosquitto com autenticação, Telegraf, InfluxDB, Grafana e um notificador Telegram, tudo em Docker Compose. Veja a
fase 5 de [`docs/roadmap.md`](../../docs/roadmap.md). O bloco comentado no fim de `mosquitto.conf` já indica as
diretivas de autenticação e TLS.
