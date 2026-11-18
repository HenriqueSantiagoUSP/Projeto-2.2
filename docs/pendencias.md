# Pendências

**A lista única do que falta fazer.** Atualizada ao fim de toda tarefa:
- o que foi feito vai para [Concluído](#concluído), marcado, com a data e como foi verificado;
- o que surgiu entra na seção certa.

O plano por fases está em [`roadmap.md`](roadmap.md), e a fase atual e o diário em [`estado.md`](estado.md).

Um item só sai daqui quando o critério dele foi cumprido. Se ele pede bancada, compilar não basta.

## Entregas da disciplina (S2 P3)

Pedidos e prazos em [`relatorios/README.md`](../relatorios/README.md), Projeto 3.

- [ ] **Q2 (17/11):** relatório com sensores, condicionamento de sinais, interface CAN, interface sem fio e a
  placa desenvolvida (primeira interação de entrega).
  - Pronto: o relatório final, em `relatorios/Semestre 2/Projeto 3/Q2/latex/` (72 páginas, com os gestos do
    botão, as fotos da PCB fabricada, a seção de custo e a explicação da métrica de alarmes falsos desde
    2026-11-17).
  - Falta:
    - a revisão do grupo;
    - **para a entrega final** (decidido em 2026-11-17: esta entrega é provisória, e o grupo revisa e completa
      depois), as duas fotos que faltam. As figuras foram tiradas desta entrega: os blocos estão comentados no
      `09_sem_fio.tex` e no `11_central.tex`. Para voltar, basta salvar a foto em `imagens/` e tirar o `%`
      (instruções no topo do `main.tex`):
      - **monitor serial do hub recebendo a pulseira por ESP-NOW** (`imagens/serial_espnow`): precisa das duas
        placas gravadas e pareadas, e hoje só uma está em mãos. Depende dos itens "Hub gravado e conferido" e
        "Pulseira gravada e conferida" da bancada;
      - **alerta de queda no Telegram** (`imagens/telegram_alerta`): depende do item "Bot do Telegram" da
        bancada. Com o bot no `.env`, o simulador ou a demo levam a queda ao celular.

      Já entraram as da PCB fabricada, a captura do Grafana com o simulador e a janela da demo (2026-11-17).
      Saíram do relatório (2026-11-17) a PCB montada e a bancada CAN, porque ainda faltam componentes para
      montar a placa, e a pulseira no pulso, porque o protótipo é a placa 2 em 1, não uma pulseira pronta;
    - preencher `\docentes`, se o grupo quiser.
- [ ] **Q3 (24/11):** apresentação em sala da comunicação sem fio funcionando com os sensores. Depende da
  bancada abaixo: pulseira → ESP-NOW → hub → MQTT → central.

## Agora: verificação em bancada

Tudo isto já está no código e compila; falta rodar na placa.

- [ ] **Hub gravado e conferido**
  - preencher no `menuconfig`: Wi-Fi, broker, MAC da pulseira e chaves ([`firmware/room/README.md`](../firmware/room/README.md));
  - conferir:
    - portas com abre-fecha rápido, sem perder nem inventar estado;
    - PIR depois dos 60 s de aquecimento;
    - temperatura contra um termômetro;
    - **envio por mudança** ([`decisoes.md`](decisoes.md)): com o quarto parado por 30 min, o log mostra uma
      leitura por minuto e o MQTT recebe a temperatura só a cada 5 min. O ruído da leitura parada precisa ficar
      abaixo de 0,2 °C; se não ficar, subir o limiar (`MQTT_TEMP_REPORT_DELTA_C`) ou a média. Aquecer o LM35 com
      a mão: a mensagem sai logo que muda 0,2 °C;
    - `pulseira/<id>` no primeiro heartbeat e depois a cada 5 min; desligar a pulseira por 2 min e religar: o
      `offline` sai em 90 s, e a volta é publicada no primeiro heartbeat;
    - **correções da revisão de 2026-11-18:**
      - a temperatura aquecida até passar de `temperatura.max` sai na hora em que cruza o limite;
      - os sinais de vida da temperatura e da pulseira saem a cada 5 min, nunca 6;
      - com um pedido de ajuda aberto, uma queda cancelada não cala o buzzer;
      - uma pulseira que volta de offline com uma queda publica `pulseira/<id>` só com estado e rssi, sem a
        atividade de antes;
    - `status` offline (Last Will) até ~45 s depois de desligar;
  - **sem peças**, cada caso dá **um** aviso no log e o resto continua:
    - Wi-Fi sem SSID ou com senha errada (o aviso diz o motivo);
    - broker parado;
    - LM35 solto;
    - CAN sem transceptor, com o modo bancada desligado;
    - PIR, LM35 e uma porta declarados ausentes no menu "Room hub: sensors": nenhum alerta falso de inatividade
      nem de saída noturna.
- [ ] **Ensaio da demonstração com uma placa** (antes da aula; é o primeiro teste do firmware em placa).
  - **2026-11-13, na bancada (COM3, conversor CH343):**
    - a placa com o perfil demo mandou CSV a 200 Hz sem parar por 16 min;
    - parada, \|a\| bruto ≈ 4 077 contagens, ~1 g na escala de ±8 g;
    - o `demo_pulseira.py` parou de atualizar quando o link USB piscou (`SerialException`), e foi corrigido.
  - Ainda falta: conferir as linhas de boot, os disparos ao balançar, a queda na almofada e o Grafana.
  - **Puxar o cabo USB por 2 s com a janela aberta:** o título deve mostrar "PERDIDA, reabrindo…" e o gráfico
    voltar sozinho. A placa reinicia, porque perde a alimentação.
  - Gravar o perfil demo ([`firmware/gadget/README.md`](../firmware/gadget/README.md), "Demonstração com uma placa
    só"). No boot devem aparecer:
    - `+-8 g, +-500 dps`;
    - `fall pipeline: trigger 2.00 g, random forest…`;
    - `no hub configured … stand-alone`.
  - `interface/demo_pulseira.py --porta COMx`:
    - balançar a placa: disparos com P baixa;
    - soltar a placa de pé numa almofada e deixá-la deitada: "QUEDA DETECTADA".
  - Com a central no Docker e `--mqtt-host`: o alerta aparece no Grafana, com a P(queda).
  - Gravar a sessão com `--gravar`: ela serve de primeiro dado real do sensor.
- [ ] **Pulseira gravada e conferida**
  - 200 Hz ± 5 ms medidos pelo modo de coleta;
  - **sem peças:** MPU solto no boot e durante o uso dá um aviso, LED vermelho lento e nenhum heartbeat, e volta
    com "MPU-6050 connected"; hub desligado dá um aviso só;
  - **faixa de ±8 g:**
    - no boot, a linha `+-8 g, +-500 dps, 200 Hz into the FIFO`;
    - parada, o eixo vertical lê ~4096 no CSV do modo de coleta (~16384 seria ±2 g);
    - uma batida passa de 3,46 g sem ficar cortada num teto plano;
  - descoberta do canal do hub;
  - heartbeat aparecendo no Grafana;
  - queda simulada (dois toques sem alerta, com `GADGET_BUTTON_SIMULATES_FALL`) chegando ao Telegram;
  - **gestos do botão** ([`decisoes.md`](decisoes.md), *Gestos do botão*):
    - um aperto simples não faz nada, com ou sem alerta;
    - dois toques nos 15 s depois de uma queda: `FALL_CANCEL`, alerta `cancelado` no Telegram, LED volta ao
      normal;
    - segurar 3 s fora de alerta: o LED vai a vermelho ainda com o botão apertado, e o alerta `ajuda` chega ao
      Telegram; dois toques nos 15 s seguintes o cancelam;
    - segurar durante um alerta, e dois toques fora dele, não fazem nada;
    - ficar com o botão apertado por 30 s dá um pedido só; ligar a placa com o botão apertado não dá nenhum;
    - calibrar com alguém mais velho os 3 s e o intervalo de 1 s entre os toques (`menuconfig`);
    - com o hub desligado, dois toques logo depois de uma queda ainda cancelam (a `comm` estará ocupada
      reenviando, e a task `button` mede o gesto à parte). Desde 2026-11-18, a janela conta da detecção da
      queda, e um gesto que encontra a fila cheia guarda a hora em que aconteceu: repetir com várias quedas
      seguidas e o hub desligado, para encher a fila;
  - **pipeline de queda na placa:**
    - no boot, a linha `fall pipeline: trigger 2.00 g, random forest, fall when P >= 0.73`;
    - balançar ou bater a placa gera `trigger at sample …: peak … g, P(fall) …`;
    - a linha `FEAT:` traz os 23 atributos, P, `acima`, `queda` e o custo em ciclos (esperado: bem abaixo de
      1 ms). Na demo, um disparo com `acima=1` e `queda=0` aparece como "já avisada". O perfil de demonstração
      com o `acima` compilou em 2026-11-18, mas ainda não foi gravado: a placa não estava ligada;
    - repetir com a regressão logística (`menuconfig` → Fall classifier).
  - **paridade na placa:** passar o CSV bruto do modo de coleta pelo Python (`gatilho_causal`, atributos,
    modelo) deve dar os mesmos picos e as mesmas P das linhas `FEAT:`. Falta um script que leia o log da
    serial e faça essa comparação.
- [ ] **CAN entre as duas placas**
  - pulseira com `GADGET_CAN`, modo de bancada desligado nas duas, dois transceptores e terminação;
  - quadros `0x0FD` (3 bytes, com P) e `0x0FE` no log do hub;
  - recuperação de *bus-off* tirando um fio do barramento: um aviso "CAN bus not connected" e um "connected again"
    ao recolocar.
- [ ] **MQTT da pulseira**
  - com o hub desligado, uma queda simulada chega ao Telegram "enviado pela própria pulseira";
  - com o hub ligado, chega uma só notificação;
  - sem hub (MAC vazio) e com o broker parado: uma queda, um pedido de ajuda e o cancelamento esperam o broker
    voltar e saem nessa hora (desde 2026-11-18; antes, iam só para o log).
- [ ] **As correções de erros não tratados (2026-11-13), na bancada** (só compilaram e passaram nos testes de PC):
  - **FALL atrasada:** hub desligado, queda simulada, hub religado em ~20 s. O alerta chega uma vez só, com o
    log "FALL seq … delivered late";
  - **queda depois de reinício:** queda sem "estou bem", pulseira desligada e religada, queda de novo. Chegam
    dois alertas, com `id` diferentes;
  - **MPU que reinicia:** cortar só o VCC do MPU por 1 s. Deve aparecer "MPU-6050 stopped sending samples" e,
    depois, "connected";
  - **CAN sem transceptor e sem autoteste:** um aviso só, sem pares "not connected"/"connected again" a cada
    quadro;
  - **remetente estranho:** um segundo ESP32 mandando ESP-NOW ao hub pareado. Um aviso e nenhum alerta;
  - **URI do broker vazia no hub:** um aviso "MQTT off" e o resto funcionando;
  - **notificador:** derrubar a internet por 1 min com um alerta no meio. Ele chega quando a rede volta;
    reiniciar o container com um alerta publicado nesse intervalo, e ele também chega.
- [ ] **Bot do Telegram**
  - criar pelo @BotFather e preencher `TELEGRAM_TOKEN` e `TELEGRAM_CHAT_ID` no `interface/.env`;
  - medir a latência alerta → celular (meta ≤ 5 s, [`requisitos.md`](requisitos.md)).
- [ ] **Latência pico → MQTT ≤ 500 ms e autonomia da pulseira medidas.** É o critério de pronto da fase 7.

## Hardware

- [ ] **Custo da PCB:** o custo estimado de um quarto (`hardware/placas/v1/custo-estimado.csv`, Apêndice C do
  relatório) usa uma estimativa para a PCB (US$ 2 pelas 5 placas + frete de ~US$ 15 + impostos, ~R$ 35 por
  placa). Se o grupo achar o total pago no pedido da JLCPCB, trocar o valor no CSV e rodar o
  `12_relatorio_q2.py`. Os preços de varejo são de 17/11/2026.
- [ ] **Montar e testar a PCB v1** (fabricada; em mãos).
  - Antes de soldar os conectores da DevKit, conferir na placa fabricada a ordem dos pinos: os nomes no U1 da
    serigrafia (3V3, 3V3, EN, GPIO4…) contra os impressos na própria DevKit. A ordem e o encaixe já foram
    conferidos no teste no papel 1:1 com as peças (2026-10-23); falta só olhar a placa real. A distância entre
    fileiras também já foi conferida (~25,4 mm medidos, 25,25 mm no footprint).
  - Antes de encaixar a DevKit, com o multímetro: sem curto entre 5V e GND nem entre 3V3 e GND (TP1, TP2,
    TP3). Depois ligar pela USB-C e medir 5 V no TP1.
  - Encaixar a DevKit e medir 3,3 V no TP2.
  - Gravar o hub e a pulseira e repetir na placa os testes de "Agora: verificação em bancada".
  - O CANH passa a 0,9 mm do pino 2 do LM358: comparar a leitura do LM35 com o autoteste do CAN ligado e
    desligado.
- [ ] **Refazer a fiação de bancada com os pinos novos.**
  - Hub: LM35 no 4, CAN no 13/14, PIR no 8, portas no 11/12.
  - Pulseira: I²C no 1/2, INT no 17, botão no 18 (o buzzer e a medição da bateria saíram do esquema).
- [ ] **Conferir a curva do LM35 na placa nova:** 3 ou 4 pontos (perto de 10, 20 e 30 °C), medindo a tensão
  no GPIO 4 depois do filtro. Se divergir, refazer a calibração e atualizar `lm35.h`.
- [ ] **Validar o LM35 com termopar sobre a PCB** (pendente desde o S1 P3; [`requisitos.md`](requisitos.md),
  RNF10).
- [ ] **Power bank da pulseira:** comprar um com **saída USB-A** e, de preferência, modo "always on" ou de baixa
  corrente.
  - Testar algumas horas ligado à placa, para ver se desliga sozinho.
  - Medir o consumo com um medidor USB.
- [ ] **Placa própria da pulseira (futuro):** LiPo 1S com proteção, carregador e regulador LDO de 3,3 V, divisor no
  GPIO 5 (`GADGET_BATTERY_SENSE`). Só então o alerta de bateria baixa volta a funcionar.
- [ ] **Entrada de 5 V da PCB:**
  - no esquema: 10 µF eletrolítico na saída V do módulo e Schottky 1N5819 do 5 V da placa até o pino 5V da
    DevKit (a faixa do lado da DevKit); polyfuse e TVS ficaram de fora (versão mínima);
  - USB-C: o módulo usado (pinos V, D−, D+, G) **não tem os resistores de 5,1 kΩ no CC** e não expõe esses
    pinos. No teste de 2026-10-23, deu 5 V só com cabo USB-A → C. Alimentar sempre com carregador USB-A de
    ≥ 2 A e cabo A → C, e escrever isso perto do conector. Para aceitar carregador USB-C, trocar por um
    módulo com os resistores.
- [ ] **Como a pulseira vai ser usada com a PCB única:** o MPU-6050 precisa ficar preso ao corpo. Decidir entre
  a placa presa ao antebraço ou o MPU num cabo curto (com o I²C a 100 kHz).
- [ ] **Transceptores CAN de 3,3 V e terminação de 120 Ω** para a bancada e para a sala.
- [ ] **Furos de fixação (M3) e pontos de teste** (5 V, 3V3, GND, GPIO 4): deixados para uma próxima versão
  da placa (decisão de 2026-10-23).

## Firmware

- [ ] **Refazer a captura da janela da demo** (Seção 11.3 do relatório do Q2, hoje a Figura 34) com o firmware e
  a demo corrigidos. A atual mostra o defeito na lista de disparos ("P 0,88 não"); hoje o mesmo caso aparece
  como "já avisada".
- [ ] ***Light sleep* na pulseira.** A estratégia depende do consumo medido. O mapa de pinos já deixa o botão
  e o INT do MPU em RTC GPIO e o I²C no par do ULP.
- [ ] **Hub sem internet:** apontar `APP_SNTP_SERVER` para o NTP local quando ele existir (ver Central).
- [ ] **CAN de outros grupos da sala no MQTT:** hoje só vai para o log; falta definir os tópicos em
  [`protocolo.md`](protocolo.md) §4.
- [ ] **Decidir: tirar do treino as janelas que passam da borda do ensaio** (item 19 da revisão de 2026-11-13).
  - Hoje o `montar` completa a janela repetindo a última amostra, o que imita "corpo parado". O firmware nunca vê
    isso, porque o sinal não acaba.
  - São 55 de 10 939 janelas de atividade da pulseira (0,5 %) e 134 de 12 472 na comparação; nenhuma de queda.
  - Medido sem elas: Random Forest de 0,55 para 0,48 alarme/h (95,1 → 95,9 % das quedas); regressão logística
    de 0,62 para 0,41 alarme/h (94,9 → 95,7 %), passando o Random Forest.
  - Corrigir exige regenerar o modelo da pulseira, os vetores, os números da apresentação, os resultados (03, 04,
    06, 07, 11) e as conclusões que citam esses números.
- [ ] **Retreinar o pipeline de queda com os dados do pulso** (depende da coleta, em "Dados e avaliação").
  - O modelo na pulseira foi treinado na SisFall (cintura, ±8 g): é provisório.
  - Com os dados do pulso:
    - refazer a varredura do gatilho (`02_gatilho.py`) e ajustar `GADGET_FALL_TRIGGER_CG`;
    - rodar `09_exportar_pulseira.py`, que gera o modelo e os vetores de teste juntos;
    - rodar os testes de host do `fall_pipeline`;
    - comparar de novo Random Forest, SVM e regressão logística (`11_svm_pulseira.py`). Na SisFall os três
      empatam (ver [`decisoes.md`](decisoes.md), 2026-11-13); o SVM só entra no firmware se ganhar com folga,
      porque precisa de outro classificador em C.
  - Avaliar no pulso se os atributos de frequência voltam. Na cintura, os de FFT e de AR juntos levam o Random
    Forest da pulseira de 0,55 para 0,34 alarme/h; na comparação, só os de FFT o levam de 0,76 para 0,48, com as
    mesmas ~95,5 % das quedas. Exigem FFT no C e testes de paridade dela.
  - **A decidir com os dados do pulso** (teste rápido de 2026-11-10 na SisFall, cenário A, script só no
    scratchpad):
    - **janela de +2 s depois do pico:** na regressão logística, quedas com ≤ 1 alarme/h passam de 92,8 % para
      95,3 % e os alarmes/h caem de 1,4 para 0,8. Exige buffer de ≥ 600 amostras e atrasa a decisão em ~1 s;
    - **cascata "E" (regressão logística e Random Forest precisam concordar):** de 95,7 % para 96,3 % das
      quedas com ≤ 1 alarme/h. Ganho pequeno, escolhido nos mesmos dados; só vale se os alarmes falsos no
      pulso pedirem.

## Central

- [ ] **Envio por mudança na central:** com o Docker no ar, rodar o simulador e conferir que os painéis de
  temperatura, atividade e bateria aparecem com os pontos espaçados (um a cada 5 min, ou a cada 0,2 °C) e que o
  InfluxDB recebe ~12 pontos de temperatura por hora com o quarto estável.
  - Em 2026-11-17, com intervalos acelerados (temperatura medida a cada 5 s, resumo da pulseira a cada 60 s), os
    painéis apareceram e o InfluxDB gravou 6 pontos de temperatura em 14 min, só nas mudanças de 0,2 °C.
  - Falta a conta com os intervalos reais (60 s e 5 min), numa rodada de 1 h.
- [ ] **Painel no Grafana para `pulseira_telemetria`**, a telemetria que a pulseira publica direto (hoje ela só
  é gravada).
- [ ] **Servidor NTP local** (no Raspberry Pi ou num container da central), para a ILPI funcionar sem internet.
- [ ] **Banco relacional da central**, para integrar com a interface. O banco final vai ser PostgreSQL.
  - **Protótipo:** a atividade de Fundamentos de Software em SQLite (`C:\Users\henri\dev\tarefa python sql`,
    repositório do grupo) já tem um esquema de 9 tabelas, revisado em 2026-11-10:
    - tabelas: ilpi, cuidador, quarto, idoso, gadget, sensor, evento_sensor, historico_quarto e alerta;
    - DAOs, dados do Faker e consultas com JOIN;
    - o README de lá resume as decisões para levar ao PostgreSQL.
  - **Decisões já tomadas:**
    - ids gerados pelo banco, com `RETURNING`;
    - ids externos em TEXT (MAC, `id` do alerta no MQTT, `site`, código do quarto);
    - datas em ISO 8601 e instantes em UTC (`DATE`/`TIMESTAMPTZ` no PostgreSQL);
    - `historico_quarto` em períodos, com no máximo um quarto atual por idoso;
    - alerta com `estado`, `inicio`, `fim` e o cuidador que atendeu; `UNIQUE(id_quarto, id_mqtt)`.
  - **Falta decidir:**
    - o que fica no banco e o que continua só no InfluxDB (proposta: cadastro, alertas e mudanças de estado no
      banco; séries de 30 s no InfluxDB);
    - o estado "reconhecido" no ciclo do alerta e a tabela de transições (auditoria);
    - quem grava: um consumidor MQTT em Python, como o notificador.

## Dados e avaliação

- [ ] **Fase 8 — coleta de dados**, pelo protocolo de [`coleta-de-dados.md`](coleta-de-dados.md). Pronto quando
  existir um conjunto de dados descrito e reprodutível.
  - Já existem a SisFall e 18 ensaios próprios ([`dados/README.md`](../dados/README.md)).
  - Os ensaios próprios são da cintura, em ±2 g (saturam), curtos e sem metadados.
  - Falta a coleta **no pulso**, com o firmware atual (±8 g), várias pessoas, ensaios de ≥ 10 s com ≥ 3 s
    depois da queda e `metadados.csv`.
  - As quedas de joelhos foram as que os modelos perderam nos ensaios próprios: incluir mais delas.
- [ ] **Limiar de operação do modelo** calibrado numa validação separada. O de `modelo/` sai da curva ROC dos
  escores fora da dobra, e só serve para comparar os métodos.
- [ ] **Fase 9 — avaliação:**
  - latências e disponibilidade;
  - sensibilidade e especificidade do limiar provisório;
  - falsos alarmes por dia;
  - cobertura da pulseira pelo `rssi`;
  - 24 h sem reinício.

  Pronto quando existir a tabela de resultados.

## Repositório

- [ ] **Baixar de novo 3 PDFs** de `relatorios/Semestre 2/Projeto 2/`: `Entrega 1.pdf`,
  `EnunciadoProjeto 2_parcial.pdf` e `EnunciadoProjeto 2_PCB.pdf`. Foram apagados por engano e não têm cópia no
  disco.
- [ ] **Cópia fora deste PC:** o repositório não tem remoto, e os commits estão só neste disco. Decidir onde
  hospedar (GitHub privado do grupo, por exemplo) e fazer o primeiro push.
  - **Desde 2026-11-17, o relatório do Q2 cita `https://github.com/HenriqueSantiagoUSP/Projeto-2.2`**
    (Apêndices C e D). Os caminhos citados, `hardware/fontes/esquematico/` e `hardware/placas/v1/` (com a
    `custo-estimado.csv`, ainda sem commit), têm de estar lá antes da entrega. O repositório precisa estar
    público, ou aberto a quem corrige.
  - Antes do primeiro push, decidir se `aulas/` (14 MB de PDFs) sai também do histórico: ela saiu do git em
    2026-11-16, mas continua nos commits antigos. Apagar exige reescrever o histórico.
- [ ] **Fim de linha na cópia de trabalho:** o `core.autocrlf=true` do sistema faz o checkout em CRLF (o
  repositório está em LF). Um `.gitattributes` com `* text=auto eol=lf` e um `git add --renormalize .` deixam a
  cópia em LF também.

## Futuro (sem data)

Detalhes em [`roadmap.md`](roadmap.md), seção Futuro.

- [ ] Modelo preditivo de queda, no lugar do limiar provisório. Primeira versão desenvolvida e comparada no PC
  (ver Concluído, 2026-11-09); falta embarcar (ver Firmware) e treinar com dados do pulso.
- [ ] Backend + PostgreSQL: residentes, dispositivos, cuidadores, alertas com ciclo de vida.
- [ ] Nuvem: bridge do Mosquitto com TLS.
- [ ] App ou web do cuidador, com reconhecimento de alertas.
- [ ] OTA.
- [ ] Configuração da pulseira pelo hub (mensagem `CONFIG` do ESP-NOW).
- [ ] Cobertura além do quarto: vários hubs e roaming da pulseira.

## Concluído

O mais recente no topo.

- [x] **2026-11-18 — Correções da revisão de código** (`c607918..75291f9`; detalhes no diário):
  - **pulseira:**
    - um gesto que encontra a fila cheia guarda a hora;
    - a janela da queda conta da detecção;
    - sem hub e com MQTT, o alerta e o cancelamento esperam o broker;
    - a FEAT traz `acima`;
  - **hub:**
    - o buzzer só para sem queda ou ajuda aberta;
    - a volta de offline não republica a atividade velha;
    - a temperatura sai ao cruzar um limite;
    - o sinal de vida não escorrega para 6 min;
  - **demo:** cancela só o tipo que a placa cancelou;
  - **relatório:** a data dos preços vem da planilha.
  - Verificado:
    - testes de host (regras 87, `report` 28, com um erro plantado pego em cada um; os demais sem mudança);
    - demo 26 e notificador;
    - builds sem avisos (hub; pulseira padrão, demonstração, sem hub com MQTT, com hub e todas as opções);
    - o script 12, sem diferença nas tabelas.
  - Falta a bancada: os itens do hub, da pulseira e do MQTT da pulseira acima.

- [x] **2026-11-17 — Disparos que sumiam na demo:**
  - **causa:** a linha FEAT era cortada pelas linhas CSV, e a demo a descartava (4 de 11 numa sessão gravada,
    uma delas uma queda avisada);
  - **correção:** a linha agora sai num `printf` só. A demo também mostra como "já avisada" a queda que cai na
    espera de 5 s depois de outra, que antes aparecia como "não";
  - **verificado na placa:** numa sessão nova gravada, os 13 disparos do log chegaram com a FEAT inteira, e a
    demo leu os 13. Nenhuma linha CSV chegou quebrada. Os builds da pulseira saem sem avisos, e os 24 testes da
    demo passam.

- [x] **2026-11-17 — Painel "Quartos" conferido no navegador,** com a central em Docker e o simulador. Saíram
  quatro defeitos, corrigidos:
  - o Telegraf não gravava a pulseira;
  - a tabela de alertas dava erro do Flux;
  - os quadros de estado mostravam a hora;
  - a presença aparecia como `_value` e `id`.

  A captura do painel entrou no relatório do Q2 (Seção 11.1). Detalhes no diário.
  - Verificado: dados no InfluxDB conferidos por consulta; todos os painéis preenchidos na captura; o
    notificador formatou os 8 alertas da rodada, inclusive o `ajuda` e o cancelamento.

- [x] **2026-11-17 — Envio por mudança (temperatura e pulseira):** o hub mede a temperatura a cada 60 s e só a
  publica (MQTT e CAN) quando ela muda 0,2 °C ou a cada 5 min; `pulseira/<id>` sai no primeiro heartbeat e
  depois como resumo a cada 5 min (a telemetria opcional da pulseira também). Componente comum novo
  `firmware/common/report`. Detalhes em [`decisoes.md`](decisoes.md) e no diário.
  - Verificado: builds sem avisos (hub; pulseira padrão, demo e com MQTT); testes de host (`report` 23, com seis
    erros plantados pegos; os demais sem mudança); notificador e demo; o simulador, com a publicação
    interceptada, deu 12 mensagens de temperatura em 1 h estável (eram 120) e 15 numa rampa de 3,6 °C;
    relatório recompilado.
  - Falta: a bancada (item do hub) e a central no Docker (item da Central).

- [x] **2026-11-17 — Gestos do botão da pulseira:** dois toques = "estou bem" (cancela a queda ou o pedido de
  ajuda nos 15 s); segurar 3 s = pedido de ajuda (`HELP` → alerta `ajuda`); aperto simples não faz nada. No
  firmware das duas placas, no contrato, na central, na documentação e no relatório do Q2. Detalhes em
  [`decisoes.md`](decisoes.md) (*Gestos do botão*) e no diário.
  - Verificado: builds sem avisos (hub; pulseira padrão, demo e com todas as opções); testes de host
    (`button_gesture` 42, regras 82, `hub_config` 60, `fall_pipeline` 885, `signal_proc` 25); notificador 22 e
    demo 19 testes; uma janela da demo renderizada com a faixa nova; relatório recompilado.
  - Falta a bancada: item "Pulseira gravada e conferida", gestos do botão.

- [x] **2026-11-17 — "Pulseira" no lugar de "vestível" em todo o projeto,** inclusive no contrato MQTT
  (tópicos, alerta `pulseira_offline`, usuário do broker). Detalhes em [`decisoes.md`](decisoes.md) e no diário.
  - Verificado: builds sem avisos (hub e pulseira em três perfis), testes de host, do notificador, da demo e do
    modelo, e o relatório do Q2 recompilado.
  - Central em Docker com os tópicos novos: conferida em 2026-11-17 (item acima). O Telegraf não gravava
    `pulseira/<id>` por causa da sessão persistente, e foi corrigido.

- [x] **2026-11-16 — Relatório do Q2 escrito** (primeira versão do relatório final,
  `relatorios/Semestre 2/Projeto 3/Q2/latex/`, 66 páginas). A entrega segue em "Entregas da disciplina".
  - Gerado por `modelo/experimentos/12_relatorio_q2.py`: gráficos dos sensores, tabelas, números e PCB (renders,
    camadas, folhas do esquema, ERC e DRC).
  - Diagramas em TikZ/circuitikz; espaços marcados para as fotos que o grupo vai tirar.
  - Verificado:
    - o ajuste dos 22 pontos do LM35 reproduz a curva do firmware (`assert` no script);
    - o biquad do gráfico é o do `fall_model.c`;
    - o DRC rodado de novo deu 0 erros, 0 ligações faltando e 0 divergências (2 avisos de serigrafia); o
      ERC, 0;
    - o tamanho dos firmwares saiu do `idf.py size` (hub, pulseira sem hub e um build de teste com ESP-NOW);
    - `pdflatex` sem erros, sem *overfull* e sem referência indefinida;
    - as páginas conferidas na imagem;
    - a macro das fotos testada com e sem o arquivo.
- [x] **2026-11-16 — S2 P3 Q1 entregue e apresentado** (relatório em LaTeX e 22 slides). Informado pelo grupo.
- [x] **2026-11-16 — PCB v1 entregue (S2 P2, 16/10), fabricada e em mãos.** Informado pelo grupo. A montagem
  e os testes seguem em Hardware.
- [x] **2026-11-16 — `aulas/` fora do git.** Os slides ficam só no disco de cada um: `aulas/` no `.gitignore`
  e removida do índice (`git rm -r --cached`; os 18 arquivos continuam na pasta). Verificado: `git check-ignore`
  e a contagem dos arquivos no disco.
- [x] **2026-11-13 — Erros não tratados: 32 achados da revisão de código e 6 menores corrigidos.**
  - O que mudou e por quê está em [`decisoes.md`](decisoes.md) (2026-11-13). Os pontos de contrato (`seq`
    aleatório no boot, a FALL reenviada até entregar, a queda reconhecida por `seq` e `uptime_ms`) estão em
    [`protocolo.md`](protocolo.md) §3.
  - Testes novos:
    - regras: queda nova depois de a pulseira reiniciar;
    - notificador: alerta com dados inesperados, erro HTTP fora de `OSError`, mensagem recusada, espera pela
      internet, sessão persistente;
    - demo: porta ocupada no início;
    - modelo: os dois formatos de ensaio próprio, `nan`, tempo que recomeça, arquivo desconhecido, pasta
      ausente, cache da SisFall, classe única, limiar infinito, gatilho sem 99 %.
  - Verificado:
    - build sem avisos: pulseira padrão, demo e completo (MAC, MQTT, CAN, coleta); hub;
    - um MAC malformado ("aa:bb:cc:dd:ee") falha o build com a mensagem certa;
    - testes de host: regras 62, `hub_config` 60, `fall_pipeline` 885, `signal_proc` 25; notificador 19;
      demo 15; `pytest` do modelo 30;
    - o `09` reorganizado gera o mesmo modelo e os mesmos vetores (só a data do cabeçalho mudou);
    - os ensaios próprios saem idênticos pelo leitor novo;
    - o `04` dá os mesmos resultados, com a coluna `ensaios_adl` nova;
    - o `08`, rodado numa pasta temporária, dá as mesmas legendas (o relatório entregue não foi regenerado).
  - Não verificado: nada em placa (ver "As correções de erros não tratados" em "Agora: verificação em bancada").
  - Pendente: a decisão sobre as janelas de borda (Firmware).
- [x] **2026-11-13 — `demo_pulseira.py` não congela mais quando a USB pisca.** Na bancada, o gráfico parou
  depois de ~16 min. A placa continuava mandando dados (conferido abrindo a COM3), mas a thread de leitura tinha
  morrido com `SerialException: GetOverlappedResult failed (Acesso negado)`.
  - A leitura agora reabre a porta a cada 1 s, sem o pulso de reset, e o título mostra "PERDIDA, reabrindo…".
  - Uma linha que dá erro é ignorada (e avisada uma vez no terminal), em vez de parar a leitura.
  - Linhas estragadas (números emendados, `inf`) são descartadas.
  - O sinal do gráfico tem um teto em pontos, para um tempo absurdo não fazê-lo crescer.
  - Verificado:
    - 5 testes novos em `test_demo_pulseira.py` (14 no total), que falhavam antes da correção, um deles na
      mesma linha do `Traceback`;
    - 30 min simulados, com uma linha absurda: no máximo 2 400 pontos;
    - 200 mil linhas estragadas, sem nenhuma exceção;
    - leitura da placa real na COM3.
  - Não verificado: a reconexão com a placa real (falta puxar o cabo).
- [x] **2026-11-13 — Apresentação do Q1 enxugada para 22 slides:** sem os slides do filtro e do Gini, sem o
  tópico "Paridade C × Python" e com o último slide só com "Obrigado". Roteiro e guia renumerados; `main.pdf`
  atualizado. Verificado: `pdflatex` sem erros nem *overfull* e os slides alterados conferidos na imagem.
- [x] **2026-11-13 — Guia dos modelos para a apresentação do Q1**
  (`relatorios/Semestre 2/Projeto 3/Q1/explicacao-modelos.md`): cada método testado, o Random Forest a fundo e
  perguntas prováveis. Verificado contra o código dos modelos e os arquivos de resultados.
- [x] **2026-11-13 — SVM na configuração da pulseira e tamanho do Random Forest nos slides.**
  - `modelo/experimentos/11_svm_pulseira.py`: o SVM ao lado do Random Forest 60 × 10 e da regressão logística,
    com bootstrap por pessoa. Empate estatístico; o Random Forest fica (ver [`decisoes.md`](decisoes.md)).
  - Apresentação do Q1: a tabela "Por que escolhemos o Random Forest" ganhou uma nota dizendo que é a floresta
    da comparação (150 × 12, 252 kB) e que a da pulseira ocupa 106 kB; o slide "No vestível" passou a usar kB,
    como a tabela (antes, "104 KB").
  - Verificado: o tamanho real no `gadget.elf` (`fall_rf_nodes`, 106 248 bytes); o `11` reproduz o limiar do
    `09`; as 15 figuras da apresentação saíram idênticas byte a byte; `pdflatex` sem erros nem *overfull*, e
    os slides 21 e 24 renderizados e conferidos.
- [x] **2026-11-11 — Demonstração com uma placa e peças ausentes sem erro.**
  - **Pulseira:**
    - sem hub (MAC vazio) não liga o ESP-NOW;
    - sem MPU-6050 segue rodando, sem heartbeat;
    - perfil `sdkconfig.demo.defaults`.
  - **Hub:** menu "Room hub: sensors" (PIR, LM35, portas ausentes); sem PIR, as regras de presença ficam suspensas.
  - **Comuns:** Wi-Fi, MQTT e CAN dão um aviso por queda, não um erro por tentativa.
  - **Central:**
    - `interface/demo_pulseira.py` (janela ao vivo e ponte para a central);
    - coluna P(queda) no Grafana.
  - **Verificado no PC:**
    - build sem avisos: pulseira padrão, demo, e com MAC, MQTT, CAN e coleta; hub padrão, e com PIR, LM35 e uma
      porta ausentes e o CAN fora do modo bancada;
    - testes: regras (59, com 2 casos novos), `hub_config` (60), `fall_pipeline` (885), `signal_proc` (25),
      notificador (12), `demo_pulseira` (9), `pytest` do `modelo/` (16);
    - a janela tocando uma sessão sintética (trechos da SisFall): disparos de corrida com P = 0,00 e a queda com
      P = 0,99 e a faixa;
    - a ponte num broker Mosquitto local: `status` online/offline, `pulseira/<id>` e dois alertas com `prob`.
  - **Não verificado:**
    - nada rodou em placa;
    - o caminho até o Grafana, porque o Docker não estava no ar.
- [x] **2026-11-11 — Apresentação do Q1 em Beamer** (`relatorios/Semestre 2/Projeto 3/Q1/apresentacao/`).
  - 26 slides:
    - objetivo e dados;
    - o pipeline, com o sinal antes e depois do filtro;
    - visão geral dos métodos e resultados;
    - o Random Forest a fundo (árvore, Gini, floresta, votos, importância, por que foi escolhido);
    - a versão na pulseira.
  - Figuras, tabelas e números gerados por `modelo/experimentos/10_figuras_apresentacao.py`; roteiro da fala
    atualizado.
  - Verificado: `pdflatex` sem erros nem *overfull*; slides renderizados e conferidos um a um.
- [x] **2026-11-11 — Pipeline de queda na pulseira** (`firmware/gadget/components/fall_pipeline`), no lugar do
  detector provisório (`fall_detect`, removido).
  - Etapas: biquad de 20 Hz (ESP-DSP) → gatilho causal em picos de \|a\| ≥ 2,0 g → janela de −1,0 a +0,8 s →
    23 atributos no tempo → Random Forest de 60 árvores (padrão) ou regressão logística (`menuconfig`).
  - O modelo é gerado por `modelo/experimentos/09_exportar_pulseira.py`. Na SisFall (±8 g, validação por
    pessoa): Random Forest com 0,55 alarme falso/h e 95,1 % das quedas com ≤ 1 alarme/h; logística com 0,62/h
    e 94,9 %.
  - Contrato: P(queda) no `FALL` (13 bytes), no CAN `0x0FD` (3 bytes) e `"prob"` no alerta. O hub, o
    notificador e o simulador foram atualizados juntos.
  - Verificado no PC:
    - testes de host do `fall_pipeline`: 885 verificações. A paridade com o Python cobre 8 trechos da SisFall
      e 26 janelas: mesmos picos, atributos a ≤ 3 % da tolerância e a mesma P até 10⁻⁶;
    - regras do hub (54), `hub_config` (60), `signal_proc` (25), notificador (12) e `pytest` do `modelo/` (16);
    - build sem avisos: pulseira padrão (38 % livre; `s_pipeline` com 20 KB de RAM e a floresta com 104 KB de
      flash), pulseira com MQTT + CAN + coleta + logística, e hub.
  - **Nada disso rodou em placa:** a conferência está em "Pulseira gravada e conferida".
- [x] **2026-11-10 — Relatório do Q1 em LaTeX** (`relatorios/Semestre 2/Projeto 3/Q1/latex/`).
  - Layout do modelo de Numéricos, com as pastas `figuras/`, `imagens/` e `tabelas/`.
  - 26 páginas, 19 figuras e 9 tabelas.
  - As tabelas de resultados são geradas dos CSVs por `modelo/experimentos/08_relatorio_latex.py`.
  - Verificado: `pdflatex` sem erros, avisos nem linhas estouradas; páginas conferidas visualmente.
- [x] **2026-11-10 — Figuras que explicam os resultados do Q1** (`modelo/experimentos/07_figuras_explicativas.py`).
  - 11 figuras: saturação, funil do gatilho, escores, separação por atributos, importância e ablação, AR,
    templates, FFT, erros por tipo e os 18 ensaios próprios (sinais e matriz por método).
  - Nova tabela `ensaios_com_alarme_A.csv`.
  - Relatório e roteiro apontam para as figuras.
  - Verificado:
    - o Random Forest refeito no script confere com `proprios_por_arquivo.csv` arquivo a arquivo;
    - os totais da matriz conferem com `proprios_resumo.csv`;
    - os escores salvos correspondem às janelas refeitas;
    - `pytest` com 13 testes passando;
    - cada figura conferida visualmente.
- [x] **2026-11-09 — Modelos de queda desenvolvidos e comparados no PC** (S2 P3 Q1, [`modelo/`](../modelo/README.md)).
  - Dados: SisFall (38 pessoas, 4505 ensaios, conferida) e 18 ensaios próprios, descritos em `dados/README.md`.
  - Pipeline: filtro → gatilho (picos > 2,0 g, 99,2 % das quedas) → janela → atributos → classificador.
  - 22 métodos comparados, com validação cruzada por pessoa, em dois cenários de faixa (±8 g e ±2 g):
    - das aulas: correlação normalizada e não normalizada, DTW, AR/ARX, FFT, distâncias;
    - da literatura: regras, regressão logística, árvore, SVM, Random Forest, CNN 1D.
  - Resultados (±8 g): limiar atual com 69,9 alarmes falsos/h; Random Forest com 0,4/h; regressão logística
    com 1,4/h (~94 % das quedas).
  - Teste nos ensaios próprios: 0 alarmes falsos com RF, regressão logística e SVM.
  - Verificado:
    - 13 testes de unidade (`uv run pytest`), inclusive os exemplos dos slides (AR(2), DTW, correlação) e o
      gatilho vetorizado contra o laço do firmware;
    - os scripts rodam do início ao fim;
    - nenhuma pessoa fica no treino e no teste (`assert` no script).
  - Rascunho do relatório e roteiro da apresentação em `relatorios/Semestre 2/Projeto 3/Q1/`.

- [x] **2026-10-23 — PCB v1 desenhada e arquivos de fabricação gerados** (`hardware/placas/v1/`).
  - Esquema com ERC 0. Layout de 100,0 × 59,8 mm, 2 camadas: trilhas no F.Cu, plano de GND no B.Cu, área livre
    sob a antena, regras para a JLCPCB.
  - Verificado pelo `kicad-cli`: DRC com 0 erros, 0 ligações faltando e 0 divergências com o esquema (só 2
    avisos de serigrafia esperados); contorno e furos conferidos nos Gerbers (114 furos metalizados).
  - Teste no papel em 1:1 feito pelo usuário (encaixe e ordem dos pinos).
  - Saídas: Gerbers, furação, zip para a JLCPCB, esquema em PDF, lista de materiais e render 3D.
- [x] **2026-10-23 — Alimentação decidida.**
  - Entrada USB-C única (cabo A → C), com 10 µF e Schottky até a DevKit.
  - A pulseira roda com power bank, sem circuito de bateria na placa.
  - Firmware com `GADGET_BATTERY_SENSE` desligado por padrão.

- [x] **2026-10-23 — Sem buzzer.**
  - A sinalização local fica só com o LED, e o alerta chega pela central.
  - Firmware com `PIN_BUZZER = GPIO_NUM_NC`, verificado por build.
  - RF12 e os documentos atualizados.

- [x] **2026-10-21 — Condicionamento do LM35 decidido.**
  - LM358 em 5 V e GND, sem o inversor; 100 nF no LM35 e no LM358 (o ramo de 5 V filtrado foi dispensado em
    2026-10-23); RC de 10 kΩ + 1 µF (fc ≈ 16 Hz) no
    GPIO 4.
  - É decisão de projeto: a conferência na placa segue em Hardware.

- [x] **2026-10-20 — Mapa de pinos da PCB única.**
  - Em `firmware/common/pcb/include/pcb_pins.h`, verificado na compilação.
  - Verificado por: build das duas placas e testes de erro forçado.
  - O layout segue em Hardware.
- [x] **2026-10-20 — 4 dos 7 PDFs de `relatorios/` recuperados** de cópias, conferidas pelo número de páginas.
- [x] **2026-10-20 — Firmware nos moldes das aulas, MQTT e CAN nas duas placas, ESP-DSP na pulseira.**
  - Verificado por: build e testes no PC.
  - A bancada segue em "Agora".
- [x] **2026-10-20 — Campo `hora` do `status` gravado no InfluxDB.** Verificado na central em Docker.
- [x] **2026-10-20 — 11 dos 12 slides apagados recuperados;** o `CEM_Projeto_PCB.pdf` foi baixado de novo.
