# Monitoramento de Idosos com ESP32

Rede de sensores para o monitoramento contínuo e não invasivo de idosos em Instituições de Longa Permanência
(ILPIs). Projeto das disciplinas Projeto Integrativo 2 e 3 da Escola Politécnica da USP.

- **Hub do quarto** (ESP32-S3):
  - presença (HC-SR501), temperatura (LM35DZ) e portas (reed switch MC-38, quantas forem necessárias);
  - roda as regras de alerta (saída noturna, banheiro, inatividade, temperatura);
  - integra o barramento CAN da sala;
  - publica tudo via MQTT.
- **Pulseira** (ESP32-S3): o dispositivo que o residente usa no pulso, com MPU-6050; detecta a queda e avisa o
  hub por ESP-NOW.

No protótipo, o hub e a pulseira são a mesma PCB com firmwares diferentes. Desde 2026-11-17 o nome é sempre
**pulseira** (antes, "vestível"); no código C, em inglês, ela é `gadget`/`wearable`.
- **Central:** Mosquitto, Grafana (estado de todos os quartos e histórico) e Telegram para o cuidador. No
  futuro, banco de dados, backend e nuvem.

```
Pulseira ──ESP-NOW──▶ Hub do quarto ──MQTT──▶ Mosquitto ─┬─▶ Telegraf ─▶ InfluxDB ─▶ Grafana
                          │                               └─▶ notificador ─▶ Telegram
                          └──CAN──▶ demais módulos da sala
```

## Estrutura

```
projeto/
├── CLAUDE.md            contexto para o Claude Code
├── docs/
│   ├── protocolo.md     contrato entre placas e central (fonte única da verdade)
│   ├── arquitetura.md   dispositivos, regras, central e estado atual
│   ├── requisitos.md    requisitos vigentes e plano de avaliação
│   ├── roadmap.md       plano de implementação em fases
│   ├── estado.md        em que etapa estamos e o que já foi verificado
│   ├── decisoes.md      histórico das decisões e o que dá para melhorar
│   └── coleta-de-dados.md  protocolo de ensaios para o futuro modelo de queda
├── firmware/
│   ├── room/            hub do quarto (ESP-IDF)
│   ├── gadget/          pulseira (ESP-IDF)
│   └── common/          componentes das duas placas: contrato, rede, MQTT, CAN, RTOS, LED
├── interface/           central em Docker Compose + simulador; legado/ = demonstração de 18/09
├── hardware/            esquemáticos, PCB e mapa de pinos
├── relatorios/          enunciados e entregas por semestre e projeto
└── aulas/               slides das aulas (molde do firmware)
```

## Início rápido

**Central sem hardware** — detalhes em [`interface/README.md`](interface/README.md):

```powershell
cd interface
copy .env.example .env                 # e troque as senhas
docker compose up -d                   # Grafana em http://localhost:3000
python simulador.py --host <IP da máquina> --senha <MQTT_HUB_PASSWORD do .env>
```

**Firmware** (ESP-IDF 6.0.1, alvo `esp32s3`) — detalhes em [`firmware/room/README.md`](firmware/room/README.md)
e [`firmware/gadget/README.md`](firmware/gadget/README.md). Para conferir uma PCB recém-soldada, o teste de
hardware [`firmware/hwtest/`](firmware/hwtest/README.md) lê todas as peças com uma gravação só:

```powershell
cd firmware\room
idf.py menuconfig                      # Wi-Fi, broker, pareamento com a pulseira
idf.py build
idf.py -p COMx flash monitor
```

Em que etapa o projeto está: [`docs/estado.md`](docs/estado.md). O que falta fazer: [`docs/pendencias.md`](docs/pendencias.md).

## Documentação

- [Arquitetura](docs/arquitetura.md)
- [Protocolo](docs/protocolo.md)
- [Requisitos](docs/requisitos.md)
- [Roadmap](docs/roadmap.md)
- [Estado atual](docs/estado.md)
- [Pendências: o que falta fazer](docs/pendencias.md)
- [Decisões e revisões](docs/decisoes.md)
- [Coleta de dados](docs/coleta-de-dados.md)
- [Relatórios e enunciados](relatorios/README.md)
