# hardware

| Pasta | Conteúdo |
|---|---|
| `fontes/` | o que se edita: o projeto do KiCad (esquema **e layout**, em `fontes/esquematico/`), a biblioteca do projeto (`projeto.pretty`), modelos 3D e simulações (ex.: o LM358 de G = 5,5 do LM35DZ) |
| `placas/` | o que o KiCad gera para fabricar, uma pasta por versão (ex.: `placas/v1/`): Gerber, furação, PDFs do esquema e da fabricação, lista de materiais, imagens 3D e o `.zip` enviado ao fabricante |

## PCB única (primeiro projeto)

O primeiro projeto é **uma só PCB para as duas placas**: uma placa-base em que a **ESP32-S3-DevKitC-1** é
encaixada e que leva os periféricos do hub e do vestível. Só o firmware muda: o hub usa as portas, o PIR e o
LM35; o vestível usa o MPU-6050 e o botão; os dois usam o CAN e o LED da DevKit. Por isso nenhuma função
divide pino com outra.

O mapa vale para o firmware em `firmware/common/pcb/include/pcb_pins.h`. Ele é verificado na compilação: o
build falha se dois sinais caírem no mesmo GPIO, se algum cair num pino proibido ou reservado, ou se um sinal
estiver num pino que não serve para ele (analógico fora do ADC1, por exemplo). O `board_pins.h` de cada placa
só escolhe e dá nome ao que ela usa. Se um dia as placas se separarem, o mapa de cada uma vai para o
`board_pins.h` dela.

**Situação:** proposta para o esquemático, a confirmar no layout (DRC) da PCB.

### Regras de escolha

Fontes: documentação do ESP-IDF v6.0.1 para o ESP32-S3 (GPIO, JTAG, ULP RISC-V) e a aula de PCB
(`aulas/Semestre 2/CEM_Projeto_PCB.pdf`). A regra é não usar pino que uma função do chip ligada no futuro
tomaria, mesmo que hoje não haja problema.

| Nível | GPIO | Por quê |
|---|---|---|
| nunca | 0, 3, 45, 46 | pinos de *strapping*: o nível no boot muda o modo da placa |
| nunca | 19, 20 | USB-Serial-JTAG: usá-los como GPIO desliga a USB nativa (console, gravação, depuração) |
| nunca | 43, 44 | UART0: o console e a gravação pela ponte USB da DevKit |
| nunca | 26–32, 33–37 | flash SPI e, nos módulos N8R8/N16R8, a PSRAM octal |
| nunca | 38, 48, 47 | 38 e 48: LED RGB da DevKitC-1 (v1.1 e v1.0). 47 e 48 são `SPICLK_P/N`, alimentados pelo `VDD_SPI`, que é de 1,8 V nos módulos com memória de 1,8 V |
| reserva | 15, 16 | `XTAL_32K_P/N`: um cristal de 32 kHz, para relógio preciso em *light sleep* no vestível |
| expansão | 39–42 | `MTCK/MTDO/MTDI/MTMS`: JTAG por pinos. Hoje a depuração é pela USB; o que ficar aqui se perde se o JTAG por pinos for ligado |
| só analógico | ADC1: 1–10 | o ADC2 (11–20) não funciona com o Wi-Fi ligado. Todo analógico vai no ADC1, e os pinos do ADC1 ficam para analógico sempre que possível |
| I²C do ULP | 1 (SDA), 2 (SCL) | o I²C do coprocessador ULP RISC-V só aceita SDA em 1 ou 3 e SCL em 0 ou 2; tirando os de *strapping*, sobra 1/2. Com o I²C principal aí, o ULP poderá ler o MPU-6050 em *deep sleep* sem refazer a placa. Se as trilhas até o GY-521 se cruzarem, gire o módulo na placa em vez de trocar os pinos |
| acordar | RTC GPIO: 0–21 | o botão e o INT do MPU-6050 precisam ser RTC GPIO para acordar o vestível de *deep sleep* (ext1) |

### Mapa

| GPIO | Função | Placa | Observação |
|---|---|---|---|
| 1 | I²C SDA (MPU-6050) | vestível | par do ULP; ADC1_CH0 usado como digital |
| 2 | I²C SCL | vestível | par do ULP; ADC1_CH1 |
| 4 | LM35 (saída do LM358) | hub | ADC1_CH3 |
| 5 | reservado: medição da bateria de um vestível próprio no futuro (nesta PCB não há divisor) | — | ADC1_CH4 |
| 6, 7 | reserva analógica | — | ADC1_CH5 e CH6, para sensores futuros |
| 8 | PIR HC-SR501 | hub | ADC1_CH7, usado como digital |
| 9, 10 | portas 3 e 4 (opcionais) | hub | ADC1_CH8 e CH9: se faltar entrada analógica, essas portas cedem o lugar |
| 11, 12 | portas 1 (quarto) e 2 (banheiro) | hub | ADC2, mas digital: sem problema com o Wi-Fi |
| 13, 14 | CAN TX / RX | os dois | no vestível, só em bancada (`GADGET_CAN`) |
| 17 | INT do MPU-6050 | vestível | RTC GPIO |
| 18 | botão "estou bem" | vestível | RTC GPIO; pull-up interno e filtro de glitch |
| 21 | livre: o buzzer saiu em 2026-10-23 | — | |
| 39–42 | expansão (ex.: portas 5 a 8) | — | só enquanto o JTAG por pinos não for usado |
| 15, 16 | não ligar: cristal de 32 kHz | — | |
| 48 (v1.0) / 38 (v1.1) | LED WS2812 | os dois | é o da DevKit, não uma trilha da PCB; a versão se escolhe em `menuconfig` → PCB |

Ficam livres o 6 e o 7 (analógico), o 21, os 39–42 (expansão) e, se não forem montadas, as portas 3 e 4.

Na DevKitC-1 (headers J1 e J3), isso forma grupos, como pede a aula de PCB (analógico separado do que chaveia,
conectores numa borda). A ordem dos pinos é a do guia do usuário da ESP32-S3-DevKitC-1; confira na versão da
placa antes de desenhar a pegada:

```
J1 (de cima para baixo)                  J3 (de cima para baixo)
3V3, 3V3, RST                            GND, 43 (TX0), 44 (RX0)
4  LM35        ┐ bloco analógico         1  I²C SDA  ┐ MPU-6050
5  reservado   │ junto ao 3V3            2  I²C SCL  ┘
6  reserva     │                         42 ┐
7  reserva     ┘                         41 │ expansão
15, 16  (cristal 32 kHz: não ligar)      40 │
17 INT do MPU                            39 ┘
18 botão                                 38 (LED v1.1), 37–35 (PSRAM)
8  PIR         ┐                         0 (BOOT), 45, 48 (LED v1.0), 47
3, 46 (strapping: não ligar)│ entradas   21 livre
9  porta 3     │ do quarto,              20, 19 (USB)
10 porta 4     │ para os conectores      GND, GND
11 porta 1     │
12 porta 2     ┘
13 CAN TX      ┐ transceptor junto
14 CAN RX      ┘ à borda
5V, GND
```

Para acrescentar uma porta, basta uma linha em `BOARD_DOORS` no `board_pins.h` do hub. O firmware cria a
entrada, o tópico `porta/<id>` e o ID CAN. São **até 8 portas**, uma por nibble CAN (`0x0F3`–`0x0FA`): as 4 da
placa e as 4 da expansão. O build falha se passar disso.

O CAN do vestível existe só para o requisito da disciplina e para os testes em bancada: a pulseira de verdade
não tem transceptor e fala com o hub por ESP-NOW.

### Condicionamento do LM35 (decidido em 2026-10-21)

O circuito validado no S1 continua: LM35DZ em 5 V e LM358 não inversor com G = 1 + 9 kΩ / 2 kΩ = 5,5. Na PCB
nova, mudam três coisas:

1. **Sem o inversor de −5 V.** O LM358 passa a ter alimentação simples: 5 V no V+ (pino 8) e GND no V−
   (pino 4). Isso está dentro do datasheet (*single supply*, 3 V a 32 V), e nessa condição:
   - a entrada aceita de 0 V até ~3,5 V; o LM35 dá no máximo 0,6 V;
   - a saída vai de ~0,02 V até ~3,5 V; o circuito usa de 0,66 V a 1,68 V entre 10 °C e 30 °C, e no máximo
     3,07 V a 57 °C.

   Com isso saem da placa:
   - a fonte de ruído chaveada no bloco analógico;
   - as peças do inversor;
   - o risco de tensão negativa no pino da ESP32.
2. **Desacoplamento:** 100 nF cerâmico (X7R) colado ao pino de alimentação do LM35 e do LM358. Os dois saem
   direto do 5 V da placa. Um ramo de 5 V separado para o analógico (ferrite ou 10 Ω, mais 10 µF) foi
   avaliado e **dispensado** (decisão de 2026-10-23):
   - o LM35 quase ignora o ruído da alimentação (regulação de linha de ~0,01 mV/V);
   - o LM358 rejeita bem o ruído lento;
   - o ruído rápido, o filtro RC abaixo corta.
3. **Filtro RC na entrada do ADC:** 10 kΩ em série da saída do LM358 (pino 1) até o GPIO 4, e 1 µF cerâmico
   (X7R, PTH) do GPIO 4 ao GND, colado no pino. Eletrolítico aqui não: a fuga dele, de µA, passando pelos
   10 kΩ, daria dezenas de mV de erro.
   - É um passa-baixa de 1ª ordem: fc = 1 / (2π · 10 kΩ · 1 µF) ≈ 16 Hz.
   - Atenua −12 dB em 60 Hz e −36 dB em 1 kHz.
   - Fica abaixo do Nyquist de 50 Hz das rajadas do firmware (16 amostras a cada 10 ms) e muito acima da banda
     do sinal (a temperatura muda em minutos). Acomoda em ~50 ms (5τ).
   - O capacitor fornece a carga do capacitor de amostragem do ADC. A documentação do ESP-IDF v6.0.1 recomenda
     um bypass de 100 nF na entrada do ADC; com 1 µF o fc desce para baixo dos 60 Hz.
   - O resistor também protege o pino: limita a corrente quando a saída sai da faixa (*back-powering* ao ligar
     a placa, LM35 com mau contato, ponta de prova no 5 V).
   - A queda nele é desprezível: só passa a fuga do pino, da ordem de nA, ou seja, menos de 1 mV.
   - Sem tensão negativa na placa, o diodo de *clamp* (BAT54S) é dispensável.

A outra metade do LM358 (pinos 5, 6 e 7) fica como seguidor em 0 V: o pino 5 no GND e o 6 ligado ao 7. Uma
entrada solta pode fazer essa metade oscilar.

| Pino do LM358 | Liga em |
|---|---|
| 8 (V+) | 5 V da placa, com 100 nF ao GND junto do pino |
| 4 (V−) | GND |
| 3 (IN+ A) | saída do LM35 |
| 2 (IN− A) | R1 = 2 kΩ ao GND e R2 = 9 kΩ ao pino 1 (9,1 kΩ na série E24: G = 5,55, compatível com o ganho medido de 5,56) |
| 1 (OUT A) | 10 kΩ até o GPIO 4; 1 µF do GPIO 4 ao GND |
| 5 / 6, 7 | GND / ligados entre si |

**Firmware e calibração:** o firmware não muda. O LM35, o ganho e os resistores são os mesmos, então a curva
`T = (V_mV − 144,739) / 51,3335` continua valendo. Na placa nova, basta conferir 3 ou 4 pontos (perto de 10,
20 e 30 °C), medindo a tensão já no GPIO 4, depois do filtro.

**Posição:** longe da DevKit e do regulador, que esquentam com o Wi-Fi, numa ponta da borda dos conectores (é a
borda que fica na abertura da caixa, onde chega o ar do quarto):
- a ponta mais longe do módulo e, se a placa ficar em pé, a de baixo, porque o ar quente sobe da ESP;
- não colado na entrada USB-C (diodo) nem no transceptor CAN.

No TO-92, os terminais de cobre são o principal caminho do calor até o chip (datasheet do LM35, montagem). Por
isso:
- recorte na placa entre o LM35 e o resto, passando só as trilhas;
- sem plano de cobre em volta dele: o GND chega por trilha (área proibida para a zona de cobre no KiCad);
- o LM35 montado alguns milímetros acima da placa, sem cortar os terminais rente.

Se ele for para um cabo, entram 1 a 2 kΩ em série na saída do LM35 (ele oscila com carga capacitiva) e
TVS/ESD no conector.

### Alimentação (decidida em 2026-10-23)

A placa tem **uma entrada só**, um módulo USB-C, e não carrega bateria:
- o **hub** é alimentado por um carregador de tomada;
- o **vestível** (protótipo), por um **power bank** na mesma entrada.

Como só existe uma entrada, nunca há duas fontes juntas.

```
 USB-C  V ──●── +5V ──┬── C 10 µF ── GND
                      ├──▶ D1 1N5819 ──▶ pino 5V da DevKit
                      └──▶ LM35, LM358, PIR
        G ──── GND
```

| Peça | Detalhe |
|---|---|
| Módulo USB-C (V, D−, D+, G) | não tem os resistores de 5,1 kΩ no CC: **só liga com cabo USB-A → C**. D− e D+ sem conexão |
| C 10 µF eletrolítico | na saída V do módulo, junto ao conector (+ no `+5V`) |
| D1 1N5819 | do `+5V` ao pino 5V da DevKit (faixa do lado da DevKit), para não juntar duas fontes quando a USB da DevKit for ligada para gravar |

- **Fonte do hub:** carregador USB-A de 1 A ou mais, com cabo A → C.
- **Fonte do vestível:** power bank **com saída USB-A**, ligado pelo mesmo cabo A → C. Um de 5000 mAh deve durar
  ~20–25 h com o consumo atual (estimativa).
  - Muitos power banks desligam sozinhos abaixo de ~50–100 mA. Hoje a placa puxa ~110–130 mA, mas com o *light
    sleep* o consumo cai, e aí é preciso um modelo com modo "always on" ou de baixa corrente. Alguns desses modos
    duram só ~2 h: confirmar com um teste.
- **Sem medição de bateria:** o vestível manda a bateria como desconhecida (`0xFFFF`), e o alerta `bateria_baixa`
  não dispara. Quem mostra a carga são os LEDs do power bank.
- **Descartado nesta versão:** bateria LiPo com TP4056, boost MT3608 e troca automática por diodo. Funcionaria,
  mas toma tempo e aumenta a placa, num protótipo que já junta as duas placas numa só. Fica para a placa própria
  do vestível: LiPo + regulador LDO de 3,3 V, mais leve e eficiente ([`docs/decisoes.md`](../docs/decisoes.md)).

### Recomendações para o esquemático e o layout

Da aula de PCB, aplicadas a este mapa:

- **Agrupar por função:** o bloco analógico fica do lado do GPIO 4 (J1 da DevKit), longe do transceptor CAN e do
  I²C. O retorno do terra analógico não divide caminho com o digital. Dentro do bloco:
  - LM35 e LM358 juntos, longe da DevKit (calor, ver a posição do LM35), com a trilha LM35 → LM358 curta: é o
    sinal pequeno (10 mV/°C), em que 1 mV de ruído já é 0,1 °C;
  - o sinal já amplificado segue até o GPIO 4, e o RC (10 kΩ + 1 µF) fica junto ao GPIO 4;
  - essa trilha não corre em paralelo ao I²C, ao CAN nem às trilhas das portas e do PIR; se precisar cruzar
    alguma, cruza a 90°, com o plano de terra inteiro embaixo.
- **Conectores numa borda só** (portas, CAN, alimentação), com o filtro de cada sinal junto ao conector:
  - nas portas, que chegam por fios longos (antenas), RC e TVS/ESD;
  - no CAN, transceptor de 3,3 V e terminação de 120 Ω quando a placa estiver numa ponta do barramento.
- **PIR (HC-SR501, encaixado na placa pelo J2):** o módulo fica deitado sobre a placa, com o domo para fora, e
  ocupa ~24 × 32 mm (o courtyard do footprint).
  - **Longe da antena da ESP**, na metade oposta da placa: o HC-SR501 tem muitos relatos de disparo falso
    perto do Wi-Fi. Com a placa em pé, isso também o tira de cima do ar quente da ESP.
  - Sem componentes embaixo dele: os dois trimpots e o jumper ficam virados para a placa. Para ajustar, tira-se
    o módulo do soquete. Trilhas por baixo podem passar.
  - O domo precisa sair por um furo na tampa da caixa: acrílico e vidro barram o infravermelho que ele vê.
  - O 100 nF da alimentação junto ao pino de 5 V do J2.
  - Opcional: um espaçador M2 de náilon do lado oposto ao soquete, se o módulo tiver os furos de fixação, para
    ele não ficar balançando só nos 3 pinos.
- **Analógico:**
  - o LM35: ver [Condicionamento do LM35](#condicionamento-do-lm35-decidido-em-2026-10-21);
- **Estado no boot:** enquanto a ESP reinicia, os pinos ficam em alta impedância.
  - Pull-up no CAN TX, para o barramento ficar recessivo.
- **Desacoplamento:** 100 nF em cada pino de alimentação dos CIs, com vias curtas ao plano de terra, e 1 a 10 µF
  na entrada de alimentação da placa.
- **Plano de terra** contínuo, com o mínimo de fendas sob as trilhas de sinal.

### Fabricação (decidida em 2026-10-23)

A placa vai para a **JLCPCB**: 2 camadas, FR-4, 1,6 mm, cobre de 1 oz, furo metalizado, HASL. Com furo
metalizado, as trilhas ficam no F.Cu e o plano de GND no B.Cu. Os limites da JLCPCB para 2 camadas são 0,127 mm
de trilha e de afastamento e via de 0,3 mm de furo por 0,5 mm de diâmetro. Os valores abaixo ficam com folga
sobre esses limites, para uma placa soldada à mão. No KiCad, ficam em *Arquivo → Configurações da placa*.

| Onde | Item | Valor |
|---|---|---|
| Regras de projeto → Restrições | afastamento mínimo / trilha mínima | 0,2 mm / 0,2 mm |
| | anel anular mínimo | 0,15 mm |
| | via: diâmetro / furo mínimos | 0,6 mm / 0,3 mm |
| | cobre até a borda / furo a furo | 0,5 mm / 0,5 mm |
| | texto da serigrafia: altura / espessura mínimas | 1,0 mm / 0,15 mm |
| Classes de rede: `Default` (sinais) | trilha / afastamento / via | 0,4 mm / 0,25 mm / 0,8 mm com furo de 0,4 mm |
| Classes de rede: `Power` (`+5V`, `+3.3V`, `GND`, `Net-(D1-K)`) | trilha / afastamento | 0,8 mm / 0,25 mm (o pico é ~0,5 A; 0,8 mm em 1 oz aguenta bem mais; em 5 V, 0,3 mm de afastamento não ganha nada e trava trilhas entre pinos) |
| Tamanhos predefinidos | trilhas / via | 0,25 (só em trecho apertado), 0,4, 0,8 e 1,0 mm / 0,8 × 0,4 mm |
| Texto e gráficos → Padrões | serigrafia: linha / texto | 0,15 mm / 1,0 × 1,0 mm, espessura 0,15 mm |
| Empilhamento | espessura / cobre | 1,6 mm / 35 µm (1 oz) |

**Pads:** os dos footprints da biblioteca servem (anel de 0,3 a 0,55 mm), com duas exceções:
- **LM35:** o `TO-92_Inline` tem anel de só 0,15 mm, difícil de soldar à mão. O `TO-92_Inline_Wide` (passo de
  2,54 mm, pad de 1,5 mm, furo de 0,8 mm) dá 0,35 mm; basta abrir um pouco os terminais.
- **DevKit:** o footprint da Espressif tem furo de 0,8 mm e pad de 1,2 × 2,0 mm. O terminal quadrado de 0,64 mm
  de uma barra de pinos tem 0,9 mm de diagonal e não entra. Usar furo de 1,0 mm com pad **oval de 1,4 × 2,2 mm**,
  estreito no sentido da fileira: o anel fica com 0,2 mm nos lados e 0,6 mm nas pontas, e sobra 1,14 mm entre
  dois pinos, onde passa uma trilha de 0,4 mm. Com pad redondo de 1,7 mm, sobrariam 0,84 mm, e nem a trilha de
  0,4 mm passaria; daria para trocar trilhas para o outro lado da DevKit só com 0,25 mm.

**Pedido:** a JLCPCB grava um número de pedido na serigrafia. Para escolher onde, pôr o texto `JLCJLCJLCJLC` no
F.Silkscreen numa área livre e marcar "Specify a location" no pedido. O preço promocional vale para placas de
até 100 × 100 mm.

## Mapas anteriores (só histórico)

Os relatórios tiveram três mapas diferentes. Nenhum serve mais:

| Função | S1 P2 (DevKit) | PCB do S1 P3 | Proposta de 2026-10-20 (antes da PCB única) |
|---|---|---|---|
| CAN TX / RX | — | 43 / 44 | 4 / 5 |
| PIR HC-SR501 | 4 | 12 | 17 |
| Portas (reed MC-38) | 5 | 48 | 18, 16 |
| LM35 (saída condicionada) | 1 (ADC1_CH0) | 18 (ADC2_CH7) | 1 |
| MPU-6050 SDA / SCL, INT | 8 / 9 | 35 / 36 | 8 / 9, INT 10 |
| Botão, buzzer (vestível) | — | — | 11, 12 |
| Bateria | — | — | 2 |

Conflitos que a PCB do S1 tinha, todos cobertos pelas regras acima: o LM35 no ADC2 (GPIO18), o CAN no UART0
(43/44), o reed no LED da DevKit (48) e o MPU na PSRAM octal (35/36).

## Notas de componentes

- **HC-SR501:** alimentação de 5 V (pino 5V da DevKit), saída de 3,3 V compatível com o GPIO; precisa de
  ~60 s de aquecimento; modo H (*retriggerable*).
- **MC-38:** contato seco ao GND com pull-up interno, sem resistor externo; precisa de debounce. O firmware
  liga também o filtro de glitch do GPIO (pulsos de poucos ns), útil com fios longos até a porta.
- **LM35DZ:**
  - alimentado com 5 V; a Tabela 4 do relatório do S1 P3, que diz 3,3 V, está errada;
  - condicionamento: LM358 não inversor com G = 5,5 (R1 = 2 kΩ, R2 = 9 kΩ na PCB do S1), com desacoplamento
    de 100 nF;
  - até o S1, o amp-op era alimentado com ±5 V, com o −5 V vindo de um circuito inversor; na PCB nova, só
    5 V e GND, com filtro RC na saída (ver [Condicionamento do LM35](#condicionamento-do-lm35-decidido-em-2026-10-21));
  - a 12 dB, o ADC lê até ~3,1 V, o que limita a faixa útil a cerca de 57 °C.
- **MPU-6050 (GY-521):** o módulo já traz os pull-ups do I²C (para a saída do regulador dele) e um regulador de
  3,3 V; conferir no módulo usado se ele aceita o 3V3 da DevKit na entrada.
- **CAN:** transceptor de 3,3 V e terminação de 120 Ω nas pontas do barramento.
- **Vestível:** no protótipo, power bank na entrada USB-C; bateria própria só numa placa futura do vestível.
