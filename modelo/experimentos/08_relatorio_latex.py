"""
Prepara o relatório LaTeX do Q1 (relatorios/Semestre 2/Projeto 3/Q1/latex/): copia as figuras de resultados/
para latex/figuras/ e escreve as tabelas em latex/tabelas/ a partir dos CSVs. Assim nenhum número das tabelas
é digitado à mão. Rode depois de 03 a 07:

    uv run python experimentos/08_relatorio_latex.py

O relatório é compilado com `pdflatex main.tex` (duas vezes, por causa do sumário) dentro de latex/.
"""

import json
import shutil

import _comum
import pandas as pd

from queda.dados import ATIVIDADES_SISFALL

R = _comum.RESULTADOS


def l_on_escolhido(cenario="A"):
    return json.loads((R / "gatilho_escolhido.json").read_text(encoding="utf-8"))[cenario]
LATEX = _comum.RAIZ_MODELO.parent / "relatorios" / "Semestre 2" / "Projeto 3" / "Q1" / "latex"
FIGURAS = ["fig_sinais", "fig_saturacao", "fig_pipeline", "fig_gatilho", "fig_picos", "fig_funil", "fig_resumo",
           "fig_escores", "fig_roc_A", "fig_ordem_ar_A", "fig_ar_explicado", "fig_templates", "fig_fft",
           "fig_separacao", "fig_atributos_rf", "fig_erros_tipo", "fig_custo_A", "fig_proprios_matriz",
           "fig_proprios_ensaios"]

NOMES = {
    "limiar_do_firmware (2,5 g)": "Limiar atual (2,5\\,g)",
    "regras_cascata": "Regras em cascata", "corr_nao_normalizada": "Correlação não normalizada",
    "corr_normalizada": "Correlação normalizada", "dtw": "DTW", "ar12_anomalia": "AR(12) como anomalia",
    "arx12_anomalia": "ARX(12) como anomalia", "centroide_euclidiana": "Centroide (euclidiana)",
    "centroide_manhattan": "Centroide (Manhattan)", "centroide_cosseno": "Centroide (cosseno)",
    "knn_euclidiana": "kNN (euclidiana)", "knn_manhattan": "kNN (Manhattan)", "knn_cosseno": "kNN (cosseno)",
    "regressao_logistica": "Regressão logística", "arvore_decisao": "Árvore de decisão", "svm_rbf": "SVM (RBF)",
    "rf_so_tempo": "RF: só tempo", "rf_so_freq": "RF: só FFT", "rf_so_ar": "RF: só AR(4)",
    "rf_tempo_freq": "RF: tempo + FFT", "random_forest": "Random Forest", "cnn_1d": "CNN 1D (módulos)",
    "cnn_1d_6_eixos": "CNN 1D (6 eixos)",
}
FAMILIA = {"linha de base": "base", "slides": "aula", "literatura": "lit.", "ablação": "ablação"}


def num(v, casas):
    return "---" if pd.isna(v) else f"\\num{{{v:.{casas}f}}}"


def pct(v, casas=1):
    return "---" if pd.isna(v) else f"\\num{{{100 * v:.{casas}f}}}\\,\\%"


def tamanho(b):
    if pd.isna(b):
        return "---"
    if b >= 1e6:
        return f"\\num{{{b / 1e6:.2f}}}\\,MB"
    if b >= 1e3:
        return f"\\num{{{b / 1e3:.1f}}}\\,kB"
    return f"\\num{{{b:.0f}}}\\,B"


def tabela(nome, legenda, rotulo, colunas, cabecalho, linhas, fonte="\\small", separadores=()):
    corpo = []
    for i, l in enumerate(linhas):
        if i in separadores:
            corpo.append("\\midrule")
        corpo.append(" & ".join(l) + " \\\\")
    texto = "\n".join([
        "\\begin{table}[H]", "\\centering", f"\\caption{{{legenda}}}", f"\\label{{{rotulo}}}",
        fonte, "\\setlength{\\tabcolsep}{4pt}", f"\\begin{{tabular}}{{{colunas}}}",
        "\\toprule", cabecalho if isinstance(cabecalho, str) else " & ".join(cabecalho) + " \\\\", "\\midrule",
        *corpo, "\\bottomrule", "\\end{tabular}",
        "\\end{table}", ""])
    (LATEX / "tabelas" / f"{nome}.tex").write_bytes(texto.encode("utf-8"))
    print("  tabelas/", f"{nome}.tex", sep="")


def comparacao_a():
    df = pd.read_csv(R / "comparacao_A.csv")
    df.loc[df.metodo.str.startswith("limiar"), "bytes_modelo"] = 8       # 2 limiares em float (L_on, L_off)
    linhas, separadores = [], []
    ordem = list(df.familia)
    for i, l in df.iterrows():
        if i > 0 and ordem[i] != ordem[i - 1]:
            separadores.append(i)
        nome = NOMES[l.metodo]
        if l.metodo == "random_forest":
            nome = f"\\textbf{{{nome}}}"
        linhas.append([nome, FAMILIA[l.familia], num(l.auc, 3), pct(l.sens_ensaio), num(l.alarmes_h, 1),
                       pct(l.sens_1alarme_h), num(l.ops_janela / 1e3, 1) if l.ops_janela > 1 else "---",
                       tamanho(l.bytes_modelo)])
    tabela("comparacao_A",
           "Comparação dos métodos no cenário A ($\\pm$8\\,g, vestível atual), SisFall com validação cruzada por "
           "pessoa (5 dobras). ``Quedas'' e ``alarmes/h'': no ponto em que 95\\,\\% das janelas de impacto ficam "
           "acima do limiar; as quedas são contadas por ensaio, inclusive as que o gatilho perdeu. ``Quedas com "
           "$\\le$1 alarme/h'': maior fração de quedas detectadas com no máximo um alarme falso por hora de atividade. "
           "Custo no ESP32-S3: milhares de operações por janela (com os atributos) e tamanho do modelo, ambos "
           "estimados.",
           "tab:comparacao_A", "@{}llcccccr@{}",
           ["Método", "Família", "AUC", "Quedas", r"\makecell{Alarmes\\por hora}",
            r"\makecell{Quedas com\\$\le$1 alarme/h}", r"\makecell{Operações\\($\times10^3$)}", "Modelo"],
           linhas, fonte="\\footnotesize", separadores=separadores)


def cenarios():
    a = pd.read_csv(R / "comparacao_A.csv").set_index("metodo")
    b = pd.read_csv(R / "comparacao_B.csv").set_index("metodo")
    escolhidos = ["limiar_do_firmware (2,5 g)", "regras_cascata", "corr_nao_normalizada", "corr_normalizada", "dtw",
                  "ar12_anomalia", "centroide_cosseno", "knn_manhattan", "regressao_logistica", "svm_rbf",
                  "random_forest", "cnn_1d", "cnn_1d_6_eixos"]
    linhas = []
    for m in escolhidos:
        linhas.append([NOMES[m], num(a.loc[m, "auc"], 3), num(b.loc[m, "auc"], 3), num(a.loc[m, "alarmes_h"], 1),
                       num(b.loc[m, "alarmes_h"], 1), pct(a.loc[m, "sens_1alarme_h"]),
                       pct(b.loc[m, "sens_1alarme_h"])])
    tabela("cenarios_AB",
           "Efeito da faixa do sensor: os mesmos métodos no cenário A ($\\pm$8\\,g / $\\pm$500\\,°/s) e no B "
           "($\\pm$2\\,g / $\\pm$250\\,°/s, a faixa dos nossos ensaios), na SisFall saturada em cada faixa. Mesmas "
           "métricas da Tabela~\\ref{tab:comparacao_A}. O limiar do firmware detecta "
           f"{pct(a.loc[escolhidos[0], 'sens_ensaio'])} das quedas em A e {pct(b.loc[escolhidos[0], 'sens_ensaio'])} em B.",
           "tab:cenarios_AB", "@{}lcccccc@{}",
           r" & \multicolumn{2}{c}{AUC} & \multicolumn{2}{c}{Alarmes por hora} & "
           r"\multicolumn{2}{c}{Quedas com $\le$1 alarme/h} \\" "\n"
           r"\cmidrule(lr){2-3}\cmidrule(lr){4-5}\cmidrule(lr){6-7}" "\n"
           r"Método & A & B & A & B & A & B \\", linhas)


def gatilho():
    df = pd.read_csv(R / "gatilho.csv")
    a = df[(df.cenario == "A") & (df.base == "sisfall")]
    linhas = [[f"\\num{{{l.l_on_g:.2f}}}", pct(l.quedas_que_passam), num(l.disparos_adl_por_h, 0)]
              for _, l in a.iterrows()]
    tabela("gatilho",
           "Varredura do limiar $L_\\text{on}$ do gatilho no cenário A (SisFall): fração das quedas cujo impacto "
           "dispara o gatilho e disparos por hora de atividade, que viram janelas para o classificador. "
           f"Escolhido: {num(l_on_escolhido(), 1)}\\,g.",
           "tab:gatilho", "ccc", ["$L_\\text{on}$ (g)", "Quedas que passam", "Disparos por hora"], linhas)


def ordem_ar():
    df = pd.read_csv(R / "ordem_ar_A.csv")
    base = df.bic.iloc[0]
    linhas = [[f"{int(l.p)}", num(l.var_residuo, 3), num((l.aic - df.aic.iloc[0]) / 1e3, 1),
               num((l.bic - base) / 1e3, 1)] for _, l in df.iterrows()]
    p_bic, p_max = int(df.loc[df.bic.idxmin(), "p"]), int(df.p.max())
    final = (f"O BIC continua caindo até $p={p_max}$, o maior valor testado." if p_bic == p_max else
             f"O BIC é mínimo em $p={p_bic}$.")
    tabela("ordem_ar",
           "Escolha da ordem do AR($p$) em $|a|$ a 50\\,Hz, ajustado por mínimos quadrados em todas as janelas de "
           "atividade do cenário A: variância do resíduo e AIC e BIC em relação a $p=1$ (em milhares). " + final,
           "tab:ordem_ar", "cccc", ["$p$", "$\\sigma^2_e$ (g$^2$)", "$\\Delta$AIC ($\\times10^3$)",
                                    "$\\Delta$BIC ($\\times10^3$)"], linhas)


def proprios():
    df = pd.read_csv(R / "proprios_resumo.csv")
    linhas = []
    for _, l in df.iterrows():
        adl = f"{int(l.ensaios_adl_com_alarme)} de {int(l.ensaios_adl)}"
        linhas.append([NOMES[l.metodo], f"{int(l.quedas_detectadas)}/{int(l.quedas)}", f"{int(l.alarmes_falsos)}",
                       adl])
    n_quedas, n_adl = int(df.quedas.iloc[0]), int(df.ensaios_adl.iloc[0])
    tabela("proprios",
           f"Teste externo nos nossos {n_quedas + n_adl} ensaios (uma pessoa, cintura, $\\pm$2\\,g): modelos "
           "treinados só na SisFall saturada no cenário B, com o limiar de operação tirado da SisFall. Alarmes "
           f"falsos: disparos classificados como queda nos {n_adl} ensaios de atividade.",
           "tab:proprios", "@{}lccc@{}", ["Método", r"\makecell{Quedas\\detectadas}", r"\makecell{Alarmes\\falsos}",
                                          r"\makecell{Ensaios de atividade\\com alarme}"], linhas)


def erros_quedas():
    df = pd.read_csv(R / "erros_por_tipo_A.csv")
    rf = df[(df.metodo == "random_forest") & df.queda]
    linhas = [[l.codigo, ATIVIDADES_SISFALL[l.codigo], f"{int(l.ensaios)}", f"{int(l.detectadas)}",
               f"{int(l.perdidas)}", f"{int(l.perdidas_no_gatilho)}"] for _, l in rf.iterrows()]
    tabela("erros_quedas",
           "Quedas perdidas pelo Random Forest por tipo (cenário A, mesmo ponto de operação da "
           f"Tabela~\\ref{{tab:comparacao_A}}). ``No gatilho'': quedas cujo impacto não passou de "
           f"{num(l_on_escolhido(), 1)}\\,g.",
           "tab:erros_quedas", "@{}llcccc@{}", ["Código", "Queda", "Ensaios", "Detectadas", "Perdidas",
                                                 "No gatilho"], linhas)


def main():
    for pasta in ("figuras", "tabelas", "imagens"):
        (LATEX / pasta).mkdir(parents=True, exist_ok=True)
    for f in FIGURAS:
        shutil.copy2(R / f"{f}.png", LATEX / "figuras" / f"{f}.png")
    print(f"  {len(FIGURAS)} figuras copiadas para figuras/")
    comparacao_a()
    cenarios()
    gatilho()
    ordem_ar()
    proprios()
    erros_quedas()


if __name__ == "__main__":
    main()
