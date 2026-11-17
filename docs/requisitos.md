# Requisitos

Requisitos vigentes do sistema. É uma revisão dos requisitos do S1 P1 (março/2026), atualizada com as
decisões registradas em [`decisoes.md`](decisoes.md). A coluna "Origem" aponta o requisito antigo
correspondente; "novo" indica que ele não existia.

**Escopo desta fase:** o quarto de um residente de ILPI (portas, presença, temperatura) e a pulseira de
detecção de queda. Outros cômodos e áreas comuns ficam fora por enquanto.

## Funcionais

### Pulseira

| ID | Requisito | Origem |
|---|---|---|
| RF01 | Adquirir aceleração nos 3 eixos a 200 Hz, faixa ±8 g | S1 P1 RF01 (era 100 Hz) |
| RF02 | Detectar queda na própria pulseira. Desde 2026-11-11: gatilho em picos de \|a\| ≥ 2,0 g + classificador (Random Forest) sobre 23 atributos da janela. O modelo, treinado na SisFall (cintura), é **provisório até retreinar com os dados do pulso** (antes: a² > 6,25 g²) | S1 P1 RF02/RF03 |
| RF03 | Enviar `FALL` ao hub imediatamente, com reenvio até a confirmação | S1 P1 RF04 |
| RF04 | Após uma queda, não gerar outra por 30 s (cooldown) | S1 P1 RF05 |
| RF05 | Dois toques rápidos no botão ("estou bem") cancelam a queda ou o pedido de ajuda nos 15 s seguintes (`FALL_CANCEL`/`HELP_CANCEL`); um aperto simples não faz nada. Desde 2026-11-17 (antes: um aperto simples) | novo |
| RF20 | Segurar o botão por 3 s, fora de alerta, pede ajuda (`HELP` → alerta `ajuda`) | novo (2026-11-17) |
| RF06 | Enviar heartbeat a cada 30 s com tensão da bateria e nível de atividade | novo (substitui S1 P1 RF06) |
| RF07 | Modo de coleta que grava amostras brutas para formar o conjunto de dados | novo |

### Hub do quarto

| ID | Requisito | Origem |
|---|---|---|
| RF08 | Detectar toda transição de cada porta (quantidade configurável) e publicar o estado | S1 P1 RF07/RF08 |
| RF09 | Detectar presença (PIR) e publicar o estado | novo |
| RF10 | Medir a temperatura a cada 60 s (16 amostras, curva MMQ de cadeia completa) e publicá-la quando mudar 0,2 °C ou mais, ou a cada 5 min (era a cada 30 s, até 2026-11-17) | S1 P1 RF11/RF14/RF15 |
| RF11 | Executar as regras de tempo e publicar os episódios em `alerta` ([`arquitetura.md`](arquitetura.md), Regras) | S1 P1 RF09/RF13 (revisadas) |
| RF12 | Sinalizar o estado e os alertas por um LED de status (sem buzzer desde 2026-10-23; o aviso ao cuidador vai pela central) | S1 P2 (atuador) |
| RF13 | Aplicar parâmetros recebidos por MQTT sem recompilar, guardando-os em NVS | S1 P1 RNF12 |
| RF14 | Publicar no barramento CAN os estados dos sensores locais (a pulseira também tem CAN, só para bancada: quedas e heartbeats) | S2 P1 Q2 |
| RF15 | Sincronizar a hora por SNTP | S1 P1 RF08 |

### Central

| ID | Requisito | Origem |
|---|---|---|
| RF16 | Broker MQTT com autenticação, disponível continuamente na rede local | S1 P1 RF16 |
| RF17 | Gravar todos os tópicos no InfluxDB, com retenção mínima de 30 dias | S1 P1 RF17 |
| RF18 | Grafana com estado de todos os quartos, histórico de portas (24 h), temperatura, atividade e alertas | S1 P1 RF18 |
| RF19 | Notificar cada alerta no Telegram do cuidador: tipo, quarto e horário | S1 P1 RF19 |

## Não funcionais

| ID | Requisito | Meta | Como medir |
|---|---|---|---|
| RNF01 | Amostragem do acelerômetro estável | 200 Hz, jitter ≤ ±5 ms | intervalos entre amostras no modo de coleta |
| RNF02 | Latência da queda | pico de aceleração → publicação MQTT ≤ 500 ms | `ts` na pulseira/hub e no broker |
| RNF03 | Latência da porta | transição → MQTT ≤ 200 ms; → InfluxDB ≤ 1 s | log com hora nos dois lados |
| RNF04 | Latência da notificação | alerta → Telegram ≤ 5 s | hora do `alerta` vs. hora da mensagem |
| RNF05 | Disponibilidade | 24/7, com reconexão automática Wi-Fi/MQTT (*backoff* exponencial) | ensaio de 24 h derrubando a rede |
| RNF06 | Sem perda de eventos | nenhum evento QoS 1 perdido numa queda de conexão | derrubar o Wi-Fi durante eventos |
| RNF07 | Watchdog | reinício se uma task crítica travar por > 30 s | travamento forçado em teste |
| RNF08 | Desempenho da detecção (meta do modelo; o limiar provisório não precisa atingir) | sensibilidade ≥ 90 %; ≤ 1 falso alarme por dia por residente | plano de avaliação abaixo |
| RNF09 | Segurança | MQTT com usuário/senha + ACL; ESP-NOW criptografado; TLS na nuvem; segredos fora do git | revisão de configuração |
| RNF10 | Medição de temperatura | resolução ≤ 0,1 °C; exatidão ±1 °C | comparação com termopar (pendente desde o S1 P3) |
| RNF11 | Funcionar sem internet | todas as funções locais sem acesso externo, exceto o Telegram | ensaio com a internet desligada |
| RNF12 | Proteção física | caixas IP42 no mínimo, sobretudo perto do banheiro | inspeção |
| RNF13 | Diagnóstico | logs na serial a 115200 baud: estado das tasks, conexão, contagem de eventos | inspeção |
| RNF14 | Autonomia da pulseira | a definir depois de medir o consumo | medição com bateria real |

## Restrições

| ID | Restrição | Origem |
|---|---|---|
| RR01 | Processamento na ESP32: detecção na pulseira, regras e alertas no hub | diretriz da disciplina |
| RR02 | Firmware em C com ESP-IDF 6 e FreeRTOS, em ESP32-S3 DevKitC-1 | S1 P1 RR02 (era C++) |
| RR03 | Hub alimentado em 5 V; pulseira a bateria (no protótipo, power bank na USB-C) | S1 P1 RR03 (era só USB) |
| RR04 | Custo do protótipo ≤ R$ 600 (revisar com 2 placas + Raspberry Pi) | S1 P1 RR01 |
| RR05 | Central em ferramentas abertas: Mosquitto, Telegraf, InfluxDB, Grafana, Python | S1 P1 RR02 (revisada) |

## Requisitos descartados

| Antigo | Por quê |
|---|---|
| Modbus RTU sobre RS-485 (S1 P1) | substituído por CAN |
| Pinos do ESP32 DevKit V1 (S1 P1 RI01/RI06) | a placa é o ESP32-S3 DevKitC-1 |
| Classes C++ com `SensorBase` (S1 P1 RNF13) | firmware em C com componentes ESP-IDF |
| Validação obrigatória no Wokwi (S1 P1 RR04) | opcional; o simulador MQTT cobre a interface |
| Node-RED para a lógica (S1 P1) | as regras rodam no hub |
| Pulseira em Wi-Fi/MQTT alimentada por USB (S1 P1) | pulseira a bateria com ESP-NOW |
| Queda = 2,5 g sustentado por 100 ms (S1 P1 RF03) | o impacto dura dezenas de ms; substituído pelo limiar provisório e, depois, pelo modelo |
| Inatividade = 30 min sem PIR à noite | dispararia toda noite com o residente dormindo |

## Plano de avaliação

| O quê | Como | Meta |
|---|---|---|
| Detecção de queda | quedas simuladas em colchão (frente, costas, lado, joelhos) + atividades diárias (andar, sentar, levantar, deitar, palmas, acenar, bater na mesa), com várias pessoas; sensibilidade = VP / (VP + FN), especificidade = VN / (VN + FP) | RNF08 (para o modelo); o limiar provisório vira a linha de base |
| Falsos alarmes em uso | horas de uso contínuo com a pulseira | ≤ 1 por dia (modelo) |
| Latências | RNF02–RNF04 com registro de hora nos dois lados | metas da tabela |
| Regras | perfil "demo" com tempos curtos, um cenário por regra | dispara e resolve como especificado |
| Cobertura | `rssi` da pulseira no quarto, no banheiro e no corredor | sem perda de heartbeat no quarto e no banheiro |
| Disponibilidade | 24 h contínuas, com quedas de rede provocadas | RNF05 e RNF06 |
