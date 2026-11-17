"""
Figuras que explicam os resultados do Q1, para a apresentação e o relatório. Usam os escores fora da dobra de
03_comparar.py (escores_A.npz) e as tabelas de 04_dados_proprios.py; só o Random Forest do cenário B é
treinado de novo, para marcar as decisões nos nossos ensaios (e confere com proprios_por_arquivo.csv).

    uv run python experimentos/07_figuras_explicativas.py

Saídas em resultados/:
- fig_saturacao.png: a mesma queda e a mesma corrida em ±16, ±8 e ±2 g;
- fig_funil.png: o que o gatilho deixa passar e o que o classificador decide;
- fig_escores.png: escores de quedas × atividades em seis métodos, com o limiar de operação;
- fig_separacao.png: dois pares de atributos que separam queda de corrida;
- fig_atributos_rf.png: atributos que o Random Forest mais usa e a ablação por grupo;
- fig_ar_explicado.png: previsão do AR(12) e erro de predição em andar, correr e cair;
- fig_templates.png: templates da correlação normalizada e por que a corrida também casa;
- fig_fft.png: espectro de |a| ao andar, correr e cair, com as bandas usadas como atributos;
- fig_erros_tipo.png: quedas detectadas por tipo e alarmes por atividade (limiar atual × Random Forest);
  a parte das atividades também vai para ensaios_com_alarme_A.csv;
- fig_proprios_ensaios.png: os 18 ensaios próprios, com os disparos e a decisão do Random Forest;
- fig_proprios_matriz.png: acerto de cada método em cada ensaio próprio.
"""

import json

import _comum
from _estilo import (AMARELO, AZUL, BOM, COR_ATIVIDADE, COR_BRUSCO, COR_QUEDA, CRITICO, EIXO, GRADE,  # noqa: F401
                     LARANJA, MUDO, NOMES_ATR, R, TINTA, TINTA2, VERDE_AGUA, VIOLETA, br, grupo_atr, nome_atr,
                     salvar)
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from matplotlib.lines import Line2D
from matplotlib.patches import Patch

from queda import atributos, avaliacao, modelos
from queda.dados import ATIVIDADES_SISFALL, FS, saturar
from queda.pipeline import FS_REDUZIDA, PRE_S, gatilho, gatilho_histerese, montar, passa_baixa

BRUSCOS = ("D03", "D04", "D06", "D18", "D19")          # correr, escada rápida, tropeço, pulo


def magnitude(e, g_max, dps_max=2000.0):
    acc, gyr = saturar(e.acc, e.gyr, g_max, dps_max)
    return np.linalg.norm(passa_baixa(np.hstack([acc, gyr]))[:, 0:3], axis=1)


# ---------------------------------------------------------------------------------------------------------

def fig_saturacao(sisfall):
    quedas = [e for e in sisfall if e.queda]
    queda = min(quedas, key=lambda e: abs(magnitude(e, 16.0).max() - 11.0))   # passa bem de 8 g
    corridas = [e for e in sisfall if e.codigo == "D04"]
    picos = np.array([magnitude(e, 16.0).max() for e in corridas])
    corrida = corridas[int(np.argsort(picos)[len(picos) // 2])]              # corrida típica (mediana)

    estilos = ((16.0, EIXO, 3.2, "sem saturar (±16 g, como a SisFall)"),
               (8.0, TINTA2, 1.3, "cenário A: ±8 g por eixo (pulseira atual)"),
               (2.0, VIOLETA, 1.5, "cenário B: ±2 g por eixo (nossos ensaios)"))
    fig, eixos = plt.subplots(1, 2, figsize=(11, 4.2), sharey=True)
    for ax, e, titulo in ((eixos[0], queda, "Queda"), (eixos[1], corrida, "Corrida típica")):
        bruto = magnitude(e, 16.0)
        centro = int(np.argmax(bruto))
        idx = np.arange(max(0, centro - int(1.5 * FS)), min(len(bruto), centro + int(1.5 * FS)))
        t = (idx - centro) / FS
        texto = []
        for g, cor, lw, _ in estilos:
            m = magnitude(e, g)[idx]
            ax.plot(t, m, color=cor, lw=lw)
            texto.append(f"±{g:g} g: pico {br(m.max())} g")
        ax.text(0.98, 0.95, "\n".join(texto), transform=ax.transAxes, ha="right", va="top", fontsize=8.5,
                color=TINTA)
        ax.set_title(f"{titulo}: {e.codigo} ({ATIVIDADES_SISFALL[e.codigo]}), {e.pessoa}")
        ax.set_xlabel("tempo em relação ao pico (s)")
    eixos[0].set_ylabel("|a| filtrado (g)")
    fig.legend(handles=[Line2D([], [], color=c, lw=lw, label=r) for _, c, lw, r in estilos], loc="upper left",
               ncol=3, bbox_to_anchor=(0.01, 0.93))
    fig.suptitle("Em ±2 g os picos da queda e da corrida ficam quase iguais (o teto é 2√3 ≈ 3,46 g): sobram a "
                 "forma e o que vem depois do impacto", x=0.01, ha="left", fontsize=11, color=TINTA)
    fig.tight_layout(rect=(0, 0, 1, 0.88))
    salvar(fig, "fig_saturacao.png")


def fig_funil(conj, escores, comp):
    lim_rf = avaliacao.metricas(escores["random_forest"], conj).limiar
    quedas = [e for e in conj.ensaios if e["queda"]]
    passam = sum(e["pegou_impacto"] for e in quedas)
    detectadas = int((escores["random_forest"][conj.y == 1] >= lim_rf).sum())
    horas = sum(e["duracao_s"] for e in conj.ensaios if not e["queda"]) / 3600
    disparos_h = (conj.y == 0).sum() / horas
    rf_h = (escores["random_forest"][conj.y == 0] >= lim_rf).sum() / horas
    firmware_h = comp.loc["limiar_do_firmware (2,5 g)", "alarmes_h"]

    fig, (a1, a2) = plt.subplots(1, 2, figsize=(12, 3.4), gridspec_kw={"width_ratios": [1, 1.1], "wspace": 0.55})
    barras = [("ensaios de queda", len(quedas)), ("passam pelo gatilho (2 g)", passam),
              ("Random Forest diz \"queda\"", detectadas)]
    a1.barh([b[0] for b in barras], [b[1] for b in barras], color=COR_QUEDA, height=0.55)
    for i, (_, v) in enumerate(barras):
        a1.text(v, i, f" {v} ({br(100 * v / len(quedas))} %)", va="center", color=TINTA)
    a1.set_xlim(0, len(quedas) * 1.32)
    a1.invert_yaxis()
    a1.grid(axis="y", visible=False)
    a1.set_title("Quedas (SisFall, 1798 ensaios)")
    a1.set_xlabel("ensaios")

    barras = [("disparos do gatilho", disparos_h), ("alarmes: limiar atual", firmware_h),
              ("alarmes: Random Forest", rf_h)]
    a2.barh([b[0] for b in barras], [b[1] for b in barras], color=[COR_ATIVIDADE, TINTA2, COR_ATIVIDADE],
            height=0.55)
    for i, (_, v) in enumerate(barras):
        a2.text(v, i, f" {br(v)}/h", va="center", color=TINTA)
    a2.set_xscale("log")
    a2.set_xlim(0.1, disparos_h * 20)
    a2.invert_yaxis()
    a2.grid(axis="y", visible=False)
    a2.set_title(f"Atividades do dia a dia ({br(horas)} h de SisFall)")
    a2.set_xlabel("por hora de atividade (escala log)")
    fig.suptitle(f"O gatilho acorda o classificador ~{disparos_h:.0f} vezes por hora; o classificador separa as "
                 "quedas (cenário A, validação por pessoa)", x=0.01, ha="left", fontsize=11, color=TINTA)
    fig.subplots_adjust(left=0.19, right=0.97, top=0.8, bottom=0.17)
    salvar(fig, "fig_funil.png")


def fig_escores(conj, F, nomes, escores, comp):
    paineis = [("pico de |a| (o que o limiar olha)", F[:, nomes.index("a_max")], 2.5, "limiar_do_firmware (2,5 g)"),
               ("correlação normalizada com templates", escores["corr_normalizada"], None, "corr_normalizada"),
               ("DTW com templates", escores["dtw"], None, "dtw"),
               ("AR(12): maior erro de predição", escores["ar12_anomalia"], None, "ar12_anomalia"),
               ("regressão logística (atributos)", escores["regressao_logistica"], None, "regressao_logistica"),
               ("Random Forest (atributos)", escores["random_forest"], None, "random_forest")]
    fig, eixos = plt.subplots(2, 3, figsize=(12, 6.4))
    for ax, (titulo, s, limiar, chave) in zip(eixos.flat, paineis):
        if limiar is None:
            limiar = avaliacao.metricas(s, conj).limiar
        lo, hi = np.percentile(s, [0.2, 99.8])
        bins = np.linspace(lo, hi, 50)
        for y, cor, rot in ((0, COR_ATIVIDADE, "janelas de atividade"), (1, COR_QUEDA, "janelas de queda")):
            v = np.clip(s[conj.y == y], lo, hi)
            pesos = np.full(len(v), 1 / len(v))
            ax.hist(v, bins=bins, weights=pesos, color=cor, alpha=0.45, label=rot)
            ax.hist(v, bins=bins, weights=pesos, color=cor, histtype="step", lw=1.2)
        ax.axvline(limiar, color=TINTA, ls="--", lw=1)
        ax.set_yscale("log")
        acima = 100 * (s[conj.y == 0] >= limiar).mean()
        ax.set_title(f"{titulo}\nacima do limiar: {br(acima)} % das atividades "
                     f"({br(comp.loc[chave, 'alarmes_h'])}/h)", fontsize=9.5)
        ax.set_xlabel("escore (maior = mais parecido com queda)")
    eixos[0, 0].set_ylabel("fração das janelas (log)")
    eixos[1, 0].set_ylabel("fração das janelas (log)")
    eixos[0, 0].legend(loc="lower left")
    fig.suptitle("Quanto menos as duas cores se sobrepõem, menos alarmes falsos. A linha tracejada é o limiar de "
                 "operação (95 % das quedas acima)", x=0.01, ha="left", fontsize=11, color=TINTA)
    fig.tight_layout()
    salvar(fig, "fig_escores.png")


def _dispersao(ax, conj, x, y, nome_x, nome_y, validas, log_y=False):
    adl = conj.y == 0
    brusco = adl & np.isin(conj.codigo, BRUSCOS)
    for sel, cor, rot, s, alfa in ((brusco, COR_BRUSCO, "correr, escada rápida, tropeço, pulo", 4, 0.2),
                                   (adl & ~brusco, COR_ATIVIDADE, "outras atividades", 5, 0.4),
                                   (conj.y == 1, COR_QUEDA, "quedas", 7, 0.45)):
        sel = sel & validas
        ax.scatter(x[sel], y[sel], s=s, color=cor, alpha=alfa, lw=0, label=f"{rot} ({sel.sum()} janelas)")
    ax.set_xlabel(nome_x)
    ax.set_ylabel(nome_y)
    if log_y:
        ax.set_yscale("log")


def fig_separacao(conj, F, nomes):
    col = {n: F[:, nomes.index(n)] for n in nomes}
    # Janelas que acabam depois do fim do ensaio repetem a última amostra: o desvio no fim dá 0 e não diz nada.
    validas = col["pos_std"] > 1e-4
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(11.5, 4.9))
    _dispersao(a1, conj, col["a_max"], col["pos_std"], "pico de |a| (g)",
               "desvio de |a| nos últimos 0,3 s (g, log)", validas, log_y=True)
    a1.axvline(2.5, color=TINTA, ls="--", lw=1)
    a1.text(2.6, 2.5, "limiar atual (2,5 g)", fontsize=8, color=TINTA)
    a1.set_title("O pico sozinho não separa; a quietude depois do impacto, sim")
    _dispersao(a2, conj, col["inclinacao_graus"], col["pos_std"], "mudança de inclinação (graus)",
               "desvio de |a| nos últimos 0,3 s (g, log)", validas, log_y=True)
    a2.set_title("Os dois atributos mais usados pelo Random Forest")
    leg = a1.legend(loc="lower right", markerscale=2.5)
    for h in leg.legend_handles:
        h.set_alpha(1)
    fora = int((~validas).sum())
    fig.suptitle("Cada ponto é uma janela que passou pelo gatilho (SisFall, ±8 g). Quedas: impacto forte, "
                 "depois corpo parado e deitado", x=0.01, ha="left", fontsize=11, color=TINTA)
    fig.text(0.01, 0.01, f"{fora} janelas no fim do ensaio (sem 0,3 s depois do pico) ficaram fora do gráfico.",
             fontsize=8, color=TINTA2)
    fig.tight_layout(rect=(0, 0.03, 1, 1))
    salvar(fig, "fig_separacao.png")


def fig_atributos_rf(conj, F, nomes, comp):
    rf = modelos.Sklearn("random_forest").ajustar(conj, F).modelo
    imp = pd.Series(rf.feature_importances_, index=nomes).sort_values(ascending=False)
    print("   importância (top 10):", ", ".join(f"{k} {v:.3f}" for k, v in imp.head(10).items()))
    top = imp.head(15)[::-1]
    cores = {"tempo": AZUL, "FFT": VIOLETA, "AR": AMARELO}
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(12, 5), gridspec_kw={"width_ratios": [1.35, 1]})
    a1.barh([nome_atr(n) for n in top.index], 100 * top.values, color=[cores[grupo_atr(n)] for n in top.index],
            height=0.65)
    a1.grid(axis="y", visible=False)
    a1.set_xlabel("importância no Random Forest (% da redução de impureza)")
    a1.set_title("Os 15 atributos que o Random Forest mais usa")
    a1.legend(handles=[Patch(color=c, label=f"atributo {g}") for g, c in cores.items()], loc="lower right")

    abl = [("só AR(4)", "rf_so_ar", AMARELO), ("só FFT", "rf_so_freq", VIOLETA), ("só tempo", "rf_so_tempo", AZUL),
           ("tempo + FFT", "rf_tempo_freq", AZUL), ("tempo + FFT + AR", "random_forest", AZUL)]
    v = [100 * comp.loc[k, "sens_1alarme_h"] for _, k, _ in abl]
    a2.barh([r for r, _, _ in abl], v, color=[c for _, _, c in abl], height=0.6)
    for i, x in enumerate(v):
        a2.text(x, i, f" {br(x)} %", va="center", color=TINTA)
    a2.set_xlim(0, 118)
    a2.grid(axis="y", visible=False)
    a2.set_xlabel("quedas detectadas com ≤ 1 alarme falso/h (%)")
    a2.set_title("Ablação: Random Forest com cada grupo")
    fig.suptitle("O que separa queda de corrida está no tempo: queda livre, impacto e o que vem depois",
                 x=0.01, ha="left", fontsize=11, color=TINTA)
    fig.tight_layout()
    salvar(fig, "fig_atributos_rf.png")


def fig_ar(conj, escores):
    p = int(pd.read_csv(R / "ordem_ar_A.csv").sort_values("bic").iloc[0].p)
    ar = modelos.ARAnomalia(p).ajustar(conj)
    s = escores[f"ar{p}_anomalia"]
    limiar = avaliacao.metricas(s, conj).limiar
    x = conj.reduzidas[:, 0, :].astype(np.float64)
    A, b = modelos._regressores(x, None, p)
    pred = A @ ar.coef
    pre = int(PRE_S * FS_REDUZIDA)
    t = (np.arange(p, x.shape[1]) - pre) / FS_REDUZIDA
    exemplos = [("D02", "andar rápido"), ("D04", "correr rápido"), ("F01", "queda de frente")]
    fig, eixos = plt.subplots(2, 3, figsize=(12, 5.2), sharex=True, gridspec_kw={"height_ratios": [1.4, 1]})
    for j, (c, nome) in enumerate(exemplos):
        idx = np.flatnonzero(conj.codigo == c)
        k = idx[np.argsort(s[idx])[len(idx) // 2]]                 # janela típica: escore mediano do tipo
        cor = COR_QUEDA if c.startswith("F") else COR_BRUSCO if c in BRUSCOS else COR_ATIVIDADE
        ax = eixos[0, j]
        ax.plot(t, b[k], color=cor, lw=1.8, label="|a| medido")
        ax.plot(t, pred[k], color=TINTA, lw=1, ls="--", label=f"previsão do AR({p}), um passo")
        ax.set_title(f"{c}: {nome}")
        e = np.abs(b[k] - pred[k])
        ax = eixos[1, j]
        ax.fill_between(t, e, color=cor, alpha=0.5, lw=0)
        ax.axhline(limiar, color=TINTA, ls="--", lw=1)
        alarme = e.max() >= limiar
        ax.text(0.02, 0.92, f"maior erro {br(e.max(), 2)} g → {'ALARME' if alarme else 'sem alarme'}",
                transform=ax.transAxes, va="top", fontsize=8.5, color=TINTA)
        ax.set_xlabel("tempo em relação ao pico (s)")
    eixos[0, 0].set_ylabel("|a| a 50 Hz (g)")
    eixos[1, 0].set_ylabel("|erro de predição| (g)")
    fig.legend(handles=[Line2D([], [], color=TINTA2, lw=1.8, label="|a| medido"),
                        Line2D([], [], color=TINTA, lw=1, ls="--", label=f"previsão do AR({p}), um passo à frente"),
                        Line2D([], [], color=TINTA, lw=1, ls="--", label="limiar de operação (painéis de baixo)")],
               loc="upper left", ncol=3, bbox_to_anchor=(0.01, 0.94))
    fig.suptitle(f"AR({p}) treinado só em atividades: ele erra no impacto da queda, mas também em qualquer "
                 f"pico forte, como na corrida", x=0.01, ha="left", fontsize=11, color=TINTA)
    fig.tight_layout(rect=(0, 0, 1, 0.9))
    salvar(fig, "fig_ar_explicado.png")


def _melhor_atraso(x, r, max_lag=10):
    n, melhor = len(x), (-np.inf, 0)
    for m in range(-max_lag, max_lag + 1):
        xs, rs = x[max(0, m):n + min(0, m)], r[max(0, -m):n - max(0, m)]
        v = np.corrcoef(xs, rs)[0, 1]
        melhor = max(melhor, (v, m))
    return melhor


def fig_templates(conj, escores):
    tpl = modelos.Templates("corr_norm").ajustar(conj)
    z = modelos._znorm
    pre = int(PRE_S * FS_REDUZIDA)
    n = conj.reduzidas.shape[2]
    t = (np.arange(n) - pre) / FS_REDUZIDA
    s = escores["corr_normalizada"]
    limiar = avaliacao.metricas(s, conj).limiar
    x = conj.reduzidas[:, 0, :].astype(np.float64)

    fig, eixos = plt.subplots(2, 2, figsize=(11.5, 6.2), sharex=True)
    for ax, ts, cor, titulo in ((eixos[0, 0], tpl.t_queda, COR_QUEDA, "8 templates de queda (medoides)"),
                                (eixos[0, 1], tpl.t_adl, COR_ATIVIDADE, "8 templates de atividade (medoides)")):
        for r in ts:
            ax.plot(t, z(r), color=cor, alpha=0.6, lw=1)
        ax.set_title(titulo)
    eixos[0, 0].set_ylabel("|a| padronizado")

    def exemplo(ax, sel, cor, titulo):
        idx = np.flatnonzero(sel)
        k = idx[np.argsort(s[idx])[len(idx) // 2]]
        vals = [_melhor_atraso(x[k], r) for r in tpl.t_queda]
        r_adl = max(_melhor_atraso(x[k], r)[0] for r in tpl.t_adl)
        j = int(np.argmax([v for v, _ in vals]))
        v, m = vals[j]
        r = tpl.t_queda[j]
        ax.plot(t, z(x[k]), color=cor, lw=1.8, label="janela")
        lo, hi = max(0, m), n + min(0, m)
        ax.plot(t[lo:hi], z(r)[max(0, -m):n - max(0, m)], color=TINTA, ls="--", lw=1,
                label="template de queda mais parecido")
        ax.set_title(f"{titulo}\nr com queda = {br(v, 2)}; r com atividade = {br(r_adl, 2)} → "
                     f"escore {br(v - r_adl, 2)}", fontsize=9.5)
        ax.set_xlabel("tempo em relação ao pico (s)")
        ax.legend(loc="upper left")

    exemplo(eixos[1, 0], conj.y == 1, COR_QUEDA, "Queda típica")
    exemplo(eixos[1, 1], (conj.codigo == "D04") & (s >= limiar), COR_BRUSCO,
            "Corrida que deu alarme falso (típica)")
    eixos[1, 0].set_ylabel("|a| padronizado")
    fig.suptitle(f"Correlação normalizada compara só a forma de |a|. Escore = r com queda − r com atividade; "
                 f"alarme acima de {br(limiar, 2)}", x=0.01, ha="left", fontsize=11, color=TINTA)
    fig.tight_layout()
    salvar(fig, "fig_templates.png")


def fig_fft(conj):
    a = np.linalg.norm(conj.janelas[:, :, 0:3], axis=2)
    a = (a - a.mean(axis=1, keepdims=True)) * np.hanning(a.shape[1])
    esp = np.abs(np.fft.rfft(a, n=512, axis=1)) ** 2
    f = np.fft.rfftfreq(512, 1 / FS)
    util = f >= 0.5
    esp = esp / esp[:, util].sum(axis=1, keepdims=True)
    grupos = [("andar (D01, D02)", np.isin(conj.codigo, ("D01", "D02")), COR_ATIVIDADE),
              ("correr (D03, D04)", np.isin(conj.codigo, ("D03", "D04")), COR_BRUSCO),
              ("quedas", conj.y == 1, COR_QUEDA)]
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(12, 4.2), gridspec_kw={"width_ratios": [1.4, 1]})
    vis = (f >= 0.5) & (f <= 25)
    for rot, sel, cor in grupos:
        q25, q50, q75 = np.percentile(esp[sel][:, vis], [25, 50, 75], axis=0)
        a1.plot(f[vis], 100 * q50, color=cor, lw=1.8, label=f"{rot}: mediana ({sel.sum()} janelas)")
        a1.fill_between(f[vis], 100 * q25, 100 * q75, color=cor, alpha=0.15, lw=0)
    for lim in (3, 6, 12):
        a1.axvline(lim, color=EIXO, lw=1)
    a1.set_xlabel("frequência (Hz)")
    a1.set_ylabel("% da energia de |a| (por raia)")
    a1.set_title("Espectro de |a| na janela (Hann, FFT de 512)")
    a1.legend()

    bandas = atributos.BANDAS_HZ
    largura = 0.26
    for i, (rot, sel, cor) in enumerate(grupos):
        v = [100 * np.median(esp[sel][:, (f >= lo) & (f < hi)].sum(axis=1)) for lo, hi in bandas]
        a2.bar(np.arange(len(bandas)) + (i - 1) * largura, v, largura * 0.92, color=cor, label=rot)
    a2.set_xticks(range(len(bandas)), [f"{br(lo, 1 if lo % 1 else 0)}–{hi:g} Hz" for lo, hi in bandas])
    a2.grid(axis="x", visible=False)
    a2.legend(loc="upper right")
    a2.set_ylabel("% da energia (mediana)")
    a2.set_title("Energia por banda (os atributos de FFT)")
    fig.suptitle("A corrida é periódica (~2–3 Hz) e a queda é mais espalhada, mas as distribuições se "
                 "sobrepõem: só a FFT detecta 7,8 % das quedas com ≤ 1 alarme/h", x=0.01, ha="left",
                 fontsize=11, color=TINTA)
    fig.tight_layout()
    salvar(fig, "fig_fft.png")


def fig_erros_tipo(sisfall, conj, escores):
    df = pd.read_csv(R / "erros_por_tipo_A.csv")
    rf = df[df.metodo == "random_forest"].set_index("codigo")
    quedas = rf[rf.queda]
    codigos_adl = [c for c in rf.index if not rf.loc[c, "queda"]]
    # % dos ensaios de cada atividade com algum alarme falso (por hora engana: D18 e D19 duram ~15 s)
    lim_rf = avaliacao.metricas(escores["random_forest"], conj).limiar
    com_alarme_rf = set(conj.ensaio[(conj.y == 0) & (escores["random_forest"] >= lim_rf)])
    firmware, modelo = {}, {}
    for c in codigos_adl:
        ks = [k for k, i in enumerate(conj.ensaios) if i["codigo"] == c]
        firmware[c] = 100 * np.mean([len(eventos_firmware(sisfall[k], 8.0, 500.0)) > 0 for k in ks])
        modelo[c] = 100 * np.mean([k in com_alarme_rf for k in ks])
    pd.DataFrame({"codigo": codigos_adl, "atividade": [ATIVIDADES_SISFALL[c] for c in codigos_adl],
                  "limiar_atual_pct": [firmware[c] for c in codigos_adl],
                  "random_forest_pct": [modelo[c] for c in codigos_adl]}
                 ).to_csv(R / "ensaios_com_alarme_A.csv", index=False, float_format="%.1f")

    fig, (a1, a2) = plt.subplots(1, 2, figsize=(14.5, 6.4), gridspec_kw={"width_ratios": [1, 1.1], "wspace": 0.95})
    pct = 100 * quedas.detectadas / quedas.ensaios
    rot = [f"{c} {ATIVIDADES_SISFALL[c]}" for c in quedas.index]
    a1.barh(rot, pct, color=COR_QUEDA, height=0.65)
    for i, (c, v) in enumerate(pct.items()):
        a1.text(v, i, f" {br(v, 0)} % ({int(quedas.loc[c, 'perdidas'])} perdida{'' if quedas.loc[c, 'perdidas'] == 1 else 's'})", va="center", fontsize=8,
                color=TINTA)
    a1.set_xlim(0, 125)
    a1.invert_yaxis()
    a1.grid(axis="y", visible=False)
    a1.set_xlabel("quedas detectadas pelo Random Forest (%)")
    a1.set_title("Quedas: as mais difíceis começam\nsentado ou ao sentar (F10, F13, F14)")

    rot = [f"{c} {ATIVIDADES_SISFALL[c]}" for c in codigos_adl]
    y = np.arange(len(codigos_adl))
    a2.barh(y - 0.2, [firmware[c] for c in codigos_adl], 0.38, color=TINTA2, label="limiar atual (2,5 g)")
    a2.barh(y + 0.2, [modelo[c] for c in codigos_adl], 0.38, color=COR_ATIVIDADE, label="Random Forest")
    for i, c in enumerate(codigos_adl):
        a2.text(max(firmware[c], modelo[c]), i, f"  {br(firmware[c], 0)} % → {br(modelo[c], 0)} %",
                va="center", fontsize=8, color=TINTA)
    a2.set_yticks(y, rot)
    a2.set_xlim(0, 135)
    a2.invert_yaxis()
    a2.grid(axis="y", visible=False)
    a2.set_xlabel("ensaios com algum alarme falso (%)")
    a2.set_title("Atividades: ensaios com alarme falso\n(limiar atual → Random Forest)")
    a2.legend(loc="lower right", bbox_to_anchor=(1.0, -0.17), ncol=2)
    fig.suptitle("Erros por tipo (SisFall, ±8 g, validação por pessoa, mesmo ponto de operação da tabela)",
                 x=0.01, ha="left", fontsize=11, color=TINTA)
    fig.subplots_adjust(left=0.2, right=0.98, top=0.86, bottom=0.12)
    salvar(fig, "fig_erros_tipo.png")


# ---------------------------------------------------------------------------------------------------------
# Ensaios próprios (cenário B, ±2 g)
# ---------------------------------------------------------------------------------------------------------

ORDEM_PROPRIOS = ["andando", "correndo", "caindo_costas", "caindo_frente", "caindo_joelho"]
NOME_PROPRIO = {"andando": "andando", "correndo": "correndo", "caindo_costas": "queda de costas",
                "caindo_frente": "queda de frente", "caindo_joelho": "queda de joelhos"}


def decisoes_rf_proprios(sisfall, proprios, l_on):
    """O mesmo procedimento de 04_dados_proprios.py, só para o Random Forest."""
    treino, teste = montar(sisfall, "B", l_on), montar(proprios, "B", l_on)
    # a amostra do pico na janela: o mesmo PRE_S que recorta a janela (pipeline) e que o 09 exporta
    pre = int(PRE_S * FS)
    F_tr, nomes = atributos.calcular(treino.janelas, pre)
    F_te, _ = atributos.calcular(teste.janelas, pre)
    cols = [i for i, n in enumerate(nomes) if n not in {"corr_xy", "corr_xz", "corr_yz"}]
    F_tr, F_te = F_tr[:, cols], F_te[:, cols]
    fabrica = lambda: modelos.Sklearn("random_forest")          # noqa: E731
    oof, _, _ = avaliacao.escores_fora_da_dobra(fabrica, treino, F_tr, avaliacao.dobras(treino))
    limiar = avaliacao.metricas(oof, treino).limiar
    alarme = fabrica().ajustar(treino, F_tr).escore(teste, F_te) >= limiar
    return teste, alarme


def eventos_firmware(e, g_max=2.0, dps_max=250.0):
    """Picos dos eventos do detector atual (o mesmo de avaliacao.detector_do_firmware)."""
    acc, _ = saturar(e.acc, e.gyr, g_max, dps_max)
    a2 = (acc.astype(np.float64) ** 2).sum(axis=1)
    return gatilho_histerese(a2, 6.25, 2.25, max_evento_s=1.0, refratario_s=30.0)


def fig_proprios(sisfall, proprios, l_on):
    teste, alarme = decisoes_rf_proprios(sisfall, proprios, l_on)
    csv = pd.read_csv(R / "proprios_por_arquivo.csv")
    por_nome = {e.arquivo: e for e in proprios}
    janelas_de = {k: np.flatnonzero(teste.ensaio == k) for k in range(len(teste.ensaios))}
    # o RF refeito aqui tem de dar o mesmo que 04_dados_proprios.py, arquivo a arquivo
    for k, info in enumerate(teste.ensaios):
        esperado = int(csv.loc[csv.arquivo == info["arquivo"], "random_forest"].iloc[0])
        assert int(alarme[janelas_de[k]].sum()) == esperado, info["arquivo"]
    print("   Random Forest refeito confere com proprios_por_arquivo.csv")

    fig, eixos = plt.subplots(5, 4, figsize=(13, 11), sharey=True)
    for linha, rotulo in enumerate(ORDEM_PROPRIOS):
        ks = sorted((k for k, i in enumerate(teste.ensaios) if i["codigo"] == rotulo),
                    key=lambda k: teste.ensaios[k]["arquivo"])
        for col in range(4):
            ax = eixos[linha, col]
            if col >= len(ks):
                ax.set_visible(False)
                continue
            k = ks[col]
            info = teste.ensaios[k]
            e = por_nome[info["arquivo"]]
            a = magnitude(e, 2.0, 250.0)
            t = np.arange(len(a)) / FS
            ax.plot(t, a, color=COR_QUEDA if e.queda else COR_ATIVIDADE, lw=1)
            ax.axhline(l_on, color=MUDO, ls=":", lw=1)
            ax.axhline(2 * np.sqrt(3), color=EIXO, lw=1)
            picos = gatilho(a, l_on)
            if e.queda:
                # Como no montar: o disparo mais perto do impacto, ou nenhum se o gatilho o perdeu.
                impacto = int(np.argmax(a))
                perto = [p for p in picos if abs(p - impacto) <= 0.5 * FS]
                picos_janela = [min(perto, key=lambda p: abs(p - impacto))] if perto else []
            else:
                picos_janela = picos
            assert len(picos_janela) == len(janelas_de[k])
            for p, w in zip(picos_janela, janelas_de[k]):
                if alarme[w]:
                    ax.plot(t[p], a[p] + 0.25, marker="v", ms=8, color=TINTA, ls="none")
                else:
                    ax.plot(t[p], a[p] + 0.25, marker="o", ms=6, mfc="white", mec=TINTA2, ls="none")
            fw = eventos_firmware(e)
            for p in fw:
                ax.plot(t[p], 0.35, marker="^", ms=7, color=CRITICO, ls="none")
            rf_n = int(alarme[janelas_de[k]].sum())
            if e.queda:
                resumo = f"limiar: {'detectou' if fw else 'perdeu'} · RF: {'detectou' if rf_n else 'perdeu'}"
            else:
                resumo = f"limiar: {len(fw)} alarme(s) · RF: {rf_n}"
            ax.set_title(f"{NOME_PROPRIO[rotulo]} {e.rep}\n{resumo}", fontsize=9)
            ax.set_ylim(0, 4.1)
            ax.tick_params(labelsize=8)
            if col == 0:
                ax.set_ylabel("|a| (g)")
            if linha == 4 or (linha == 3 and col == 3):
                ax.set_xlabel("tempo (s)")
    marcas = [Line2D([], [], color=COR_ATIVIDADE, label="|a| de atividade"),
              Line2D([], [], color=COR_QUEDA, label="|a| de queda"),
              Line2D([], [], color=MUDO, ls=":", label=f"gatilho ({br(l_on)} g)"),
              Line2D([], [], color=EIXO, label="saturação (3,46 g)"),
              Line2D([], [], marker="v", color=TINTA, ls="none", label="disparo: RF diz queda"),
              Line2D([], [], marker="o", mfc="white", mec=TINTA2, ls="none", label="disparo: RF diz não queda"),
              Line2D([], [], marker="^", color=CRITICO, ls="none", label="alarme do limiar atual (2,5 g)")]
    eixos[2, 3].legend(handles=marcas, loc="center", fontsize=8.5)
    eixos[2, 3].set_visible(True)
    eixos[2, 3].axis("off")
    fig.suptitle("Nossos 18 ensaios (cintura, ±2 g), com o Random Forest treinado só na SisFall: "
                 "nenhum alarme ao andar ou correr; perde as quedas de joelhos", x=0.01, ha="left",
                 fontsize=11.5, color=TINTA)
    fig.tight_layout()
    salvar(fig, "fig_proprios_ensaios.png")
    return teste


def fig_proprios_matriz(proprios):
    csv = pd.read_csv(R / "proprios_por_arquivo.csv")
    metodos = [("limiar atual", None), ("regras", "regras_cascata"), ("correlação\nnormalizada", "corr_normalizada"),
               ("DTW", "dtw"), ("AR(12)\nanomalia", "ar12_anomalia"), ("centroide\ncosseno", "centroide_cosseno"),
               ("kNN\nManhattan", "knn_manhattan"), ("regressão\nlogística", "regressao_logistica"),
               ("SVM", "svm_rbf"), ("Random\nForest", "random_forest"), ("CNN 1D", "cnn_1d")]
    por_nome = {e.arquivo: e for e in proprios}
    csv["ordem"] = csv.codigo.map(ORDEM_PROPRIOS.index)
    csv = csv.sort_values(["ordem", "arquivo"]).reset_index(drop=True)
    fig, ax = plt.subplots(figsize=(12, 7.6))
    for i, l in csv.iterrows():
        e = por_nome[l.arquivo]
        for j, (_, chave) in enumerate(metodos):
            n = len(eventos_firmware(e)) if chave is None else int(l[chave])
            if l.queda:
                certo, texto = n > 0, ("detectou" if n > 0 else "perdeu")
            else:
                certo, texto = n == 0, ("0" if n == 0 else f"{n} alarme{'s' if n > 1 else ''}")
            ax.add_patch(plt.Rectangle((j + 0.04, i + 0.06), 0.92, 0.88, color=BOM if certo else CRITICO,
                                       alpha=0.9 if not certo else 0.75, lw=0))
            ax.text(j + 0.5, i + 0.5, texto, ha="center", va="center", fontsize=8, color="white")
    ax.set_xlim(0, len(metodos))
    ax.set_ylim(len(csv), 0)
    ax.set_xticks(np.arange(len(metodos)) + 0.5, [m for m, _ in metodos], fontsize=8.5)
    ax.xaxis.tick_top()
    rot = [f"{NOME_PROPRIO[c]} {por_nome[a].rep}" for a, c in zip(csv.arquivo, csv.codigo)]
    ax.set_yticks(np.arange(len(csv)) + 0.5, rot)
    ax.grid(False)
    for s in ax.spines.values():
        s.set_visible(False)
    ax.tick_params(length=0)
    totais = []
    for _, chave in metodos:
        if chave is None:
            n = [len(eventos_firmware(por_nome[a])) for a in csv.arquivo]
        else:
            n = list(csv[chave])
        q = sum(1 for v, qd in zip(n, csv.queda) if qd and v > 0)
        f = sum(v for v, qd in zip(n, csv.queda) if not qd)
        totais.append(f"{q}/{int(csv.queda.sum())} quedas\n{f} alarme{'' if f == 1 else 's'}")
    for j, txt in enumerate(totais):
        ax.text(j + 0.5, len(csv) + 0.15, txt, ha="center", va="top", fontsize=8, color=TINTA)
    ax.set_ylim(len(csv) + 1.3, 0)
    ax.legend(handles=[Patch(color=BOM, alpha=0.75, label="acertou"), Patch(color=CRITICO, label="errou")],
              loc="upper left", bbox_to_anchor=(1.0, 1.0))
    ax.set_title("Cada método treinado só na SisFall (±2 g) e testado nos nossos 18 ensaios; nas atividades, "
                 "o número de alarmes falsos", pad=38)
    fig.tight_layout()
    salvar(fig, "fig_proprios_matriz.png")


def main():
    sisfall, proprios = _comum.carregar()
    l_on = json.loads((R / "gatilho_escolhido.json").read_text())
    conj = montar(sisfall, "A", l_on["A"])
    F, nomes = atributos.calcular(conj.janelas, int(PRE_S * FS))
    escores = np.load(R / "escores_A.npz")
    assert np.array_equal(escores["y"], conj.y) and np.array_equal(escores["codigo"], conj.codigo), \
        "escores_A.npz não corresponde às janelas: rode 03_comparar.py --cenario A de novo"
    comp = pd.read_csv(R / "comparacao_A.csv").set_index("metodo")

    fig_saturacao(sisfall)
    fig_funil(conj, escores, comp)
    fig_escores(conj, F, nomes, escores, comp)
    fig_separacao(conj, F, nomes)
    fig_atributos_rf(conj, F, nomes, comp)
    fig_ar(conj, escores)
    fig_templates(conj, escores)
    fig_fft(conj)
    fig_erros_tipo(sisfall, conj, escores)
    fig_proprios(sisfall, proprios, l_on["B"])
    fig_proprios_matriz(proprios)


if __name__ == "__main__":
    main()
