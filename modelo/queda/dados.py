"""
Leitores das bases de dados para um formato comum.

Cada ensaio vira um `Ensaio`: aceleração em g e velocidade angular em °/s, a 200 Hz, com a pessoa, o
código da atividade e se é queda. A SisFall é lida uma vez e guardada em cache (dados/cache/), porque o
texto leva ~1 min para ler.

Bases (descrição em dados/README.md):
- SisFall: sensor na cintura, 200 Hz, ADXL345 (±16 g, 13 bits) e ITG3200 (±2000 °/s, 16 bits).
  A pessoa é lida pela pasta: em SA15/ há ensaios D17 da SA15 gravados com o nome D17_SE15.
- Própria: sensor na cintura, uma pessoa, valores já em g e °/s, t_ms com resolução de 10 ms
  (~193 Hz efetivos), gravada com o MPU-6050 em ±2 g e ±250 °/s.
"""

from __future__ import annotations

import io
import os
import pickle
import re
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
import pandas as pd

FS = 200.0                                   # taxa comum de todos os ensaios (Hz)
RAIZ_PROJETO = Path(__file__).resolve().parents[2]
DADOS = RAIZ_PROJETO / "dados"
CACHE = DADOS / "cache"

ADXL345_G = 2 * 16 / 2 ** 13                 # g por LSB
ITG3200_DPS = 2 * 2000 / 2 ** 16             # °/s por LSB

ATIVIDADES_SISFALL = {
    "D01": "andar devagar", "D02": "andar rápido", "D03": "correr devagar", "D04": "correr rápido",
    "D05": "escada devagar", "D06": "escada rápido", "D07": "sentar devagar (cadeira média)",
    "D08": "sentar rápido (cadeira média)", "D09": "sentar devagar (cadeira baixa)",
    "D10": "sentar rápido (cadeira baixa)", "D11": "tentar levantar e desabar na cadeira",
    "D12": "deitar devagar", "D13": "deitar rápido", "D14": "virar de lado deitado",
    "D15": "agachar dobrando os joelhos", "D16": "curvar sem dobrar os joelhos", "D17": "entrar e sair do carro",
    "D18": "tropeçar andando", "D19": "pular de leve",
    "F01": "frente, escorregão andando", "F02": "trás, escorregão andando", "F03": "lado, escorregão andando",
    "F04": "frente, tropeço andando", "F05": "frente, tropeço correndo", "F06": "vertical, desmaio andando",
    "F07": "desmaio andando, apoia as mãos", "F08": "frente, ao levantar", "F09": "lado, ao levantar",
    "F10": "frente, ao sentar", "F11": "trás, ao sentar", "F12": "lado, ao sentar",
    "F13": "frente, sentado (desmaio)", "F14": "trás, sentado (desmaio)", "F15": "lado, sentado (desmaio)",
}

ATIVIDADES_PROPRIAS = {
    "andando": "andar", "correndo": "correr",
    "caindo_costas": "queda de costas", "caindo_frente": "queda de frente", "caindo_joelho": "queda de joelhos",
}


@dataclass
class Ensaio:
    base: str            # "sisfall" ou "propria"
    pessoa: str
    codigo: str          # D01..D19 / F01..F15 ou o rótulo próprio
    rep: int
    queda: bool
    acc: np.ndarray      # (n, 3) em g
    gyr: np.ndarray      # (n, 3) em °/s
    arquivo: str = ""
    extra: dict = field(default_factory=dict)

    @property
    def duracao_s(self) -> float:
        return len(self.acc) / FS

    @property
    def descricao(self) -> str:
        return ATIVIDADES_SISFALL.get(self.codigo) or ATIVIDADES_PROPRIAS.get(self.codigo, self.codigo)


def saturar(acc: np.ndarray, gyr: np.ndarray, g_max: float, dps_max: float) -> tuple[np.ndarray, np.ndarray]:
    """Satura cada eixo na faixa do sensor, como um MPU-6050 configurado em ±g_max e ±dps_max."""
    return np.clip(acc, -g_max, g_max), np.clip(gyr, -dps_max, dps_max)


# ---------------------------------------------------------------------------------------------------------
# SisFall
# ---------------------------------------------------------------------------------------------------------

_NOME_SISFALL = re.compile(r"^([DF]\d{2})_S[AE]\d{2}_R(\d{2})\.txt$")
_PASTA_PESSOA = re.compile(r"^S[AE]\d{2}$")


def _ler_txt_sisfall(caminho: Path) -> np.ndarray:
    """Devolve as 6 primeiras colunas (ADXL345 e ITG3200) brutas, em int16."""
    texto = caminho.read_text(encoding="ascii", errors="replace").replace(";", "")
    tabela = pd.read_csv(io.StringIO(texto), header=None, usecols=range(6), engine="c", dtype=np.int16)
    return tabela.to_numpy()


def _arquivos_sisfall(raiz: Path) -> list[tuple[Path, str, str, int]]:
    achados = []
    for pasta, _, nomes in os.walk(raiz):
        pessoa = os.path.basename(pasta)
        if not _PASTA_PESSOA.match(pessoa):
            continue
        for nome in nomes:
            m = _NOME_SISFALL.match(nome)
            if m:
                achados.append((Path(pasta) / nome, pessoa, m.group(1), int(m.group(2))))
    return sorted(achados, key=lambda a: (a[1], a[2], a[3]))


def carregar_sisfall(raiz: Path | None = None, usar_cache: bool = True) -> list[Ensaio]:
    """Lê todos os ensaios da SisFall em dados/publicos/ (busca as pastas SAxx/SExx em qualquer nível)."""
    raiz = Path(raiz) if raiz else DADOS / "publicos"
    cache = CACHE / "sisfall_v1.pkl"
    if usar_cache and cache.exists():
        with open(cache, "rb") as f:
            brutos = pickle.load(f)
    else:
        arquivos = _arquivos_sisfall(raiz)
        if not arquivos:
            raise FileNotFoundError(f"nenhum ensaio da SisFall em {raiz}")
        brutos = [(str(p.relative_to(DADOS)), pessoa, codigo, rep, _ler_txt_sisfall(p))
                  for p, pessoa, codigo, rep in arquivos]
        CACHE.mkdir(parents=True, exist_ok=True)
        with open(cache, "wb") as f:
            pickle.dump(brutos, f, protocol=pickle.HIGHEST_PROTOCOL)
    ensaios = []
    for arquivo, pessoa, codigo, rep, bruto in brutos:
        acc = (bruto[:, 0:3].astype(np.float32) * np.float32(ADXL345_G))
        gyr = (bruto[:, 3:6].astype(np.float32) * np.float32(ITG3200_DPS))
        ensaios.append(Ensaio("sisfall", pessoa, codigo, rep, codigo.startswith("F"), acc, gyr, arquivo))
    return ensaios


# ---------------------------------------------------------------------------------------------------------
# Dados próprios
# ---------------------------------------------------------------------------------------------------------

_NOME_PROPRIO = re.compile(r"^(?P<rotulo>[a-z_]+?)_(?P<rep>\d+)\.csv$")


def _ler_csv_proprio(caminho: Path) -> np.ndarray:
    linhas = []
    for linha in caminho.read_text(encoding="utf-8", errors="replace").splitlines():
        linha = linha.strip()
        if linha.startswith("CSV:"):
            linha = linha[4:]
        partes = linha.split(",")
        if len(partes) < 7:
            continue
        try:
            linhas.append([float(p) for p in partes[:7]])
        except ValueError:
            continue                         # cabeçalho ou linha truncada
    return np.asarray(linhas, dtype=np.float64)


def _reamostrar(t_s: np.ndarray, x: np.ndarray, fs: float) -> np.ndarray:
    """Interpola cada coluna de x para uma grade uniforme a fs, do primeiro ao último instante."""
    n = int(np.floor((t_s[-1] - t_s[0]) * fs)) + 1
    grade = t_s[0] + np.arange(n) / fs
    return np.stack([np.interp(grade, t_s, x[:, j]) for j in range(x.shape[1])], axis=1)


def carregar_proprios(raiz: Path | None = None, pessoa: str = "p01") -> list[Ensaio]:
    """
    Lê os ensaios de dados/proprios/<rotulo>_<n>/<rotulo>_<n>.csv (ignora *_processado.csv).

    O t_ms dos arquivos tem resolução de 10 ms (duas amostras por tique), então o tempo de cada amostra é
    reconstruído como uniforme entre o primeiro e o último t_ms; depois tudo é reamostrado para 200 Hz.
    """
    raiz = Path(raiz) if raiz else DADOS / "proprios"
    ensaios = []
    for caminho in sorted(raiz.rglob("*.csv")):
        if caminho.name.endswith("_processado.csv"):
            continue
        m = _NOME_PROPRIO.match(caminho.name)
        if not m or m.group("rotulo") not in ATIVIDADES_PROPRIAS:
            continue
        bruto = _ler_csv_proprio(caminho)
        if len(bruto) < 10:
            continue
        duracao_s = (bruto[-1, 0] - bruto[0, 0]) / 1000.0
        t_s = np.linspace(0.0, duracao_s, len(bruto))
        dados = _reamostrar(t_s, bruto[:, 1:7], FS).astype(np.float32)
        rotulo = m.group("rotulo")
        ensaios.append(Ensaio("propria", pessoa, rotulo, int(m.group("rep")), rotulo.startswith("caindo"),
                              dados[:, 0:3], dados[:, 3:6], str(caminho.relative_to(DADOS)),
                              {"fs_efetiva": (len(bruto) - 1) / duracao_s}))
    return ensaios
