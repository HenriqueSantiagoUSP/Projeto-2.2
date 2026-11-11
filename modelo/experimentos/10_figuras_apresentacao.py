"""
Figuras, tabelas e números da apresentação do Q1 (relatorios/Semestre 2/Projeto 3/Q1/apresentacao/). Rode depois
de 03, 04 e 09, que ele lê:

    uv run python experimentos/10_figuras_apresentacao.py

Depois, em apresentacao/: `pdflatex main.tex`, duas vezes.

As figuras são feitas para o slide 16:9: uma figura de largura L = 8,4 polegadas ocupa a largura útil do slide
(14 cm) com as letras em ~8 pt. Por isso a apresentação não reaproveita as figuras do relatório, desenhadas para
a página A4. Ela também não trata do limiar antigo do firmware: o limiar aparece só como o gatilho do pipeline.

Figuras, em resultados/ (prefixo ap_):
- ap_dados: |a| de uma queda, uma corrida e um sentar rápido da SisFall;
- ap_saturacao: a mesma queda e a mesma corrida sem saturar e saturadas em ±8 g e ±2 g;
- ap_filtro_bruto, ap_filtro_filtrado: o mesmo ensaio (F01 da SA01, ±8 g) antes e depois do filtro;
- ap_filtro_detalhe: o impacto de perto e a resposta em frequência do filtro sobre o espectro do sinal;
- ap_gatilho_exemplo: os disparos do gatilho num tropeço correndo (F05), um por passo e um no impacto;
- ap_gatilho_curva: quedas que passam × disparos por hora, em função de L_on;
- ap_janela: a janela da queda com os atributos que mais pesam;
- ap_comparacao: alarmes falsos/h e quedas com ≤ 1 alarme/h dos métodos, cenários A e B;
- ap_separacao: pico × quietude e inclinação × quietude;
- ap_arvore: uma árvore de decisão de profundidade 3 treinada nas janelas;
- ap_fronteira: fronteira de uma árvore × da floresta, com dois atributos;
- ap_votos: as 60 árvores votando em três janelas da SA01 (modelo treinado sem a SA01);
- ap_importancia: importância dos atributos no Random Forest do vestível;
- ap_funil: o pipeline do vestível na SisFall, do gatilho à decisão.

Tabelas em apresentacao/tabelas/ (métodos, por que o Random Forest, nossos ensaios) e números em
apresentacao/numeros.tex: nenhum número dos slides é digitado à mão.

O Random Forest daqui é o do vestível (o mesmo do 09): o script confere que ele reproduz o limiar de
vestivel.json e as probabilidades dos vetores de teste do firmware.
"""

import importlib.util
import json
import shutil

import _comum
from _estilo import (AZUL, COR_ATIVIDADE, COR_BRUSCO, COR_QUEDA, EIXO, MUDO, R, TINTA, TINTA2, VERDE_AGUA, VIOLETA,
                     br, nome_atr, salvar)
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from matplotlib.colors import LinearSegmentedColormap, to_rgb
from matplotlib.lines import Line2D
from matplotlib.patches import Patch
from scipy import signal
from sklearn.ensemble import RandomForestClassifier
from sklearn.tree import DecisionTreeClassifier

from queda import atributos, avaliacao
from queda.dados import ATIVIDADES_SISFALL, FS, saturar
from queda.pipeline import CENARIOS, FC_PASSA_BAIXA, PRE_S, gatilho_causal, montar, passa_baixa

RAIZ = _comum.RAIZ_MODELO.parent
Q1 = RAIZ / "relatorios" / "Semestre 2" / "Projeto 3" / "Q1"
APRESENTACAO = Q1 / "apresentacao"
LOGO = Q1 / "latex" / "imagens" / "Logo-Escola-Politecnica.jpg"
ESPERADO = RAIZ / "firmware" / "gadget" / "components" / "fall_pipeline" / "test_host" / "vetores" / "esperado.csv"
PRE = int(PRE_S * FS)
L = 8.4                               # polegadas: a largura útil do slide 16:9 (14 cm) com letras de ~8 pt

plt.rcParams.update({
    "font.size": 12, "axes.titlesize": 12.5, "axes.labelsize": 11.5, "xtick.labelsize": 10.5,
    "ytick.labelsize": 10.5, "legend.fontsize": 10, "lines.linewidth": 1.6,
})

UNIDADE = {"a_mean": "g", "a_rms": "g", "a_std": "g", "a_min": "g", "a_max": "g", "a_pico_a_pico": "g", "sma": "g",
           "queda_livre_min": "g", "queda_livre_ate_impacto_s": "s", "pos_std": "g", "pos_media": "g",
           "inclinacao_graus": "°", "w_mean": "°/s", "w_rms": "°/s", "w_max": "°/s", "w_rotacao_graus": "°"}
NOME_CURTO = {"inclinacao_graus": "inclinação", "a_assimetria": "assimetria de |a|", "sma": "SMA",
              "pos_std": "quietude no fim", "w_rotacao_graus": "rotação total", "a_curtose": "curtose de |a|",
              "pos_media": "|a| médio no fim", "a_crest": "fator de crista", "a_max": "pico de |a|"}
CORES_EIXOS = (AZUL, VERDE_AGUA, VIOLETA)


def carregar_script(nome):
    """Importa um script de experimentos/ cujo nome começa com número (07, 09)."""
    spec = importlib.util.spec_from_file_location(nome.removesuffix(".py"), _comum.RAIZ_MODELO / "experimentos" / nome)
    modulo = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(modulo)
    return modulo


M07 = carregar_script("07_figuras_explicativas.py")
M09 = carregar_script("09_exportar_vestivel.py")
plt.rcParams.update({"font.size": 12})        # o 07 importa o _estilo de novo; vale o tamanho do slide


def mistura(p):
    """Cor entre atividade (p = 0) e queda (p = 1), clareada para fundo de caixa."""
    a, q = np.array(to_rgb(COR_ATIVIDADE)), np.array(to_rgb(COR_QUEDA))
    return tuple(0.38 * ((1 - p) * a + p * q) + 0.62)


def ensaio(sisfall, codigo, pessoa="SA01", rep=1):
    k = next(i for i, e in enumerate(sisfall) if (e.codigo, e.pessoa, e.rep) == (codigo, pessoa, rep))
    return k, sisfall[k]


def sinais(e, g_max=8.0, dps_max=500.0):
    """Aceleração bruta (saturada na faixa) e filtrada, em g."""
    acc, gyr = saturar(e.acc, e.gyr, g_max, dps_max)
    return acc, passa_baixa(np.hstack([acc, gyr]))[:, 0:3]


def sem_grade_y(ax):
    ax.grid(axis="y", visible=False)


# ---------------------------------------------------------------------------------------------------------
# Dados
# ---------------------------------------------------------------------------------------------------------

def fig_dados(sisfall):
    exemplos = [("F01", "Queda", COR_QUEDA), ("D04", "Correr rápido", COR_BRUSCO),
                ("D10", "Sentar rápido", COR_ATIVIDADE)]
    fig, eixos = plt.subplots(1, 3, figsize=(L, 2.55), sharey=True)
    for ax, (codigo, titulo, cor) in zip(eixos, exemplos):
        _, e = ensaio(sisfall, codigo)
        a = np.linalg.norm(sinais(e)[1], axis=1)
        c = int(np.argmax(a))
        idx = np.arange(max(0, c - int(1.5 * FS)), min(len(a), c + int(1.5 * FS)))
        ax.plot((idx - c) / FS, a[idx], color=cor, lw=1.4)
        ax.text(0.03, 0.95, f"pico {br(a[c])} g", transform=ax.transAxes, va="top", fontsize=10, color=TINTA)
        ax.set_title(f"{titulo} ({codigo})", fontsize=11)
        ax.set_xlabel("tempo (s)")
    eixos[0].set_ylabel("|a| (g)")
    fig.tight_layout(w_pad=0.6)
    salvar(fig, "ap_dados.png")


def fig_saturacao(sisfall):
    quedas = [e for e in sisfall if e.queda]
    queda = min(quedas, key=lambda e: abs(M07.magnitude(e, 16.0).max() - 11.0))   # passa bem de 8 g
    corridas = [e for e in sisfall if e.codigo == "D04"]
    picos = np.array([M07.magnitude(e, 16.0).max() for e in corridas])
    corrida = corridas[int(np.argsort(picos)[len(picos) // 2])]
    estilos = ((16.0, EIXO, 3.0, "sem saturar (±16 g)"), (8.0, TINTA2, 1.3, "±8 g (cenário A)"),
               (2.0, VIOLETA, 1.6, "±2 g (cenário B)"))
    fig, eixos = plt.subplots(1, 2, figsize=(L, 2.75), sharey=True)
    for ax, e, titulo in ((eixos[0], queda, "Uma queda"), (eixos[1], corrida, "Uma corrida típica")):
        bruto = M07.magnitude(e, 16.0)
        c = int(np.argmax(bruto))
        idx = np.arange(max(0, c - int(1.0 * FS)), min(len(bruto), c + int(1.0 * FS)))
        texto = []
        for g, cor, lw, _ in estilos:
            m = M07.magnitude(e, g)[idx]
            ax.plot((idx - c) / FS, m, color=cor, lw=lw)
            texto.append(f"±{g:g} g: {br(m.max())} g")
        ax.text(0.98, 0.95, "picos\n" + "\n".join(texto), transform=ax.transAxes, ha="right", va="top", fontsize=9,
                color=TINTA)
        ax.set_title(f"{titulo} ({e.codigo})", fontsize=11)
        ax.set_xlabel("tempo em relação ao pico (s)")
    eixos[0].set_ylabel("|a| filtrado (g)")
    fig.legend(handles=[Line2D([], [], color=c, lw=lw, label=r) for _, c, lw, r in estilos], loc="lower center",
               ncol=3, bbox_to_anchor=(0.5, -0.01))
    fig.tight_layout(rect=(0, 0.09, 1, 1))
    salvar(fig, "ap_saturacao.png")


# ---------------------------------------------------------------------------------------------------------
# Filtro
# ---------------------------------------------------------------------------------------------------------

def fig_filtro(e):
    acc, filtrado = sinais(e)
    t = np.arange(len(acc)) / FS
    a_bruto, a_filt = np.linalg.norm(acc, axis=1), np.linalg.norm(filtrado, axis=1)
    centro = int(np.argmax(a_filt))
    zoom = (t[centro] - 0.5, t[centro] + 0.5)
    lim_eixos, lim_a = (acc.min() * 1.1, acc.max() * 1.1), (0, a_bruto.max() * 1.15)

    for nome, mostra_filtrado in (("ap_filtro_bruto.png", False), ("ap_filtro_filtrado.png", True)):
        fig, (a1, a2) = plt.subplots(2, 1, figsize=(L, 3.65), sharex=True)
        for k, (cor, rot) in enumerate(zip(CORES_EIXOS, ("x", "y", "z"))):
            if mostra_filtrado:
                a1.plot(t, acc[:, k], color=EIXO, lw=0.6)
                a1.plot(t, filtrado[:, k], color=cor, lw=1.1, label=f"eixo {rot}")
            else:
                a1.plot(t, acc[:, k], color=cor, lw=0.7, label=f"eixo {rot}")
        a1.set_ylim(*lim_eixos)
        a1.set_ylabel("aceleração (g)")
        a1.legend(loc="upper left", ncol=3, fontsize=9.5)
        if mostra_filtrado:
            a2.plot(t, a_bruto, color=EIXO, lw=0.7, label="bruto")
            a2.plot(t, a_filt, color=COR_QUEDA, lw=1.4, label="filtrado")
            i_pico, rotulo = int(np.argmax(a_filt)), f"pico filtrado: {br(a_filt.max())} g"
        else:
            a2.plot(t, a_bruto, color=TINTA2, lw=0.8, label="bruto")
            i_pico, rotulo = int(np.argmax(a_bruto)), f"pico bruto: {br(a_bruto.max())} g"
        a2.annotate(rotulo, (t[i_pico], (a_filt if mostra_filtrado else a_bruto)[i_pico]), xytext=(14, -8),
                    textcoords="offset points", fontsize=10, color=TINTA)
        a2.set_ylim(*lim_a)
        a2.set_ylabel("|a| (g)")
        a2.set_xlabel("tempo (s)")
        a2.legend(loc="upper left", fontsize=9.5)
        for ax in (a1, a2):
            ax.axvspan(*zoom, color=EIXO, alpha=0.2, lw=0)
        fig.tight_layout(h_pad=0.4)
        salvar(fig, nome)

    fig, (a1, a2) = plt.subplots(1, 2, figsize=(L, 2.9), gridspec_kw={"width_ratios": [1.1, 1]})
    idx = (t >= zoom[0]) & (t <= zoom[1])
    a1.plot(t[idx] - t[centro], a_bruto[idx], "o-", color=EIXO, ms=2.2, lw=0.8, label="bruto (1 ponto a cada 5 ms)")
    a1.plot(t[idx] - t[centro], a_filt[idx], color=COR_QUEDA, lw=2, label="filtrado")
    a1.set_xlabel("tempo em relação ao impacto (s)")
    a1.set_ylabel("|a| (g)")
    a1.set_title("O impacto de perto")
    a1.legend(loc="upper left", fontsize=9)

    b, a = signal.butter(2, FC_PASSA_BAIXA / (FS / 2))
    f_h, h = signal.freqz(b, a, worN=1024, fs=FS)
    f_p, p_bruto = signal.welch(a_bruto - a_bruto.mean(), fs=FS, nperseg=256)
    _, p_filt = signal.welch(a_filt - a_filt.mean(), fs=FS, nperseg=256)
    ref = p_bruto.max()
    a2.axvspan(FC_PASSA_BAIXA, FS / 2, color=EIXO, alpha=0.18, lw=0)
    a2.plot(f_p, 10 * np.log10(p_bruto / ref), color=EIXO, lw=1.3, label="espectro do bruto")
    a2.plot(f_p, 10 * np.log10(p_filt / ref), color=COR_QUEDA, lw=1.5, label="espectro do filtrado")
    a2.plot(f_h, 20 * np.log10(np.maximum(np.abs(h), 1e-6)), color=TINTA, ls="--", lw=1.3, label="ganho do filtro")
    a2.axvline(FC_PASSA_BAIXA, color=TINTA2, lw=0.8, ls=":")
    a2.annotate("−3 dB em 20 Hz", (FC_PASSA_BAIXA, -3), xytext=(6, 4), textcoords="offset points", fontsize=9.5,
                color=TINTA)
    a2.set_xlim(0, FS / 2)
    a2.set_ylim(-60, 5)
    a2.set_xlabel("frequência (Hz)")
    a2.set_ylabel("dB")
    a2.set_title("Em frequência")
    a2.legend(loc="lower left", fontsize=8.5)
    fig.tight_layout(w_pad=1.2)
    salvar(fig, "ap_filtro_detalhe.png")
    return {"picoBruto": a_bruto.max(), "picoFiltrado": a_filt.max()}


# ---------------------------------------------------------------------------------------------------------
# Gatilho, janela e atributos
# ---------------------------------------------------------------------------------------------------------

def fig_gatilho_exemplo(e, l_on):
    a = np.linalg.norm(sinais(e)[1], axis=1)
    t = np.arange(len(a)) / FS
    picos = gatilho_causal(a, l_on)
    impacto = int(np.argmax(a))
    fig, ax = plt.subplots(figsize=(L / 2, 3.1))
    ax.plot(t, a, color=TINTA, lw=1.0)
    ax.axhline(l_on, color=COR_QUEDA, ls="--", lw=1.1)
    ax.text(t[0], l_on + 0.15, f"L_on = {br(l_on)} g", fontsize=9.5, color=COR_QUEDA)
    for p in picos:
        cor = COR_QUEDA if p == impacto else COR_ATIVIDADE
        ax.plot(t[p], a[p], "v", color=cor, ms=8, mec="white", mew=0.6)
    ax.axvspan(t[impacto] - 1.0, t[impacto] + 0.8, color=COR_QUEDA, alpha=0.12, lw=0)
    ax.set_xlabel("tempo (s)")
    ax.set_ylabel("|a| filtrado (g)")
    ax.set_title(f"Tropeço correndo ({e.codigo}): {len(picos)} disparos", fontsize=11)
    ax.legend(handles=[Line2D([], [], ls="", marker="v", color=COR_ATIVIDADE, label="disparo: passos da corrida"),
                       Line2D([], [], ls="", marker="v", color=COR_QUEDA, label="disparo: impacto"),
                       Patch(color=COR_QUEDA, alpha=0.2, label="janela do impacto")], loc="upper left", fontsize=8.5)
    ax.set_ylim(0, a.max() * 1.45)
    fig.tight_layout()
    salvar(fig, "ap_gatilho_exemplo.png")
    return {"exDisparos": len(picos)}


def fig_gatilho_curva(disparos_causal_h):
    g = pd.read_csv(R / "gatilho.csv")
    g = g[(g.cenario == "A") & (g.base == "sisfall")]
    escolhido = json.loads((R / "gatilho_escolhido.json").read_text())["A"]
    linha = g[np.isclose(g.l_on_g, escolhido)].iloc[0]
    fig, a1 = plt.subplots(figsize=(L / 2, 3.1))
    a2 = a1.twinx()
    a1.plot(g.l_on_g, 100 * g.quedas_que_passam, "o-", color=COR_QUEDA, ms=4)
    a2.plot(g.l_on_g, g.disparos_adl_por_h, "s-", color=COR_ATIVIDADE, ms=4)
    a2.grid(False)
    a2.spines["right"].set_visible(True)
    a1.axvline(escolhido, color=TINTA2, ls=":", lw=1)
    a1.set_xlabel("L_on (g)")
    a1.set_ylabel("quedas que passam (%)", color=COR_QUEDA)
    a2.set_ylabel("disparos por hora", color=COR_ATIVIDADE)
    a1.text(1.52, 78.3, f"{br(escolhido)} g:\n{br(100 * linha.quedas_que_passam)} % das quedas\n"
            f"~{linha.disparos_adl_por_h:.0f} disparos/h", fontsize=9.5, color=TINTA, va="bottom")
    a1.set_title("Escolha de L_on (SisFall, ±8 g)", fontsize=11)
    fig.tight_layout()
    salvar(fig, "ap_gatilho_curva.png")


def fig_janela(conj, F, nomes, k_exemplo):
    i = int(np.flatnonzero((conj.ensaio == k_exemplo) & (conj.y == 1))[0])
    w = conj.janelas[i].astype(np.float64)
    a = np.linalg.norm(w[:, 0:3], axis=1)
    t = (np.arange(len(a)) - PRE) / FS
    f = dict(zip(nomes, F[i]))
    cabeca, cauda = int(0.5 * FS), int(0.3 * FS)

    fig, (a1, a2) = plt.subplots(2, 1, figsize=(L, 3.3), sharex=True, gridspec_kw={"height_ratios": [1.2, 1]})
    a1.plot(t, a, color=TINTA, lw=1.4)
    for ax in (a1, a2):
        ax.axvspan(t[0], t[cabeca - 1], color=COR_ATIVIDADE, alpha=0.10, lw=0)
        ax.axvspan(t[-cauda], t[-1], color=COR_QUEDA, alpha=0.12, lw=0)
        ax.axvline(0, color=EIXO, lw=0.8)
    i_min = int(np.argmin(a[:PRE]))
    a1.plot(t[i_min], a[i_min], "o", color=COR_ATIVIDADE)
    a1.annotate(f"queda livre: {br(f['queda_livre_min'], 2)} g,\n{br(f['queda_livre_ate_impacto_s'], 2)} s antes",
                (t[i_min], a[i_min]), xytext=(-0.72, 4.0), textcoords="data", fontsize=9.5, color=TINTA,
                arrowprops={"arrowstyle": "-", "color": TINTA2, "lw": 0.8})
    a1.plot(0, a[PRE], "v", color=COR_QUEDA, ms=8)
    a1.annotate(f"impacto: {br(f['a_max'])} g", (0, a[PRE]), xytext=(10, -6), textcoords="offset points",
                fontsize=9.5, color=TINTA)
    a1.text(t[-cauda] + 0.01, a.max() * 0.45, f"parado:\ndesvio\n{br(f['pos_std'], 3)} g", fontsize=9.5,
            color=TINTA)
    a1.text(t[0] + 0.01, a.max() * 0.72, "antes", fontsize=9.5, color=TINTA)
    a1.set_ylabel("|a| (g)")
    for k, (cor, rot) in enumerate(zip(CORES_EIXOS, ("x", "y", "z"))):
        a2.plot(t, w[:, k], color=cor, lw=1.1, label=f"eixo {rot}")
        a2.hlines(w[:cabeca, k].mean(), t[0], t[cabeca - 1], color=cor, lw=3.5, alpha=0.6)
        a2.hlines(w[-cauda:, k].mean(), t[-cauda], t[-1], color=cor, lw=3.5, alpha=0.6)
    a2.text(0.35, 5.0, f"a gravidade muda de eixo:\ninclinação de {f['inclinacao_graus']:.0f}°", fontsize=9.5,
            color=TINTA)
    a2.set_ylabel("eixos (g)")
    a2.set_xlabel("tempo em relação ao pico (s)")
    a2.legend(loc="lower left", ncol=3, fontsize=9)
    fig.tight_layout(h_pad=0.3)
    salvar(fig, "ap_janela.png")
    return {"exQuedaLivre": f["queda_livre_min"], "exPico": f["a_max"], "exInclinacao": f["inclinacao_graus"],
            "exPosStd": f["pos_std"]}


def fig_separacao(conj, F, nomes):
    col = {n: F[:, nomes.index(n)] for n in nomes}
    validas = col["pos_std"] > 1e-4      # janelas no fim do ensaio repetem a última amostra
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(L, 3.0))
    M07._dispersao(a1, conj, col["a_max"], col["pos_std"], "pico de |a| (g)", "quietude no fim (g)", validas,
                   log_y=True)
    M07._dispersao(a2, conj, col["inclinacao_graus"], col["pos_std"], "mudança de inclinação (graus)",
                   "quietude no fim (g)", validas, log_y=True)
    a1.set_title("Pico × quietude", fontsize=11)
    a2.set_title("Inclinação × quietude", fontsize=11)
    alcas = [Line2D([], [], ls="", marker="o", color=c, label=r) for c, r in
             ((COR_BRUSCO, "correr, escada rápida, tropeço, pulo"), (COR_ATIVIDADE, "outras atividades"),
              (COR_QUEDA, "quedas"))]
    fig.legend(handles=alcas, loc="lower center", ncol=3, bbox_to_anchor=(0.5, -0.01), fontsize=9.5)
    fig.tight_layout(rect=(0, 0.08, 1, 1))
    salvar(fig, "ap_separacao.png")


# ---------------------------------------------------------------------------------------------------------
# Comparação dos métodos (resultados do relatório, 03_comparar.py)
# ---------------------------------------------------------------------------------------------------------

METODOS = [  # chave, nome, família, ideia em uma linha
    ("corr_normalizada", "Correlação com templates", "aula",
     "compara a forma de |a| com quedas e atividades típicas (filtro casado)"),
    ("dtw", "DTW", "aula", "a mesma comparação, aceitando que o movimento estique ou encolha no tempo"),
    ("ar12_anomalia", "AR(12) como anomalia", "aula",
     "prevê |a| pelas amostras anteriores; erro grande = movimento estranho"),
    ("centroide_cosseno", "Centroide (cosseno)", "aula", "distância aos vetores de atributos médios de cada classe"),
    ("knn_manhattan", "kNN (Manhattan)", "aula", "votação entre as 7 janelas de treino mais parecidas"),
    ("regras_cascata", "Regras em cascata", "lit.", "queda livre E (corpo parado OU inclinação mudou)"),
    ("regressao_logistica", "Regressão logística", "lit.", "soma ponderada dos atributos vira probabilidade"),
    ("arvore_decisao", "Árvore de decisão", "lit.", "uma sequência de perguntas sim/não sobre os atributos"),
    ("svm_rbf", "SVM (RBF)", "lit.", "fronteira curva com a maior margem entre as classes"),
    ("cnn_1d_6_eixos", "CNN 1D (6 eixos)", "lit.", "rede convolucional que aprende direto do sinal"),
    ("random_forest", "Random Forest", "lit.", "muitas árvores diferentes, e a média dos votos"),
]
COR_FAMILIA = {"aula": VERDE_AGUA, "lit.": AZUL}


def fig_comparacao(comp_a, comp_b):
    ordem = sorted(METODOS, key=lambda m: comp_a.loc[m[0], "alarmes_h"])
    y = np.arange(len(ordem))
    h = 0.4
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(L, 3.6), sharey=True, gridspec_kw={"width_ratios": [1.35, 1]})
    for ax, campo, escala in ((a1, "alarmes_h", 1), (a2, "sens_1alarme_h", 100)):
        va = [comp_a.loc[m[0], campo] * escala for m in ordem]
        vb = [comp_b.loc[m[0], campo] * escala for m in ordem]
        cores = [COR_FAMILIA[m[2]] for m in ordem]
        ax.barh(y - h / 2, va, h, color=cores)
        ax.barh(y + h / 2, vb, h, color=cores, alpha=0.4)
        for yi, v in zip(y - h / 2, va):
            casas = (2 if v < 10 else 0) if escala == 1 else 0
            ax.text(v, yi, f" {br(v, casas)}", va="center", fontsize=8.5, color=TINTA)
        sem_grade_y(ax)
    a1.set_xscale("log")
    a1.set_xlim(0.2, 5000)
    a1.set_xlabel("alarmes falsos por hora (log)")
    a2.set_xlim(0, 118)
    a2.set_xlabel("quedas com ≤ 1 alarme/h (%)")
    a1.set_yticks(y, [m[1] for m in ordem], fontsize=10)
    a1.invert_yaxis()
    [t for t in a1.get_yticklabels() if t.get_text() == "Random Forest"][0].set_fontweight("bold")
    a1.set_title("Menos é melhor", fontsize=11)
    a2.set_title("Mais é melhor", fontsize=11)
    fig.legend(handles=[Patch(color=COR_FAMILIA["aula"], label="das aulas"),
                        Patch(color=COR_FAMILIA["lit."], label="da literatura"),
                        Patch(color=TINTA2, label="cenário A (±8 g)"),
                        Patch(color=TINTA2, alpha=0.4, label="cenário B (±2 g)")],
               loc="lower center", ncol=4, bbox_to_anchor=(0.55, -0.01), fontsize=9.5)
    fig.tight_layout(rect=(0, 0.07, 1, 1))
    salvar(fig, "ap_comparacao.png")


# ---------------------------------------------------------------------------------------------------------
# Random Forest: a árvore, a floresta, os votos
# ---------------------------------------------------------------------------------------------------------

def _posicoes(t):
    pos, proxima = {}, [0]

    def visita(n, d):
        if t.children_left[n] == -1:
            pos[n] = (proxima[0], -d)
            proxima[0] += 1
            return
        visita(t.children_left[n], d + 1)
        visita(t.children_right[n], d + 1)
        pos[n] = ((pos[t.children_left[n]][0] + pos[t.children_right[n]][0]) / 2, -d)

    visita(0, 0)
    return pos


def fig_arvore(conj, F, nomes):
    arvore = DecisionTreeClassifier(max_depth=3, class_weight="balanced", random_state=0).fit(F, conj.y)
    t = arvore.tree_
    caminho = arvore.decision_path(F)
    quedas = np.asarray(caminho[conj.y == 1].sum(axis=0)).ravel()
    ativ = np.asarray(caminho[conj.y == 0].sum(axis=0)).ravel()
    pos = _posicoes(t)
    largura = max(x for x, _ in pos.values()) + 1

    fig, ax = plt.subplots(figsize=(L, 3.15))
    for n in range(t.node_count):
        x, y = pos[n]
        for filho, rot in ((t.children_left[n], "sim"), (t.children_right[n], "não")):
            if filho != -1:
                xf, yf = pos[filho]
                ax.plot([x, xf], [y, yf], color=EIXO, lw=1.2, zorder=1)
                ax.text((x + xf) / 2, (y + yf) / 2, rot, fontsize=9, color=TINTA2, ha="center", va="center",
                        bbox={"boxstyle": "round,pad=0.12", "fc": "white", "ec": "none"}, zorder=2)
    for n in range(t.node_count):
        x, y = pos[n]
        v = t.value[n, 0]
        p = v[1] / v.sum()
        if t.children_left[n] == -1:
            texto = f"P = {100 * p:.0f} %\n{quedas[n]} Q / {ativ[n]} A"
            caixa = {"boxstyle": "round,pad=0.3", "fc": mistura(p), "ec": TINTA2, "lw": 0.8}
        else:
            nome = nomes[t.feature[n]]
            limiar = t.threshold[n]
            unidade = UNIDADE.get(nome, "")
            casas = 0 if unidade == "°" else 3 if abs(limiar) < 0.1 else 2
            sufixo = unidade if unidade in ("", "°") else f" {unidade}"
            texto = f"{NOME_CURTO.get(nome, nome_atr(nome))}\n≤ {br(limiar, casas)}{sufixo}?"
            caixa = {"boxstyle": "round,pad=0.3", "fc": "white", "ec": AZUL, "lw": 1.3 if n == 0 else 1.0}
        ax.text(x, y, texto, ha="center", va="center", fontsize=9, color=TINTA, bbox=caixa, zorder=3,
                fontweight="bold" if n == 0 else "normal")
    ax.set_xlim(-0.6, largura - 0.4)
    ax.set_ylim(-3.4, 0.35)
    ax.axis("off")
    fig.tight_layout(pad=0.2)
    salvar(fig, "ap_arvore.png")

    # Números da primeira divisão, para o slide do Gini
    e, d = t.children_left[0], t.children_right[0]
    w0, we, wd = t.weighted_n_node_samples[[0, e, d]]
    ganho = t.impurity[0] - (we / w0) * t.impurity[e] - (wd / w0) * t.impurity[d]
    return {"giniAtributo": NOME_CURTO.get(nomes[t.feature[0]], nome_atr(nomes[t.feature[0]])),
            "giniLimiar": t.threshold[0], "giniUnidade": UNIDADE.get(nomes[t.feature[0]], ""),
            "giniPai": t.impurity[0], "giniEsq": t.impurity[e], "giniDir": t.impurity[d], "giniGanho": ganho,
            "giniQuedasEsq": int(quedas[e]), "giniAtivEsq": int(ativ[e]), "giniQuedasDir": int(quedas[d]),
            "giniAtivDir": int(ativ[d])}


def fig_fronteira(conj, F, nomes):
    incl = F[:, nomes.index("inclinacao_graus")]
    quiet = F[:, nomes.index("pos_std")]
    validas = quiet > 1e-4
    X = np.column_stack([incl, np.log10(quiet)])[validas]
    y = conj.y[validas]
    # Os mesmos hiperparâmetros das árvores do vestível (profundidade 10, folha com 2 janelas).
    arvore = DecisionTreeClassifier(max_depth=10, min_samples_leaf=2, class_weight="balanced",
                                    random_state=0).fit(X, y)
    floresta = RandomForestClassifier(n_estimators=60, max_depth=10, min_samples_leaf=2, n_jobs=-1,
                                      class_weight="balanced_subsample", random_state=0).fit(X, y)
    gx, gy = np.meshgrid(np.linspace(0, 180, 360), np.linspace(-2.6, 0.5, 360))
    grade = np.column_stack([gx.ravel(), gy.ravel()])
    mapa = LinearSegmentedColormap.from_list("queda", [COR_ATIVIDADE, "#ffffff", COR_QUEDA])
    rng = np.random.default_rng(0)
    amostra = np.concatenate([rng.choice(np.flatnonzero(y == 0), 700, replace=False),
                              rng.choice(np.flatnonzero(y == 1), 350, replace=False)])

    fig, eixos = plt.subplots(1, 2, figsize=(L, 2.8), sharey=True, layout="constrained")
    for ax, modelo, titulo in ((eixos[0], arvore, "Uma árvore"), (eixos[1], floresta, "Floresta de 60 árvores")):
        p = modelo.predict_proba(grade)[:, 1].reshape(gx.shape)
        malha = ax.pcolormesh(gx, gy, p, cmap=mapa, vmin=0, vmax=1, shading="auto", alpha=0.85, rasterized=True)
        for cls, cor in ((0, COR_ATIVIDADE), (1, COR_QUEDA)):
            sel = amostra[y[amostra] == cls]
            ax.scatter(X[sel, 0], X[sel, 1], s=5, color=cor, edgecolor="white", lw=0.2, alpha=0.9)
        ax.set_title(titulo, fontsize=11)
        ax.set_xlabel("inclinação (graus)")
        ax.grid(False)
    eixos[0].set_ylabel("quietude no fim (g)")
    eixos[0].set_yticks([-2, -1, 0], ["0,01", "0,1", "1"])
    cb = fig.colorbar(malha, ax=eixos, fraction=0.03, pad=0.01)
    cb.set_label("P(queda)")
    salvar(fig, "ap_fronteira.png")


def fig_votos(conj, F, dobra):
    """As árvores do modelo treinado sem a SA01 votando em três janelas dela (nenhuma foi vista no treino)."""
    da_sa01 = conj.pessoa == "SA01"
    k = int(np.unique(dobra[da_sa01])[0])
    treino = np.flatnonzero(dobra != k)
    rf = M09.RandomForestVestivel().ajustar(avaliacao.subconjunto(conj, treino), F[treino]).modelo
    limiar = json.loads((R / "vestivel.json").read_text())["rf"]["limiar_prob"]

    def votos(i):
        x = F[i:i + 1].astype(np.float32)
        return np.array([arv.predict_proba(x)[0, 1] for arv in rf.estimators_])

    idx = np.flatnonzero(da_sa01)
    queda = next(i for i in idx if conj.codigo[i] == "F01")
    corridas = [i for i in idx if conj.codigo[i] == "D04"]
    p_corridas = np.array([votos(i).mean() for i in corridas])
    corrida = corridas[int(np.argsort(p_corridas)[len(p_corridas) // 2])]
    atividades = [i for i in idx if conj.y[i] == 0 and conj.codigo[i] != "D04"]
    p_ativ = np.array([votos(i).mean() for i in atividades])
    dificil = atividades[int(np.argmax(p_ativ))]

    exemplos = [(queda, "Queda (F01)"), (corrida, "Corrida (D04)"),
                (dificil, f"A atividade mais difícil ({conj.codigo[dificil]})")]
    rng = np.random.default_rng(1)
    fig, eixos = plt.subplots(1, 3, figsize=(L, 2.85), sharey=True)
    resultado = {}
    for ax, (i, titulo) in zip(eixos, exemplos):
        v = votos(i)
        p = v.mean()
        ax.scatter(v + rng.uniform(-0.015, 0.015, len(v)), rng.uniform(0.05, 0.95, len(v)), s=16,
                   color=[mistura(min(max(x, 0), 1)) for x in v], edgecolor=TINTA2, lw=0.5)
        ax.axvline(limiar, color=TINTA, ls="--", lw=1)
        ax.axvline(p, color=COR_QUEDA if p >= limiar else COR_ATIVIDADE, lw=2.8)
        decisao = "QUEDA" if p >= limiar else "não é queda"
        ax.set_xlim(-0.06, 1.06)
        ax.set_ylim(0, 1)
        ax.set_yticks([])
        ax.set_xticks([0, 0.5, 1], ["0", "0,5", "1"])
        ax.set_xlabel("voto de cada árvore")
        ax.set_title(f"{titulo}\nmédia {br(p, 2)} → {decisao}", fontsize=10.5)
        sem_grade_y(ax)
        resultado[i] = p
    fig.legend(handles=[Line2D([], [], color=TINTA2, lw=2.8, label="média = P(queda) da floresta"),
                        Line2D([], [], color=TINTA, ls="--", lw=1, label=f"limiar ({br(limiar, 2)})")],
               loc="lower center", ncol=2, bbox_to_anchor=(0.5, -0.02), fontsize=9.5)
    fig.tight_layout(rect=(0, 0.08, 1, 1), w_pad=0.8)
    salvar(fig, "ap_votos.png")
    return {"votoQueda": resultado[queda], "votoCorrida": resultado[corrida], "votoDificil": resultado[dificil],
            "votoDificilCodigo": conj.codigo[dificil],
            "votoDificilNome": ATIVIDADES_SISFALL[conj.codigo[dificil]]}


def fig_importancia(rf, nomes):
    imp = pd.Series(rf.feature_importances_, index=nomes).sort_values(ascending=False)
    topo = imp.head(10)[::-1]
    fig, ax = plt.subplots(figsize=(5.1, 3.6))
    ax.barh([nome_atr(n) for n in topo.index], 100 * topo.values, color=AZUL, height=0.65)
    for i, v in enumerate(100 * topo.values):
        ax.text(v, i, f" {br(v)} %", va="center", fontsize=9.5, color=TINTA)
    sem_grade_y(ax)
    ax.tick_params(axis="y", labelsize=10)
    ax.set_xlim(0, 100 * topo.max() * 1.25)
    ax.set_xlabel("importância (% da redução de Gini)")
    fig.tight_layout()
    salvar(fig, "ap_importancia.png")
    return imp


def fig_funil(conj, s, limiar):
    quedas = [e for e in conj.ensaios if e["queda"]]
    passam = sum(e["pegou_impacto"] for e in quedas)
    detectadas = int((s[conj.y == 1] >= limiar).sum())
    horas = sum(e["duracao_s"] for e in conj.ensaios if not e["queda"]) / 3600
    disparos_h = (conj.y == 0).sum() / horas
    alarmes_h = (s[conj.y == 0] >= limiar).sum() / horas

    fig, (a1, a2) = plt.subplots(1, 2, figsize=(L, 2.45), gridspec_kw={"wspace": 0.75})
    barras = [("ensaios de queda", len(quedas)), ("passam no gatilho", passam), ("Random Forest: queda", detectadas)]
    a1.barh([b[0] for b in barras], [b[1] for b in barras], color=COR_QUEDA, height=0.55)
    for i, (_, v) in enumerate(barras):
        a1.text(v, i, f" {v} ({br(100 * v / len(quedas))} %)", va="center", fontsize=9.5, color=TINTA)
    a1.set_xlim(0, len(quedas) * 1.6)
    a1.invert_yaxis()
    sem_grade_y(a1)
    a1.set_title("Quedas", fontsize=11)
    a1.set_xlabel("ensaios")
    barras = [("disparos do gatilho", disparos_h), ("alarmes falsos", alarmes_h)]
    a2.barh([b[0] for b in barras], [b[1] for b in barras], color=COR_ATIVIDADE, height=0.5)
    for i, (_, v) in enumerate(barras):
        a2.text(v, i, f" {br(v, 2 if v < 10 else 0)}/h", va="center", fontsize=9.5, color=TINTA)
    a2.set_xscale("log")
    a2.set_xlim(0.1, disparos_h * 40)
    a2.invert_yaxis()
    sem_grade_y(a2)
    a2.set_title("Atividades", fontsize=11)
    a2.set_xlabel("por hora (log)")
    fig.subplots_adjust(left=0.22, right=0.97, top=0.86, bottom=0.22)
    salvar(fig, "ap_funil.png")
    return {"funilQuedas": len(quedas), "funilPassam": passam, "funilDetectadas": detectadas,
            "funilHoras": horas, "funilDisparos": disparos_h, "funilAlarmes": alarmes_h}


# ---------------------------------------------------------------------------------------------------------
# Tabelas e números
# ---------------------------------------------------------------------------------------------------------

def tamanho(b):
    if b >= 1e6:
        return f"{br(b / 1e6, 1)}\\,MB"
    if b >= 1e3:
        return f"{b / 1e3:.0f}\\,kB"
    return f"{b:.0f}\\,B"


def milhar(n):
    return f"{int(round(n)):,}".replace(",", "\\,")


def escrever_tabelas(comp_a, comp_b, prop, tab):
    linhas = ["\\begin{tabular}{@{}l p{7.4cm} r r@{}}", "\\toprule",
              "Método & Ideia & \\makecell[r]{Alarmes\\\\falsos/h} & \\makecell[r]{Quedas com\\\\$\\le$1 alarme/h} "
              "\\\\", "\\midrule"]
    familia_atual = None
    for chave, nome, familia, ideia in METODOS:
        if familia != familia_atual:
            rotulo = "Das aulas" if familia == "aula" else "Da literatura"
            if familia_atual is not None:
                linhas.append("\\midrule")
            linhas.append(f"\\multicolumn{{4}}{{@{{}}l}}{{\\textcolor{{uspblue}}{{\\textbf{{{rotulo}}}}}}} \\\\")
            familia_atual = familia
        a = comp_a.loc[chave]
        destaque = "\\textbf{" if chave == "random_forest" else "{"
        ideia = ideia.replace("|a|", "$|a|$")
        linhas.append(f"{destaque}{nome}}} & {ideia} & {br(a.alarmes_h, 2 if a.alarmes_h < 10 else 1)} & "
                      f"{br(100 * a.sens_1alarme_h)}\\,\\% \\\\")
    linhas += ["\\bottomrule", "\\end{tabular}"]
    (tab / "metodos.tex").write_text("\n".join(linhas) + "\n", encoding="utf-8", newline="\n")

    escolhidos = [("random_forest", "Random Forest"), ("svm_rbf", "SVM (RBF)"), ("knn_manhattan", "kNN (Manhattan)"),
                  ("regressao_logistica", "Regressão logística"), ("cnn_1d_6_eixos", "CNN 1D (6 eixos)"),
                  ("arvore_decisao", "Uma árvore só")]
    linhas = ["\\begin{tabular}{@{}l rr rr r r@{}}", "\\toprule",
              " & \\multicolumn{2}{c}{Alarmes falsos/h} & \\multicolumn{2}{c}{Quedas com $\\le$1 alarme/h} & "
              "\\makecell[r]{Modelo\\\\na flash} & \\makecell[r]{Operações\\\\por janela} \\\\",
              "\\cmidrule(lr){2-3}\\cmidrule(lr){4-5}",
              "Método & A (±8\\,g) & B (±2\\,g) & A & B & & \\\\", "\\midrule"]
    for chave, nome in escolhidos:
        a, b = comp_a.loc[chave], comp_b.loc[chave]
        celulas = [br(a.alarmes_h, 2), br(b.alarmes_h, 2), f"{br(100 * a.sens_1alarme_h)}\\,\\%",
                   f"{br(100 * b.sens_1alarme_h)}\\,\\%", tamanho(a.bytes_modelo), milhar(a.ops_janela)]
        if chave == "random_forest":
            nome = f"\\textbf{{{nome}}}"
            celulas = [f"\\textbf{{{c}}}" for c in celulas]
        linhas.append(" & ".join([nome] + celulas) + " \\\\")
    linhas += ["\\bottomrule", "\\end{tabular}"]
    (tab / "porque_rf.tex").write_text("\n".join(linhas) + "\n", encoding="utf-8", newline="\n")

    # Os nossos ensaios (04_dados_proprios.py): modelos treinados na SisFall em ±2 g
    nomes = dict((c, n) for c, n, _, _ in METODOS) | {"cnn_1d": "CNN 1D"}
    ordem = ["random_forest", "svm_rbf", "regressao_logistica", "knn_manhattan", "centroide_cosseno",
             "regras_cascata", "dtw", "corr_normalizada", "ar12_anomalia", "cnn_1d"]
    linhas = ["\\begin{tabular}{@{}l c c c@{}}", "\\toprule",
              "Método & \\makecell{Quedas\\\\detectadas} & \\makecell{Alarmes\\\\falsos} & "
              "\\makecell{Atividades\\\\com alarme} \\\\", "\\midrule"]
    for chave in ordem:
        p = prop.loc[chave]
        celulas = [f"{int(p.quedas_detectadas)} de {int(p.quedas)}", f"{int(p.alarmes_falsos)}",
                   f"{int(p.ensaios_adl_com_alarme)} de 8"]
        nome = nomes[chave]
        if chave == "random_forest":
            nome = f"\\textbf{{{nome}}}"
            celulas = [f"\\textbf{{{c}}}" for c in celulas]
        linhas.append(" & ".join([nome] + celulas) + " \\\\")
    linhas += ["\\bottomrule", "\\end{tabular}"]
    (tab / "proprios.tex").write_text("\n".join(linhas) + "\n", encoding="utf-8", newline="\n")


def tex(texto):
    """Texto de figura para LaTeX: |a| e símbolos em modo matemático."""
    return texto.replace("|a|", "$|a|$").replace("ω", "$\\omega$").replace("°", "$^\\circ$")


def escrever_numeros(n, destino):
    linhas = ["% GERADO por modelo/experimentos/10_figuras_apresentacao.py: não edite à mão.", ""]
    for nome, valor in n.items():
        assert nome.isalpha(), f"nome de macro LaTeX só com letras: {nome}"
        linhas.append(f"\\newcommand{{\\{nome}}}{{{valor}}}")
    destino.write_text("\n".join(linhas) + "\n", encoding="utf-8", newline="\n")


# ---------------------------------------------------------------------------------------------------------

def main():
    sisfall, proprios = _comum.carregar()
    vestivel = json.loads((R / "vestivel.json").read_text())
    l_on = vestivel["l_on_g"]
    comp_a = pd.read_csv(R / "comparacao_A.csv").set_index("metodo")
    comp_b = pd.read_csv(R / "comparacao_B.csv").set_index("metodo")
    prop = pd.read_csv(R / "proprios_resumo.csv").set_index("metodo")

    # O conjunto e o modelo do vestível (09), conferidos
    conj = montar(sisfall, "A", l_on, causal=True)
    F, nomes = atributos.calcular(conj.janelas, PRE, grupos=("tempo",))
    dobra = avaliacao.dobras(conj)
    s_rf, _, _ = avaliacao.escores_fora_da_dobra(M09.RandomForestVestivel, conj, F, dobra)
    r_rf = avaliacao.metricas(s_rf, conj)
    assert abs(r_rf.limiar - vestivel["rf"]["limiar_prob"]) < 1e-9, "não reproduz o limiar do 09: rode o 09 de novo"
    rf = M09.RandomForestVestivel().ajustar(conj, F).modelo
    esperado = pd.read_csv(ESPERADO)
    dif = np.abs(M09.prob_rf(rf, esperado[nomes].to_numpy()) - esperado.rf_prob.to_numpy()).max()
    assert dif < 1e-5, f"o Random Forest não bate com os vetores do firmware (|dP| = {dif})"
    print(f"Random Forest do vestível conferido: limiar {r_rf.limiar:.4f}, |dP| nos vetores = {dif:.1g}")

    k_exemplo, exemplo = ensaio(sisfall, "F01")
    n = {}
    fig_dados(sisfall)
    fig_saturacao(sisfall)
    n.update(fig_filtro(exemplo))
    n.update(fig_gatilho_exemplo(ensaio(sisfall, "F05")[1], l_on))
    fig_gatilho_curva(vestivel["gatilho_disparos_h"])
    n.update(fig_janela(conj, F, nomes, k_exemplo))
    fig_separacao(conj, F, nomes)
    fig_comparacao(comp_a, comp_b)
    n.update(fig_arvore(conj, F, nomes))
    fig_fronteira(conj, F, nomes)
    n.update(fig_votos(conj, F, dobra))
    imp = fig_importancia(rf, nomes)
    n.update(fig_funil(conj, s_rf, r_rf.limiar))

    g = pd.read_csv(R / "gatilho.csv")
    g_a = g[(g.cenario == "A") & (g.base == "sisfall") & np.isclose(g.l_on_g, l_on)].iloc[0]
    peso_queda = len(conj.y) / (2 * conj.y.sum())
    peso_ativ = len(conj.y) / (2 * (conj.y == 0).sum())
    numeros = {
        "nPessoas": len({e.pessoa for e in sisfall}), "nEnsaios": milhar(len(sisfall)),
        "nEnsaiosQueda": milhar(n["funilQuedas"]), "nEnsaiosAtividade": milhar(len(sisfall) - n["funilQuedas"]),
        "horasAtividade": br(n["funilHoras"]), "nProprios": len(proprios),
        "nPropriosQueda": sum(e.queda for e in proprios), "nPropriosAtividade": sum(not e.queda for e in proprios),
        "lOn": br(l_on), "gatilhoPassam": br(100 * g_a.quedas_que_passam),
        "disparosPicos": f"{g_a.disparos_adl_por_h:.0f}", "disparosCausal": f"{vestivel['gatilho_disparos_h']:.0f}",
        "nJanelas": milhar(len(conj.y)), "nImpactos": milhar(conj.y.sum()), "nAtributos": len(nomes),
        "nJanelasAtividade": milhar((conj.y == 0).sum()),
        "picoBruto": br(n["picoBruto"]), "picoFiltrado": br(n["picoFiltrado"]), "exDisparos": n["exDisparos"],
        "exQuedaLivre": br(n["exQuedaLivre"], 2), "exPico": br(n["exPico"]),
        "exInclinacao": f"{n['exInclinacao']:.0f}", "exPosStd": br(n["exPosStd"], 3),
        # resultados do relatório (03): Random Forest com todos os atributos
        "rfRelAlarmesA": br(comp_a.loc["random_forest", "alarmes_h"]),
        "rfRelSensA": br(100 * comp_a.loc["random_forest", "sens_1alarme_h"]),
        "rfRelAlarmesB": br(comp_b.loc["random_forest", "alarmes_h"]),
        "rfRelSensB": br(100 * comp_b.loc["random_forest", "sens_1alarme_h"]),
        "arvoreAlarmesA": br(comp_a.loc["arvore_decisao", "alarmes_h"]),
        "lrRelAlarmesA": br(comp_a.loc["regressao_logistica", "alarmes_h"]),
        "knnMB": br(comp_a.loc["knn_manhattan", "bytes_modelo"] / 1e6),
        "soTempo": br(100 * comp_a.loc["rf_so_tempo", "sens_1alarme_h"]),
        "soFFT": br(100 * comp_a.loc["rf_so_freq", "sens_1alarme_h"]),
        "soAR": br(100 * comp_a.loc["rf_so_ar", "sens_1alarme_h"]),
        "rfPropriosQuedas": int(prop.loc["random_forest", "quedas_detectadas"]),
        "rfPropriosAlarmes": int(prop.loc["random_forest", "alarmes_falsos"]),
        # o modelo do vestível (09)
        "rfVestAlarmes": br(vestivel["rf"]["alarmes_h"], 2),
        "rfVestSens": br(100 * vestivel["rf"]["sens_1alarme_h"]),
        "rfVestLimiar": br(vestivel["rf"]["limiar_prob"], 2),
        "rfVestNos": milhar(vestivel["rf"]["nos"]), "rfVestKB": f"{vestivel['rf']['bytes_flash'] / 1024:.0f}",
        "lrVestAlarmes": br(vestivel["lr"]["alarmes_h"], 2),
        "nVetores": len(esperado), "nTrechos": esperado.arquivo.nunique(),
        "funilDetectadas": milhar(n["funilDetectadas"]),
        "funilDetectadasPct": br(100 * n["funilDetectadas"] / n["funilQuedas"]),
        "funilPassam": milhar(n["funilPassam"]),
        # a árvore de exemplo e os pesos das classes
        "giniAtributo": tex(n["giniAtributo"]),
        "giniLimiar": br(n["giniLimiar"], 3 if abs(n["giniLimiar"]) < 0.1 else 1),
        "giniUnidade": tex(n["giniUnidade"]),
        "giniPai": br(n["giniPai"], 2), "giniEsq": br(n["giniEsq"], 2), "giniDir": br(n["giniDir"], 2),
        "giniGanho": br(n["giniGanho"], 2),
        "giniQuedasEsq": milhar(n["giniQuedasEsq"]), "giniAtivEsq": milhar(n["giniAtivEsq"]),
        "giniQuedasDir": milhar(n["giniQuedasDir"]), "giniAtivDir": milhar(n["giniAtivDir"]),
        "pesoQueda": br(peso_queda, 2), "pesoAtividade": br(peso_ativ, 2),
        "atributosPorNo": int(np.sqrt(len(nomes))),
        "impPrimeiro": tex(nome_atr(imp.index[0])), "impSegundo": tex(nome_atr(imp.index[1])),
        "votoQueda": br(n["votoQueda"], 2), "votoCorrida": br(n["votoCorrida"], 2),
        "votoDificil": br(n["votoDificil"], 2), "votoDificilCodigo": n["votoDificilCodigo"],
        "votoDificilNome": n["votoDificilNome"],
    }

    figuras, tabelas = APRESENTACAO / "figuras", APRESENTACAO / "tabelas"
    for pasta in (figuras, tabelas, APRESENTACAO / "imagens"):
        pasta.mkdir(parents=True, exist_ok=True)
    for antiga in figuras.glob("*.png"):
        antiga.unlink()
    novas = sorted(R.glob("ap_*.png"))
    for f in novas:
        shutil.copy2(f, figuras / f.name)
    shutil.copy2(LOGO, APRESENTACAO / "imagens" / LOGO.name)
    escrever_tabelas(comp_a, comp_b, prop, tabelas)
    escrever_numeros(numeros, APRESENTACAO / "numeros.tex")
    print(f"apresentação: {len(novas)} figuras, 3 tabelas e {len(numeros)} números em {APRESENTACAO}")


if __name__ == "__main__":
    main()
