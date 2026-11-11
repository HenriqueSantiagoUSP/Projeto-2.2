# Estado do projeto

**O único lugar que diz em que etapa estamos.** O plano está em [`roadmap.md`](roadmap.md); aqui fica o que já
foi feito e **como foi verificado**. Atualize ao fim de cada tarefa, com uma entrada no diário.

Níveis de verificação usados abaixo:

| Nível | Significa |
|---|---|
| **build** | compila no ESP-IDF 6.0.1 sem erros nem avisos |
| **testes no PC** | testes automáticos rodando no PC (lógica pura, sem placa) |
| **ponta a ponta (sem placa)** | central real em Docker + simulador no lugar do hub |
| **bancada** | rodando na placa, com os sensores |

## Etapa atual

**Fases 1 a 7 com código pronto, revisado nos moldes das aulas; nada verificado em bancada ainda.** O firmware
das duas placas agora segue os slides de [`aulas/`](../aulas/README.md) e o ESP-IDF v6.0.1:
- componentes comuns em `firmware/common/`;
- tasks com prioridade nomeada e núcleo fixo;
- event group no lugar de variáveis compartilhadas;
- CAN e MQTT nas duas placas (no vestível, como opção);
- ESP-DSP no vestível.

O próximo passo é gravar as placas e verificar em bancada (ver [`pendencias.md`](pendencias.md)). O mapa de pinos é o da
**PCB única** das duas placas ([`hardware/README.md`](../hardware/README.md)). A **PCB v1** está desenhada,
conferida (ERC e DRC sem erros) e com os arquivos de fabricação em
[`hardware/placas/v1/`](../hardware/placas/v1/README.md). Falta encomendar e montar.

**Modelo de queda:**
- **2026-11-09 (S2 P3 Q1):** o pipeline e 22 métodos foram desenvolvidos e comparados **no PC**
  ([`modelo/`](../modelo/README.md)), com a SisFall e 18 ensaios próprios, validação cruzada por pessoa.
- **2026-11-11:** o pipeline foi para o vestível, com o Random Forest (`fall_pipeline`, no lugar do detector
  provisório). Foi verificado nos testes de host, com paridade C × Python, e no build; ainda não rodou em placa.
- O modelo foi treinado na SisFall (cintura) e precisa ser retreinado com os dados do pulso.

## Situação por fase

| Fase | O quê | Situação | Verificado por |
|---|---|---|---|
| 0 | Estrutura do repositório e documentação | feito | — |
| 1 | Contrato em código (`firmware/common/protocol`, agora com `protocol_can.h`) e componentes comuns (`rtos_util`, `sys_events`, `net`, `mqtt_link`, `can`, `indicators`) | feito | build (hub e vestível) |
| 2 | Hub: debounce corrigido, portas modulares, LM35 com `adc_cali` + curva MMQ, aquecimento do PIR | código pronto | build |
| 3 | Hub: Wi-Fi STA + `WIFI_PS_NONE` + backoff, SNTP, MQTT 3.1.1 com senha, Last Will, outbox, config em NVS, watchdog de 30 s | código pronto | build; validação da config em testes no PC (60 verificações) |
| 4 | Hub: motor de regras, `alerta`, buzzer, LED de status | código pronto | build; regras em testes no PC (16 cenários, 54 verificações) |
| 5 | Central: Mosquitto com ACL (agora com o usuário `vestivel`), Telegraf, InfluxDB, Grafana, notificador, simulador novo | feito | ponta a ponta (sem placa); notificador em testes no PC (12 testes) |
| 6 | Vestível: MPU-6050 a 200 Hz pelo FIFO, modo de coleta, ESP-NOW com varredura e heartbeat; MQTT e CAN opcionais | código pronto | build (padrão e com MQTT + CAN + coleta ligados) |
| 7 | Vestível: pipeline de queda (filtro, gatilho de 2,0 g, 23 atributos, Random Forest ou regressão logística) + cooldown, botão "estou bem", bateria. Até 2026-11-10 era o detector provisório com histerese | código pronto, **sem light sleep**; modelo provisório (SisFall, cintura) | build; `fall_pipeline` (885 verificações, com paridade C × Python em 26 janelas) e `signal_proc` (25 verificações) no PC |
| 8 | Coleta de dados | protocolo escrito ([`coleta-de-dados.md`](coleta-de-dados.md)); SisFall e 18 ensaios próprios (cintura, ±2 g, fora do protocolo) descritos em [`dados/README.md`](../dados/README.md); falta a coleta no pulso | leitura e contagem pelo `modelo/` |
| 9 | Avaliação | não iniciada | — |

### O que a verificação "ponta a ponta (sem placa)" cobriu (2026-10-20)

- `docker compose up` sobe os 5 serviços.
- O broker recusa conexão anônima e senha errada; a ACL impede o `notificador` de ler portas.
- O simulador publicou estados, heartbeats e episódios de alerta:
  - o notificador formatou cada episódio;
  - o InfluxDB recebeu as 6 medidas, com as tags `site`, `quarto` e `id`;
  - as 12 consultas do painel Grafana rodaram sem erro e devolveram dados.
- `config/desejada` publicada pelo usuário `painel` voltou em `config/aplicada`.
- Não coberto: o envio real ao Telegram (falta um bot) e a aparência do painel no navegador.

## O que falta

A lista de pendências e próximos passos fica em [`pendencias.md`](pendencias.md), com caixas de marcar.
Ela é atualizada ao fim de toda tarefa, junto com este arquivo.

## Diário

### 2026-11-11 — apresentação do Q1 em Beamer

- **Pedido:** apresentação do relatório do Q1, com:
  - os modelos testados (visão geral), o pipeline e os resultados, com figuras;
  - um sinal de exemplo antes e depois do filtro;
  - o Random Forest explicado a fundo.

  Sem o limiar antigo como "problema": o limiar aparece só como o gatilho do pipeline.
- **Feito:** `relatorios/Semestre 2/Projeto 3/Q1/apresentacao/`, em Beamer 16:9 com 26 slides e a identidade do
  relatório.
  - As figuras ficam em tamanho de slide, geradas por `modelo/experimentos/10_figuras_apresentacao.py`.
  - As tabelas e os números (`numeros.tex`) são gerados pelo mesmo script: nenhum é digitado à mão.
  - O `roteiro-apresentacao.md` foi reescrito para seguir os slides.
  - O estilo das figuras passou para `experimentos/_estilo.py`, usado pelo `07` e pelo `10`.
- **Verificado:**
  - o `07` gera as mesmas figuras, byte a byte, depois de extrair o estilo;
  - o script 10 confere que o Random Forest que ele explica é o do vestível (limiar e vetores de teste,
    \|ΔP\| = 5·10⁻⁷);
  - `pdflatex` sem erros nem *overfull*;
  - os 26 slides foram renderizados (`pdftoppm`) e conferidos um a um.

### 2026-11-11 — pipeline de queda no vestível

- **Pedido:** registrar a escolha do modelo e do gatilho e levar o pipeline ao firmware, com o envio da
  queda pelo CAN e pelo MQTT.
- **Antes de portar, uma comparação no PC** (SisFall, cenário A, validação por pessoa):
  - o gatilho causal (o que roda amostra a amostra) deixa passar as mesmas 99,2 % das quedas que o
    `find_peaks` e dispara menos: 753 contra 858 por hora;
  - só os 23 atributos no tempo bastam:
    - logística: 0,62 alarme/h (0,55 com todos);
    - Random Forest de 60 × 10: 0,55 (0,34 com todos).
- **Python:**
  - `pipeline.gatilho_causal` e `montar(..., causal=True)`, com 3 testes novos;
  - `experimentos/09_exportar_vestivel.py` treina os dois classificadores e gera o modelo em C e os vetores de
    teste;
  - `resultados/vestivel.json` guarda as métricas e os limiares.
- **Firmware do vestível:**
  - componente `fall_pipeline`:
    - biquad do ESP-DSP por eixo;
    - gatilho;
    - um buffer circular por eixo;
    - atributos;
    - Random Forest e logística;
    - custo em ciclos com `portMUX`;
  - `Kconfig`:
    - `GADGET_FALL_TRIGGER_CG` (200);
    - escolha do classificador;
    - os limiares L<sub>on</sub>/L<sub>off</sub> da histerese saíram;
  - `fall_detect` e `sp_features_compute_timed` removidos;
  - a linha `FEAT:` passa a ter os 23 atributos, P e os ciclos.
- **Contrato** ([`protocolo.md`](protocolo.md)): P(queda) no `FALL` (13 bytes), no CAN `0x0FD` (3 bytes) e
  `"prob"` no `detalhe` do alerta.
  - O hub (`espnow_link`, regras, publicação e log do CAN) foi atualizado junto.
  - O notificador mostra a confiança do modelo, e o simulador publica `prob`.
- **Verificado:**
  - testes de host do `fall_pipeline` (885 verificações):
    - o gatilho em sinais sintéticos;
    - o filtro partindo do repouso;
    - a janela logo depois do boot;
    - os classificadores sobre os atributos do Python (\|ΔP\| < 10⁻⁶);
    - o pipeline inteiro em 8 trechos da SisFall: 26 janelas, mesmos picos, atributos a ≤ 3 % da tolerância;
  - regras do hub (54), `hub_config` (60), `signal_proc` (25), notificador (12) e `pytest` do `modelo/` (16);
  - build sem avisos:
    - vestível padrão: 38 % da partição livre, `s_pipeline` com 20 KB de RAM, floresta com 104 KB de flash;
    - vestível com MQTT + CAN + coleta + logística;
    - hub.
- **Não verificado:**
  - nada rodou em placa;
  - o custo em ciclos e a decisão na placa ficam para a bancada ([`pendencias.md`](pendencias.md)).

### 2026-11-10 — esquema SQLite da atividade de Fundamentos de Software

- **Onde:** fora deste repositório, em `C:\Users\henri\dev\tarefa python sql` (repositório do grupo).
- **Entrega:** notebook com SQLite, classes, DAOs, Faker e JOINs, sobre o banco deste projeto. Serve de
  protótipo para o banco relacional da central.
- **Revisão do esquema do grupo:**
  - `quarto` com chave artificial + `codigo` do tópico MQTT;
  - `ilpi.site`;
  - histórico em períodos, com índice parcial único;
  - alerta com estado, fim e cuidador;
  - tabelas `STRICT` e `CHECK` de datas com `IS date()`. Com `=`, uma data inválida passava, porque `date()`
    devolve NULL.
- **Correções nos módulos do grupo:**
  - conflito de merge;
  - nomes de tabela e de coluna errados;
  - `BancoSQLite` sem importar;
  - recursão infinita em `EventoSensor.id_evento`;
  - `faker.py` encobrindo a biblioteca.
- **Notebook:** gerado dos módulos por `gerar_notebook.py`.
- **Verificação:**
  - o notebook rodou duas vezes seguidas sem erro, com os `assert`s de CRUD, injection, integridade e JOIN ×
    Python, `foreign_key_check` vazio e `integrity_check` = ok;
  - o roteiro também roda no Python 3.12 (o do Colab).
- **Pendências:** as decisões para o PostgreSQL estão no item "Banco relacional" de `pendencias.md`.

### 2026-11-10 — cascata de modelos × modelo único (teste rápido)

- **Pergunta do usuário:** vale pôr modelos em cascata ou é melhor escolher um?
- **Teste** com os escores fora da dobra do cenário A (validação por pessoa), num script só no scratchpad.
  Quedas detectadas com ≤ 1 alarme/h:

  | Variante | Resultado |
  |---|---|
  | Random Forest sozinho | 95,7 % |
  | cascata "E" de LR + RF | 96,3 % |
  | cascata "E" de LR + SVM + RF | 96,9 % |
  | média de LR + SVM + RF + CNN | 98,1 % |
  | cascata "OU" | 94,0 % (piora) |

  As combinações foram escolhidas nos mesmos dados, então os números são otimistas.
- **Cascata por custo** (LR permissivo → RF): o LR deixa só 3,2 % das janelas de atividade chegarem ao RF, sem
  perda. No ESP32 isso não é necessário: o classificador roda ~860 vezes por hora no pior caso.
- **Janela mais longa depois do impacto** (+2 s em vez de +0,8 s): AUC do RF de 0,9987 para 0,9996. A regressão
  logística vai de 92,8 % para 95,3 % das quedas com ≤ 1 alarme/h.
- **Conclusão:** manter um classificador por vez (o pipeline já é uma cascata gatilho → classificador) e testar
  a janela mais longa quando houver dados do pulso. Registrado em `pendencias.md`, no item do pipeline.

### 2026-11-10 — relatório do Q1 em LaTeX

- **Onde:** `relatorios/Semestre 2/Projeto 3/Q1/latex/`, com `main.tex` e as pastas `figuras/`, `imagens/` e
  `tabelas/`.
- **Layout:** o do relatório de Numéricos do usuário: capa com o logo da Poli e os três integrantes, contracapa,
  resumo, sumário, seções em azul e cabeçalho "EPUSP - Projeto Integrativo 3".
- **Conteúdo:** introdução (binário × caracterizador), dados, pipeline, modelos (com as equações de
  correlação, DTW, AR/ARX, mínimos quadrados e AIC/BIC), avaliação e os 13 testes, resultados, discussão,
  conclusões, limitações, referências e apêndice de reprodução.
- **Figuras e tabelas:** 19 figuras e 9 tabelas. As 6 tabelas de resultados saem dos CSVs por
  `modelo/experimentos/08_relatorio_latex.py`, que também copia as figuras.
- **Verificação:**
  - `pdflatex` duas vezes: 26 páginas, sem erro, aviso ou linha estourada (corrigidos `headheight`, a ordem
    do `tocloft` e três tabelas largas);
  - páginas renderizadas e conferidas.
- **Falta:** os docentes do S2 não estão em `relatorios/README.md`. A linha deles na contracapa aparece quando
  o macro `\docentes` for preenchido.

### 2026-11-10 — figuras explicativas do Q1

- **Script novo**, `modelo/experimentos/07_figuras_explicativas.py`, com 11 figuras em `modelo/resultados/`:
  - saturação em ±16, ±8 e ±2 g;
  - funil gatilho → classificador;
  - escores de quedas × atividades em seis métodos;
  - separação por pico, quietude e inclinação;
  - importância e ablação do Random Forest;
  - AR(12) prevendo e errando;
  - templates da correlação;
  - espectro (FFT);
  - erros por tipo;
  - os 18 ensaios próprios: sinais e matriz por método.
- **Tabela nova** `ensaios_com_alarme_A.csv`: o limiar atual dá alarme em 97–100 % dos ensaios de correr,
  pular, tropeçar e subir escada rápido; o Random Forest, em nenhum.
- **Relatório e roteiro** do Q1 passam a apontar para as figuras.
- **Verificação:**
  - os escores salvos correspondem às janelas refeitas (assert no script);
  - o Random Forest do cenário B refeito confere com `proprios_por_arquivo.csv`, arquivo a arquivo;
  - os totais da matriz batem com `proprios_resumo.csv`;
  - `pytest`: 13 passando;
  - todas as figuras abertas e conferidas (sem texto cortado ou sobreposto).

### 2026-11-10 — banco relacional da central: modelagem

- **Decisão do grupo:** banco relacional em Python + `sqlite3`, integrado à interface. Antes, o plano era
  PostgreSQL no futuro. Por enquanto só ideias e arquitetura, sem código.
- **Revisão do diagrama ER do usuário** (`idoso`, `quarto`, `historico_quedas`, `historico_quartos`,
  `historico_sensores`):
  - FKs explícitas no lado "muitos", com cardinalidade;
  - `historico_quedas` vira `alerta`, para os 7 tipos do `protocolo.md`, com uma tabela de transições;
  - `historico_sensores` vira `sensor` + `evento_sensor`, porque cada porta é um sensor e as leituras chegam
    em horas diferentes;
  - entram `vestivel` (liga a queda ao idoso) e, opcional, `cuidador`.
- **Verificação:** nenhuma; é proposta. O que falta decidir está em [`pendencias.md`](pendencias.md) (Central).

### 2026-11-09 — modelos de queda no PC (S2 P3 Q1)

- **Pastas criadas a pedido do usuário:** `dados/` (fora do git, com README) e `modelo/` (Python 3.12 via `uv`).
- **Verificação dos dados**, antes de escrever o código:
  - SisFall com 4505 arquivos íntegros, 38 pessoas; os ensaios D17 da SA15 estão com o nome D17_SE15;
  - os 18 ensaios próprios são de uma pessoa, na cintura, em ±2 g e saturados, com tempo de resolução de 10 ms
    e curtos.
  - O plano foi revisto com isso: dois cenários de faixa (±8 g e ±2 g) e janela de −1,0 s a +0,8 s.
- **Pipeline:** filtro → gatilho → janela → atributos → classificador.
  - A primeira versão do gatilho (histerese + período refratário, como o `fall_detect.c`) só deixava passar
    ~93 % das quedas: um disparo da caminhada escondia o impacto. Trocada por detecção de picos (≥ 0,5 s
    entre picos): 99,2 % com L_on = 2,0 g.
- **22 métodos comparados** com validação cruzada por pessoa (números em `modelo/resultados/resumo.md`):
  - limiar atual: 69,9 alarmes falsos/h (±8 g);
  - Random Forest: 0,4/h; regressão logística: 1,4/h; ambos com ~94 % das quedas;
  - correlação, DTW e AR como anomalia ficam atrás;
  - a CNN só com os módulos fica em 59 % das quedas com ≤ 1 alarme/h; com os 6 eixos, 94 %.
- **Nos ensaios próprios:** RF, regressão logística e SVM dão 0 alarmes falsos e perdem as quedas de joelhos.
- **Como foi verificado:**
  - 13 testes de unidade, com os exemplos numéricos dos slides (AR(2) → a₁ = 2 e a₂ = −1; DTW = 0) e o gatilho
    vetorizado igual ao laço do firmware em 50 sinais aleatórios;
  - todos os scripts rodaram do início ao fim;
  - `assert` de que nenhuma pessoa fica no treino e no teste.
  - É verificação no PC; **nada disso roda no vestível ainda**.
- **Rascunho do relatório e roteiro da apresentação** em `relatorios/Semestre 2/Projeto 3/Q1/`.
- **Documentação atualizada:** decisões, roadmap, coleta, pendências, CLAUDE.md e os índices de aulas e
  relatórios.

### 2026-10-23 — arquivos de fabricação da PCB v1

- Gerados pelo `kicad-cli` em `hardware/placas/v1/`: Gerbers, furação (Excellon, mm), `placa-v1-jlcpcb.zip`,
  esquema em PDF, lista de materiais e render 3D, com um README que traz o pedido na JLCPCB.
- Verificado antes de gerar: DRC com 0 erros, 0 ligações faltando e 0 divergências com o esquema; ERC 0.
  Verificado nos arquivos gerados: contorno de 100,0 × 59,8 mm; 114 furos metalizados, nas quantidades que os
  footprints pedem. O teste no papel foi feito pelo usuário.
- Falta conferir a ordem dos pinos da DevKit contra a peça e fazer o pedido (`pendencias.md`).

### 2026-10-23 — PCB roteada

- O usuário terminou o roteamento. Conferido pelo `kicad-cli` no arquivo salvo e numa cópia com o plano
  repreenchido: DRC com 0 erros e 0 ligações faltando, sem divergência com o esquema; tudo no F.Cu (uma via, a
  do TP3), plano de GND inteiro, larguras como as classes de rede. Sobram avisos de serigrafia e um toco de
  trilha. O CANH passa a 0,9 mm do pino 2 do LM358 (anotado como opcional em `pendencias.md`).
- Gerado um PDF 1:1 da face de cima para o teste no papel (no scratchpad, fora do repositório).

### 2026-10-23 — plano de GND e área da antena

- Área proibida "Antena" (F.Cu e B.Cu, sem trilhas, vias e zonas) e plano de GND no B.Cu, com alívio térmico.
  Placa reduzida para 100,0 × 58,2 mm.
- DRC: os itens sem ligação caíram de 66 para 42, porque o plano ligou os pads de GND. Sobram o TP3 (pad só
  de F.Cu, precisa de via) e um `starved_thermal` no pino 44 da DevKit (a área da antena encosta nele; recuar
  a borda para X 154,5). Os 3 avisos de serigrafia do C7 continuam.
- No roteamento, as trilhas não passavam entre os pinos da DevKit (0,84 mm livres para 0,9 mm necessários).
  Decidido: pads ovais de 1,4 × 2,2 mm, afastamento `Power` em 0,25 mm e trilha de 0,25 mm predefinida.

### 2026-10-23 — fabricação na JLCPCB

- Decidido: JLCPCB, 2 camadas com furo metalizado. Regras de projeto, classes de rede e padrões de serigrafia
  propostos em `hardware/README.md` (Fabricação) e registrados em `decisoes.md`.
- Conferi os anéis de todos os pads da placa: dois footprints ficam fora do recomendado (LM35 com 0,15 mm;
  DevKit com furo de 0,8 mm). As trocas estão em `pendencias.md`.
- O usuário aplicou as trocas e as regras. Conferido pelo arquivo e pelo `kicad-cli`: DRC sem erros, sem
  divergência com o esquema; LM35 no `TO-92_Inline_Wide`, DevKit com furo de 1,0 mm, anel mínimo de 0,30 mm;
  classes de rede e restrições como propostas. Ainda sem plano de GND nem áreas proibidas.

### 2026-10-23 — posicionamento da PCB conferido

- Li o `.kicad_pcb` (placa de 108 × 64,5 mm, 30 footprints, sem trilhas). O que falta corrigir está em
  `pendencias.md` (Hardware): TPs fora da placa, área proibida sob a antena e no LM35, RC longe do GPIO 4, par
  do CAN passando pelo bloco analógico, e as fileiras da DevKit com 22,86 mm no footprint, a medir.
- Explicado como criar o plano de GND (zona no B.Cu, rede GND, alívio térmico).
- Segunda conferência, depois das correções do usuário: DRC pelo `kicad-cli` com paridade de esquema. Restam
  courtyards malformados no J1 e no J5, os valores dos TPs fora de sincronia com o esquema e ajustes de
  posição; tudo em `pendencias.md`. A DevKit foi medida com ~25,4 mm entre as fileiras.
- Terceira conferência: DRC sem erros (courtyards corrigidos, nenhuma sobreposição). Restam avisos: J1, J2, J5
  e J6 diferentes da biblioteca, valores dos TPs fora de sincronia com o esquema. Os 25,25 mm do footprint da
  DevKit foram aceitos pelo usuário.
- Quarta conferência: o footprint da DevKit foi para o `projeto.pretty` e está associado ao U1. Os courtyards
  da biblioteca ficaram abertos (sobraram linhas do soquete): removi essas linhas dos quatro footprints, com
  cópia de segurança, e conferi que a biblioteca abre no `kicad-cli`. Falta atualizar os quatro na placa e
  acertar os valores de TP1–TP4.
- Quinta conferência: a biblioteca continua certa, mas a placa ainda tem as cópias antigas de J1, J2, J5 e J6
  (7 erros de courtyard). TP1–TP4 sincronizados; TP5/TP6 ainda diferem do esquema.
- Sexta conferência: paridade com o esquema zerada (TPs sincronizados). O editor de footprint regravou o
  WCMCU-230 e o MPU6050 com linhas abertas; corrigi de novo. A placa (aberta no KiCad, com alterações não
  salvas) ainda tem as cópias antigas de J1, J2, J5 e J6.
- Com o KiCad fechado, tirei as linhas soltas de courtyard do J1 e do J5 direto no `.kicad_pcb` (cópia de
  segurança no scratchpad; J2 e J6 já estavam atualizados). DRC: 0 erros, 0 divergências com o esquema e com a
  biblioteca; só 4 avisos de serigrafia na borda. O posicionamento está fechado; próximo passo: plano de GND,
  áreas proibidas e roteamento.

### 2026-10-23 — posição do bloco analógico no layout

- `hardware/README.md`: o LM358 fica junto ao LM35, longe da DevKit (calor), e não junto ao GPIO 4. Só o RC
  fica no GPIO 4; a trilha que vai até ele leva o sinal já amplificado. Só documentação.
- Posição do LM35: numa ponta da borda dos conectores, longe do módulo e embaixo se a placa ficar em pé; recorte
  na placa, GND por trilha (sem plano em volta) e terminais longos, porque no TO-92 o calor chega pelos
  terminais.
- Posição do PIR: encaixado na placa (J2), na metade oposta à antena (disparo falso com Wi-Fi), sem componentes
  embaixo, domo saindo por um furo na tampa. O PIR saiu da lista de conectores com RC/TVS (não chega por fio).

### 2026-10-23 — I²C: troca feita e desfeita

- SCL e SDA chegaram a ser trocados (SCL no 1, SDA no 2), mas a troca foi desfeita no mesmo dia: ela tirava a
  opção do ULP ler o MPU-6050 em *deep sleep*.
- O cruzamento das trilhas até o GY-521 vai ser resolvido girando o módulo no layout.
- Continua SDA no GPIO1 e SCL no GPIO2, no firmware e no esquema. Verificado por build das duas placas e,
  no esquema, pelo netlist exportado (`J5.4`–GPIO1 SDA, `J5.3`–GPIO2 SCL) e pelo ERC (0 violações).

### 2026-10-23 — esquemático e sem buzzer

- **Esquemático da PCB única em andamento:** DevKit, LM35 + LM358, MPU-6050, HC-SR501, 2 portas, CAN
  (WCMCU-230), botão e entrada USB (10 µF + Schottky até a DevKit).
- **Sem buzzer:**
  - a sinalização local é o LED, e o alerta chega ao cuidador pela central;
  - `PIN_BUZZER = GPIO_NUM_NC` nas duas placas, e o GPIO 21 fica livre;
  - RF12, arquitetura, protocolo e hardware atualizados;
  - verificado por build das duas placas.
- **O ramo de 5 V filtrado do LM35 foi dispensado:** ficou só o desacoplamento de 100 nF.
- **Alimentação:**
  - entrada USB-C única (só com cabo A → C, o módulo não tem os resistores de CC), com 10 µF e Schottky até a
    DevKit;
  - o vestível roda com **power bank** na mesma entrada. O circuito de bateria na placa (LiPo + TP4056 + boost)
    foi avaliado e descartado nesta versão, por tempo e tamanho;
  - firmware: `GADGET_BATTERY_SENSE` passou a vir desligado (verificado por build).

### 2026-10-21 — condicionamento do LM35

- **Documentação corrigida:** o LM35 já era alimentado com 5 V (a Tabela 4 do S1 P3 estava errada), e o
  LM358 com ±5 V, por um inversor.
- **Decisão para a PCB nova:**
  - sem o inversor: o LM358 em 5 V e GND;
  - ~~ramo de 5 V analógico filtrado~~ (dispensado em 2026-10-23: só 100 nF de desacoplamento);
  - RC de 10 kΩ + 1 µF no GPIO 4 (passa-baixa de ~16 Hz, que também protege o pino);
  - o firmware e a curva continuam; na placa, conferir 3 ou 4 pontos;
  - detalhes em [`hardware/README.md`](../hardware/README.md).

### 2026-10-21 — lista de pendências

- Criada a [`pendencias.md`](pendencias.md): a lista única do que falta, com caixas de marcar.
- As pendências e os próximos passos que estavam aqui foram para lá.
- Regra nova no `CLAUDE.md`: atualizar a lista ao fim de toda tarefa.

### 2026-10-20 (noite) — mapa de pinos da PCB única

- **Decisão:** no primeiro projeto, uma só PCB para o hub e o vestível; só o firmware muda.
- **Mapa:** 16 sinais sem conflito, escolhidos pela aula de PCB e pela documentação do ESP-IDF v6.0.1
  ([`hardware/README.md`](../hardware/README.md), com as regras e as recomendações de esquemático). Ficaram
  livres 6 e 7 (analógico) e 39–42 (expansão); 15 e 16 ficam reservados para o cristal de 32 kHz.
- **Código:**
  - novo componente `firmware/common/pcb` (`pcb_pins.h`), com o menu PCB para a versão da DevKit;
  - os `board_pins.h` do hub e do vestível passaram a escolher a partir dele.
- **Mudança de pinos:**

  | Placa | Sinal | Antes → agora |
  |---|---|---|
  | hub | PIR | 17 → 8 |
  | hub | portas | 18/16 → 11/12 |
  | hub | LM35 | 1 → 4 |
  | hub | CAN | 4/5 → 13/14 |
  | vestível | I²C | 8/9 → 1/2 |
  | vestível | INT | 10 → 17 |
  | vestível | botão | 11 → 18 |
  | vestível | buzzer | 12 → 21 |
  | vestível | bateria | 2 → 5 |
  | vestível | CAN | 4/5 → 13/14 |

- **Verificação:**
  - build sem avisos: hub, vestível padrão e vestível com MQTT + CAN + coleta;
  - a verificação do mapa compilada com o gcc do PC: o original passa, e cada erro é recusado com a mensagem
    certa:
    - pino repetido;
    - pino de *strapping*;
    - pino do cristal de 32 kHz;
    - LM35 no ADC2;
    - botão no JTAG (não acorda do *deep sleep*);
    - SDA fora do par do ULP.
- **`relatorios/`:** 7 PDFs tinham sumido. 4 voltaram de cópias nas pastas de cada semestre, conferidas pelo
  número de páginas. 3 precisam ser baixados de novo (Pendências).

### 2026-10-20 (tarde) — firmware nos moldes das aulas; MQTT e CAN nas duas placas; ESP-DSP

- **`aulas/`:** slides do S1 e do S2 organizados por semestre, `anotacoes.md` e `material/` removidos,
  `README.md` virou o índice dos slides (assunto e onde cada um se aplica). 11 slides apagados por engano
  foram recuperados das pastas de aula de cada semestre, fora do repositório; `CEM_Projeto_PCB.pdf` não
  tinha cópia.
- **Componentes comuns** (`firmware/common/`):
  - `net`, `mqtt_link` e `can` saíram do hub, com pinos e tópicos passados pela placa;
  - novos: `rtos_util` (`task_config.h` com as prioridades nomeadas do slide de RTOS) e `sys_events` (event
    group);
  - `protocol_can.h`: nibbles e payloads do grupo `0xF`;
  - menus genéricos: Network (Wi-Fi), MQTT, CAN bus (TWAI).
- **Revisão contra os slides:**
  - toda task com `xTaskCreatePinnedToCore` e prioridade nomeada (núcleo 1: sensores e amostragem; núcleo 0:
    rádio e protocolos);
  - estado de rede, MQTT, hora e CAN no event group;
  - filtro de glitch nas portas, no PIR e no botão;
  - `portMUX` nos indicadores.
- **CAN reescrito:**
  - fila de TX e task dona do periférico (antes só uma task podia transmitir);
  - recuperação de *bus-off*, retransmissão até o ACK, filtro de hardware para IDs `0x0GS`.
- **Vestível:**
  - MQTT opcional: telemetria própria e reserva da queda;
  - CAN opcional de bancada (`0x0FD`, `0x0FE`);
  - CAN TX/RX nos GPIO 4/5;
  - detector provisório com histerese L<sub>on</sub>/L<sub>off</sub>;
  - componente `signal_proc`: buffer circular e atributos com `espressif/esp-dsp` 1.8.2, custo medido em ciclos.
- **Central:**
  - usuário `vestivel` na ACL;
  - consumidores do Telegraf para `vestivel/<id>/telemetria` e `vestivel/<id>/mqtt`;
  - `hora` passa a ser gravado (o parser descartava booleanos);
  - o notificador marca a queda enviada pela pulseira.
- **Defeitos achados na revisão:**
  - o CAN do hub não definia `quanta_clk_out` nem `bus_off_indicator`; com o valor 0, o driver mandava o clock
    do TWAI e o sinal de *bus-off* para o GPIO0, que é pino de *strapping* e botão BOOT. Corrigido com
    `GPIO_NUM_NC`, como no slide;
  - os indicadores liam um prazo de 64 bits sem trava: num processador de 32 bits ele pode ser lido pela metade.
    Corrigido com `portMUX`;
  - o vestível segurava o mutex do detector durante a impressão do modo de coleta (dezenas de ms). Agora o
    mutex só cobre a soma da atividade de cada lote.
- **Verificação:**
  - build sem avisos: hub (36% livre), vestível padrão (45% livre) e vestível com MQTT + CAN + coleta + botão de
    teste (35% livre);
  - testes no PC: regras 53, config 60, detector 14 e `signal_proc` 25 verificações; notificador 11 testes;
  - central:
    - `vestivel` publicou em `telemetria`, `mqtt` e `alerta`;
    - as publicações dele em `porta/...`, `vestivel/<id>` e `status` foram descartadas pelo broker;
    - o InfluxDB gravou `vestivel_telemetria` e `vestivel_mqtt` com a tag `id`;
  - todas as 10 criações de task conferidas: núcleo explícito e prioridade ≤ 20.
- **Encontrado no caminho:** um script de edição gravou arquivos com fim de linha CRLF, e o `entrypoint.sh`
  do Mosquitto parou de rodar (`set: illegal option -`). Voltaram para LF; a dica ficou em
  `interface/README.md`, Problemas comuns.

### 2026-10-20 — correções e melhorias da revisão

- **Firmware comum:**
  - `firmware/common/protocol`: contrato ESP-NOW com tamanhos verificados em compilação e nomes MQTT;
  - `firmware/common/indicators`: LED WS2812 + buzzer, compartilhados pelas duas placas.
- **Hub:**
  - debounce reescrito: o nível só é lido depois de 50 ms sem bordas, e só uma mudança real é reportada;
  - portas modulares por `BOARD_DOORS`;
  - LM35 implementado;
  - CAN com autoteste no `menuconfig` e IDs `0x0F2` (temperatura) e `0x0F3+` (portas);
  - componentes novos: `rules`, `hub_config`, `net`, `mqtt_link`, `espnow_link`;
  - watchdog de 30 s com pânico e partição de app grande (`sdkconfig.defaults`).
- **Vestível** (era só esqueleto): `mpu6050`, `fall_detect` (provisório), `espnow_tx`, `battery`, botão, buzzer
  e modo de coleta.
- **Central:**
  - `interface/` virou a central em Docker Compose;
  - a demonstração de 18/09 foi para `interface/legado/`;
  - simulador reescrito no contrato atual.
- **Documentação:** `estado.md` (este arquivo) e `coleta-de-dados.md` criados. Atualizados `protocolo.md`,
  `arquitetura.md`, `decisoes.md`, `roadmap.md`, `CLAUDE.md` e os READMEs.
- **Encontrado no teste:** o serviço Mosquitto do Windows (início automático) escuta em `127.0.0.1:1883` e
  captura conexões a `localhost`. Para usar a central em Docker, use o IP da máquina ou pare esse serviço
  (`interface/README.md`, Problemas comuns).
