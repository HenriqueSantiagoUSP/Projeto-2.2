# Coleta de dados para o modelo de queda

O detector de queda atual é um **limiar provisório**. O destino é um pipeline com modelo preditivo treinado com
grande volume de dados ([`roadmap.md`](roadmap.md), fase 8 e Futuro). Este protocolo garante que os dados
coletados desde já sirvam para isso: sinais brutos, rotulados, de várias pessoas, sempre no mesmo formato.

## Equipamento

- Vestível com o firmware em **modo de coleta**: `idf.py menuconfig` → Wearable → *Data collection mode*.
- O MPU-6050 na **posição de uso** (pulso), sempre na mesma orientação; anote qual pulso.
- Colchão grosso para as quedas e uma pessoa de apoio. Segurança acima de tudo: nada de quedas sem proteção.

## O que o firmware grava

Uma linha por amostra, a 200 Hz, temporizada pelo próprio sensor (não pelo ESP32):

```
CSV:t_ms,ax,ay,az,gx,gy,gz
CSV:15,123,-40,4101,-12,3,0
```

Valores **brutos** do MPU-6050: 4096 LSB/g (±8 g) e 65,5 LSB/(°/s) (±500 °/s). Guarde sempre o bruto; converter
é trabalho do pipeline.

Captura no PC:

```powershell
idf.py -p COMx monitor | Tee-Object -FilePath bruto.log
Select-String -Path bruto.log -Pattern '^CSV:' | ForEach-Object { $_.Line.Substring(4) } > ensaio.csv
```

## Atividades

Cada pessoa faz cada atividade o número de vezes indicado. Cada repetição é um arquivo.

| Grupo | Atividade | Repetições | Rótulo |
|---|---|---|---|
| Queda | para frente | 5 | `queda_frente` |
| Queda | para trás | 5 | `queda_tras` |
| Queda | de lado (esq./dir.) | 5 + 5 | `queda_lado` |
| Queda | ajoelhando e caindo | 5 | `queda_joelhos` |
| Dia a dia | andar 20 s | 3 | `andar` |
| Dia a dia | sentar e levantar da cadeira | 5 | `sentar` |
| Dia a dia | deitar e levantar da cama | 5 | `deitar` |
| Dia a dia | sentar-se bruscamente | 5 | `sentar_brusco` |
| Braço | bater palmas, acenar, bater na mesa | 5 cada | `palmas`, `acenar`, `bater_mesa` |
| Braço | gesticular conversando 30 s | 2 | `gesticular` |

Os movimentos de braço são de propósito: o pulso é a posição mais difícil para detectar queda, e é aí que o
limiar provisório mais erra.

## Nome do arquivo e metadados

`<pessoa>_<rotulo>_<repeticao>.csv`, por exemplo `p03_queda_frente_2.csv`. Em cada pasta de sessão, um
`metadados.csv`:

| Campo | Exemplo |
|---|---|
| pessoa | p03 (nunca o nome) |
| idade, altura_cm, peso_kg | 22, 175, 70 |
| pulso | esquerdo |
| data | 2026-11-05 |
| firmware | versão do gadget (`git describe`) |
| observacoes | "queda 3 sem colchão alto" |

## Onde guardar

Dados grandes não vão para o git: a pasta `dados/` na raiz é ignorada, exceto `dados/README.md`. Guarde os
arquivos lá ou num drive compartilhado da equipe, e versione só este protocolo e o `dados/README.md`
descrevendo o conjunto: quantas pessoas, quantos arquivos por rótulo e onde está.

## O que já existe (2026-11-09)

Descrição completa em [`dados/README.md`](../dados/README.md).

- **SisFall** (base pública, sensor na cintura): usada para treinar e comparar os modelos em
  [`modelo/`](../modelo/README.md).
- **18 ensaios próprios** (uma pessoa, cintura). Eles **não** seguiram este protocolo:
  - acelerômetro em ±2 g (satura);
  - valores já convertidos;
  - ensaios de 2 a 5 s;
  - sem metadados.

  Servem só como teste externo pequeno. A próxima coleta deve seguir este protocolo à risca, **no pulso**,
  com o firmware atual (±8 g) e ensaios de ≥ 10 s, com pelo menos 3 s depois de cada queda: a postura depois
  do impacto é o que mais separa queda de corrida.

## Para depois

- Janelas brutas em torno de cada disparo enviadas à central/nuvem durante o uso real (roadmap, Futuro).
- Outras bases públicas (ex.: MobiFall, citada no S1 P1; FallAllD, que tem pulso) para complementar,
  respeitando as licenças.
