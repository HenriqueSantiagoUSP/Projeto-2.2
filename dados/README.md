# dados

Dados de sensores para os modelos de detecção de queda ([`modelo/`](../modelo/README.md) e
[`docs/coleta-de-dados.md`](../docs/coleta-de-dados.md)). **Nada aqui vai para o git, exceto este arquivo**
(regra `dados/*` no `.gitignore`). Aqui fica só a descrição do conjunto. Os dados em si ficam nesta pasta, em
cada computador, ou num drive da equipe.

## Estrutura

```
dados/
  publicos/
    SisFall_dataset/SisFall_dataset/   a SisFall como baixada: SA01…SA23, SE01…SE15, Readme.txt, Supplementary.pdf
  proprios/
    <rotulo>_<n>/<rotulo>_<n>.csv      os ensaios de 2026, um por pasta (ex.: caindo_frente_2/caindo_frente_2.csv)
    <sessao>/<pessoa>_<rotulo>_<n>.csv a coleta pelo protocolo (ex.: p03_queda_frente_2.csv), com metadados.csv
  cache/                               gerado por modelo/ (pode apagar; é refeito)
```

Os leitores (`modelo/queda/dados.py`) procuram as pastas SAxx/SExx em qualquer nível de `publicos/` e os CSVs
em qualquer nível de `proprios/`. Os `*_processado.csv`, os `*_plot.png` e o `metadados.csv` ficam de fora.
- **Os dois formatos de nome** são aceitos, com os rótulos de `ATIVIDADES_PROPRIAS`. Um arquivo com nome ou
  rótulo desconhecido não é lido, e o leitor avisa quais foram ignorados.
- **Unidade:** deduzida do próprio sinal. Parado, o \|a\| dá ~1 em g e ~4096 em contagens brutas (4096 LSB/g e
  65,5 LSB/(°/s), como no protocolo).
- **Defeitos tratados:** linhas com `nan`/`inf` são descartadas. Se o `t_ms` voltar atrás (o firmware recomeça a
  contagem a cada cabeçalho), fica o trecho contínuo mais longo.
- **Cache da SisFall** (`cache/sisfall_v2.pkl`): guarda a pasta e uma assinatura dos arquivos (quantos, tamanho e
  data). É relido sozinho se algo mudar. Um cache truncado também é refeito. O `sisfall_v1.pkl` antigo pode ser
  apagado.

## SisFall

- **Origem:** Sucerquia, A.; López, J. D.; Vargas-Bonilla, J. F. "SisFall: A Fall and Movement Dataset".
  *Sensors* 17(1):198, 2017. Uso acadêmico, **citando o artigo**.
- **Sensor:** na **cintura**, a 200 Hz. Cada linha tem 9 colunas brutas, terminadas em `;`:
  - ADXL345: ±16 g, 13 bits;
  - ITG3200: ±2000 °/s, 16 bits;
  - MMA8451Q: ±8 g, 14 bits, não usado.
- **Conferido em 2026-11-09:**
  - 4505 arquivos, todos com 9 colunas, nenhum vazio;
  - 38 pessoas: 23 jovens (SA) e 15 idosos (SE); 24 delas fizeram quedas (SA01–SA23 e SE06);
  - 2707 ensaios de atividade (D01–D19, 14,5 h) e 1798 quedas (F01–F15, 7,5 h);
  - o pico de |a| das quedas fica, na mediana, entre 4 e 10 g, e ~25 % dos arquivos de queda passam de 8 g.
- **Peculiaridade:** em `SA15/` há `D17_SE15_R01…R05.txt`, diferentes dos de `SE15/`, e não há nenhum
  `D17_SA15`. São os ensaios D17 da SA15 com o nome errado. A pessoa é lida pela pasta.
- **Arquivos ignorados:** `desktop.ini`, `SA04/images.rar` e o `README.md` de um repositório espelho.

## Dados próprios

- **Quem e onde:** uma pessoa (p01), sensor **na cintura**.
- **Ensaios:** 18, de 1,9 a 5,5 s:

  | Rótulo | Ensaios |
  |---|---|
  | `andando` | 4 |
  | `correndo` | 4 |
  | `caindo_costas` | 3 |
  | `caindo_frente` | 4 |
  | `caindo_joelho` | 3 |

- **Formato** (o antigo, diferente do protocolo de `docs/coleta-de-dados.md`; o leitor aceita os dois):
  - sem cabeçalho, 7 colunas: `t_ms,ax,ay,az,gx,gy,gz`;
  - aceleração já em g e giroscópio em °/s;
  - `t_ms` com resolução de 10 ms (duas amostras por tique), ~190–198 Hz efetivos.
- **Faixa:** **±2 g e ±250 °/s** (os padrões do MPU-6050), saturados em todos os arquivos. O |a| máximo fica
  em 2√3 ≈ 3,46 g. O firmware atual usa ±8 g e ±500 °/s.
- **Duração depois do impacto:** só 0,8–1,6 s nas quedas.
- **Sem `metadados.csv`.**

A próxima coleta deve seguir o protocolo: firmware atual em modo de coleta, ±8 g, sensor no pulso, várias
pessoas, ensaios de ≥ 10 s e metadados.
