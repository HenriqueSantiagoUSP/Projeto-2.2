# Roadmap — plano de implementação

Ordem das fases de implementação, com o objetivo e o critério de pronto de cada uma. **Em que fase estamos e o
que já foi verificado fica em [`estado.md`](estado.md)**, e a lista viva do que falta, item por item, em
[`pendencias.md`](pendencias.md); o porquê de cada decisão, em [`decisoes.md`](decisoes.md). Mudou o plano? Atualize este arquivo e registre em `decisoes.md`.

Princípios que definem a ordem:

- **Processamento na borda:** a queda é detectada na pulseira e as regras de tempo rodam no hub
  (diretriz da disciplina). A central só armazena, mostra e notifica.
- **Contrato antes do código:** tópicos, payloads e mensagens são definidos em
  [`protocolo.md`](protocolo.md) antes de implementar.
- **Caminho ponta a ponta cedo:** a queda pode ser simulada por botão até existir detecção; o algoritmo vem
  depois do enlace.

## Fase 0 — Feito

- Hub: presença e porta → CAN (modo bancada, `CAN_SELF_TEST`).
- Broker Mosquitto, painel web e simulador (provisórios).
- Estrutura do repositório e documentação.

## Fase 1 — Contrato em código (`firmware/common/`)

- Header da mensagem ESP-NOW: cabeçalho, tipos e payloads.
- Nomes de tópicos e tipos de alerta como constantes.
- Ligado ao `room` e ao `gadget` por `EXTRA_COMPONENT_DIRS`.

**Pronto quando:** os dois firmwares compilam incluindo o header, e ele bate com `protocolo.md`.

## Fase 2 — Hub: sensores locais corretos

- Corrigir o debounce: após uma borda, esperar ~50 ms sem novas bordas, ler o nível e publicar só se mudou.
- Portas modulares: tabela `{gpio, id}` no firmware e papel (`saida`, `banheiro`, `outra`) na config; um
  nibble CAN por porta.
- LM35 no ADC1 com `adc_cali` e a curva MMQ `T = (V_mV − 144,739) / 51,3335`; 16 amostras a cada 30 s.
- PIR: aguardar o aquecimento de 60 s antes de considerar as leituras.

**Pronto quando:**
- abrir e fechar rápido registra os dois eventos;
- 100 acionamentos seguidos terminam no estado correto;
- a temperatura fica a ±1 °C de um termômetro de referência.

## Fase 3 — Hub: rede e MQTT

- Wi-Fi STA com `WIFI_PS_NONE` e reconexão com *backoff* exponencial.
- SNTP. O servidor é configurável; ainda falta subir um servidor NTP local no Raspberry Pi para funcionar sem
  internet.
- MQTT com usuário e senha, Last Will, estados retidos e outbox para QoS 1.
- Configuração: `config/desejada` → validação → NVS → `config/aplicada`.
- Task watchdog assinado pelas tasks críticas.

**Pronto quando:**
- os estados aparecem no broker com `ts` em epoch;
- derrubar e religar o Wi-Fi não perde nenhum evento QoS 1;
- mudar um parâmetro por MQTT tem efeito sem regravar a placa.

## Fase 4 — Hub: regras e alertas

- Motor de regras ([`arquitetura.md`](arquitetura.md), seção Regras) publicando episódios em `alerta`
  (`ativo` → `resolvido`/`cancelado`).
- Perfil de configuração "demo", com tempos encurtados.
- LED de status (1 Hz normal, 4 Hz em alerta); sem buzzer desde 2026-10-23.

**Pronto quando:** cada regra foi testada com o perfil demo, e nenhuma repete o alerta no mesmo episódio.

## Fase 5 — Central

- Docker Compose com:
  - Mosquitto com usuário/senha e ACL;
  - Telegraf (MQTT → InfluxDB);
  - InfluxDB com retenção de 30 dias;
  - Grafana com painéis versionados: visão de todos os quartos, histórico e alertas;
  - notificador Telegram em Python.
- Simulador atualizado para o contrato novo.
- `dashboard.html` aposentado: foi para `interface/legado/`, junto com a demonstração de 18/09.

**Pronto quando:**
- `docker compose up` funciona numa máquina limpa;
- um alerta simulado chega ao Telegram em ≤ 5 s;
- o Grafana mostra todos os quartos.

## Fase 6 — Pulseira: aquisição e enlace

- Driver do MPU-6050: ±8 g, filtro digital (DLPF), 200 Hz pela taxa interna (`SMPLRT_DIV`) com FIFO ou pino
  INT. Nada de `vTaskDelay` para temporizar.
- Modo de coleta: amostras brutas em CSV pela serial.
- ESP-NOW: varredura de canal, heartbeat com bateria e atividade, criptografia.
- Queda simulada por botão.

**Pronto quando:**
- 200 Hz ± 5 ms medidos;
- o hub publica a pulseira como online/offline;
- uma queda por botão vira alerta no Telegram.

## Fase 7 — Pulseira: detecção e energia

- **Detecção:** o pipeline do PC na pulseira (`fall_pipeline`, desde 2026-11-11):
  - filtro de 20 Hz;
  - gatilho em picos de \|a\| ≥ 2,0 g;
  - janela de −1,0 a +0,8 s;
  - 23 atributos;
  - Random Forest, ou regressão logística escolhida no `menuconfig`.

  Até 2026-11-10 era a segmentação provisória de a² com histerese (L<sub>on</sub> 6,25 g², L<sub>off</sub>
  2,25 g²). O modelo é provisório até ser retreinado com os dados do pulso (fase 8).
- Cooldown de 30 s após uma queda; `FALL` com reenvio até o ACK.
- Botão "estou bem" (`FALL_CANCEL`), com o LED em alerta durante a janela de cancelamento.
- Leitura da bateria e alerta `bateria_baixa`.
- *Light sleep* entre leituras do FIFO, depois de medir o consumo em placa: a estratégia depende do número
  medido.

**Pronto quando:** latência pico → MQTT ≤ 500 ms e autonomia da pulseira medida.

## Fase 8 — Coleta de dados

- Protocolo de ensaios rotulados, com várias pessoas:
  - quedas em colchão: frente, costas, lado, joelhos;
  - atividades diárias: andar, sentar, levantar, deitar;
  - movimentos de braço: palmas, acenar, bater na mesa (o pulso é a posição mais difícil).
- Formato de arquivo e metadados: pessoa, posição do sensor, atividade, rótulo.
- Janelas brutas em torno de cada disparo, gravadas pela serial agora e enviadas à central/nuvem depois. O
  buffer circular e os atributos de cada janela (linhas `FEAT:` do modo de coleta) já estão na pulseira.
- Dados grandes ficam fora do git, com a descrição versionada.

**Pronto quando:** existe um conjunto de dados descrito e reprodutível.

**Situação (2026-11-09):**
- A SisFall (38 pessoas, cintura) e 18 ensaios próprios (uma pessoa, cintura, ±2 g) estão descritos em
  [`dados/README.md`](../dados/README.md).
- Os modelos foram comparados no PC em [`modelo/`](../modelo/README.md) (S2 P3 Q1).
- Falta a coleta no pulso com o firmware atual (±8 g), com várias pessoas.

## Fase 9 — Avaliação

- Latências e disponibilidade (metas em [`requisitos.md`](requisitos.md)).
- Sensibilidade e especificidade do limiar provisório: é a linha de base do modelo futuro.
- Falsos alarmes por dia em uso contínuo.
- Cobertura da pulseira pelo `rssi` (quarto, banheiro, corredor).
- 24 h de operação sem reinício.

**Pronto quando:** existe a tabela de resultados para o relatório final.

## Futuro (sem data)

- **Detecção de queda por modelo preditivo:** pipeline de processamento complexo treinado com grande volume
  de dados (fase 8). Onde ele roda (pulseira, hub ou nuvem) e como é atualizado (OTA) se decide quando houver
  dados.
  - **Primeiro passo feito no PC (2026-11-09, [`modelo/`](../modelo/README.md)):**
    - pipeline filtro → gatilho (picos > 2,0 g) → janela → atributos → classificador;
    - Random Forest e regressão logística com < 1,5 alarme falso/h e ~94 % das quedas, contra ~70 alarmes/h
      do limiar atual.
  - **Na pulseira desde 2026-11-11** (`fall_pipeline`):
    - gatilho causal e 23 atributos no tempo;
    - Random Forest de 60 árvores: 0,55 alarme falso/h e 95,1 % das quedas com ≤ 1 alarme/h na SisFall;
    - paridade C × Python testada no PC.
  - **Próximo:**
    - conferir na bancada;
    - retreinar com os dados do pulso;
    - decidir a atualização do modelo (OTA). Ver [`pendencias.md`](pendencias.md).
- **Backend + banco relacional:** residentes, quartos, dispositivos, cuidadores, alertas com ciclo de vida
  (ativo → reconhecido → resolvido) e auditoria. O banco final é **PostgreSQL**. O esquema foi prototipado em SQLite
  em 2026-11-10, na atividade de Fundamentos de Software (ver [`pendencias.md`](pendencias.md)).
- **Nuvem:** bridge do Mosquitto local para um broker na nuvem com TLS e credenciais por dispositivo. O sistema
  local continua funcionando sem internet.
- **App ou web do cuidador,** com reconhecimento de alertas.
- **OTA:** atualização de firmware pela rede.
- **Configuração da pulseira pelo hub:** mensagem `CONFIG` no ESP-NOW (tipo `0x05`, reservado).
- **Cobertura além do quarto:** vários hubs e roaming da pulseira. O Wi-Fi como reserva da queda já existe
  como opção da pulseira (MQTT direto, desligado por padrão); falta medir o custo em bateria e decidir se fica
  ligado.

## Pendências de hardware que bloqueiam fases

| Pendência | Bloqueia |
|---|---|
| PCB única (S2 P2): esquemático e layout sobre o mapa proposto ([`hardware/README.md`](../hardware/README.md)) | fases 2–4 no hardware final |
| Revisão da DevKitC-1 (LED no GPIO48 ou no GPIO38: `menuconfig` → PCB) | LED de status |
| Bateria, carregador e leitura de tensão da pulseira | fase 7 |
