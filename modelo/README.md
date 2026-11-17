# modelo

Desenvolvimento e comparação, no PC, dos modelos de detecção de queda que depois vão para a pulseira
(S2 P3 Q1: "desenvolvimento do modelo autorregressivo e/ou dos modelos de classificação"). O código usa as
bases descritas em [`../dados/README.md`](../dados/README.md):
- a SisFall: 38 pessoas e 4505 ensaios, com sensor na cintura;
- os nossos 18 ensaios: uma pessoa, sensor na cintura, ±2 g.

Os resultados ficam em [`resultados/`](resultados/), e o resumo com todos os números em
[`resultados/resumo.md`](resultados/resumo.md).

## Como rodar

Precisa do [`uv`](https://docs.astral.sh/uv/). Ele instala o Python 3.12 e as dependências em `modelo/.venv`;
o Python 3.15 da máquina ainda não tem pacotes do PyTorch. Rode de dentro de `modelo/`:

```bash
uv sync
```

```bash
uv run pytest
```

```bash
uv run python experimentos/02_gatilho.py
```

```bash
uv run python experimentos/03_comparar.py --cenario A
```

```bash
uv run python experimentos/03_comparar.py --cenario B
```

```bash
uv run python experimentos/04_dados_proprios.py
```

```bash
uv run python experimentos/01_explorar.py
```

```bash
uv run python experimentos/05_figuras.py
```

```bash
uv run python experimentos/06_resumo.py
```

```bash
uv run python experimentos/07_figuras_explicativas.py
```

```bash
uv run python experimentos/08_relatorio_latex.py
```

```bash
uv run python experimentos/09_exportar_pulseira.py
```

```bash
uv run python experimentos/10_figuras_apresentacao.py
```

```bash
uv run python experimentos/11_svm_pulseira.py
```

```bash
uv run python experimentos/12_relatorio_q2.py
```

A ordem importa:
1. `02_gatilho.py` escolhe o L_on, que os outros scripts leem.
2. `05_figuras.py` e `06_resumo.py` só leem as tabelas.
3. `07_figuras_explicativas.py` lê os escores de `03_comparar.py --cenario A` e as tabelas de
   `04_dados_proprios.py`.
   - Ele treina de novo só o Random Forest do cenário B e confere, arquivo a arquivo, que dá o mesmo que o
     `04`.
   - Leva ~1 min e gera as figuras que explicam os resultados: saturação, funil do gatilho, escores, atributos,
     AR, templates, FFT, erros por tipo e os nossos ensaios.
4. `08_relatorio_latex.py` copia as figuras e gera as tabelas do relatório LaTeX do Q1
   (`relatorios/Semestre 2/Projeto 3/Q1/latex/`) a partir dos CSVs. Depois, compile lá com `pdflatex main.tex`,
   duas vezes.
5. `09_exportar_pulseira.py` é independente dos outros, exceto pelo L_on do `02`. Ele gera o que vai para a
   pulseira (seção [Na pulseira](#no-pulseira)) e leva ~1 min.
6. `10_figuras_apresentacao.py` vem por último: lê os resultados do `03`, do `04` e do `09`.
   - Gera as figuras da apresentação do Q1 em tamanho de slide (`resultados/ap_*.png`).
   - Escreve as tabelas e os números em `relatorios/Semestre 2/Projeto 3/Q1/apresentacao/`.
   - Confere que o Random Forest que ele explica é o da pulseira (limiar e vetores de teste).
   - Depois, compile lá com `pdflatex main.tex`, duas vezes.
7. `11_svm_pulseira.py` roda depois do `09`. Ele avalia o SVM na configuração da pulseira, ao lado do Random
   Forest e da regressão logística, com intervalos de confiança por pessoa, e leva ~30 s. Não muda o modelo
   exportado.
8. `12_relatorio_q2.py` roda depois do `09` e do `11`. Ele prepara o relatório do Q2
   (`relatorios/Semestre 2/Projeto 3/Q2/latex/`):
   - os gráficos dos sensores: o LM35 com os 22 pontos do S1, a faixa do ADC, o filtro RC, o biquad e os
     cenários do MPU;
   - cópias de figuras do Q1;
   - a lista de materiais e a tabela dos classificadores;
   - `tabelas/numeros.tex`;
   - a PCB, pelo `kicad-cli` (renders, camadas, ERC e DRC) e pelo `pdftoppm` (folhas do esquema).

   Sem o `kicad-cli`, ele avisa e segue. Depois, compile lá com `pdflatex main.tex`, duas vezes.

A primeira leitura da SisFall leva ~1 min e fica em cache em `dados/cache/`. A comparação completa, com as
duas CNNs, leva ~3 min por cenário.

## Pipeline

```
200 Hz → passa-baixa (biquad, 20 Hz) → gatilho: picos de |a| > L_on separados por ≥ 0,5 s
       → janela em torno do pico (−1,0 s; +0,8 s) → atributos → classificador → queda / não queda
```

- **O gatilho só acorda o classificador**:
  - L_on = 2,0 g deixa passar 99,2 % das quedas da SisFall;
  - o preço é ~860 janelas por hora de atividade;
  - correr, pular, sentar com força e escada rápida também passam do limiar, e quem separa esses casos da
    queda é o classificador.
- **Saída binária** (queda ou não). Os 15 tipos de queda e as 19 atividades da SisFall servem para a análise
  de erros, não como classes.
- **Validação cruzada por pessoa** (5 dobras): ninguém fica no treino e no teste ao mesmo tempo.

## Código

| Arquivo | O que faz |
|---|---|
| `queda/dados.py` | leitores da SisFall (pessoa pela pasta; cache) e dos nossos ensaios (reamostrados para 200 Hz) e saturação na faixa do sensor |
| `queda/pipeline.py` | filtro, gatilho (detector de picos; `gatilho_causal`, o que roda na pulseira; e a histerese do firmware antigo, para a linha de base), janelas e cenários A (±8 g) e B (±2 g) |
| `queda/atributos.py` | atributos no tempo (as definições do `signal_proc.c` + as dos slides), FFT e AR(p) por mínimos quadrados |
| `queda/modelos.py` | regras em cascata, correlação com templates (normalizada e não), DTW, AR/ARX como anomalia, centroide, kNN, regressão logística, árvore, SVM e Random Forest |
| `queda/cnn.py` | CNN 1D: só módulos (|a| e \|ω\|) ou os 6 eixos |
| `queda/avaliacao.py` | dobras por pessoa, métricas por ensaio e por hora, erros por tipo e o detector antigo do firmware (linha de base) |
| `tests/` | blocos contra valores conhecidos e os exemplos dos slides (AR(2), DTW, correlação), o gatilho vetorizado contra o laço do firmware antigo e o gatilho causal |
| `experimentos/07_figuras_explicativas.py` | 11 figuras para a apresentação e o relatório, e `ensaios_com_alarme_A.csv` (% de ensaios de cada atividade com alarme: limiar atual × Random Forest) |
| `experimentos/08_relatorio_latex.py` | figuras e tabelas (`.tex`) do relatório LaTeX do Q1, geradas dos resultados |
| `experimentos/09_exportar_pulseira.py` | treina os classificadores da pulseira com o gatilho causal e gera `fall_model.h`/`.c`, os vetores de teste de paridade e `resultados/pulseira.json` |
| `experimentos/11_svm_pulseira.py` | SVM (RBF) na configuração da pulseira (gatilho causal, 23 atributos no tempo, ±8 g) contra o Random Forest 60 × 10 e a regressão logística: métricas, tamanho, operações por janela e bootstrap por pessoa. Grava `resultados/svm_pulseira.json` |
| `experimentos/10_figuras_apresentacao.py` | figuras em tamanho de slide (`ap_*.png`), tabelas e números da apresentação Beamer do Q1. Inclui o sinal antes e depois do filtro, a árvore de decisão, a fronteira árvore × floresta e os votos das 60 árvores |
| `experimentos/12_relatorio_q2.py` | gráficos, tabelas, números e exportações da PCB do relatório do Q2 (o relatório final); confere que o ajuste do LM35 reproduz a curva do firmware e que o biquad é o do `fall_model.c` |
| `experimentos/_estilo.py` | paleta, parâmetros do matplotlib e nomes dos atributos, comuns ao `07`, ao `10` e ao `12` |

## Principais resultados

Números de [`resultados/resumo.md`](resultados/resumo.md).

- **O limiar provisório do firmware até 2026-11-10** (2,5 g, decidindo sozinho):
  - cenário A: 97,4 % das quedas, com **69,9 alarmes falsos por hora** de atividade;
  - nos nossos ensaios: as 10 quedas, mas alarme nas 8 caminhadas e corridas.
- **Random Forest** com atributos de tempo, frequência e AR, no cenário A:
  - 94,3 % das quedas com 0,4 alarme/h;
  - 95,7 % das quedas com no máximo 1 alarme/h;
  - AUC 0,999.
- **Regressão logística**, com um modelo de 412 bytes: 92,8 % das quedas com ≤ 1 alarme/h. É o melhor custo
  × benefício para o ESP32.
- **Métodos dos slides:**
  - **comparar formas de onda** (correlação com templates, DTW) e o **AR como detector de anomalia** ficam bem
    atrás (AUC de 0,79 a 0,97): correr e tropeçar têm formas parecidas com a queda, e o AR erra muito em
    qualquer movimento brusco. Os coeficientes AR sozinhos, como atributos, dão AUC 0,71;
  - as **distâncias entre vetores de atributos** vão bem: o kNN com distância de Manhattan é o melhor de todos
    (96,1 % com ≤ 1 alarme/h). Mas ele guarda o treino inteiro (~1,5 MB), o que inviabiliza o ESP32.
- **O que separa a queda** é a postura e a quietude depois do impacto e a queda livre antes, que os
  atributos no tempo capturam.
- **CNN 1D:**
  - com os 6 eixos: AUC 0,998 e 93,9 % das quedas com ≤ 1 alarme/h;
  - só com os módulos: 59,1 %, porque sem os eixos ela não vê a mudança de postura.
- **Nos nossos ensaios** (treino na SisFall saturada em ±2 g):
  - Random Forest, regressão logística e SVM: **nenhum alarme falso**, com as 7 quedas de frente e de costas
    detectadas;
  - as quedas de joelhos foram perdidas (o SVM pegou uma das três): são mais lentas, e os ensaios acabam ~1 s
    depois do impacto.

## Na pulseira

Desde 2026-11-11, o pipeline roda no firmware (`firmware/gadget/components/fall_pipeline`). O que vai para lá
sai de `experimentos/09_exportar_pulseira.py`, com os números em
[`resultados/pulseira.json`](resultados/pulseira.json) (SisFall, cenário A, validação por pessoa):

- **Gatilho causal** (`pipeline.gatilho_causal`), o que roda amostra a amostra:
  - acompanha o pico de \|a\| filtrado desde uma subida até 2,0 g e o emite depois de 0,5 s sem um valor maior;
  - deixa passar as mesmas 99,2 % das quedas que o `find_peaks` e dispara menos (753 contra 858 por hora).
- **Só os 23 atributos no tempo:** sem FFT nem AR no C, perdendo pouco.
- **Random Forest** de 60 árvores com profundidade ≤ 10 (106 kB na flash), o padrão:
  - 0,55 alarme falso/h;
  - 95,1 % das quedas com ≤ 1 alarme/h;
  - limiar P ≥ 0,73.
- **Regressão logística**, escolhida no `menuconfig`:
  - 0,62 alarme falso/h;
  - 94,9 % das quedas com ≤ 1 alarme/h;
  - limiar P ≥ 0,96.
- **SVM (RBF)**, avaliado na mesma configuração por `11_svm_pulseira.py` ([`resultados/svm_pulseira.json`](resultados/svm_pulseira.json)), mas
  não exportado:
  - treinado com todas as janelas: 0,69 alarme falso/h e 96,2 % das quedas com ≤ 1 alarme/h;
  - 34 kB na flash (348 vetores de suporte), mas ~24 mil operações e 348 exponenciais por janela, contra ≤ 600
    comparações do Random Forest;
  - **empate estatístico** com o Random Forest e a logística. No bootstrap por pessoa, todos os intervalos de 95 %
    das diferenças passam pelo zero: para o SVM, alarmes/h de −0,42 a +0,80 e quedas com ≤ 1 alarme/h de −4,3 a
    +2,9 pontos.

O script gera `fall_model.h`/`fall_model.c`, com o filtro, as constantes e os dois modelos. Gera também os
vetores de `test_host/vetores/`: 8 trechos da SisFall e o que o Python calcula neles. Os testes de host do C
comparam o pipeline inteiro com esses números. Ao retreinar (por exemplo, com os dados do pulso), rode o script
e depois os testes:

```bash
sh ../firmware/gadget/components/fall_pipeline/test_host/run_tests.sh
```
