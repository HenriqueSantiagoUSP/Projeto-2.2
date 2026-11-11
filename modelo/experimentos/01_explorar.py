"""
Figuras de exploração para a apresentação.

- fig_sinais.png: |a| típico de atividades e quedas (SisFall, cenário A) e dos nossos ensaios (±2 g),
  centrado no maior pico.
- fig_pipeline.png: o pipeline num ensaio de queda da SisFall: sinal, limiar do gatilho, disparos e a janela
  que vai para o classificador.
- fig_picos.png: distribuição do maior |a| por tipo de atividade e de queda: por que o limiar sozinho não basta.
"""

import json

import _comum
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

from queda.dados import FS, saturar
from queda.pipeline import CENARIOS, POS_S, PRE_S, gatilho, passa_baixa

EXEMPLOS_SISFALL = ["D01", "D04", "D10", "D19", "F01", "F05", "F13"]
EXEMPLOS_PROPRIOS = ["andando", "correndo", "caindo_frente", "caindo_joelho"]


def magnitude(e, cenario):
    faixa = CENARIOS[cenario]
    acc, gyr = saturar(e.acc, e.gyr, faixa["g_max"], faixa["dps_max"])
    return np.linalg.norm(passa_baixa(np.hstack([acc, gyr]))[:, 0:3], axis=1)


def main():
    sisfall, proprios = _comum.carregar()
    l_on = json.loads((_comum.RESULTADOS / "gatilho_escolhido.json").read_text())

    fig, eixos = plt.subplots(2, 1, figsize=(10, 7), sharex=True)
    for ax, ensaios, codigos, cenario in ((eixos[0], sisfall, EXEMPLOS_SISFALL, "A"),
                                         (eixos[1], proprios, EXEMPLOS_PROPRIOS, "B")):
        for c in codigos:
            e = next(x for x in ensaios if x.codigo == c and x.pessoa in ("SA01", "SA02", "p01"))
            a = magnitude(e, cenario)
            p = int(np.argmax(a))
            ini, fim = max(0, p - int(2 * FS)), min(len(a), p + int(2 * FS))
            t = (np.arange(ini, fim) - p) / FS
            ax.plot(t, a[ini:fim], lw=1.4 if e.queda else 0.9, ls="-" if e.queda else "--",
                    label=f"{c}: {e.descricao}")
        ax.axhline(l_on[cenario], color="gray", ls=":", lw=1)
        ax.axvspan(-PRE_S, POS_S, color="tab:green", alpha=0.08)
        ax.set_ylabel("|a| filtrado (g)")
        titulo = ("SisFall (cintura), ±8 g" if cenario == "A"
                  else "Nossos ensaios (cintura), ±2 g: o |a| satura em 3,46 g")
        ax.set_title(titulo)
        ax.legend(fontsize=7, ncol=2, loc="upper right")
    eixos[1].set_xlabel("tempo em torno do maior pico (s); faixa verde = janela do classificador")
    fig.tight_layout()
    fig.savefig(_comum.RESULTADOS / "fig_sinais.png", dpi=150)

    # o pipeline num ensaio: F01 da SA01 tem passos antes do escorregão
    e = next(x for x in sisfall if x.codigo == "F01" and x.pessoa == "SA01")
    a = magnitude(e, "A")
    picos = gatilho(a, l_on["A"])
    impacto = int(np.argmax(a))
    t = np.arange(len(a)) / FS
    fig, ax = plt.subplots(figsize=(10, 3.6))
    ax.plot(t, a, lw=0.8, label="|a| filtrado (passa-baixa de 20 Hz)")
    ax.axhline(l_on["A"], color="tab:red", ls="--", lw=1, label=f"L_on do gatilho = {l_on['A']} g")
    ax.plot(t[picos], a[picos], "v", color="tab:red", label="disparos (cada um vira uma janela)")
    ax.axvspan(t[impacto] - PRE_S, t[impacto] + POS_S, color="tab:green", alpha=0.2,
               label="janela do impacto (−1,0 s; +0,8 s)")
    ax.set_xlabel("tempo (s)")
    ax.set_ylabel("g")
    ax.set_title(f"Pipeline num ensaio da SisFall: {e.codigo} ({e.descricao}), {e.pessoa}")
    ax.legend(fontsize=8, loc="upper left")
    fig.tight_layout()
    fig.savefig(_comum.RESULTADOS / "fig_pipeline.png", dpi=150)

    # maior |a| por código (cenário A)
    codigos = sorted({x.codigo for x in sisfall})
    picos_por = [[magnitude(x, "A").max() for x in sisfall if x.codigo == c] for c in codigos]
    fig, ax = plt.subplots(figsize=(11, 4))
    caixas = ax.boxplot(picos_por, tick_labels=codigos, showfliers=False, patch_artist=True)
    for caixa, c in zip(caixas["boxes"], codigos):
        caixa.set_facecolor("tab:red" if c.startswith("F") else "tab:blue")
        caixa.set_alpha(0.5)
    ax.axhline(2.5, color="black", ls="--", lw=1, label="limiar atual do firmware (2,5 g)")
    ax.axhline(l_on["A"], color="gray", ls=":", lw=1, label=f"gatilho do pipeline ({l_on['A']} g)")
    ax.set_ylabel("maior |a| do ensaio (g)")
    ax.set_title("SisFall, ±8 g: correr (D03, D04), escada rápida (D06), tropeço (D18) e pulo (D19) passam do "
                 "limiar como as quedas")
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(_comum.RESULTADOS / "fig_picos.png", dpi=150)


if __name__ == "__main__":
    main()
