"""
SVM (RBF) na configuração da pulseira, lado a lado com o Random Forest e a regressão logística do 09.

A comparação de métodos (03) avaliou o SVM com todos os atributos (tempo, FFT e AR) e o gatilho por picos. A
pulseira usa o gatilho causal e só os 23 atributos no tempo, e nessa configuração o SVM nunca tinha rodado.
Aqui os modelos rodam no mesmo conjunto, com as mesmas dobras e o mesmo ponto de operação do 09:
- SisFall no cenário A (±8 g), gatilho causal com o L_on escolhido, janela de -1,0 a +0,8 s;
- os 23 atributos de atributos.tempo();
- validação cruzada por pessoa (5 dobras) e limiar com 95 % das janelas de impacto acima dele.

O SVM usa os parâmetros da comparação (padronização, C = 3, gamma "scale", classes balanceadas), em duas
versões: treinado com 6000 janelas de atividade sorteadas, como no 03, e com todas.

Um bootstrap por pessoa (as pessoas sorteadas com reposição, as mesmas para todos os modelos) dá o intervalo de
95 % da diferença entre cada modelo e o Random Forest:
- em alarmes falsos/h, cada um no seu limiar (o das 95 % das janelas de impacto);
- em quedas detectadas com <= 1 alarme falso/h, recalculado em cada sorteio.

Saída: resultados/svm_pulseira.json. Não muda o modelo exportado nem os vetores de teste do firmware.

    uv run python experimentos/11_svm_pulseira.py
"""

import importlib.util
import json

import _comum
import numpy as np

from queda import atributos, avaliacao, modelos
from queda.dados import FS
from queda.pipeline import PRE_S, montar

CENARIO = "A"
BOOTSTRAP = 2000


def carregar_script(nome):
    """Importa um script de experimentos/ cujo nome começa com número."""
    spec = importlib.util.spec_from_file_location(nome.removesuffix(".py"), _comum.RAIZ_MODELO / "experimentos" / nome)
    modulo = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(modulo)
    return modulo


M09 = carregar_script("09_exportar_pulseira.py")

MODELOS = {
    "lr": ("Regressão logística", lambda: modelos.Sklearn("regressao_logistica")),
    "rf": ("Random Forest 60 × 10", M09.RandomForestPulseira),
    "svm_6000": ("SVM (RBF), 6000 atividades", lambda: modelos.Sklearn("svm", max_negativos=6000)),
    "svm_todas": ("SVM (RBF), todas as janelas", lambda: modelos.Sklearn("svm")),
}


def custo(chave, modelo, n):
    """Bytes na flash e operações do classificador por janela (sem calcular os atributos), como o C faria."""
    m = modelo.modelo
    if chave == "lr":
        # média, escala e peso de cada atributo, mais o intercepto e o limiar; uma exponencial na sigmoide
        return {"bytes": 4 * (3 * n + 2), "ops": 3 * n, "exp": 1}
    if chave == "rf":
        nos = sum(e.tree_.node_count for e in m.estimators_)
        comparacoes = sum(e.get_depth() for e in m.estimators_)      # no máximo uma por nível de cada árvore
        return {"bytes": 12 * nos + 2 * len(m.estimators_), "nos": nos, "ops": comparacoes, "exp": 0}
    svc = m[-1]
    vs = svc.support_vectors_.shape[0]
    # cada vetor de suporte (n floats) e o seu coeficiente, a padronização (2n) e o intercepto;
    # por vetor: n diferenças, n quadrados e n somas, e uma exponencial
    return {"bytes": 4 * (vs * (n + 1) + 2 * n + 1), "vetores_suporte": int(vs), "ops": 3 * n * vs, "exp": int(vs)}


def por_pessoa(escores, limiar, conj):
    """Por pessoa: alarmes falsos no limiar dado, horas de atividade, quedas, e os escores das janelas de
    atividade e de impacto (para recalcular a sensibilidade com <= 1 alarme/h em cada sorteio)."""
    tabela, negativos, positivos = [], [], []
    for p in sorted(set(conj.pessoa)):
        sel = conj.pessoa == p
        horas = sum(e["duracao_s"] for e in conj.ensaios if e["pessoa"] == p and not e["queda"]) / 3600
        quedas = sum(1 for e in conj.ensaios if e["pessoa"] == p and e["queda"])
        tabela.append((int(((escores >= limiar) & sel & (conj.y == 0)).sum()), horas, quedas))
        negativos.append(escores[sel & (conj.y == 0)])
        positivos.append(escores[sel & (conj.y == 1)])
    return np.array(tabela, dtype=float), negativos, positivos


def sens_1alarme_h(amostra, tabela, negativos, positivos):
    """O mesmo cálculo de avaliacao.metricas, só com as pessoas sorteadas (repetidas quando saem duas vezes)."""
    permitidos = int(np.floor(tabela[amostra, 1].sum()))
    neg = np.concatenate([negativos[i] for i in amostra])
    pos = np.concatenate([positivos[i] for i in amostra])
    corte = -np.partition(-neg, permitidos)[permitidos] if permitidos < len(neg) else -np.inf
    return (pos > corte).sum() / max(tabela[amostra, 2].sum(), 1)


def main():
    sisfall, _ = _comum.carregar()
    pulseira = json.loads((_comum.RESULTADOS / "pulseira.json").read_text(encoding="utf-8"))
    l_on = pulseira["l_on_g"]
    conj = montar(sisfall, CENARIO, l_on, causal=True, distancia_s=M09.DISTANCIA_S)
    F, nomes = atributos.calcular(conj.janelas, int(PRE_S * FS), grupos=("tempo",))
    dobra = avaliacao.dobras(conj)
    print(f"cenário {CENARIO}, gatilho causal com L_on = {l_on} g: {len(conj.y)} janelas "
          f"({int(conj.y.sum())} de impacto), {len(nomes)} atributos, {len(set(conj.pessoa))} pessoas")

    resultado = {"cenario": CENARIO, "l_on_g": l_on, "janelas": int(len(conj.y)), "impactos": int(conj.y.sum()),
                 "atributos": len(nomes), "modelos": {}}
    tabelas = {}
    for chave, (nome, fabrica) in MODELOS.items():
        s, dt, _ = avaliacao.escores_fora_da_dobra(fabrica, conj, F, dobra)
        r = avaliacao.metricas(s, conj)
        if chave == "rf":
            assert abs(r.limiar - pulseira["rf"]["limiar_prob"]) < 1e-9, "não reproduz o 09: rode o 09 de novo"
        c = custo(chave, fabrica().ajustar(conj, F), len(nomes))
        tabelas[chave] = por_pessoa(s, r.limiar, conj)
        resultado["modelos"][chave] = {"nome": nome, "auc": r.auc, "sens_ensaio": r.sens_ensaio,
                                       "alarmes_h": r.alarmes_h, "sens_1alarme_h": r.sens_1alarme_h,
                                       "tempo_validacao_s": dt, **c}
        print(f"  {nome}: AUC {r.auc:.4f}, {r.alarmes_h:.2f} alarmes/h com {100 * r.sens_ensaio:.1f} % das quedas, "
              f"{100 * r.sens_1alarme_h:.1f} % com <= 1 alarme/h; {c['bytes'] / 1e3:.1f} kB, "
              f"{c['ops']} operações + {c['exp']} exp por janela ({dt:.0f} s)")

    # Bootstrap por pessoa: as mesmas pessoas sorteadas para todos os modelos (comparação pareada)
    rng = np.random.default_rng(modelos.SEMENTE)
    n_pessoas = len(tabelas["rf"][0])
    sorteio = rng.integers(0, n_pessoas, size=(BOOTSTRAP, n_pessoas))

    def alarmes_h(t):
        return t[sorteio, 0].sum(axis=1) / t[sorteio, 1].sum(axis=1)

    def sens(chave):
        return np.array([sens_1alarme_h(a, *tabelas[chave]) for a in sorteio])

    base_alarmes, base_sens = alarmes_h(tabelas["rf"][0]), sens("rf")
    print(f"bootstrap por pessoa ({BOOTSTRAP} sorteios), diferença para o Random Forest:")
    for chave in MODELOS:
        if chave == "rf":
            continue
        da = alarmes_h(tabelas[chave][0]) - base_alarmes
        ds = 100 * (sens(chave) - base_sens)
        ic_a, ic_s = np.percentile(da, [2.5, 97.5]), np.percentile(ds, [2.5, 97.5])
        resultado["modelos"][chave]["vs_rf"] = {
            "dif_alarmes_h": {"media": float(da.mean()), "ic95": ic_a.tolist(), "p_rf_melhor": float((da > 0).mean())},
            "dif_sens_1alarme_h_pp": {"media": float(ds.mean()), "ic95": ic_s.tolist(),
                                      "p_rf_melhor": float((ds < 0).mean())}}
        print(f"  {MODELOS[chave][0]}: alarmes/h {da.mean():+.2f} [{ic_a[0]:+.2f}, {ic_a[1]:+.2f}] "
              f"(RF melhor em {100 * (da > 0).mean():.0f} %); quedas com <= 1 alarme/h {ds.mean():+.1f} pp "
              f"[{ic_s[0]:+.1f}, {ic_s[1]:+.1f}] (RF melhor em {100 * (ds < 0).mean():.0f} %)")

    with open(_comum.RESULTADOS / "svm_pulseira.json", "w", encoding="utf-8", newline="\n") as f:
        json.dump(resultado, f, ensure_ascii=False, indent=2)
        f.write("\n")


if __name__ == "__main__":
    main()
