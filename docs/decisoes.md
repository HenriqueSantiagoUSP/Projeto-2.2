# Decisões e revisões

Registro de como o projeto evoluiu entre as entregas, do que vale hoje e por quê. Serve de base para o
relatório final, que precisa justificar as mudanças. Revisão feita em 2026-10-20 a partir de todos os PDFs de
[`relatorios/`](../relatorios/README.md). Mudou uma decisão? Registre aqui.

## Linha do tempo

| Quando | Entrega | Decisão marcante |
|---|---|---|
| mar/26 | S1 P1 (Q1–Q6) | ILPI Butantã; Modbus RTU/RS-485 com Raspberry Pi mestre; ESP32 DevKit V1; C++; Mosquitto + Node-RED + InfluxDB + Grafana + Telegram; regras de 15/40 min; queda = 2,5 g por 100 ms |
| mai/26 | S1 P2 | ESP32-S3 DevKitC-1; 5 nós; LM35 calibrado por MMQ + LM358 com G = 5,5; 18 ensaios do MPU-6050; LED de status; queda = queda livre + impacto + giroscópio |
| jul/26 | S1 P3 (final) | CAN no lugar do RS-485; PCB única com todos os sensores; firmware em C; queda = a² > 6,25 g² |
| ago/26 | S2 P1 (Q1–Q3) | MQTT + Mosquitto; CAN validado em 28/08; hub + vestível; ESP-NOW + Wi-Fi em AP+STA; cascata de detecção em 4 estágios |
| set/26 | S2 P2 parcial | simulador → Mosquitto → painel web, demonstrado em 18/09 |
| out/26 | S2 P2 PCB | entrega pendente |
| out/26 | revisão (este documento) | processamento na borda; contrato MQTT por site/quarto; central com Grafana; limiar de queda provisório |
| out/26 | implementação das correções | fases 1–7 com código pronto (só build e testes no PC; ver [`estado.md`](estado.md)) |
| nov/26 | S2 P3 Q1 e porte do modelo | 22 métodos comparados no PC (09/11); pipeline com Random Forest no vestível no lugar do limiar (11/11) |

## Decisões vigentes

| Tema | Vale hoje | Detalhe |
|---|---|---|
| Cenário | quarto de residente em ILPI (~60 residentes, turno noturno com poucos cuidadores) | [`arquitetura.md`](arquitetura.md) |
| Dispositivos | hub do quarto + vestível (pulseira) + central | [`arquitetura.md`](arquitetura.md) |
| Enlaces | ESP-NOW (vestível → hub), MQTT/Wi-Fi (hub → central), CAN (hub ↔ sala); no vestível, MQTT direto opcional (reserva da queda) e CAN só de bancada | [`protocolo.md`](protocolo.md) |
| Onde se processa | queda no vestível; regras de tempo e alertas no hub (diretriz da disciplina) | [`arquitetura.md`](arquitetura.md) |
| Detecção de queda | pipeline no vestível: filtro de 20 Hz → gatilho causal (picos de \|a\| ≥ 2,0 g) → janela de −1,0 a +0,8 s → 23 atributos no tempo → **Random Forest** (regressão logística como opção). Treinado na SisFall (cintura, ±8 g): **provisório até retreinar com os dados do pulso** | seção "Pipeline de queda no vestível" abaixo |
| Central | Mosquitto + Telegraf + InfluxDB + Grafana + notificador Telegram; banco relacional PostgreSQL (protótipo em SQLite, 2026-11-10); futuro: nuvem | [`roadmap.md`](roadmap.md), [`pendencias.md`](pendencias.md) |
| Firmware | C com componentes ESP-IDF **v6.0.1**, FreeRTOS, ESP32-S3 DevKitC-1; segue os slides de `aulas/`; componentes comuns em `firmware/common/` | [`CLAUDE.md`](../CLAUDE.md) |
| Portas | até 8 por hub (uma por nibble CAN), cada uma com papel (`saida`, `banheiro`, `outra`) | [`protocolo.md`](protocolo.md) |
| Pinos | a confirmar na PCB nova | [`hardware/README.md`](../hardware/README.md) |
| Existentes provisórios | painel web, simulador, código do semestre 1 (só referência) | — |

## Decisões tomadas na implementação (2026-10-20)

Detalhes que o plano deixava em aberto e foram decididos ao escrever o código; todos já constam em
[`protocolo.md`](protocolo.md) ou em [`arquitetura.md`](arquitetura.md).

| Decisão | Por quê |
|---|---|
| MQTT 3.1.1, keepalive de 30 s | 3.1.1 basta para o contrato; com 30 s o Last Will sai em ~45 s, não em 3 min |
| `battery_mv = 0xFFFF` = bateria não medida | um vestível sem divisor não pode disparar `bateria_baixa` falso |
| Temperatura no CAN em `0x0F2`; portas em `0x0F3` + índice de `BOARD_DOORS` | um nibble por sensor, derivado da mesma tabela que cria as portas |
| ESP-NOW descarta reenvio pelo par (`seq`, `uptime_ms`) | só o `seq` confundiria o primeiro quadro de um vestível que reiniciou |
| Presença só conta como "alguém no quarto" 60 s depois do evento de porta | antes disso, é o residente passando pelo PIR a caminho da porta |
| Episódio de queda só fecha com `FALL_CANCEL` | o hub não tem como saber que o socorro chegou; o reconhecimento pelo cuidador é futuro (backend) |
| ~~Vestível reinicia se o MPU-6050 não responde~~ (trocado em 2026-11-11: segue rodando sem heartbeat; ver "Peças ausentes") | um vestível sem sensor não pode mandar heartbeats que pareçam saudáveis |
| Usuários do broker fixos (`hub`, `telegraf`, `notificador`, `painel`); só as senhas no `.env` | a ACL do Mosquitto se refere a nomes literais |
| Telegraf grava com a hora de recepção; `ts` vira campo | antes de sincronizar, o hub manda sem `ts`, e o Telegraf descartaria o ponto |
| Demonstração de 18/09 movida para `interface/legado/` | os arquivos não estavam no git; reescrever no lugar apagaria a versão entregue |

## Revisão do firmware nos moldes das aulas (2026-10-20, tarde)

Pedido: o firmware das duas placas deve seguir os slides de `aulas/` e a documentação do ESP-IDF v6.0.1, com
componentes modulares, tasks e cuidado com recursos compartilhados; MQTT, CAN e mapa de pinos nas duas placas;
ESP-DSP no vestível. Detalhes em [`arquitetura.md`](arquitetura.md) (Componentes comuns; Tasks, núcleos e
recursos compartilhados) e [`protocolo.md`](protocolo.md) §1 e §2.

| Decisão | Por quê |
|---|---|
| `net`, `mqtt_link` e `can` passam do hub para `firmware/common/`; a placa informa pinos, Last Will e assinatura | as duas placas usam os três; uma cópia por placa divergiria |
| Prioridades nomeadas do slide (`PRIO_CRITICA` 20 … `PRIO_MINIMA` 2), núcleo sempre explícito | slide de RTOS: nunca acima de 20 (Wi-Fi em 23), núcleo 1 para tempo real e núcleo 0 para o rádio |
| Event group `sys_events` no lugar de `volatile bool` | slide de RTOS; um escritor por bit, e qualquer task pode esperar com timeout |
| CAN: fila de TX + task dona do periférico; um quadro por vez no driver | o driver guarda só o ponteiro do quadro; antes, só uma task podia transmitir com segurança |
| CAN: `fail_retry_cnt = -1`, recuperação de *bus-off* após 1 s, filtro de hardware `0x000`–`0x0FF` | slide de CAN (retransmissão ilimitada, monitoramento de estado e recuperação); o atraso evita ficar alternando num barramento quebrado |
| CAN: o callback de estado só notifica a `can_tx` (notificação de task, não fila) | uma notificação não se perde com a fila cheia; perder um *bus-off* deixaria o nó parado |
| No vestível, CAN só de bancada (`GADGET_CAN`, desligado), quadros `0x0FD` (queda) e `0x0FE` (heartbeat), mesmos pinos do hub | o produto não tem transceptor; o CAN existe para o requisito da disciplina e para testes |
| O hub só registra no log os quadros do vestível | a queda depende do `seq` do ESP-NOW para o episódio e a deduplicação; dois caminhos para a mesma queda gerariam alertas duplicados |
| Portas do hub limitadas a 8 (`0x0F3`–`0x0FA`), verificado em compilação | `0x0FD`/`0x0FE` ficam para o vestível; antes nada impedia uma porta de invadir outro nibble |
| No vestível, MQTT opcional (`GADGET_MQTT`, desligado): associa ao AP, telemetria própria e alerta de queda quando o hub não confirma | reserva para a cobertura do ESP-NOW, que é o ponto fraco do enlace; custa bateria, por isso é opção |
| O alerta publicado pelo vestível usa o mesmo `id` do hub (`<vestível>-<seq>`) e `"via":"vestivel"` | o notificador já descarta `id` + `estado` repetidos; o cuidador fica sabendo que o hub não respondeu |
| Usuário `vestivel` restrito a `vestivel/<id>/telemetria`, `vestivel/<id>/mqtt` e `alerta` | `vestivel/#` também casaria com `vestivel/<id>`, que é a visão do hub sobre o vestível |
| Detector provisório com histerese L<sub>on</sub>/L<sub>off</sub> e evento de até 1 s, informado no fim | slide de amostragem (segmentação por evento); um sinal oscilando perto de um limiar não vira vários eventos |
| `signal_proc` com `espressif/esp-dsp` ^1.8.0: buffer circular de 512, janela de 384 (256 antes, 128 depois), atributos no tempo, custo em ciclos com `portMUX` | slides de amostragem e de atributos; é o esqueleto do pipeline futuro, sem mudar ainda o critério de queda |
| A atividade do vestível saiu do detector para `signal_proc`, somada por lote sob um mutex curto | o mutex ficava preso durante a impressão do modo de coleta |
| Filtro de glitch do GPIO nas portas, no PIR e no botão | slide de GPIO; complementa o debounce por estabilização, sem substituí-lo |
| Telegraf grava booleanos (`hora`, `hub`) como texto `"true"`/`"false"` | o parser JSON os descartava; resolve a pendência do campo `hora` |

## Mapa de pinos da PCB única (2026-10-20)

Pedido: no primeiro projeto, uma só PCB para as duas placas, sem conflito de pinos e sem usar pino que um
recurso ligado no futuro tomaria. Base: aula de PCB (`aulas/Semestre 2/CEM_Projeto_PCB.pdf`) e documentação
do ESP-IDF v6.0.1 para o ESP32-S3. Tabela completa em [`hardware/README.md`](../hardware/README.md).

| Decisão | Por quê |
|---|---|
| Uma PCB, um mapa (`firmware/common/pcb/include/pcb_pins.h`); o `board_pins.h` de cada placa só escolhe | as duas placas são a mesma peça física; o `board_pins.h` fica para quando elas se separarem |
| O build falha com pino repetido, proibido, reservado ou impróprio para a função | o conflito aparece na compilação, não na bancada |
| Proibidos: 0, 3, 45, 46 (*strapping*); 19, 20 (USB); 43, 44 (console); 26–37 (flash e PSRAM octal); 38, 47, 48 (LED da DevKit; `VDD_SPI`) | documentação do ESP-IDF e o guia da DevKitC-1 |
| Reservados: 15 e 16 (cristal de 32 kHz) | um *light sleep* preciso no vestível pode precisar do cristal |
| 39–42 só como expansão | são o JTAG por pinos; hoje a depuração é pela USB, mas não há por que perder o JTAG com uma função essencial |
| Analógico só no ADC1, e o ADC1 poupado: LM35 no 4, bateria no 5, 6 e 7 livres | o ADC2 não funciona com o Wi-Fi ligado |
| I²C no 1 (SDA) e no 2 (SCL), e o LM35 sai do GPIO1 | é o único par que o I²C do ULP RISC-V aceita fora dos pinos de *strapping*; o ULP poderá ler o MPU-6050 em *deep sleep*. Se as trilhas até o GY-521 se cruzarem, o módulo é girado no layout (uma troca de SCL e SDA chegou a ser feita e foi desfeita em 2026-10-23 por isso) |
| Botão (18) e INT do MPU (17) em RTC GPIO | só RTC GPIO acorda o chip de *deep sleep* |
| CAN no 13/14, no fim do J1; analógico no topo do J1 | aula de PCB: analógico longe do que chaveia, transceptor junto ao conector na borda |
| Buzzer unificado no 21 (antes: 21 no hub, 12 no vestível) | na mesma PCB é um só buzzer |
| 4 portas na placa (11, 12, 9, 10) e 4 na expansão | as portas 3 e 4 estão em pinos do ADC1, para cederem o lugar se faltar analógico |
| Versão da DevKit (LED no 48 ou no 38) no `menuconfig` → PCB | as duas versões existem, e o LED é da DevKit |

## Condicionamento do LM35 na PCB nova (2026-10-21)

Detalhes e ligação pino a pino em [`hardware/README.md`](../hardware/README.md), seção Condicionamento do LM35.

| Decisão | Por quê |
|---|---|
| Fica o circuito validado: LM35 em 5 V, LM358 não inversor com G = 5,5 | já foi caracterizado em 22 pontos; a curva do firmware continua valendo |
| Sai o inversor de −5 V; o LM358 vai em 5 V e GND | o LM358 é de alimentação simples (3 a 32 V), e entrada e saída chegam ao GND; o LM35 nunca chega perto de 0 V num quarto. Saem uma fonte de ruído chaveada, peças e o risco de tensão negativa no pino |
| Sem ramo de 5 V filtrado para o analógico (revisto em 2026-10-23); só 100 nF junto ao LM35 e ao LM358 | o LM35 quase ignora o ruído da alimentação (~0,01 mV/V), o LM358 rejeita bem o ruído lento, e o ruído rápido o RC da entrada do ADC corta; o ramo melhoraria algo já abaixo da precisão do LM35 |
| 10 kΩ em série + 1 µF ao GND no GPIO 4 (passa-baixa de 1ª ordem, fc ≈ 16 Hz) | abaixo do Nyquist de 50 Hz das rajadas de leitura e dos 60 Hz da rede; o capacitor dá ao ADC uma fonte de baixa impedância (o ESP-IDF recomenda ao menos 100 nF); o resistor limita a corrente em defeitos; queda desprezível (fuga de nA) |
| Sem diodo de *clamp* | sem tensão negativa na placa e com o amp-op limitado a ~3,5 V, o resistor basta |
| Calibração: conferir 3 ou 4 pontos no GPIO 4 em vez de refazer os 22 | o sensor, o ganho e os resistores não mudaram |

## Sem buzzer (2026-10-23)

| Decisão | Por quê / consequência |
|---|---|
| A PCB não tem buzzer; o GPIO 21 fica livre | escolha do grupo: a sinalização local é o LED da DevKit, e o alerta chega ao cuidador pela central (Telegram, painel) |
| No firmware, `PIN_BUZZER = GPIO_NUM_NC` nas duas placas | o componente `indicators` já ignora uma saída NC; o código do buzzer continua, se um dia ele voltar |
| A janela de cancelamento do vestível passa a ser sinalizada só pelo LED | risco aceito: o residente pode não perceber o LED; avaliar na bancada se um motor de vibração faz falta |
| RF12 revisto: LED de status, sem buzzer | [`requisitos.md`](requisitos.md) |

## Bateria do vestível (2026-10-23)

Detalhes em [`hardware/README.md`](../hardware/README.md), seção Alimentação.

| Decisão | Por quê |
|---|---|
| No protótipo, o vestível roda com **power bank** (saída USB-A) na entrada USB-C da placa, com cabo A → C | a placa já junta as duas num protótipo; pôr o circuito de bateria tomaria tempo que o grupo não tem e aumentaria bastante a placa |
| Sem medição da bateria: `GADGET_BATTERY_SENSE` desligado por padrão, e o GPIO 5 fica reservado | o power bank não expõe a tensão da célula; com a bateria "desconhecida" (`0xFFFF`), o hub não aplica a regra `bateria_baixa` |
| Avaliado e descartado nesta versão: LiPo + TP4056 + boost MT3608 na placa, com troca automática por Schottky | funcionava, mas pelo custo de tempo e de área. Antes dele também saíram o pacote externo e a chave USB/BAT |
| Risco aceito: o power bank pode desligar sozinho com corrente baixa | hoje o consumo (~110–130 mA) fica acima do limite da maioria; com o *light sleep*, vai precisar de um modelo "always on" |
| Para o vestível próprio (futuro): LiPo + regulador LDO de 3,3 V direto no 3V3, com divisor no GPIO 5 | mais leve e mais eficiente que um boost; o firmware já tem a medição pronta |

## Fabricação da PCB (2026-10-23)

Detalhes em [`hardware/README.md`](../hardware/README.md), seção Fabricação.

| Decisão | Por quê |
|---|---|
| Placa feita na **JLCPCB**: 2 camadas, furo metalizado, 1,6 mm, 1 oz, HASL | escolha do grupo; com furo metalizado, trilhas no F.Cu e plano de GND no B.Cu |
| Regras com folga sobre os limites da fábrica: trilha de sinal 0,4 mm, de alimentação 0,8 mm, afastamento de 0,25 a 0,3 mm | a placa é soldada à mão; a fábrica aceita 0,127 mm |
| LM35 no `TO-92_Inline_Wide` e furos da DevKit de 1,0 mm | o `TO-92_Inline` tem anel de 0,15 mm, e o furo de 0,8 mm da Espressif não recebe o terminal quadrado de 0,64 mm |
| Pads da DevKit ovais de 1,4 × 2,2 mm (estreitos no sentido da fileira) e afastamento da classe `Power` em 0,25 mm | com pads redondos de 1,7 mm, a trilha de 0,4 mm não passava entre os pinos para chegar embaixo da DevKit |

## Modelo de queda: desenvolvimento no PC (S2 P3 Q1, 2026-11-09)

Detalhes e números em [`modelo/README.md`](../modelo/README.md) e
[`modelo/resultados/resumo.md`](../modelo/resultados/resumo.md).

| Decisão | Por quê |
|---|---|
| Modelos desenvolvidos e comparados **no PC** (pasta `modelo/`, Python 3.12 via `uv`) antes de irem para o ESP32 | comparar métodos com validação séria é mais rápido no PC. O que vai para o vestível sai daí: classificador e limiar |
| Saída **binária** (queda / não queda); os tipos de queda e de atividade servem só para a análise de erros | o alarme só precisa saber se houve queda; 15 classes dividiriam os dados (~120 exemplos por tipo) |
| **Pipeline:** filtro passa-baixa → **gatilho** → janela → atributos → classificador | o limiar decide mal sozinho (~70 alarmes falsos/h na SisFall), mas como gatilho deixa passar 99 % das quedas e o classificador só roda quando ele dispara |
| Gatilho por **picos de \|a\| > 2,0 g separados por ≥ 0,5 s**, e não pela histerese com período refratário do `fall_detect.c` | a histerese deixava um disparo da caminhada anterior esconder o impacto (só ~93 % das quedas passavam). Com os picos, o maior pico sempre dispara |
| Janela de **−1,0 s a +0,8 s** em torno do pico | serve aos nossos ensaios curtos (só 0,8 s depois do impacto) e cabe no buffer circular de 512 |
| **Validação cruzada por pessoa** (5 dobras) | janelas da mesma pessoa no treino e no teste inflam os resultados |
| **Dois cenários de faixa:** ±8 g (vestível atual) e ±2 g (nossos ensaios) | os nossos ensaios foram gravados em ±2 g e saturam; a SisFall é saturada em cada faixa para comparar e treinar |
| Candidatos para o ESP32: **regressão logística** (412 B, 92,8 % das quedas com ≤ 1 alarme/h) e **Random Forest** (252 kB na flash, 95,7 %) | melhor acurácia × custo. O kNN (96,1 %) guarda o treino inteiro (1,5 MB) e fica de fora; a CNN de 6 eixos (93,9 %, 26 kB) depende da orientação da placa |
| AR e ARX entram como atributos e como detector de anomalia, mas **não** como decisor | como anomalia a AUC é 0,79, porque qualquer movimento brusco também gera erro grande de predição |
| **Limitação aceita:** todos os dados são da **cintura**, e a pulseira vai no pulso | falta coletar no pulso, com o firmware atual (±8 g), antes de embarcar o modelo |

## Pipeline de queda no vestível (2026-11-11)

O pipeline do PC foi portado para o firmware (`firmware/gadget/components/fall_pipeline`) e substitui o detector
provisório com histerese. Os números são da SisFall no cenário A (±8 g), com validação por pessoa, e estão em
[`modelo/resultados/vestivel.json`](../modelo/resultados/vestivel.json).

| Decisão | Por quê |
|---|---|
| Modelo escolhido: **Random Forest** de 60 árvores com profundidade ≤ 10 (~104 KB na flash). A regressão logística fica como opção no `menuconfig` | foi o melhor nos dois cenários de faixa e o que menos perde com a saturação, que pode voltar no pulso, acima de 8 g. Neste pipeline: 0,55 alarme falso/h e 95,1 % das quedas com ≤ 1 alarme/h. A logística (0,62/h e 94,9 %, 23 pesos) é a mais simples de auditar |
| Floresta de 60 × 10 no lugar da de 150 × 12 (252 KB) | mesmo resultado na mesma configuração (0,55 alarme/h), com metade dos nós |
| Só os **23 atributos no tempo**, sem FFT nem AR | perdem pouco (Random Forest de 0,34 para 0,55 alarme/h; logística de 0,55 para 0,62) e dispensam FFT e mínimos quadrados no C. Os de frequência voltam se os dados do pulso pedirem |
| **Gatilho causal**: começa numa subida até L<sub>on</sub>, acompanha o máximo e emite o pico depois de 0,5 s sem um valor maior (`pipeline.gatilho_causal`) | o `find_peaks` olha o futuro e não roda amostra a amostra. O causal deixa passar as mesmas 99,2 % das quedas e dispara menos (753 contra 858 por hora de atividade). Os modelos exportados foram treinados com ele |
| L<sub>on</sub> = **2,0 g** no \|a\| filtrado, configurável no `menuconfig` | é o maior limiar que deixa passar ≥ 99 % das quedas da SisFall em ±8 g. Com ±8 g, o teto de 8 g por eixo fica longe do gatilho. Deve ser revisto com os dados do pulso |
| Limiar do classificador: o que mantém 95 % das janelas de impacto acima dele (Random Forest: P ≥ 0,73; logística: P ≥ 0,96) | é o mesmo ponto de operação das comparações, e vai junto do modelo em `fall_model.c` |
| O modelo é **gerado** por `modelo/experimentos/09_exportar_vestivel.py` (`fall_model.h`/`.c`), com testes de paridade C × Python | o C reproduz os números do Python. Os vetores de teste (8 trechos da SisFall, 26 janelas) saem junto com o modelo, então retreinar atualiza os dois |
| Limiares das árvores arredondados para baixo em `float` | para um `x` em float, `x <= t` em double (scikit-learn) decide igual a `x <= float_abaixo(t)`: a floresta do C decide como a do Python |
| **Contrato:** P(queda) em % no `FALL` do ESP-NOW (13 bytes), no quadro CAN `0x0FD` (3 bytes) e `"prob"` no `detalhe` do alerta | o cuidador e a central veem a confiança do modelo. Nada foi gravado em placa ainda, então as duas placas mudam juntas; o hub descarta, com log, um `FALL` de tamanho diferente. A versão do ESP-NOW continua 1 |
| O alerta continua saindo do hub; o vestível só publica no MQTT se o hub não confirmar | processamento na borda e deduplicação pelo `id`, como antes; o caminho de reserva já existia |
| Cooldown de 30 s depois de uma queda informada; as janelas seguintes continuam classificadas e vão para o log | a mesma queda, ainda tremendo, não vira dois alertas |
| `fall_detect` (histerese) removido do firmware | o limiar virou só o gatilho. A linha de base continua no Python (`avaliacao.detector_do_firmware`) |

## Peças ausentes e demonstração com uma placa (2026-11-11)

Pedido: mostrar o modelo funcionando com uma ESP só, com o MPU-6050, ligada ao computador; e, nas duas placas,
seguir funcionando sem CAN, sem um sensor ou sem rede, com um aviso em vez de erros repetidos.

| Decisão | Por quê |
|---|---|
| **MAC do hub vazio = vestível sem hub:** não liga o ESP-NOW; as quedas vão ao log serial (e ao MQTT e ao CAN, se ligados), e com MQTT o alerta sai direto | antes, o `espnow_tx_init` falhava e o vestível reiniciava em laço; com um MAC falso, cada queda gastaria ~1 s em reenvios |
| **Perfil `sdkconfig.demo.defaults`**: sem hub, modo de coleta, 5 s entre quedas, console a 921600 (console UART "custom", a mesma UART0) | o CSV a 200 Hz ocupa ~75 % de uma UART a 115200, e o `printf` espera a UART dentro da task de amostragem. A opção de baud só existe com o console "custom" |
| **`interface/demo_vestivel.py`**: lê a placa pela USB, mostra o sinal e as decisões ao vivo e, opcionalmente, publica no papel do hub | dispensa Wi-Fi na sala e hub. A placa decide e o programa só mostra e repassa, então o contrato MQTT não muda |
| **Sem MPU-6050, o vestível segue rodando, sem heartbeat**, com LED de falha e nova tentativa a cada 5 s, no boot e se o sensor soltar durante o uso | mantém a intenção antiga (sem sensor, o hub o declara offline) sem o reinício em laço, que impedia usar o resto da placa e enchia o log |
| **Peça ausente = um aviso (W) ao ser percebida e um I ao voltar**; novas tentativas em silêncio, com espera crescente até 60 s (Wi-Fi, CAN) | uma peça que falta é um estado, não um erro por tentativa. Continuam como E, uma vez: usuário/senha recusados no broker (configuração errada) e uma queda que não chegou a lugar nenhum |
| **O cliente MQTT só começa com a rede de pé**, e sem rede configurada nem começa; os logs da biblioteca (`mqtt_client`, `transport_base`, `esp-tls`) são silenciados, e o `mqtt_link` dá o aviso | a biblioteca tentava mesmo sem Wi-Fi e logava erros a cada ~10 s. Parar e religar o cliente a cada queda do Wi-Fi poderia travar o laço de eventos por segundos (`esp_mqtt_client_stop` espera a tentativa em curso) |
| **CAN sem barramento:** bus-off e "error passive" viram um aviso cada; a recuperação espera 1 s → 60 s | sem transceptor e com o modo bancada desligado, era um E a cada ~1 s |
| **Hub: menu "Room hub: sensors"** (`HUB_PIR_FITTED`, `HUB_LM35_FITTED`, `HUB_DOORS_ABSENT`); sem PIR, a inatividade e a saída noturna ficam suspensas | PIR e portas soltos não dão para detectar (leem "ninguém" e "aberta") e gerariam alertas falsos. No `menuconfig`, como escolheu o usuário; pela config MQTT mexeria no contrato |
| LM35 solto: aviso uma vez, sem publicar | já era descartado por estar fora da faixa, mas com um W a cada leitura |

## O que foi bom ter mudado

| De → para | Por quê |
|---|---|
| RS-485/Modbus com polling → CAN | multimestre, por evento, arbitragem e CRC em hardware; exigido na Q2 do S2 |
| 5 nós (um ESP32 por sensor) → hub do quarto + vestível | 2 placas em vez de 5: menos custo, consumo (~0,5 W cada) e manutenção |
| Pulseira em Wi-Fi alimentada por USB (RR03) → vestível a bateria com ESP-NOW | uma pulseira presa a um cabo não é usável |
| ESP32 DevKit V1 → ESP32-S3 DevKitC-1 | dois núcleos LX7 e USB nativo; é a placa de todos os relatórios desde o S1 P2 |
| C++ com `SensorBase` (RNF13, "sem exceções") → C com componentes ESP-IDF | idiomático no ESP-IDF; o relatório final precisa revisar o requisito oficialmente |
| Reed só na borda de subida (S1) → as duas bordas, com estado | as regras de porta precisam do fechamento |
| MPU em ±2 g (saturava em 3,46 g) → ±8 g | quedas chegam a 6–8 g |
| Curva do LM35 de cadeia completa, `T = (V_mV − 144,739) / 51,3335` | rastreabilidade metrológica; continua valendo |
| MQTT + JSON + QoS por criticidade | é o contrato que o backend e a nuvem vão consumir |
| Hub em AP+STA → STA com `WIFI_PS_NONE` | o ESP-NOW funciona sobre STA; o que evita perder pacotes é desligar a economia de energia |
| Struct ESP-NOW com strings e canal fixo → mensagem binária versionada, ACK + reenvio, criptografia, varredura de canal | robustez a troca de canal do roteador, duplicatas e mensagens forjadas |

## O que estava melhor antes e voltou

| Item (origem) | Como voltou |
|---|---|
| Regras de tempo e alertas gerados na ESP32 (S1 P1 RF09/RF13) | o hub roda as regras e publica `alerta`. Em out/26 elas chegaram a ser levadas para a central; foi revertido pela diretriz da disciplina |
| Tópico único de alertas + Telegram em ≤ 5 s (RF19) | tópico `alerta` com todos os tipos; notificador Telegram na central |
| Parâmetros ajustáveis sem recompilar (RNF12) e ajuste ao vivo na demonstração | `config/desejada` → o hub valida, grava em NVS e publica `config/aplicada` |
| Requisitos mensuráveis (500 ms, 5 s, 200 ms, 200 Hz ± 5 ms, 24/7) | [`requisitos.md`](requisitos.md), com metas e como medir |
| Histórico de 30 dias + Grafana (RF17/RF18) | Telegraf → InfluxDB → Grafana |
| Autenticação MQTT (RNF09) | usuário/senha + ACL desde já; TLS quando houver nuvem |
| Buffer offline e watchdog (RNF06/RNF07) | outbox do esp-mqtt para QoS 1 + task watchdog nas tasks críticas |
| Cooldown pós-queda de 30 s (RF05) | no vestível |
| LED de status por nó (1 Hz normal, 4 Hz alerta) | WS2812 da própria DevKit, no hub e no vestível |
| Duas portas, quarto e banheiro | portas modulares, cada uma com seu papel |
| Aceleração contínua no painel (RF06) | campo `atividade` no heartbeat, sem custo de rádio extra |
| Cenários de demonstração com tempos encurtados (S1 P1 Q5/Q6) | perfil de configuração "demo" |
| Sensibilidade/especificidade como métrica (Noury) | plano de avaliação em [`requisitos.md`](requisitos.md) |
| Hora via NTP (RF08) | SNTP no hub; servidor NTP local no Raspberry Pi |

## O que dá para melhorar

Pontos encontrados na revisão. Cada um tem a fase correspondente em [`roadmap.md`](roadmap.md).

**Situação em 2026-10-20**, depois da implementação. "No código" significa que compila e, quando é lógica
pura, passa nos testes no PC; nada foi verificado em bancada ainda.

| Item | Situação |
|---|---|
| 1. Detecção de queda | desde 2026-11-11, pipeline com Random Forest no código, com paridade C × Python testada no PC; amostragem por hardware e modo de coleta prontos; protocolo de coleta em [`coleta-de-dados.md`](coleta-de-dados.md); falta retreinar com os dados do pulso |
| 2. Amostragem | no código: 200 Hz pelo `SMPLRT_DIV` + FIFO; light sleep pendente (precisa medir consumo) |
| 3. Regras | no código, com a lógica corrigida; 16 cenários testados no PC |
| 4. Debounce | corrigido no código (ler só depois de 50 ms sem bordas) |
| 5. Pinos | mapa único da PCB em `pcb_pins.h`, verificado na compilação; definitivo depende do layout |
| 6. ADC | `adc_cali` no LM35 e na bateria; validação com termopar pendente |
| 7. Escala de ILPI | contrato `site/quarto` no hub e na central; Grafana com seletor de quarto |
| 8. Meta de falso positivo | revista em `requisitos.md` (RNF08) |
| 9. Bateria | leitura implementada; bateria e carregador pendentes (hardware) |
| 10. Autoteste CAN | virou opção do `menuconfig` |

1. **Detecção de queda: o limiar é provisório; o destino é um modelo preditivo.**
   - O limiar a² > 6,25 g² fecha o caminho ponta a ponta, mas nos próprios ensaios "andando" chegou a 2,92 g
     (8,5 g²) e "correndo" a 3,46 g. Falsos alarmes são esperados e aceitos enquanto ele for provisório.
   - O destino é um pipeline de processamento complexo com modelo preditivo treinado com grande volume de
     dados (roadmap, Futuro).
   - O que preparar desde já: amostragem por hardware a 200 Hz e ±8 g, modo de coleta de amostras brutas,
     protocolo de ensaios rotulados e janelas em torno de cada disparo (roadmap, fases 6 e 8).
   - O critério "2,5 g sustentado por 100 ms" (S1 P1) não é físico, porque o impacto dura dezenas de ms. Fica
     só como histórico.
2. **Amostragem com `vTaskDelay(5 ms)`** (S1 P2/P3): o período real é 5 ms + o tempo de execução e deriva.
   Usar a taxa interna do MPU-6050 (`SMPLRT_DIV`) com FIFO ou pino INT: o tempo passa a ser de hardware
   (atende ±5 ms) e permite *light sleep*.
3. **Regras com lógica frágil** (S1 P1–P3, S2 P1):
   - "porta aberta 15 min à noite" não é "saiu e não voltou", porque a porta pode ficar aberta → a porta de
     saída abriu à noite e passaram 15 min sem presença no quarto;
   - "banheiro fechado 40 min" dispara se a porta do banheiro fica normalmente fechada → o banheiro fechou,
     não há presença no quarto e a saída não abriu nesse intervalo;
   - "sem PIR 30 min à noite" dispara toda noite com o idoso dormindo → inatividade só no período diurno,
     com tempo maior (a calibrar);
   - temperatura sem histerese gera alertas intermitentes → fora da faixa por 10 min, histerese de 0,5 °C;
   - um cooldown genérico vira episódio: cada alerta é `ativo` uma vez e depois `resolvido` ou `cancelado`.
4. **Debounce do hub** (`firmware/room/components/sensors/sensor_events.c` e `reedswitch.c`).
   - O nível é lido na primeira borda, ainda quicando, e as bordas seguintes são descartadas por 500 ms.
   - Efeito: pode registrar o estado errado e perder um abre-fecha rápido.
   - Correção na fase 2 do roadmap.
5. **Três mapas de pinos diferentes**, e a PCB do S1 tinha conflitos reais (detalhes e proposta em
   [`hardware/README.md`](../hardware/README.md)):
   - LM35 no ADC2, que não funciona com Wi-Fi ligado;
   - CAN em GPIO43/44, que é o console UART0;
   - reed no GPIO48, que é o LED da placa v1.0;
   - MPU em GPIO35/36, ocupados pela PSRAM octal nos módulos N8R8/N16R8.
6. **ADC:**
   - `raw × 3300 / 4095` ignora a não linearidade do ADC do S3. Usar `adc_cali` para obter mV antes da curva
     MMQ.
   - A 12 dB, o fim útil é ~3,1 V, e 60 °C dá 3,22 V na saída condicionada. Não afeta 18–28 °C, mas a faixa
     útil precisa estar documentada.
   - A validação com termopar sobre a PCB continua pendente (S1 P3).
7. **Escala de ILPI** (~60 residentes): a documentação antiga falava de "casa" e o painel tem `quarto101`
   fixo. O contrato passou a ter `<site>/<quarto>` e ids de porta e de vestível; o Grafana terá uma variável
   de quarto.
8. **Meta de falso positivo ≤ 1 por hora** (S1 P1 RNF08): isso permite até 24 alarmes falsos por dia e leva
   o cuidador a ignorar o sistema. Nova meta: ≤ 1 por dia por residente e sensibilidade ≥ 90 % (vale para o
   modelo; o limiar provisório não precisa atingir).
9. **Bateria do vestível:** nenhum relatório define bateria, carregador ou orçamento de energia. Fica como
   pendência de hardware: LiPo + carregador + leitura da tensão no ADC1 + medição do consumo real.
10. **`CAN_SELF_TEST = true`** em `can.h`: precisa ser `false` no barramento real da sala.

## Inconsistências entre relatórios

O relatório final deve usar a coluna "Vale hoje".

| Tema | Versões nos relatórios | Vale hoje |
|---|---|---|
| Número de nós | 4 (S1 P1), 5 (S1 P2/P3, S2 P1 Q1), hub + vestível (S2 P1 Q2/Q3) | hub + vestível |
| Linguagem do firmware | C++ (S1 P1 RR02/RNF13), C (S1 P3 em diante) | C |
| Taxa do MPU-6050 | 100 Hz (S1 P1), 200 Hz (S1 P2 em diante) | 200 Hz |
| Debounce do reed | 50 ms (S1 P1), 200 ms (S1 P2/P3), 500 ms (firmware atual) | ~50 ms de estabilização (roadmap, fase 2) |
| Saída noturna | 5 min (S1 P1 RF09), 15 min (texto do Q3 e Q5/Q6) | 15 min sem presença após a saída abrir |
| Inatividade | 30 min sem PIR à noite | período diurno, 120 min (a calibrar) |
| Leitura do LM35 | a cada 10 s, média de 10 (S1 P1/P2); a cada 30 s, 16 amostras (S1 P3) | a cada 30 s, 16 amostras |
| Resistores do LM358 | R1 = 1 kΩ / R2 = 4,5 kΩ (S1 P2); 2 kΩ / 9 kΩ (S1 P3) | mesmo ganho de 5,5; vale o da PCB nova |
| Alimentação do LM35 | 3,3 V (S1 P3, Tabela 4): **erro do relatório** | 5 V, com o LM358 em ±5 V (−5 V por um inversor); confirmado pelo grupo em 2026-10-21 |
| Tópicos | `asilo/quarto01/…` (S1 P1), `idoso/quarto101/…` (S2) | `idoso/<site>/<quarto>/…` |
| Critério de queda | 2,5 g por 100 ms; queda livre + impacto + giroscópio; a² > 6,25 g²; cascata de 4 estágios com rede neural | gatilho de 2,0 g + Random Forest no vestível (2026-11-11), provisório até os dados do pulso |
| Wi-Fi do hub | AP+STA (S2 P1) | STA com `WIFI_PS_NONE` |
| Onde rodam as regras | ESP32 (S1 P1), Node-RED na central (S2 P1) | hub (ESP32) |
| Banco de dados | InfluxDB (S1 P1), "SQLite ou InfluxDB" (S2 P1) | InfluxDB para as séries; banco relacional em PostgreSQL. O SQLite é só o protótipo da atividade de Fundamentos de Software (2026-11-10), de onde vêm o esquema e as decisões de tipos |
| Versão do MQTT | 3.1.1 (S1 P1 RI03); o código antigo incluía o cliente MQTT 5 | 3.1.1 (decidido na implementação) |
| Placa | ESP32 DevKit V1 (S1 P1) | ESP32-S3 DevKitC-1 |
| Pinos | três mapas diferentes | a confirmar na PCB nova |
