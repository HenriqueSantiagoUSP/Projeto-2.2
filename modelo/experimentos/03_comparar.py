"""
Comparação de todos os métodos na SisFall, com validação cruzada por pessoa (5 dobras).

    uv run python experimentos/03_comparar.py --cenario A          # ±8 g, a pulseira atual (principal)
    uv run python experimentos/03_comparar.py --cenario B          # ±2 g, a faixa dos dados próprios

Saídas em resultados/:
- comparacao_<cenario>.csv: AUC, sensibilidade por ensaio, especificidade e alarmes/h com 95 % das janelas de
  impacto acima do limiar, sensibilidade com ≤ 1 alarme/h, tempo e custo estimado no ESP32;
- ordem_ar_<cenario>.csv: AIC/BIC do AR(p) nas janelas de atividade;
- erros_por_tipo_<cenario>.csv: perdas por tipo de queda e alarmes por atividade, do melhor método;
- fig_roc_<cenario>.png, fig_ordem_ar_<cenario>.png e escores_<cenario>.npz.

A figura de acurácia × custo sai de 05_figuras.py, que só lê as tabelas.
"""

import argparse
import json

import _comum
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from sklearn.metrics import roc_curve

from queda import atributos, avaliacao, modelos
from queda.dados import FS
from queda.pipeline import CENARIOS, PRE_S, montar

# Custo estimado de extrair os atributos de uma janela de 360 amostras (operações): momentos e demais
# atributos no tempo (~40 por amostra), FFT de 512 pontos (~5·N·log2 N) e AR(4) por mínimos quadrados.
OPS_ATRIBUTOS = {"tempo": 360 * 40, "freq": 5 * 512 * 9, "ar": 360 * 4 * 5 + 64}


def ordem_ar(conj, p_max=12):
    """AIC e BIC do AR(p) ajustado a todas as janelas de atividade (|a| a 50 Hz), slide 16."""
    a = conj.reduzidas[conj.y == 0, 0, :].astype(np.float64)
    a = a - a.mean(axis=1, keepdims=True)
    linhas = []
    for p in range(1, p_max + 1):
        _, desvio = atributos.ajustar_ar(a, p)
        n = a.size - p * len(a)
        var = float(np.mean(desvio ** 2))
        linhas.append({"p": p, "var_residuo": var, "aic": n * np.log(var) + 2 * p,
                       "bic": n * np.log(var) + p * np.log(n)})
    df = pd.DataFrame(linhas)
    return df, int(df.loc[df.bic.idxmin(), "p"])


def metodos(nomes, p_ar, com_cnn):
    """Nome → (fábrica do modelo, grupos de atributos usados, família)."""
    tudo = ("tempo", "freq", "ar")
    m = {
        "regras_cascata": (lambda: modelos.Regras(nomes), tudo, "literatura"),
        "corr_nao_normalizada": (lambda: modelos.Templates("corr"), (), "slides"),
        "corr_normalizada": (lambda: modelos.Templates("corr_norm"), (), "slides"),
        "dtw": (lambda: modelos.Templates("dtw"), (), "slides"),
        f"ar{p_ar}_anomalia": (lambda: modelos.ARAnomalia(p_ar), (), "slides"),
        f"arx{p_ar}_anomalia": (lambda: modelos.ARAnomalia(p_ar, exogena=True), (), "slides"),
        "centroide_euclidiana": (lambda: modelos.CentroideMaisProximo("euclidiana"), tudo, "slides"),
        "centroide_manhattan": (lambda: modelos.CentroideMaisProximo("manhattan"), tudo, "slides"),
        "centroide_cosseno": (lambda: modelos.CentroideMaisProximo("cosseno"), tudo, "slides"),
        "knn_euclidiana": (lambda: modelos.Sklearn("knn_euclidiana"), tudo, "slides"),
        "knn_manhattan": (lambda: modelos.Sklearn("knn_manhattan"), tudo, "slides"),
        "knn_cosseno": (lambda: modelos.Sklearn("knn_cosseno"), tudo, "slides"),
        "regressao_logistica": (lambda: modelos.Sklearn("regressao_logistica"), tudo, "literatura"),
        "arvore_decisao": (lambda: modelos.Sklearn("arvore"), tudo, "literatura"),
        "svm_rbf": (lambda: modelos.Sklearn("svm", max_negativos=6000), tudo, "literatura"),
        "rf_so_tempo": (lambda: modelos.Sklearn("random_forest"), ("tempo",), "ablação"),
        "rf_so_freq": (lambda: modelos.Sklearn("random_forest"), ("freq",), "ablação"),
        "rf_so_ar": (lambda: modelos.Sklearn("random_forest"), ("ar",), "ablação"),
        "rf_tempo_freq": (lambda: modelos.Sklearn("random_forest"), ("tempo", "freq"), "ablação"),
        "random_forest": (lambda: modelos.Sklearn("random_forest"), tudo, "literatura"),
    }
    if com_cnn:
        from queda.cnn import CNN1D
        m["cnn_1d"] = (lambda: CNN1D("modulos"), (), "literatura")
        m["cnn_1d_6_eixos"] = (lambda: CNN1D("eixos"), (), "literatura")
    return m


def main():
    args = argparse.ArgumentParser()
    args.add_argument("--cenario", default="A", choices=list(CENARIOS))
    args.add_argument("--sem-cnn", action="store_true")
    args = args.parse_args()
    c = args.cenario

    sisfall, _ = _comum.carregar()
    l_on = json.loads((_comum.RESULTADOS / "gatilho_escolhido.json").read_text())[c]
    conj = montar(sisfall, c, l_on)
    # a amostra do pico na janela: o mesmo PRE_S que recorta a janela (pipeline) e que o 09 exporta
    pre = int(PRE_S * FS)
    print(f"cenário {c} ({CENARIOS[c]['descricao']}), L_on = {l_on} g: {len(conj.y)} janelas, "
          f"{int(conj.y.sum())} de impacto, {int((conj.y == 0).sum())} de atividade")

    F_tudo, nomes = atributos.calcular(conj.janelas, pre)
    grupos = {"tempo": list(atributos.tempo(conj.janelas[:1], pre)),
              "freq": list(atributos.frequencia(conj.janelas[:1])),
              "ar": list(atributos.ar(conj.janelas[:1]))}
    dobra = avaliacao.dobras(conj)
    for k in np.unique(dobra):
        assert not set(conj.pessoa[dobra == k]) & set(conj.pessoa[dobra != k]), "pessoa no treino e no teste"

    df_ar, p_ar = ordem_ar(conj)
    df_ar.to_csv(_comum.RESULTADOS / f"ordem_ar_{c}.csv", index=False, float_format="%.4f")
    print(f"ordem do AR pelo BIC: p = {p_ar}")

    base = avaliacao.detector_do_firmware(sisfall, c)
    linhas = [{"metodo": "limiar_do_firmware (2,5 g)", "familia": "linha de base", "auc": np.nan,
               "sens_ensaio": base["sens_ensaio"], "espec": np.nan, "alarmes_h": base["alarmes_h"],
               "sens_1alarme_h": np.nan, "tempo_s": 0.0, "ops_janela": 1, "bytes_modelo": 8}]
    escores = {}
    for nome, (fabrica, gs, familia) in metodos(nomes, p_ar, not args.sem_cnn).items():
        cols = [nomes.index(n) for g in gs for n in grupos[g]] if gs else list(range(len(nomes)))
        F = F_tudo[:, cols]
        s, dt, modelo = avaliacao.escores_fora_da_dobra(fabrica, conj, F, dobra)
        r = avaliacao.metricas(s, conj)
        if gs:
            ops, bytes_ = modelo.custo(len(cols))
            ops += sum(OPS_ATRIBUTOS[g] for g in gs)
        else:
            ops, bytes_ = modelo.custo(conj.reduzidas.shape[2])
        escores[nome] = s
        linhas.append({"metodo": nome, "familia": familia, "auc": r.auc, "sens_ensaio": r.sens_ensaio,
                       "espec": r.espec, "alarmes_h": r.alarmes_h, "sens_1alarme_h": r.sens_1alarme_h,
                       "tempo_s": dt, "ops_janela": ops, "bytes_modelo": bytes_})
        print(f"  {nome:24} AUC {r.auc:.3f}  sens {r.sens_ensaio:.3f}  espec {r.espec:.3f}  "
              f"alarmes/h {r.alarmes_h:7.1f}  sens@1/h {r.sens_1alarme_h:.3f}  ({dt:.0f} s)")

    df = pd.DataFrame(linhas)
    df.to_csv(_comum.RESULTADOS / f"comparacao_{c}.csv", index=False, float_format="%.4f")
    np.savez_compressed(_comum.RESULTADOS / f"escores_{c}.npz", y=conj.y, codigo=conj.codigo,
                        pessoa=conj.pessoa, **escores)

    melhor = df.dropna(subset=["auc"]).sort_values(["sens_1alarme_h", "auc"], ascending=False).iloc[0].metodo
    erros = []
    for nome in dict.fromkeys([melhor, "random_forest"]):
        r = avaliacao.metricas(escores[nome], conj)
        erros += [dict(l, metodo=nome) for l in avaliacao.por_codigo(escores[nome], conj, r.limiar)]
    pd.DataFrame(erros).to_csv(_comum.RESULTADOS / f"erros_por_tipo_{c}.csv", index=False, float_format="%.2f")
    print(f"melhor (sensibilidade com ≤ 1 alarme/h): {melhor}")

    # ordem do AR (slide 16): variância do resíduo, AIC e BIC
    fig, ax = plt.subplots(figsize=(6, 3.5))
    ax.plot(df_ar.p, df_ar.var_residuo, "-o")
    ax.set_xlabel("ordem p do AR")
    ax.set_ylabel("variância do resíduo (g²)")
    ax.set_title(f"AR(p) em |a| (50 Hz) nas janelas de atividade, cenário {c}")
    fig.tight_layout()
    fig.savefig(_comum.RESULTADOS / f"fig_ordem_ar_{c}.png", dpi=150)

    fig, ax = plt.subplots(figsize=(6.5, 5.5))
    destaque = ["corr_nao_normalizada", "corr_normalizada", "dtw", f"ar{p_ar}_anomalia", "centroide_euclidiana",
                "regras_cascata", "regressao_logistica", "random_forest", "cnn_1d"]
    for nome in destaque:
        if nome in escores:
            fpr, tpr, _ = roc_curve(conj.y, escores[nome])
            auc = df.set_index("metodo").loc[nome, "auc"]
            ax.plot(fpr, tpr, label=f"{nome} (AUC {auc:.3f})", lw=2 if nome == melhor else 1)
    ax.plot([0, 1], [0, 1], ":", color="gray")
    ax.set_xlabel("taxa de falsos positivos (janelas de atividade)")
    ax.set_ylabel("taxa de verdadeiros positivos (janelas de impacto)")
    ax.set_title(f"ROC por janela, validação por pessoa, cenário {c}")
    ax.legend(fontsize=7, loc="lower right")
    fig.tight_layout()
    fig.savefig(_comum.RESULTADOS / f"fig_roc_{c}.png", dpi=150)


if __name__ == "__main__":
    main()
