# Projeto 3, Q1: roteiro da apresentação (15–20 min)

Slides: [`apresentacao/main.pdf`](apresentacao/main.pdf) (Beamer, 26 slides). Figuras, tabelas e números saem de
`modelo/experimentos/10_figuras_apresentacao.py`. Os números abaixo são os que estão nos slides.

A apresentação não fala do limiar antigo do firmware. O limiar aparece só como o **gatilho**, um passo do
pipeline.

## 1. Abertura (slides 1–2, ~1 min)

- **Objetivo:** reconhecer, na própria pulseira, quando o idoso cai, a partir do acelerômetro e do giroscópio do
  MPU-6050 (6 eixos a 200 Hz).
- **Resposta binária:** queda ou não queda. Os tipos de queda e de atividade servem só para entender os erros.
- **Duas metas que puxam em sentidos opostos:** detectar quase todas as quedas e quase nunca dar alarme falso.
  Elas viram as duas medidas da apresentação: quedas detectadas (%) e alarmes falsos por hora.

## 2. Dados (slides 3–4, ~2 min)

- **SisFall:** 38 pessoas (23 jovens e 15 idosos), 4505 ensaios, cintura, 200 Hz.
  - 1798 quedas de 15 tipos, em colchão;
  - 2707 atividades de 19 tipos, 14,5 h.
- **Na figura:** correr e sentar rápido também têm picos altos. O pico sozinho não decide.
- **Nossos 18 ensaios:** uma pessoa, na cintura, gravados em ±2 g. Saturam: o |a| não passa de 3,46 g.
- **Dois cenários:** A, ±8 g (o vestível), e B, ±2 g (os nossos dados). A SisFall é saturada em cada um.
- Os nossos ensaios servem só como teste externo.

## 3. Pipeline (slides 5–10, ~4 min)

- **Slide 5, visão geral:** filtro → gatilho → janela → atributos → classificador → decisão. O gatilho só acorda
  o classificador; quem decide é o classificador.
- **Slides 6 e 7, o filtro no exemplo pedido:**
  - mostrar a mesma queda (F01) primeiro bruta e depois filtrada;
  - o filtro tira o serrilhado e mantém a forma;
  - o pico de |a| passa de 10,1 g para 7,6 g.
- **Slide 8, por que 20 Hz:** o movimento do corpo fica abaixo de ~20 Hz, e acima é ruído e vibração. É um
  Butterworth de 2ª ordem: um biquad do ESP-DSP por eixo.
- **Slide 9, gatilho:**
  - o limiar vira um passo do pipeline: dispara num pico de |a| ≥ 2,0 g;
  - num tropeço correndo, cada passo dispara (6 disparos), mas só o impacto é queda;
  - 2,0 g deixa passar 99,2 % das quedas, e o custo é acordar o classificador ~858 vezes por hora.
- **Slide 10, janela e atributos:**
  - janela de −1,0 s a +0,8 s em volta do pico, com 23 atributos no tempo;
  - na figura: queda livre (0,35 g, 0,36 s antes), impacto (7,6 g), o corpo parado depois e a gravidade mudando
    de eixo (inclinação de 90°).

## 4. Métodos e resultados (slides 11–14, ~3 min)

- **Slide 11, como avaliamos:**
  - validação cruzada por pessoa: ninguém no treino e no teste;
  - ponto de operação: 95 % das janelas de queda detectadas, e aí contamos os alarmes falsos por hora.
- **Slide 12, os métodos:** só uma frase por método (tabela).
  - Das aulas: correlação, DTW, AR, centroide, kNN.
  - Da literatura: regras, logística, árvore, SVM, CNN, Random Forest.
- **Slide 13, resultados:** os que comparam formas de onda ficam em dezenas a centenas de alarmes por hora. Os
  classificadores sobre atributos ficam abaixo de 1/h. O Random Forest lidera, com 0,41/h.
- **Slide 14, por quê:** depois da queda o corpo fica parado e deitado. Correr tem pico alto, mas continua em
  movimento e em pé.

## 5. Random Forest a fundo (slides 15–21, ~5 min)

- **Slide 15, a árvore de decisão:** uma sequência de perguntas "atributo ≤ limiar?". A folha guarda a fração de
  quedas de treino que chegaram nela, que é a P(queda). Ler a primeira pergunta: inclinação ≤ 36°.
- **Slide 16, como a árvore aprende:**
  - a impureza de Gini, G = 1 − p² − (1 − p)²;
  - escolhe a divisão de maior ganho; na primeira pergunta, o Gini cai de 0,50 para 0,04 nos dois lados;
  - pesos para o desbalanceamento: queda 3,57, atividade 0,58;
  - para na profundidade 10.
- **Slide 17, a floresta:**
  - bagging: cada árvore vê um sorteio com reposição, ~63 % das janelas;
  - em cada nó, só 4 atributos sorteados;
  - as 60 árvores erram em lugares diferentes, e a média cancela os erros: uma árvore sozinha dá 5,3 alarmes/h, e
    a floresta, 0,4/h.
- **Slide 18, árvore × floresta:** a árvore decora o treino (blocos e ilhas); a floresta dá uma probabilidade
  graduada.
- **Slide 19, os votos:**
  - na queda, quase todas as árvores votam queda (média 0,86);
  - na corrida, nenhuma (0,00);
  - na atividade mais difícil (D11, tentar levantar e desabar na cadeira), elas discordam (0,58), e a média fica
    abaixo do limiar de 0,73.
- **Slide 20, o que a floresta olha:**
  - o que pesa é a postura depois e a forma do impacto; o pico quase não pesa;
  - por grupo de atributos sozinho: tempo 95,4 %, FFT 7,8 %, AR 5,2 %.
- **Slide 21, por que o escolhemos:**
  - menos alarmes falsos, e perde pouco com saturação;
  - dispensa normalização e cabe no ESP32;
  - é explicável;
  - comparar com o kNN (1,6 MB), a CNN (cai em ±2 g) e a logística (o triplo de alarmes).

## 6. Fechamento (slides 22–26, ~3 min)

- **Slide 22, o pipeline completo na SisFall:**
  - das 1798 quedas, 1784 passam pelo gatilho e 1695 são reconhecidas (94,3 %);
  - nas atividades, 753 disparos por hora viram 0,55 alarme falso por hora.
- **Slide 23, os nossos ensaios:** o Random Forest não deu nenhum alarme falso ao andar ou correr e detectou as 7
  quedas de frente e de costas. Perdeu as de joelhos, que são lentas e têm ensaios curtos.
- **Slide 24, no vestível:**
  - gatilho causal, 23 atributos, 60 árvores (104 KB);
  - o firmware reproduz o Python;
  - a queda vai ao hub por ESP-NOW e de lá à central e ao cuidador, com a P(queda).
- **Slide 25, limitações:**
  - os dados são da cintura, e a pulseira vai no pulso;
  - próximo passo: bancada, coleta no pulso com várias pessoas e retreino.
- **Slide 26:** perguntas.
