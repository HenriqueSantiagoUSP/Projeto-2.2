"""
Exporta o pipeline de queda para o vestível (firmware/gadget/components/fall_pipeline/).

O que vai para a ESP (decidido em docs/decisoes.md, 2026-11-11):
- filtro passa-baixa de 20 Hz (biquad) nos 6 eixos;
- gatilho causal (pipeline.gatilho_causal): picos de |a| filtrado ≥ L_on (2,0 g), emitidos depois de 0,5 s sem
  um valor maior;
- janela de -1,0 s a +0,8 s em volta do pico e os 23 atributos no tempo de atributos.tempo();
- Random Forest (60 árvores, profundidade ≤ 10) e regressão logística, treinados na SisFall no cenário A
  (±8 g / ±500 °/s), com o limiar que mantém 95 % das janelas de impacto acima dele (validação por pessoa).

Gera (não edite à mão; rode de novo este script):
- include/fall_model.h e fall_model.c: constantes, coeficientes do filtro e os dois modelos;
- test_host/vetores/: trechos de ensaios da SisFall e o que o Python calcula neles (picos, atributos,
  probabilidades), para os testes de paridade do C;
- resultados/vestivel.json: métricas da validação cruzada e limiares.

    uv run python experimentos/09_exportar_vestivel.py
"""

import json
from datetime import date

import _comum
import numpy as np
from scipy import signal
from sklearn.ensemble import RandomForestClassifier

from queda import atributos, avaliacao, modelos
from queda.dados import FS, saturar
from queda.pipeline import CENARIOS, FC_PASSA_BAIXA, POS_S, PRE_S, gatilho_causal, montar, passa_baixa, recortar

CENARIO = "A"
RF_ARVORES, RF_PROFUNDIDADE = 60, 10
DISTANCIA_S = 0.5
COMPONENTE = _comum.RAIZ_MODELO.parent / "firmware" / "gadget" / "components" / "fall_pipeline"
VETORES = COMPONENTE / "test_host" / "vetores"

# Trechos para os testes de paridade: (código, segundos antes e depois do maior pico) para as quedas,
# (código, início e fim em s) para as atividades. Correr e escada dão sequências de passos acima do gatilho.
TRECHOS_QUEDA = [("F01", 3.0, 3.0), ("F05", 3.0, 3.0), ("F10", 3.0, 3.0), ("F13", 3.0, 3.0)]
TRECHOS_ATIVIDADE = [("D04", 2.0, 10.0), ("D06", 2.0, 10.0), ("D10", 0.0, 8.0), ("D19", 0.0, 8.0)]


class RandomForestVestivel(modelos.Sklearn):
    """O Random Forest de modelos.Sklearn, menor: 60 árvores de profundidade ≤ 10 (~80 KB na flash)."""

    def __init__(self):
        super().__init__("random_forest")

    def _novo(self):
        return RandomForestClassifier(n_estimators=RF_ARVORES, max_depth=RF_PROFUNDIDADE, min_samples_leaf=2,
                                      n_jobs=-1, class_weight="balanced_subsample", random_state=modelos.SEMENTE)


def sigmoide(z):
    return 1.0 / (1.0 + np.exp(-z))


def prob_lr(modelo, F):
    return sigmoide(modelo.decision_function(F))


def prob_rf(modelo, F):
    return modelo.predict_proba(F)[:, 1]


def f32_abaixo(t: float) -> np.float32:
    """O maior float32 ≤ t: para um x float32, x <= t (double) vale o mesmo que x <= f32_abaixo(t)."""
    f = np.float32(t)
    return np.nextafter(f, np.float32(-np.inf)) if float(f) > t else f


def c_float(x) -> str:
    """Literal float do C que reproduz o float32 exato (9 dígitos), sempre com ponto ou expoente."""
    s = f"{float(np.float32(x)):.9g}"
    if not any(ch in s for ch in ".e"):
        s += ".0"
    return s + "f"


def nos_da_floresta(rf: RandomForestClassifier):
    """Todas as árvores num vetor só: (atributo ou -1, esquerdo, direito, limiar ou P(queda)), e as raízes."""
    nos, raizes = [], []
    for arvore in rf.estimators_:
        t = arvore.tree_
        base = len(nos)
        raizes.append(base)
        valores = t.value[:, 0, :]
        p_queda = valores[:, 1] / valores.sum(axis=1)
        for i in range(t.node_count):
            if t.children_left[i] == -1:
                nos.append((-1, 0, 0, np.float32(p_queda[i])))
            else:
                nos.append((int(t.feature[i]), base + int(t.children_left[i]), base + int(t.children_right[i]),
                            f32_abaixo(float(t.threshold[i]))))
    assert len(nos) < 65536, "os índices dos nós são uint16"
    return nos, raizes


def escrever_modelo(nomes, b, a, lr, rf, limiar_lr, limiar_rf, l_on, resumo):
    escala, logistica = lr.modelo[0], lr.modelo[-1]
    nos, raizes = nos_da_floresta(rf.modelo)
    pre, pos = int(PRE_S * FS), int(POS_S * FS)
    n = len(nomes)
    cabecalho = (f"/*\n * GERADO por modelo/experimentos/09_exportar_vestivel.py em {date.today().isoformat()}: não edite à mão.\n"
                 f" * Treino: SisFall, cenário {CENARIO} ({CENARIOS[CENARIO]['descricao']}), gatilho causal com L_on = {l_on} g,\n"
                 f" * {resumo['janelas']} janelas ({resumo['impactos']} de impacto), {resumo['pessoas']} pessoas.\n"
                 f" * Validação por pessoa (5 dobras), limiar com 95 % das janelas de impacto acima:\n"
                 f" *   regressão logística: {resumo['lr']['alarmes_h']:.2f} alarme falso/h, "
                 f"{100 * resumo['lr']['sens_1alarme_h']:.1f} % das quedas com <= 1 alarme/h\n"
                 f" *   Random Forest: {resumo['rf']['alarmes_h']:.2f} alarme falso/h, "
                 f"{100 * resumo['rf']['sens_1alarme_h']:.1f} % das quedas com <= 1 alarme/h\n */\n")

    h = [cabecalho, "#pragma once\n", "#include <stdint.h>\n",
         "/* Pipeline (modelo/queda/pipeline.py and atributos.py): the C side must match these. */",
         f"#define FALL_MODEL_FS_HZ            {int(FS)}",
         f"#define FALL_MODEL_TRIGGER_G        {c_float(l_on)}   /* L_on the models were trained with */",
         f"#define FALL_MODEL_TRIGGER_GAP      {int(DISTANCIA_S * FS)}      /* samples without a higher |a| before a peak is final */",
         f"#define FALL_MODEL_PRE              {pre}      /* window samples before the peak */",
         f"#define FALL_MODEL_POST             {pos}      /* window samples from the peak on */",
         f"#define FALL_MODEL_WINDOW_LEN       {pre + pos}",
         f"#define FALL_MODEL_N_FEATURES       {n}",
         f"#define FALL_MODEL_RANGE_G          {c_float(CENARIOS[CENARIO]['g_max'])}   /* accelerometer range of the training data */",
         "",
         "/* 2nd order Butterworth low-pass, ESP-DSP order {b0, b1, b2, a1, a2} (a0 = 1). */",
         "extern const float fall_model_biquad[5];",
         "",
         "/* Feature names in model order, as modelo/queda/atributos.py calls them. */",
         "extern const char *const fall_model_feature_names[FALL_MODEL_N_FEATURES];",
         "",
         "/* Logistic regression: p = 1 / (1 + exp(-(intercept + sum coef_i * (x_i - mean_i) / scale_i))). */",
         "extern const float fall_lr_mean[FALL_MODEL_N_FEATURES];",
         "extern const float fall_lr_scale[FALL_MODEL_N_FEATURES];",
         "extern const float fall_lr_coef[FALL_MODEL_N_FEATURES];",
         "extern const float fall_lr_intercept;",
         "extern const float fall_lr_threshold;   /* fall when p >= this */",
         "",
         "/* Random forest: p = mean over the trees of P(fall) at the leaf each one reaches. */",
         f"#define FALL_RF_N_TREES             {len(raizes)}",
         f"#define FALL_RF_N_NODES             {len(nos)}",
         "",
         "typedef struct {",
         "    int8_t   feature;   /* -1 = leaf */",
         "    uint16_t left;      /* internal node: next node when x[feature] <= value */",
         "    uint16_t right;     /* internal node: next node otherwise */",
         "    float    value;     /* internal node: threshold; leaf: P(fall) */",
         "} fall_rf_node_t;",
         "",
         "extern const uint16_t fall_rf_roots[FALL_RF_N_TREES];",
         "extern const fall_rf_node_t fall_rf_nodes[FALL_RF_N_NODES];",
         "extern const float fall_rf_threshold;   /* fall when p >= this */",
         ""]

    def vetor(nome, tipo, valores, por_linha=6):
        linhas = [f"const {tipo} {nome}[{len(valores)}] = {{"]
        for i in range(0, len(valores), por_linha):
            linhas.append("    " + ", ".join(valores[i:i + por_linha]) + ",")
        linhas.append("};")
        return linhas

    coef_biquad = [b[0], b[1], b[2], a[1], a[2]]
    c = [cabecalho, '#include "fall_model.h"', "",
         *vetor("fall_model_biquad", "float", [c_float(v) for v in coef_biquad]), "",
         f"const char *const fall_model_feature_names[{n}] = {{"]
    c += [f'    "{nome}",' for nome in nomes] + ["};", ""]
    c += vetor("fall_lr_mean", "float", [c_float(v) for v in escala.mean_]) + [""]
    c += vetor("fall_lr_scale", "float", [c_float(v) for v in escala.scale_]) + [""]
    c += vetor("fall_lr_coef", "float", [c_float(v) for v in logistica.coef_[0]]) + [""]
    c += [f"const float fall_lr_intercept = {c_float(logistica.intercept_[0])};",
          f"const float fall_lr_threshold = {c_float(limiar_lr)};", ""]
    c += vetor("fall_rf_roots", "uint16_t", [str(r) for r in raizes], por_linha=12) + [""]
    c += [f"const fall_rf_node_t fall_rf_nodes[{len(nos)}] = {{"]
    c += [f"    {{ {f}, {l}, {r}, {c_float(v)} }}," for f, l, r, v in nos] + ["};", ""]
    c += [f"const float fall_rf_threshold = {c_float(limiar_rf)};", ""]

    (COMPONENTE / "include").mkdir(parents=True, exist_ok=True)
    with open(COMPONENTE / "include" / "fall_model.h", "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(h))
    with open(COMPONENTE / "fall_model.c", "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(c))
    return len(nos)


def escolher_trechos(sisfall):
    """Um ensaio de cada código pedido (o primeiro em ordem de pessoa) e o recorte bruto, saturado no cenário."""
    faixa = CENARIOS[CENARIO]
    por_codigo = {}
    for e in sorted(sisfall, key=lambda e: (e.pessoa, e.rep)):
        por_codigo.setdefault(e.codigo, e)
    trechos = []
    for codigo, antes, depois in TRECHOS_QUEDA + TRECHOS_ATIVIDADE:
        e = por_codigo[codigo]
        acc, gyr = saturar(e.acc, e.gyr, faixa["g_max"], faixa["dps_max"])
        bruto = np.hstack([acc, gyr]).astype(np.float32)
        if e.queda:
            a = np.linalg.norm(passa_baixa(bruto)[:, 0:3], axis=1)
            centro = int(np.argmax(a))
            ini, fim = max(0, centro - int(antes * FS)), min(len(bruto), centro + int(depois * FS))
        else:
            ini, fim = int(antes * FS), min(len(bruto), int(depois * FS))
        trechos.append((f"{codigo}_{e.pessoa}_R{e.rep:02d}", bruto[ini:fim]))
    return trechos


def escrever_vetores(trechos, nomes, lr, rf, l_on):
    VETORES.mkdir(parents=True, exist_ok=True)
    for antigo in VETORES.glob("*.csv"):
        antigo.unlink()
    pre, pos = int(PRE_S * FS), int(POS_S * FS)
    linhas = ["arquivo,pico,a_pico," + ",".join(nomes) + ",lr_prob,rf_prob"]
    total = 0
    for nome, bruto in trechos:
        with open(VETORES / f"{nome}.csv", "w", encoding="utf-8", newline="\n") as f:
            f.write("ax,ay,az,gx,gy,gz\n")
            for amostra in bruto:
                f.write(",".join(f"{v:.6f}" for v in amostra) + "\n")
        # Releitura do que foi gravado (6 casas): o C lê exatamente esses valores.
        lido = np.loadtxt(VETORES / f"{nome}.csv", delimiter=",", skiprows=1, dtype=np.float32)
        x = passa_baixa(lido)
        a = np.linalg.norm(x[:, 0:3], axis=1)
        for p in gatilho_causal(a, l_on, DISTANCIA_S):
            if p + pos > len(x):
                continue    # a janela ainda não fechou quando o trecho acaba
            w = recortar(x, p, pre, pos)[None].astype(np.float64)
            F = np.column_stack(list(atributos.tempo(w, pre).values()))
            linha = [nome, str(p), f"{a[p]:.6f}"] + [f"{v:.9g}" for v in F[0]]
            linha += [f"{prob_lr(lr.modelo, F)[0]:.6f}", f"{prob_rf(rf.modelo, F)[0]:.6f}"]
            linhas.append(",".join(linha))
            total += 1
    with open(VETORES / "esperado.csv", "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(linhas) + "\n")
    return total


def main():
    sisfall, _ = _comum.carregar()
    l_on = json.loads((_comum.RESULTADOS / "gatilho_escolhido.json").read_text())[CENARIO]
    pre = int(PRE_S * FS)
    conj = montar(sisfall, CENARIO, l_on, causal=True)
    F, nomes = atributos.calcular(conj.janelas, pre, grupos=("tempo",))
    quedas = [e for e in conj.ensaios if e["queda"]]
    horas = sum(e["duracao_s"] for e in conj.ensaios if not e["queda"]) / 3600
    disparos_h = sum(e["disparos"] for e in conj.ensaios if not e["queda"]) / horas
    passam = sum(e["pegou_impacto"] for e in quedas) / len(quedas)
    print(f"gatilho causal, L_on = {l_on} g: {100 * passam:.1f} % das quedas passam, {disparos_h:.0f} disparos/h; "
          f"{len(conj.y)} janelas, {int(conj.y.sum())} de impacto, {len(nomes)} atributos")

    dobra = avaliacao.dobras(conj)
    resumo = {"cenario": CENARIO, "l_on_g": l_on, "gatilho_quedas_que_passam": passam,
              "gatilho_disparos_h": disparos_h, "janelas": int(len(conj.y)), "impactos": int(conj.y.sum()),
              "pessoas": int(len(set(conj.pessoa))), "atributos": nomes}
    finais = {}
    for chave, fabrica, prob in (("lr", lambda: modelos.Sklearn("regressao_logistica"), prob_lr),
                                 ("rf", RandomForestVestivel, prob_rf)):
        s, _, _ = avaliacao.escores_fora_da_dobra(fabrica, conj, F, dobra)
        r = avaliacao.metricas(s, conj)
        limiar = float(sigmoide(r.limiar)) if chave == "lr" else float(r.limiar)
        resumo[chave] = {"auc": r.auc, "sens_ensaio": r.sens_ensaio, "alarmes_h": r.alarmes_h,
                         "sens_1alarme_h": r.sens_1alarme_h, "limiar_prob": limiar}
        finais[chave] = fabrica().ajustar(conj, F)
        print(f"  {chave}: AUC {r.auc:.4f}, {100 * r.sens_ensaio:.1f} % das quedas, {r.alarmes_h:.2f} alarmes/h, "
              f"{100 * r.sens_1alarme_h:.1f} % com <= 1 alarme/h; limiar de probabilidade {limiar:.4f}")

    b, a = signal.butter(2, FC_PASSA_BAIXA / (FS / 2))
    n_nos = escrever_modelo(nomes, b, a, finais["lr"], finais["rf"], resumo["lr"]["limiar_prob"],
                            resumo["rf"]["limiar_prob"], l_on, resumo)
    resumo["rf"]["nos"] = n_nos
    resumo["rf"]["bytes_flash"] = 12 * n_nos
    janelas = escrever_vetores(escolher_trechos(sisfall), nomes, finais["lr"], finais["rf"], l_on)
    print(f"modelo: {n_nos} nós no Random Forest (~{12 * n_nos / 1024:.0f} KB); vetores de teste: {janelas} janelas")
    with open(_comum.RESULTADOS / "vestivel.json", "w", encoding="utf-8", newline="\n") as f:
        json.dump(resumo, f, ensure_ascii=False, indent=2)
        f.write("\n")


if __name__ == "__main__":
    main()
