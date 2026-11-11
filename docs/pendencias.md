# Pendências

**A lista única do que falta fazer.** Atualizada ao fim de toda tarefa:
- o que foi feito vai para [Concluído](#concluído), marcado, com a data e como foi verificado;
- o que surgiu entra na seção certa.

O plano por fases está em [`roadmap.md`](roadmap.md), e a fase atual e o diário em [`estado.md`](estado.md).

Um item só sai daqui quando o critério dele foi cumprido. Se ele pede bancada, compilar não basta.

## Agora: verificação em bancada

Tudo isto já está no código e compila; falta rodar na placa.

- [ ] **Hub gravado e conferido**
  - preencher no `menuconfig`: Wi-Fi, broker, MAC do vestível e chaves ([`firmware/room/README.md`](../firmware/room/README.md));
  - conferir:
    - portas com abre-fecha rápido, sem perder nem inventar estado;
    - PIR depois dos 60 s de aquecimento;
    - temperatura contra um termômetro;
    - `status` offline (Last Will) até ~45 s depois de desligar;
  - **sem peças**, cada caso dá **um** aviso no log e o resto continua:
    - Wi-Fi sem SSID ou com senha errada (o aviso diz o motivo);
    - broker parado;
    - LM35 solto;
    - CAN sem transceptor, com o modo bancada desligado;
    - PIR, LM35 e uma porta declarados ausentes no menu "Room hub: sensors": nenhum alerta falso de inatividade
      nem de saída noturna.
- [ ] **Ensaio da demonstração com uma placa** (antes da aula; é o primeiro teste do firmware em placa).
  - Gravar o perfil demo ([`firmware/gadget/README.md`](../firmware/gadget/README.md), "Demonstração com uma placa
    só"). No boot devem aparecer:
    - `+-8 g, +-500 dps`;
    - `fall pipeline: trigger 2.00 g, random forest…`;
    - `no hub configured … stand-alone`.
  - `interface/demo_vestivel.py --porta COMx`:
    - balançar a placa: disparos com P baixa;
    - soltar a placa de pé numa almofada e deixá-la deitada: "QUEDA DETECTADA".
  - Com a central no Docker e `--mqtt-host`: o alerta aparece no Grafana, com a P(queda).
  - Gravar a sessão com `--gravar`: ela serve de primeiro dado real do sensor.
- [ ] **Vestível gravado e conferido**
  - 200 Hz ± 5 ms medidos pelo modo de coleta;
  - **sem peças:** MPU solto no boot e durante o uso dá um aviso, LED vermelho lento e nenhum heartbeat, e volta
    com "MPU-6050 connected"; hub desligado dá um aviso só;
  - **faixa de ±8 g:**
    - no boot, a linha `+-8 g, +-500 dps, 200 Hz into the FIFO`;
    - parada, o eixo vertical lê ~4096 no CSV do modo de coleta (~16384 seria ±2 g);
    - uma batida passa de 3,46 g sem ficar cortada num teto plano;
  - descoberta do canal do hub;
  - heartbeat aparecendo no Grafana;
  - queda simulada pelo botão chegando ao Telegram;
  - **pipeline de queda na placa:**
    - no boot, a linha `fall pipeline: trigger 2.00 g, random forest, fall when P >= 0.73`;
    - balançar ou bater a placa gera `trigger at sample …: peak … g, P(fall) …`;
    - a linha `FEAT:` traz os 23 atributos, P e o custo em ciclos (esperado: bem abaixo de 1 ms);
    - repetir com a regressão logística (`menuconfig` → Fall classifier).
  - **paridade na placa:** passar o CSV bruto do modo de coleta pelo Python (`gatilho_causal`, atributos,
    modelo) deve dar os mesmos picos e as mesmas P das linhas `FEAT:`. Falta um script que leia o log da
    serial e faça essa comparação.
- [ ] **CAN entre as duas placas**
  - vestível com `GADGET_CAN`, modo de bancada desligado nas duas, dois transceptores e terminação;
  - quadros `0x0FD` (3 bytes, com P) e `0x0FE` no log do hub;
  - recuperação de *bus-off* tirando um fio do barramento: um aviso "CAN bus not connected" e um "connected again"
    ao recolocar.
- [ ] **MQTT do vestível**
  - com o hub desligado, uma queda simulada chega ao Telegram "enviada pela própria pulseira";
  - com o hub ligado, chega uma só notificação.
- [ ] **Bot do Telegram**
  - criar pelo @BotFather e preencher `TELEGRAM_TOKEN` e `TELEGRAM_CHAT_ID` no `interface/.env`;
  - medir a latência alerta → celular (meta ≤ 5 s, [`requisitos.md`](requisitos.md)).
- [ ] **Latência pico → MQTT ≤ 500 ms e autonomia do vestível medidas.** É o critério de pronto da fase 7.

## Hardware

- [ ] **Encomendar a PCB v1 na JLCPCB:** enviar `hardware/placas/v1/placa-v1-jlcpcb.zip` com as opções de
  [`hardware/placas/v1/README.md`](../hardware/placas/v1/README.md). Antes de pagar, conferir no visualizador de
  Gerber do site: 2 camadas, plano de GND embaixo com o retângulo livre sob a antena, todos os furos.
- [ ] **Montar e testar a PCB v1.**
  - Antes de encaixar a DevKit, com o multímetro: sem curto entre 5V e GND nem entre 3V3 e GND (TP1, TP2,
    TP3). Depois ligar pela USB-C e medir 5 V no TP1.
  - Encaixar a DevKit e medir 3,3 V no TP2.
  - Gravar o hub e o vestível e repetir na placa os testes de "Agora: verificação em bancada".
  - O CANH passa a 0,9 mm do pino 2 do LM358: comparar a leitura do LM35 com o autoteste do CAN ligado e
    desligado.
- [ ] **Conferir a ordem dos pinos da DevKitC-1 antes de encomendar.** Comparar os nomes de pino impressos no
  U1 do PDF 1:1 (3V3, 3V3, EN, GPIO4…) com os impressos na própria DevKit. O footprint `projeto:ESP32-S3-DevKitC`
  usa a ordem da biblioteca da Espressif. A distância entre fileiras já foi resolvida: medido ~25,4 mm,
  footprint com 25,25 mm, aceito.
- [ ] **Refazer a fiação de bancada com os pinos novos.**
  - Hub: LM35 no 4, CAN no 13/14, PIR no 8, portas no 11/12.
  - Vestível: I²C no 1/2, INT no 17, botão no 18 (o buzzer e a medição da bateria saíram do esquema).
- [ ] **Conferir a curva do LM35 na placa nova:** 3 ou 4 pontos (perto de 10, 20 e 30 °C), medindo a tensão
  no GPIO 4 depois do filtro. Se divergir, refazer a calibração e atualizar `lm35.h`.
- [ ] **Validar o LM35 com termopar sobre a PCB** (pendente desde o S1 P3; [`requisitos.md`](requisitos.md),
  RNF10).
- [ ] **Power bank do vestível:** comprar um com **saída USB-A** e, de preferência, modo "always on" ou de baixa
  corrente.
  - Testar algumas horas ligado à placa, para ver se desliga sozinho.
  - Medir o consumo com um medidor USB.
- [ ] **Placa própria do vestível (futuro):** LiPo 1S com proteção, carregador e regulador LDO de 3,3 V, divisor no
  GPIO 5 (`GADGET_BATTERY_SENSE`). Só então o alerta de bateria baixa volta a funcionar.
- [ ] **Entrada de 5 V da PCB:**
  - no esquema: 10 µF eletrolítico na saída V do módulo e Schottky 1N5819 do 5 V da placa até o pino 5V da
    DevKit (a faixa do lado da DevKit); polyfuse e TVS ficaram de fora (versão mínima);
  - USB-C: o módulo usado (pinos V, D−, D+, G) **não tem os resistores de 5,1 kΩ no CC** e não expõe esses
    pinos. No teste de 2026-10-23, deu 5 V só com cabo USB-A → C. Alimentar sempre com carregador USB-A de
    ≥ 2 A e cabo A → C, e escrever isso perto do conector. Para aceitar carregador USB-C, trocar por um
    módulo com os resistores.
- [ ] **Antena da DevKit fora de cobre da PCB:** área livre (*keep-out*) sob a antena do módulo, ou a antena
  para fora da borda.
- [ ] **Como o vestível vai ser usado com a PCB única:** o MPU-6050 precisa ficar preso ao corpo. Decidir entre
  a placa presa ao antebraço ou o MPU num cabo curto (com o I²C a 100 kHz).
- [ ] **Transceptores CAN de 3,3 V e terminação de 120 Ω** para a bancada e para a sala.
- [ ] **Furos de fixação (M3) e pontos de teste** (5 V, 3V3, GND, GPIO 4): deixados para uma próxima versão
  da placa (decisão de 2026-10-23).

## Firmware

- [ ] ***Light sleep* no vestível.** A estratégia depende do consumo medido. O mapa de pinos já deixa o botão
  e o INT do MPU em RTC GPIO e o I²C no par do ULP.
- [ ] **Hub sem internet:** apontar `APP_SNTP_SERVER` para o NTP local quando ele existir (ver Central).
- [ ] **CAN de outros grupos da sala no MQTT:** hoje só vai para o log; falta definir os tópicos em
  [`protocolo.md`](protocolo.md) §4.
- [ ] **Retreinar o pipeline de queda com os dados do pulso** (depende da coleta, em "Dados e avaliação").
  - O modelo no vestível foi treinado na SisFall (cintura, ±8 g): é provisório.
  - Com os dados do pulso:
    - refazer a varredura do gatilho (`02_gatilho.py`) e ajustar `GADGET_FALL_TRIGGER_CG`;
    - rodar `09_exportar_vestivel.py`, que gera o modelo e os vetores de teste juntos;
    - rodar os testes de host do `fall_pipeline`.
  - Avaliar no pulso se os atributos de frequência voltam (no cintura eles levam o Random Forest de 0,55 para
    0,34 alarme/h, mas exigem FFT no C).
  - **A decidir com os dados do pulso** (teste rápido de 2026-11-10 na SisFall, cenário A, script só no
    scratchpad):
    - **janela de +2 s depois do pico:** na regressão logística, quedas com ≤ 1 alarme/h passam de 92,8 % para
      95,3 % e os alarmes/h caem de 1,4 para 0,8. Exige buffer de ≥ 600 amostras e atrasa a decisão em ~1 s;
    - **cascata "E" (regressão logística e Random Forest precisam concordar):** de 95,7 % para 96,3 % das
      quedas com ≤ 1 alarme/h. Ganho pequeno, escolhido nos mesmos dados; só vale se os alarmes falsos no
      pulso pedirem.

## Central

- [ ] **Painel no Grafana para `vestivel_telemetria`**, a telemetria que o vestível publica direto (hoje ela só
  é gravada).
- [ ] **Servidor NTP local** (no Raspberry Pi ou num container da central), para a ILPI funcionar sem internet.
- [ ] **Conferir o painel "Quartos" no navegador:** as consultas foram testadas, a aparência não.
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

- [ ] **Entregar o S2 P3 Q1 (10/11):** revisar e formatar
  [`relatorio.md`](../relatorios/Semestre%202/Projeto%203/Q1/relatorio.md) e montar os slides pelo
  [`roteiro-apresentacao.md`](../relatorios/Semestre%202/Projeto%203/Q1/roteiro-apresentacao.md), com as figuras
  de `modelo/resultados/`.
  - Pronto: as figuras, o relatório em LaTeX (`Q1/latex/main.pdf`) e o roteiro.
  - Falta: a revisão do grupo e montar os slides.
  - Na contracapa, preencher `\docentes` em `main.tex` com os docentes do S2.
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
  - cobertura do vestível pelo `rssi`;
  - 24 h sem reinício.

  Pronto quando existir a tabela de resultados.

## Repositório

- [ ] **Baixar de novo 3 PDFs** de `relatorios/Semestre 2/Projeto 2/`: `Entrega 1.pdf`,
  `EnunciadoProjeto 2_parcial.pdf` e `EnunciadoProjeto 2_PCB.pdf`. Foram apagados por engano e não têm cópia no
  disco.
- [ ] **Decidir se os slides de `aulas/` vão para o git.** Se o repositório for público, deixar só o índice.
- [ ] **Primeiro commit** de tudo o que foi feito, quando o usuário pedir.

## Futuro (sem data)

Detalhes em [`roadmap.md`](roadmap.md), seção Futuro.

- [ ] Modelo preditivo de queda, no lugar do limiar provisório. Primeira versão desenvolvida e comparada no PC
  (ver Concluído, 2026-11-09); falta embarcar (ver Firmware) e treinar com dados do pulso.
- [ ] Backend + PostgreSQL: residentes, dispositivos, cuidadores, alertas com ciclo de vida.
- [ ] Nuvem: bridge do Mosquitto com TLS.
- [ ] App ou web do cuidador, com reconhecimento de alertas.
- [ ] OTA.
- [ ] Configuração do vestível pelo hub (mensagem `CONFIG` do ESP-NOW).
- [ ] Cobertura além do quarto: vários hubs e roaming do vestível.

## Concluído

O mais recente no topo.

- [x] **2026-11-11 — Demonstração com uma placa e peças ausentes sem erro.**
  - **Vestível:**
    - sem hub (MAC vazio) não liga o ESP-NOW;
    - sem MPU-6050 segue rodando, sem heartbeat;
    - perfil `sdkconfig.demo.defaults`.
  - **Hub:** menu "Room hub: sensors" (PIR, LM35, portas ausentes); sem PIR, as regras de presença ficam suspensas.
  - **Comuns:** Wi-Fi, MQTT e CAN dão um aviso por queda, não um erro por tentativa.
  - **Central:**
    - `interface/demo_vestivel.py` (janela ao vivo e ponte para a central);
    - coluna P(queda) no Grafana.
  - **Verificado no PC:**
    - build sem avisos: vestível padrão, demo, e com MAC, MQTT, CAN e coleta; hub padrão, e com PIR, LM35 e uma
      porta ausentes e o CAN fora do modo bancada;
    - testes: regras (59, com 2 casos novos), `hub_config` (60), `fall_pipeline` (885), `signal_proc` (25),
      notificador (12), `demo_vestivel` (9), `pytest` do `modelo/` (16);
    - a janela tocando uma sessão sintética (trechos da SisFall): disparos de corrida com P = 0,00 e a queda com
      P = 0,99 e a faixa;
    - a ponte num broker Mosquitto local: `status` online/offline, `vestivel/<id>` e dois alertas com `prob`.
  - **Não verificado:**
    - nada rodou em placa;
    - o caminho até o Grafana, porque o Docker não estava no ar.
- [x] **2026-11-11 — Apresentação do Q1 em Beamer** (`relatorios/Semestre 2/Projeto 3/Q1/apresentacao/`).
  - 26 slides:
    - objetivo e dados;
    - o pipeline, com o sinal antes e depois do filtro;
    - visão geral dos métodos e resultados;
    - o Random Forest a fundo (árvore, Gini, floresta, votos, importância, por que foi escolhido);
    - a versão no vestível.
  - Figuras, tabelas e números gerados por `modelo/experimentos/10_figuras_apresentacao.py`; roteiro da fala
    atualizado.
  - Verificado: `pdflatex` sem erros nem *overfull*; slides renderizados e conferidos um a um.
- [x] **2026-11-11 — Pipeline de queda no vestível** (`firmware/gadget/components/fall_pipeline`), no lugar do
  detector provisório (`fall_detect`, removido).
  - Etapas: biquad de 20 Hz (ESP-DSP) → gatilho causal em picos de \|a\| ≥ 2,0 g → janela de −1,0 a +0,8 s →
    23 atributos no tempo → Random Forest de 60 árvores (padrão) ou regressão logística (`menuconfig`).
  - O modelo é gerado por `modelo/experimentos/09_exportar_vestivel.py`. Na SisFall (±8 g, validação por
    pessoa): Random Forest com 0,55 alarme falso/h e 95,1 % das quedas com ≤ 1 alarme/h; logística com 0,62/h
    e 94,9 %.
  - Contrato: P(queda) no `FALL` (13 bytes), no CAN `0x0FD` (3 bytes) e `"prob"` no alerta. O hub, o
    notificador e o simulador foram atualizados juntos.
  - Verificado no PC:
    - testes de host do `fall_pipeline`: 885 verificações. A paridade com o Python cobre 8 trechos da SisFall
      e 26 janelas: mesmos picos, atributos a ≤ 3 % da tolerância e a mesma P até 10⁻⁶;
    - regras do hub (54), `hub_config` (60), `signal_proc` (25), notificador (12) e `pytest` do `modelo/` (16);
    - build sem avisos: vestível padrão (38 % livre; `s_pipeline` com 20 KB de RAM e a floresta com 104 KB de
      flash), vestível com MQTT + CAN + coleta + logística, e hub.
  - **Nada disso rodou em placa:** a conferência está em "Vestível gravado e conferido".
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
  - O vestível roda com power bank, sem circuito de bateria na placa.
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
- [x] **2026-10-20 — Firmware nos moldes das aulas, MQTT e CAN nas duas placas, ESP-DSP no vestível.**
  - Verificado por: build e testes no PC.
  - A bancada segue em "Agora".
- [x] **2026-10-20 — Campo `hora` do `status` gravado no InfluxDB.** Verificado na central em Docker.
- [x] **2026-10-20 — 11 dos 12 slides apagados recuperados;** o `CEM_Projeto_PCB.pdf` foi baixado de novo.
