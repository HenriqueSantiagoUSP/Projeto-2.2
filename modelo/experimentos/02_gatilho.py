"""
Varredura do limiar do gatilho (L_on): quanto das quedas passa × quantos disparos de atividade por hora.

O gatilho só acorda o classificador, então ele precisa deixar passar praticamente todas as quedas; o preço é
o número de janelas que o classificador tem de examinar. Escolhe-se o maior L_on que ainda deixa passar
≥ 99 % das quedas da SisFall.

Saídas: resultados/gatilho.csv e resultados/fig_gatilho.png
"""

import _comum  # noqa: F401  (ajusta o sys.path)
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

from queda.dados import FS, saturar
from queda.pipeline import CENARIOS, gatilho, passa_baixa

LIMIARES = {"A": np.round(np.arange(1.5, 4.01, 0.25), 2), "B": np.round(np.arange(1.3, 3.01, 0.1), 2)}


def varrer(ensaios, cenario, limiares, tolerancia_s=0.5):
    faixa = CENARIOS[cenario]
    magnitudes = []
    for e in ensaios:
        acc, gyr = saturar(e.acc, e.gyr, faixa["g_max"], faixa["dps_max"])
        a = np.linalg.norm(passa_baixa(np.hstack([acc, gyr]))[:, 0:3], axis=1)
        magnitudes.append((e, a, int(np.argmax(a))))
    horas = sum(e.duracao_s for e in ensaios if not e.queda) / 3600
    quedas = sum(e.queda for e in ensaios)
    linhas = []
    for l_on in limiares:
        passou, disparos_adl = 0, 0
        for e, a, impacto in magnitudes:
            picos = gatilho(a, l_on)
            if e.queda:
                passou += any(abs(p - impacto) <= tolerancia_s * FS for p in picos)
            else:
                disparos_adl += len(picos)
        linhas.append({"cenario": cenario, "l_on_g": l_on, "quedas_que_passam": passou / max(quedas, 1),
                       "disparos_adl_por_h": disparos_adl / max(horas, 1e-9)})
    return linhas


CRITERIO = 0.99


def escolher_l_on(s: pd.DataFrame, cenario: str) -> float:
    """
    O maior L_on da grade que deixa passar ≥ 99 % das quedas. Se nenhum deixa, para: o mínimo da grade
    iria para o firmware (09) como se cumprisse o critério que os documentos afirmam.
    """
    ok = s[s.quedas_que_passam >= CRITERIO]
    if ok.empty:
        raise SystemExit(f"Cenário {cenario}: nenhum L_on de {s.l_on_g.min():g} a {s.l_on_g.max():g} g deixa passar "
                         f"≥ {100 * CRITERIO:.0f} % das quedas (o melhor passa {100 * s.quedas_que_passam.max():.1f} %). "
                         "Estenda LIMIARES para baixo; gatilho_escolhido.json não foi gravado.")
    return float(ok.l_on_g.max())


def main():
    sisfall, proprios = _comum.carregar()
    linhas = []
    for c in ("A", "B"):
        linhas += [dict(l, base="sisfall") for l in varrer(sisfall, c, LIMIARES[c])]
    if proprios:
        linhas += [dict(l, base="propria") for l in varrer(proprios, "B", LIMIARES["B"])]
    else:
        print("Sem ensaios próprios: a varredura fica só com a SisFall.")
    df = pd.DataFrame(linhas)
    df.to_csv(_comum.RESULTADOS / "gatilho.csv", index=False, float_format="%.4f")

    escolha = {c: escolher_l_on(df[(df.base == "sisfall") & (df.cenario == c)], c) for c in ("A", "B")}
    print(df.to_string(index=False))
    print("L_on escolhido (≥ 99 % das quedas da SisFall):", escolha)

    fig, eixos = plt.subplots(1, 2, figsize=(11, 4))
    for ax, c in zip(eixos, ("A", "B")):
        for base, estilo in (("sisfall", "-o"), ("propria", "--s")):
            s = df[(df.base == base) & (df.cenario == c)]
            if s.empty:
                continue
            ax.plot(s.l_on_g, 100 * s.quedas_que_passam, estilo, label=f"quedas que passam ({base})")
            ax2 = ax.twinx() if base == "sisfall" else ax2
            ax2.plot(s.l_on_g, s.disparos_adl_por_h, estilo, color="tab:red", alpha=0.6,
                     label=f"disparos de atividade/h ({base})")
        ax.axvline(escolha[c], color="gray", ls=":")
        ax.set_title(f"Cenário {c}: {CENARIOS[c]['descricao']}")
        ax.set_xlabel("L_on do gatilho (g)")
        ax.set_ylabel("quedas que passam (%)")
        ax2.set_ylabel("disparos de atividade por hora", color="tab:red")
        ax.legend(loc="lower left", fontsize=8)
    fig.tight_layout()
    fig.savefig(_comum.RESULTADOS / "fig_gatilho.png", dpi=150)
    pd.Series(escolha).to_json(_comum.RESULTADOS / "gatilho_escolhido.json")


if __name__ == "__main__":
    main()
