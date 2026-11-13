"""
Avaliação dos modelos.

- Validação cruzada **por pessoa** (StratifiedGroupKFold, 5 dobras): a mesma pessoa nunca está no treino e
  no teste. Com janelas da mesma pessoa dos dois lados, os números ficariam inflados.
- Cada janela recebe o escore do modelo treinado sem a pessoa dela (escore "fora da dobra").
- Ponto de operação: o limiar que mantém 95 % das janelas de impacto acima dele. Nesse ponto:
  - sensibilidade **por ensaio**: conta também as quedas que o gatilho perdeu;
  - especificidade por janela;
  - alarmes falsos por hora de atividade.
- Também é relatada a sensibilidade máxima com no máximo 1 alarme falso por hora, e a AUC.
"""

from __future__ import annotations

import time
from dataclasses import dataclass

import numpy as np
from sklearn.metrics import roc_auc_score
from sklearn.model_selection import StratifiedGroupKFold

from .dados import FS, Ensaio, saturar
from .pipeline import CENARIOS, Conjunto, gatilho_histerese

SEMENTE = 0
SENS_ALVO = 0.95


def dobras(conj: Conjunto, n: int = 5) -> np.ndarray:
    """Número da dobra de cada janela; todas as janelas de uma pessoa ficam na mesma dobra."""
    dobra = np.full(len(conj.y), -1)
    divisor = StratifiedGroupKFold(n_splits=n, shuffle=True, random_state=SEMENTE)
    for k, (_, teste) in enumerate(divisor.split(np.zeros(len(conj.y)), conj.y, conj.pessoa)):
        dobra[teste] = k
    return dobra


def subconjunto(conj: Conjunto, idx: np.ndarray) -> Conjunto:
    return Conjunto(conj.janelas[idx], conj.reduzidas[idx], conj.y[idx], conj.pessoa[idx], conj.codigo[idx],
                    conj.ensaio[idx], conj.ensaios)


def escores_fora_da_dobra(fabrica, conj: Conjunto, F: np.ndarray, dobra: np.ndarray):
    """Treina um modelo novo por dobra e devolve o escore de cada janela, o tempo e o último modelo."""
    escores = np.zeros(len(conj.y))
    inicio = time.perf_counter()
    modelo = None
    for k in np.unique(dobra):
        treino, teste = np.flatnonzero(dobra != k), np.flatnonzero(dobra == k)
        modelo = fabrica().ajustar(subconjunto(conj, treino), F[treino])
        escores[teste] = modelo.escore(subconjunto(conj, teste), F[teste])
    return escores, time.perf_counter() - inicio, modelo


@dataclass
class Resultado:
    auc: float
    limiar: float
    sens_ensaio: float
    espec: float
    alarmes_h: float
    sens_1alarme_h: float


def metricas(escores: np.ndarray, conj: Conjunto, sens_alvo: float = SENS_ALVO) -> Resultado:
    y = conj.y
    # Sem uma das classes o limiar seria inf e a AUC NaN, e o 09 exportaria isso para o firmware.
    if not (y == 1).any():
        raise ValueError("nenhuma janela de queda: sem limiar possível (L_on alto demais, ou o gatilho perdeu "
                         "todos os impactos?)")
    if not (y == 0).any():
        raise ValueError("nenhuma janela de atividade: não há como medir alarmes falsos")
    quedas_total = sum(1 for e in conj.ensaios if e["queda"])
    horas_adl = sum(e["duracao_s"] for e in conj.ensaios if not e["queda"]) / 3600
    pos, neg = np.sort(escores[y == 1]), escores[y == 0]
    auc = roc_auc_score(y, escores) if 0 < y.sum() < len(y) else float("nan")
    limiar = pos[int(np.floor((1 - sens_alvo) * len(pos)))] if len(pos) else np.inf
    sens = (pos >= limiar).sum() / max(quedas_total, 1)
    espec = (neg < limiar).mean() if len(neg) else float("nan")
    alarmes = (neg >= limiar).sum() / max(horas_adl, 1e-9)
    # maior sensibilidade com no máximo 1 alarme falso por hora de atividade
    permitidos = int(np.floor(horas_adl))
    neg_ord = np.sort(neg)[::-1]
    corte = neg_ord[permitidos] if permitidos < len(neg_ord) else -np.inf
    sens_1 = (pos > corte).sum() / max(quedas_total, 1)
    return Resultado(auc, float(limiar), sens, espec, alarmes, sens_1)


def por_codigo(escores: np.ndarray, conj: Conjunto, limiar: float) -> list[dict]:
    """Erros por tipo: quedas perdidas (inclusive pelo gatilho) e alarmes falsos por atividade."""
    linhas = []
    codigos = sorted({e["codigo"] for e in conj.ensaios})
    for c in codigos:
        ens = [e for e in conj.ensaios if e["codigo"] == c]
        queda = ens[0]["queda"]
        sel = conj.codigo == c
        if queda:
            detectadas = int((escores[sel] >= limiar).sum())
            linhas.append({"codigo": c, "queda": True, "ensaios": len(ens), "detectadas": detectadas,
                           "perdidas": len(ens) - detectadas,
                           "perdidas_no_gatilho": sum(1 for e in ens if not e["pegou_impacto"])})
        else:
            horas = sum(e["duracao_s"] for e in ens) / 3600
            alarmes = int((escores[sel] >= limiar).sum())
            linhas.append({"codigo": c, "queda": False, "ensaios": len(ens), "janelas": int(sel.sum()),
                           "alarmes_falsos": alarmes, "alarmes_h": alarmes / max(horas, 1e-9)})
    return linhas


def detector_do_firmware(ensaios: list[Ensaio], cenario: str, l_on_g2: float = 6.25, l_off_g2: float = 2.25,
                         max_evento_s: float = 1.0, espera_s: float = 30.0) -> dict:
    """
    Linha de base: o detector provisório do firmware até 2026-11-11 (fall_detect.c: histerese em |a|², sem
    filtro, 2,5 g / 1,5 g, espera de 30 s) decidindo sozinho. Queda detectada = algum evento no ensaio de queda; cada evento em atividade é um
    alarme falso.
    """
    faixa = CENARIOS[cenario]
    detectadas, alarmes, quedas, horas = 0, 0, 0, 0.0
    ensaios_adl, adl_com_alarme = 0, 0
    for e in ensaios:
        acc, _ = saturar(e.acc, e.gyr, faixa["g_max"], faixa["dps_max"])
        a2 = (acc.astype(np.float64) ** 2).sum(axis=1)
        # mesma histerese, em |a|², com a espera de 30 s contada do fim do evento, como no firmware
        eventos = len(gatilho_histerese(a2, l_on_g2, l_off_g2, max_evento_s=max_evento_s,
                                        refratario_s=espera_s))
        if e.queda:
            quedas += 1
            detectadas += int(eventos > 0)
        else:
            horas += e.duracao_s / 3600
            alarmes += eventos
            ensaios_adl += 1
            adl_com_alarme += int(eventos > 0)
    return {"sens_ensaio": detectadas / max(quedas, 1), "alarmes_h": alarmes / max(horas, 1e-9),
            "alarmes": alarmes, "horas_adl": horas, "detectadas": detectadas, "quedas": quedas,
            "ensaios_adl": ensaios_adl, "ensaios_adl_com_alarme": adl_com_alarme}
