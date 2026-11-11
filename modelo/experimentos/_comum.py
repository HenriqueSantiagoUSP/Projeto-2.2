"""Caminhos e carregamento comuns aos experimentos (rodar de modelo/: uv run python experimentos/<x>.py)."""

import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")          # o console do Windows é cp1252
RAIZ_MODELO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RAIZ_MODELO))

RESULTADOS = RAIZ_MODELO / "resultados"
RESULTADOS.mkdir(exist_ok=True)

from queda.dados import carregar_proprios, carregar_sisfall  # noqa: E402


def carregar():
    sisfall = carregar_sisfall()
    proprios = carregar_proprios()
    print(f"SisFall: {len(sisfall)} ensaios, {len({e.pessoa for e in sisfall})} pessoas; "
          f"próprios: {len(proprios)} ensaios")
    return sisfall, proprios
