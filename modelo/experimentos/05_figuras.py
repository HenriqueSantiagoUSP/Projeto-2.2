"""
Figuras que só dependem das tabelas de 03_comparar.py (não refaz a validação).

- fig_custo_<cenario>.png: quedas detectadas com ≤ 1 alarme falso/h × tamanho do modelo; o que cabe no ESP32-S3.
- fig_resumo.png: o limiar atual do firmware × os melhores métodos, nos dois cenários.
"""

import _comum
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

CORES = {"slides": "tab:blue", "literatura": "tab:orange"}


def custo(c):
    df = pd.read_csv(_comum.RESULTADOS / f"comparacao_{c}.csv")
    d = df[df.familia.isin(CORES)].dropna(subset=["auc"])
    fig, ax = plt.subplots(figsize=(8, 5.2))
    for _, l in d.iterrows():
        x, y = max(l.bytes_modelo, 1), 100 * l.sens_1alarme_h
        ax.scatter(x, y, color=CORES[l.familia], s=36, zorder=3)
        ax.annotate(l.metodo, (x, y), fontsize=7, xytext=(4, -3 if y > 90 else 3), textcoords="offset points",
                    rotation=20 if y > 90 else 0)
    ax.set_xscale("log")
    ax.axvline(512 * 1024, color="red", ls=":", lw=1)
    ax.text(512 * 1024, 30, " 512 KB\n (SRAM do\n ESP32-S3)", color="red", fontsize=7)
    for fam, cor in CORES.items():
        ax.scatter([], [], color=cor, label=f"métodos da {'aula' if fam == 'slides' else fam}")
    ax.set_ylim(-3, 108)
    ax.set_xlabel("tamanho do modelo (bytes, escala log; estimativa)")
    ax.set_ylabel("quedas detectadas com ≤ 1 alarme falso/h (%)")
    ax.set_title(f"Acurácia × custo (SisFall, validação por pessoa, cenário {c})")
    ax.legend(fontsize=8, loc="center left")
    fig.tight_layout()
    fig.savefig(_comum.RESULTADOS / f"fig_custo_{c}.png", dpi=150)


def resumo():
    escolhidos = ["limiar_do_firmware (2,5 g)", "regras_cascata", "dtw", "corr_normalizada", "centroide_cosseno",
                  "regressao_logistica", "svm_rbf", "random_forest", "cnn_1d", "cnn_1d_6_eixos"]
    fig, eixos = plt.subplots(1, 2, figsize=(12, 4.6), sharey=True)
    for ax, c in zip(eixos, ("A", "B")):
        df = pd.read_csv(_comum.RESULTADOS / f"comparacao_{c}.csv").set_index("metodo")
        nomes = [n for n in escolhidos if n in df.index]
        alarmes = df.loc[nomes, "alarmes_h"]
        ax.barh(nomes, alarmes, color=["tab:red" if n.startswith("limiar") else "tab:blue" for n in nomes])
        ax.set_xscale("log")
        ax.set_xlabel("alarmes falsos por hora de atividade (log)")
        faixa = "±8 g" if c == "A" else "±2 g"
        ax.set_title(f"Cenário {c} ({faixa}): alarmes falsos/h (quedas detectadas)")
        for i, (n, v) in enumerate(alarmes.items()):
            ax.text(v, i, f" {v:.1f}/h ({100 * df.loc[n, 'sens_ensaio']:.0f} %)", va="center", fontsize=7)
    eixos[0].invert_yaxis()
    fig.tight_layout()
    fig.savefig(_comum.RESULTADOS / "fig_resumo.png", dpi=150)


if __name__ == "__main__":
    custo("A")
    custo("B")
    resumo()
