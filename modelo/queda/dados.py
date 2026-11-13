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
    # Ensaios de 2026 (cintura, ±2 g): <rotulo>_<n>.csv, valores em g e °/s
    "andando": "andar", "correndo": "correr",
    "caindo_costas": "queda de costas", "caindo_frente": "queda de frente", "caindo_joelho": "queda de joelhos",
    # Protocolo de coleta no pulso (docs/coleta-de-dados.md): <pessoa>_<rotulo>_<n>.csv, valores brutos
    "queda_frente": "queda para a frente", "queda_tras": "queda para trás", "queda_lado": "queda de lado",
    "queda_joelhos": "queda ajoelhando", "andar": "andar", "sentar": "sentar e levantar",
    "deitar": "deitar e levantar", "sentar_brusco": "sentar-se bruscamente", "palmas": "bater palmas",
    "acenar": "acenar", "bater_mesa": "bater na mesa", "gesticular": "gesticular conversando",
}
PREFIXOS_QUEDA = ("caindo", "queda")


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


def _assinatura(arquivos: list[tuple[Path, str, str, int]]) -> tuple[int, int, int]:
    """Muda se um arquivo entra, sai ou é alterado: quantos são, o tamanho total e a data mais recente (ns)."""
    stats = [p.stat() for p, *_ in arquivos]
    return len(stats), sum(s.st_size for s in stats), max((s.st_mtime_ns for s in stats), default=0)


def _ler_cache(cache: Path, raiz: Path, assinatura) -> list | None:
    """Os brutos do cache, se ele for desta pasta e destes arquivos; None se não servir (e diz por quê)."""
    if not cache.exists():
        return None
    try:
        with open(cache, "rb") as f:
            conteudo = pickle.load(f)
    except (EOFError, pickle.UnpicklingError, OSError, AttributeError, ValueError) as e:
        print(f"Cache da SisFall ilegível ({type(e).__name__}): relendo os arquivos, {cache} será refeito.")
        return None
    if not isinstance(conteudo, dict) or conteudo.get("raiz") != str(raiz.resolve()) or \
            conteudo.get("assinatura") != assinatura:
        print("Cache da SisFall de outra pasta ou de arquivos que mudaram: relendo.")
        return None
    return conteudo["brutos"]


def carregar_sisfall(raiz: Path | None = None, usar_cache: bool = True) -> list[Ensaio]:
    """
    Lê todos os ensaios da SisFall em dados/publicos/ (busca as pastas SAxx/SExx em qualquer nível).

    O cache (dados/cache/) guarda a pasta e uma assinatura dos arquivos: relê se algum mudou ou se a pasta é
    outra. Ele é gravado num temporário e trocado de uma vez, então uma interrupção não deixa um cache truncado.
    """
    raiz = Path(raiz) if raiz else DADOS / "publicos"
    cache = CACHE / "sisfall_v2.pkl"
    arquivos = _arquivos_sisfall(raiz)
    if not arquivos:
        raise FileNotFoundError(f"nenhum ensaio da SisFall em {raiz}")
    assinatura = _assinatura(arquivos)
    brutos = _ler_cache(cache, raiz, assinatura) if usar_cache else None
    if brutos is None:
        def nome(p: Path) -> str:
            try:
                return str(p.relative_to(DADOS))
            except ValueError:
                return str(p)
        brutos = [(nome(p), pessoa, codigo, rep, _ler_txt_sisfall(p)) for p, pessoa, codigo, rep in arquivos]
        CACHE.mkdir(parents=True, exist_ok=True)
        temporario = cache.with_name(cache.name + ".tmp")
        with open(temporario, "wb") as f:
            pickle.dump({"raiz": str(raiz.resolve()), "assinatura": assinatura, "brutos": brutos}, f,
                        protocol=pickle.HIGHEST_PROTOCOL)
        os.replace(temporario, cache)
    ensaios = []
    for arquivo, pessoa, codigo, rep, bruto in brutos:
        acc = (bruto[:, 0:3].astype(np.float32) * np.float32(ADXL345_G))
        gyr = (bruto[:, 3:6].astype(np.float32) * np.float32(ITG3200_DPS))
        ensaios.append(Ensaio("sisfall", pessoa, codigo, rep, codigo.startswith("F"), acc, gyr, arquivo))
    return ensaios


# ---------------------------------------------------------------------------------------------------------
# Dados próprios
# ---------------------------------------------------------------------------------------------------------

_NOME_PROPRIO = re.compile(r"^(?P<rotulo>[a-z_]+?)_(?P<rep>\d+)\.csv$")                       # andando_1.csv
_NOME_COLETA = re.compile(r"^(?P<pessoa>p\d+)_(?P<rotulo>[a-z_]+?)_(?P<rep>\d+)\.csv$")       # p03_queda_frente_2.csv
_ARQUIVOS_AUXILIARES = ("metadados.csv",)

# Valores brutos do MPU-6050 no firmware atual (±8 g, ±500 °/s): docs/coleta-de-dados.md
LSB_POR_G = 4096.0
LSB_POR_DPS = 65.5
# Parado, a mediana de |a| é ~1 em g e ~4096 em contagens: acima disto, o arquivo está em contagens.
_LIMIAR_CONTAGENS = 50.0


def _ler_csv_proprio(caminho: Path) -> np.ndarray:
    """As linhas t_ms,ax,ay,az,gx,gy,gz (com ou sem o prefixo CSV:), sem cabeçalhos e sem valores não finitos."""
    linhas = []
    for linha in caminho.read_text(encoding="utf-8", errors="replace").splitlines():
        linha = linha.strip()
        if linha.startswith("CSV:"):
            linha = linha[4:]
        partes = linha.split(",")
        if len(partes) < 7:
            continue
        try:
            valores = [float(p) for p in partes[:7]]
        except ValueError:
            continue                         # cabeçalho ou linha truncada
        if all(np.isfinite(valores)):        # "nan"/"inf" passam no float() e contaminariam o filtro
            linhas.append(valores)
    return np.asarray(linhas, dtype=np.float64).reshape(-1, 7)


def _trecho_continuo(bruto: np.ndarray) -> np.ndarray:
    """O trecho mais longo em que t_ms não volta atrás (o firmware recomeça a contagem a cada cabeçalho)."""
    cortes = np.flatnonzero(np.diff(bruto[:, 0]) < 0) + 1
    if len(cortes) == 0:
        return bruto
    trechos = np.split(bruto, cortes)
    return max(trechos, key=len)


def _em_unidades_fisicas(bruto: np.ndarray) -> np.ndarray:
    """Converte para g e °/s se o arquivo estiver em contagens brutas (o formato do protocolo de coleta)."""
    if np.median(np.linalg.norm(bruto[:, 1:4], axis=1)) > _LIMIAR_CONTAGENS:
        bruto = bruto.copy()
        bruto[:, 1:4] /= LSB_POR_G
        bruto[:, 4:7] /= LSB_POR_DPS
    return bruto


def _reamostrar(t_s: np.ndarray, x: np.ndarray, fs: float) -> np.ndarray:
    """Interpola cada coluna de x para uma grade uniforme a fs, do primeiro ao último instante."""
    n = int(np.floor((t_s[-1] - t_s[0]) * fs)) + 1
    grade = t_s[0] + np.arange(n) / fs
    return np.stack([np.interp(grade, t_s, x[:, j]) for j in range(x.shape[1])], axis=1)


def carregar_proprios(raiz: Path | None = None, pessoa: str = "p01") -> list[Ensaio]:
    """
    Lê os ensaios próprios em dois formatos (ignora *_processado.csv e metadados.csv):
    - os de 2026: <rotulo>_<n>.csv, em g e °/s, todos da pessoa `pessoa`;
    - os do protocolo de coleta (docs/coleta-de-dados.md): <pessoa>_<rotulo>_<n>.csv, em contagens brutas.

    A unidade é deduzida do próprio sinal (mediana de |a| ~1 em g, ~4096 em contagens). O t_ms dos arquivos
    antigos tem resolução de 10 ms (duas amostras por tique), então o tempo de cada amostra é reconstruído
    como uniforme entre o primeiro e o último t_ms; depois tudo é reamostrado para 200 Hz.

    Nada é descartado em silêncio: arquivos com nome ou rótulo desconhecido, curtos demais ou sem duração
    são listados num aviso, e uma pasta ausente também.
    """
    raiz = Path(raiz) if raiz else DADOS / "proprios"
    if not raiz.is_dir():
        print(f"Aviso: pasta de ensaios próprios não encontrada ({raiz}); nenhum ensaio próprio carregado.")
        return []
    ensaios, ignorados = [], []
    for caminho in sorted(raiz.rglob("*.csv")):
        if caminho.name.endswith("_processado.csv") or caminho.name in _ARQUIVOS_AUXILIARES:
            continue
        m = _NOME_COLETA.match(caminho.name) or _NOME_PROPRIO.match(caminho.name)
        if not m or m.group("rotulo") not in ATIVIDADES_PROPRIAS:
            ignorados.append(f"{caminho.name} (nome ou rótulo desconhecido)")
            continue
        bruto = _ler_csv_proprio(caminho)
        if len(bruto) < 10:
            ignorados.append(f"{caminho.name} ({len(bruto)} linhas válidas)")
            continue
        bruto = _em_unidades_fisicas(_trecho_continuo(bruto))
        duracao_s = (bruto[-1, 0] - bruto[0, 0]) / 1000.0
        if len(bruto) < 10 or duracao_s <= 0:
            ignorados.append(f"{caminho.name} (sem duração)")
            continue
        t_s = np.linspace(0.0, duracao_s, len(bruto))
        dados = _reamostrar(t_s, bruto[:, 1:7], FS).astype(np.float32)
        rotulo = m.group("rotulo")
        try:
            arquivo = str(caminho.relative_to(DADOS))
        except ValueError:
            arquivo = str(caminho)
        ensaios.append(Ensaio("propria", m.groupdict().get("pessoa") or pessoa, rotulo, int(m.group("rep")),
                              rotulo.startswith(PREFIXOS_QUEDA), dados[:, 0:3], dados[:, 3:6], arquivo,
                              {"fs_efetiva": (len(bruto) - 1) / duracao_s}))
    if ignorados:
        print(f"Aviso: {len(ignorados)} arquivo(s) de ensaio próprio ignorado(s): " + "; ".join(ignorados))
    return ensaios
