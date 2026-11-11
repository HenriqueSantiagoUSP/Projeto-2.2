# Relatórios e enunciados

Enunciados e entregas de cada projeto da disciplina, organizados por semestre. Para cada projeto: o que o
enunciado pediu, o que foi entregue e o que dessa entrega ainda vale. O que mudou entre as entregas está em
[`docs/decisoes.md`](../docs/decisoes.md).

Para acrescentar uma entrega, coloque os PDFs na pasta do projeto e atualize a seção correspondente.

| Semestre | Disciplina | Docentes |
|---|---|---|
| 1 | 0323220 — Projeto Integrativo 2 | Marco Isaías Alayo Chávez, Wesley Beccaro |
| 2 | 0323221 — Projeto Integrativo 3 | — |

## Semestre 1

### Projeto 1 — tema, requisitos e ambiente (entrega final 25/03/2026)

[Enunciado](<Semestre 1/Projeto 1/EnunciadoProjeto 1.pdf>)

| Quesito | Pedido | Prazo |
|---|---|---|
| Q1 | tema: problema, tipo de rede e topologia, sensores e atuadores, cenário | 11/03 |
| Q2 | instituição externa contatada e sua demanda | 11/03 |
| Q3 | requisitos do projeto | 18/03 |
| Q4 | ambientes computacionais: hardware, IDE, RTOS, simulação, backend | 18/03 |
| Q5 | entregáveis e demonstração do fim do semestre | 25/03 |
| Q6 | como os desempenhos do protótipo serão avaliados | 25/03 |

**Entregas:** [`Entrega Quesitos 1 e 2.pdf`](<Semestre 1/Projeto 1/Entrega Quesitos 1 e 2.pdf>) (11/03),
[`Entrega Quesitos 3 e 4.pdf`](<Semestre 1/Projeto 1/Entrega Quesitos 3 e 4.pdf>) (18/03),
[`Entrega Quesitos 5 e 6.pdf`](<Semestre 1/Projeto 1/Entrega Quesitos 5 e 6.pdf>) (25/03).
[`Projeto1.pdf`](<Semestre 1/Projeto 1/Projeto1.pdf>) tem o mesmo texto da entrega dos quesitos 5 e 6; só o
título muda.

- **Ainda vale:**
  - o cenário (ILPI Butantã, residentes grau I e II, turno noturno);
  - a base dos requisitos, revista em [`docs/requisitos.md`](../docs/requisitos.md);
  - as regras de porta (15/40 min, 22h–06h) e os cenários de demonstração;
  - Telegram para o cuidador;
  - InfluxDB + Grafana.
- **Superado:** Modbus RTU/RS-485, ESP32 DevKit V1, firmware em C++, um nó por sensor, pulseira em Wi-Fi
  alimentada por USB, tópicos `asilo/quarto01/…`, queda = 2,5 g por 100 ms, Node-RED.

### Projeto 2 — sensores, caracterização, condicionamento e aquisição (entrega 20/05; relatório 25/05/2026)

[Enunciado](<Semestre 1/Projeto 2/EnunciadoProjeto 2.pdf>) · Entrega: [`Projeto 2.pdf`](<Semestre 1/Projeto 2/Projeto 2.pdf>)

| Quesito | Pedido | Prazo |
|---|---|---|
| Q1 | sensores e atuadores: faixa, precisão, interface, consumo, compatibilidade | 08/04 |
| Q2 | caracterização elétrica e funcional dos sensores (datasheet + experimento) | 15/04 e 22/04 |
| Q3 | circuito de condicionamento: projeto, simulação SPICE e protótipo | 29/04 e 06/05 |
| Q4 | aquisição pelo sistema embarcado: firmware, taxa de amostragem, armazenamento/transmissão | 13/05 e 20/05 |

- **Ainda vale:**
  - calibração do LM35DZ: sensor α = 9,2246 mV/°C, β = 27,1227 mV; cadeia condicionada
    `T = (V_mV − 144,739) / 51,3335`;
  - LM358 não inversor com G = 5,5 (ganho medido: 5,56);
  - MPU-6050 em ±8 g e 200 Hz;
  - os 18 ensaios em 5 cenários (métricas da Tabela 14);
  - o LED de status como atuador.
- **Superado:** 5 nós em RS-485; pinos da DevKit (PIR 4, reed 5, I²C 8/9, LM35 no GPIO1); temporização do
  MPU com `vTaskDelay`.

### Projeto 3 — firmware em RTOS, CAN, interface, PCB e relatório final (entrega 01/07/2026)

[Enunciado](<Semestre 1/Projeto 3/EnunciadoProjeto 3.pdf>)

| Quesito | Pedido | Prazo |
|---|---|---|
| Q1 | aquisição com RTOS, curva de transferência, testes pela serial | 12/06 |
| Q2 | rede cabeada CAN: programação e testes no barramento | 19/06 |
| Q3 | interface de apresentação dos dados | — |
| Q4 | integração do sistema em placa | 26/06 |
| Q5 | relatório final e apresentação | 01/07 |

**Entregas:** [`Entrega Q1.pdf`](<Semestre 1/Projeto 3/Entrega Q1.pdf>) (12/06) e
[`Projeto3.pdf`](<Semestre 1/Projeto 3/Projeto3.pdf>), o relatório final do semestre.

- **Ainda vale:** a arquitetura de tasks (ISR mínima → task), a curva do LM35 no firmware, a migração para
  CAN e a PCB *single-layer* feita no Multisim/Ultiboard.
- **Superado:** o mapa de pinos da PCB, que tinha conflitos ([`hardware/README.md`](../hardware/README.md)), e
  o limiar instantâneo como critério final de queda (hoje é provisório).

## Semestre 2

### Projeto 1 — interface aberta, rede CAN e rede sem fio (entrega 28/08/2026)

[Enunciado](<Semestre 2/Projeto 1/EnunciadoProjeto 1.pdf>)

| Quesito | Pedido | Prazo |
|---|---|---|
| Q1 | interfaces abertas para disponibilizar, receber e exibir dados em tempo real; ao menos um protocolo de transporte | 14/08 |
| Q2 | rede CAN com comunicação entre todos os módulos da sala | 21/08 (apresentar em 28/08) |
| Q3 | topologia da rede sem fio, papel de cada nó, modos do ESP32-S3 (Station, SoftAP, AP+STA, ESP-NOW) | 28/08 |

**Entregas:** [`Entrega Q1.pdf`](<Semestre 2/Projeto 1/Entrega Q1.pdf>) (MQTT + Mosquitto) e
[`Entrega Q2 Q3.pdf`](<Semestre 2/Projeto 1/Entrega Q2 Q3.pdf>). Este último era o antigo `Q1_Q3.pdf` e
contém Q1, Q2 e Q3.

- **Ainda vale:**
  - MQTT + Mosquitto como interface aberta;
  - CAN validado com o professor em 28/08;
  - hub + vestível em duas estrelas (ESP-NOW e Wi-Fi);
  - uso dos dois núcleos.
- **Superado:**
  - hub em AP+STA (agora STA com `WIFI_PS_NONE`);
  - struct ESP-NOW com strings, canal fixo e sem criptografia;
  - regras no Node-RED (agora no hub);
  - a cascata com rede neural (vira o modelo preditivo futuro).

### Projeto 2 — implementação da interface e PCB

**Parcial (18/09/2026)** — [Enunciado](<Semestre 2/Projeto 2/EnunciadoProjeto 2_parcial.pdf>) ·
Entrega: [`Entrega 1.pdf`](<Semestre 2/Projeto 2/Entrega 1.pdf>)

- Q1: implementar a interface de dados proposta no Projeto 1, apresentada em sala. O enunciado permite
  ajustar a proposta nesta etapa.
- O enunciado lista as arquiteturas dos outros grupos: Mosquitto com WebSocket, Telegram, InfluxDB + Grafana,
  ThingsBoard, Node-RED.
- **Ainda vale:** Mosquitto com listeners 1883/9001; a separação origem / transporte / visualização.
- **Superado:** painel e simulador viram ferramentas provisórias. O formato das mensagens (`ts` em uptime,
  presença como evento, broker anônimo) foi substituído pelo de [`docs/protocolo.md`](../docs/protocolo.md).

**PCB (16/10/2026)** — [Enunciado](<Semestre 2/Projeto 2/EnunciadoProjeto 2_PCB.pdf>)

- Q1: PCB com esquema elétrico, layout, visualização 3D, Gerber e furação, ERC/DRC e apresentação. Parte do
  pressuposto de que sensores, condicionamento, CAN e interface sem fio já existem.
- **Entrega: pendente.** Os arquivos de projeto da placa vão para [`hardware/`](../hardware/README.md), e ela
  define o mapa de pinos definitivo.

### Projeto 3 — modelos, relatório técnico e comunicação sem fio (entregas em novembro de 2026)

[Enunciado](<Semestre 2/Projeto 3/EnunciadoProjeto 3.pdf>)

| Quesito | Pedido | Prazo |
|---|---|---|
| Q1 | desenvolvimento do modelo autorregressivo e/ou dos modelos de classificação; apresentação em sala e relatório | 10/11 |
| Q2 | relatório com sensores, condicionamento de sinais, interface CAN, interface sem fio e a placa desenvolvida (primeira interação de entrega) | 17/11 |
| Q3 | apresentação em sala da comunicação sem fio funcionando com os sensores | 24/11 |

**Q1:**
- Os modelos foram desenvolvidos e comparados no PC, em [`modelo/`](../modelo/README.md), com a SisFall e 18
  ensaios próprios.
- **Relatório em LaTeX:** [`Q1/latex/main.tex`](<Semestre 2/Projeto 3/Q1/latex/main.tex>), com o PDF em
  [`Q1/latex/main.pdf`](<Semestre 2/Projeto 3/Q1/latex/main.pdf>). Segue o modelo de layout dos relatórios do
  grupo: capa, contracapa e pastas `figuras/`, `imagens/` e `tabelas/`.
  - Tabelas e figuras são geradas por `modelo/experimentos/08_relatorio_latex.py`.
  - Compila com `pdflatex main.tex`, duas vezes.
- Rascunhos em Markdown: [`Q1/relatorio.md`](<Semestre 2/Projeto 3/Q1/relatorio.md>) e
  [`Q1/roteiro-apresentacao.md`](<Semestre 2/Projeto 3/Q1/roteiro-apresentacao.md>).
- Os números saem de `modelo/resultados/resumo.md`.
