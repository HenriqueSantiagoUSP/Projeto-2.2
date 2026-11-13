"""
Teste externo: modelos treinados na SisFall (cenário B, saturada em ±2 g / ±250 °/s, a mesma faixa dos nossos
dados) e testados nos 18 ensaios próprios (uma pessoa, sensor na cintura).

O limiar de cada modelo é o mesmo da comparação: o que mantém 95 % das janelas de impacto da SisFall acima
dele, calculado com os escores fora da dobra. Depois o modelo é treinado na SisFall inteira e aplicado aos
nossos ensaios.

Só entram atributos que não dependem de como a placa está presa: a correlação entre eixos (corr_xy, corr_xz,
corr_yz) sai, porque a nossa placa e a da SisFall têm os eixos em direções diferentes. A CNN dos 6 eixos fica
de fora pelo mesmo motivo.

Saídas: resultados/proprios_resumo.csv e resultados/proprios_por_arquivo.csv
"""

import json

import _comum
import numpy as np
import pandas as pd

from queda import atributos, avaliacao, modelos
from queda.cnn import CNN1D
from queda.dados import FS
from queda.pipeline import PRE_S, montar

CENARIO = "B"
DEPENDEM_DA_ORIENTACAO = {"corr_xy", "corr_xz", "corr_yz"}


def main():
    sisfall, proprios = _comum.carregar()
    l_on = json.loads((_comum.RESULTADOS / "gatilho_escolhido.json").read_text())[CENARIO]
    treino = montar(sisfall, CENARIO, l_on)
    teste = montar(proprios, CENARIO, l_on)
    # a amostra do pico na janela: o mesmo PRE_S que recorta a janela (pipeline) e que o 09 exporta
    pre = int(PRE_S * FS)
    F_tr, nomes = atributos.calcular(treino.janelas, pre)
    F_te, _ = atributos.calcular(teste.janelas, pre)
    cols = [i for i, n in enumerate(nomes) if n not in DEPENDEM_DA_ORIENTACAO]
    F_tr, F_te, nomes = F_tr[:, cols], F_te[:, cols], [nomes[i] for i in cols]
    print(f"treino SisFall-{CENARIO}: {len(treino.y)} janelas; teste próprio: {len(teste.y)} janelas "
          f"({int(teste.y.sum())} de impacto)")

    p_ar = int(pd.read_csv(_comum.RESULTADOS / f"ordem_ar_{CENARIO}.csv").sort_values("bic").iloc[0].p)
    metodos = {
        "regras_cascata": lambda: modelos.Regras(nomes),
        "corr_normalizada": lambda: modelos.Templates("corr_norm"),
        "dtw": lambda: modelos.Templates("dtw"),
        f"ar{p_ar}_anomalia": lambda: modelos.ARAnomalia(p_ar),
        "centroide_cosseno": lambda: modelos.CentroideMaisProximo("cosseno"),
        "knn_manhattan": lambda: modelos.Sklearn("knn_manhattan"),
        "regressao_logistica": lambda: modelos.Sklearn("regressao_logistica"),
        "svm_rbf": lambda: modelos.Sklearn("svm", max_negativos=6000),
        "random_forest": lambda: modelos.Sklearn("random_forest"),
        "cnn_1d": lambda: CNN1D("modulos"),
    }
    dobra = avaliacao.dobras(treino)
    base = avaliacao.detector_do_firmware(proprios, CENARIO)
    # Os totais saem dos dados: com outra coleta (o pulso), as tabelas continuam certas.
    n_quedas = sum(1 for e in proprios if e.queda)
    n_adl = len(proprios) - n_quedas
    resumo = [{"metodo": "limiar_do_firmware (2,5 g)", "quedas_detectadas": base["detectadas"],
               "quedas": n_quedas, "alarmes_falsos": base["alarmes"],
               "ensaios_adl_com_alarme": base["ensaios_adl_com_alarme"], "ensaios_adl": n_adl}]
    por_arquivo = {i: {"arquivo": e["arquivo"], "codigo": e["codigo"], "queda": e["queda"],
                       "gatilho_pegou": e["pegou_impacto"], "disparos": e["disparos"]}
                   for i, e in enumerate(teste.ensaios)}
    for nome, fabrica in metodos.items():
        oof, _, _ = avaliacao.escores_fora_da_dobra(fabrica, treino, F_tr, dobra)
        limiar = avaliacao.metricas(oof, treino).limiar
        modelo = fabrica().ajustar(treino, F_tr)
        s = modelo.escore(teste, F_te)
        alarme = s >= limiar
        detectadas = int(alarme[teste.y == 1].sum())
        falsos = int(alarme[teste.y == 0].sum())
        adl_com_alarme = len({int(k) for k in teste.ensaio[(teste.y == 0) & alarme]})
        resumo.append({"metodo": nome, "quedas_detectadas": detectadas, "quedas": n_quedas,
                       "alarmes_falsos": falsos, "ensaios_adl_com_alarme": adl_com_alarme, "ensaios_adl": n_adl})
        for k in por_arquivo:
            sel = teste.ensaio == k
            por_arquivo[k][nome] = int(alarme[sel].sum())
        print(f"  {nome:22} quedas {detectadas}/{n_quedas}   alarmes falsos {falsos:2d} "
              f"(em {adl_com_alarme} de {n_adl} ensaios de atividade)")
    pd.DataFrame(resumo).to_csv(_comum.RESULTADOS / "proprios_resumo.csv", index=False)
    pd.DataFrame(list(por_arquivo.values())).to_csv(_comum.RESULTADOS / "proprios_por_arquivo.csv", index=False)
    print("limiar do firmware:", base)


if __name__ == "__main__":
    main()
