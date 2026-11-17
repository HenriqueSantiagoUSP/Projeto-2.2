# firmware/common

Componentes ESP-IDF usados pelas duas placas. Os dois projetos os incluem com
`set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../common")` no `CMakeLists.txt` raiz; com `MINIMAL_BUILD`,
cada componente usado também entra no `PRIV_REQUIRES` de quem o usa. Nada aqui conhece os pinos ou os tópicos
de uma placa: ela os passa por uma struct de configuração (`can_config_t`, `net_config_t`,
`mqtt_link_config_t`, `indicators_config_t`).

| Componente | O quê |
|---|---|
| `pcb` | o mapa de pinos da PCB única das duas placas (`pcb_pins.h`), com as regras (pinos proibidos, reservados, ADC1, RTC, I²C do ULP) verificadas em compilação. Menu **PCB**: versão da DevKit |
| `protocol` | o contrato de [`docs/protocolo.md`](../../docs/protocolo.md) em C: `protocol_mqtt.h` (tópicos e valores de `tipo`/`estado`), `protocol_can.h` (nibbles e payloads do grupo `0xF`) e `protocol_espnow.h` (mensagens ESP-NOW, com os tamanhos verificados em tempo de compilação) |
| `report` | quando publicar uma leitura periódica: ao mudar mais que um limiar desde o último envio, ou a cada intervalo máximo, e o RMS de janelas. C puro, com testes no PC (`test_host/`). O hub o usa na temperatura e no resumo da pulseira; a pulseira, na telemetria MQTT |
| `rtos_util` | `task_config.h`: prioridades nomeadas (`PRIO_CRITICA` 20 … `PRIO_MINIMA` 2), núcleos (`CORE_PROTOCOL` 0, `CORE_REALTIME` 1) e pilhas, como no slide de RTOS; `rtos_err.h` |
| `sys_events` | event group do sistema: `SYS_EV_NET_CONNECTED`, `SYS_EV_TIME_VALID`, `SYS_EV_MQTT_CONNECTED`, `SYS_EV_CAN_RUNNING`, `SYS_EV_CAN_BUS_OFF`. Cada bit tem um só escritor. `sys_events_init()` é a primeira chamada do `app_main` |
| `net` | Wi-Fi STA com ou sem associação (`join_ap`), economia de energia opcional, reconexão com recuo de 1 a 60 s, SNTP e fuso. Menu **Network (Wi-Fi)** |
| `mqtt_link` | cliente MQTT 3.1.1 (`espressif/mqtt`), Last Will e assinatura informados pela placa, outbox para QoS 1. Menu **MQTT** |
| `can` | TWAI a 250 kbit/s: qualquer task enfileira (`can_transmit`), a task `can_tx` transmite e acompanha o estado do nó (recupera *bus-off* sozinha), a `can_rx` decodifica pelo diretório `can_ids.h` e chama os handlers. Filtro de hardware só para IDs `0x0GS`. Menu **CAN bus (TWAI)** |
| `indicators` | LED WS2812 da DevKit e um buzzer opcional (a PCB atual não tem: `GPIO_NUM_NC`), numa task própria; quem chama só define o estado (protegido por `portMUX`) e nunca bloqueia |

Mudou o contrato? Altere `docs/protocolo.md` e `protocol/` no mesmo commit. Num layout de mensagem ESP-NOW que
uma placa antiga leria errado, incremente também `ESPNOW_MSG_VERSION`.
