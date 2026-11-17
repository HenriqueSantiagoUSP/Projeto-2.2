"""Casos degenerados que antes passavam calados e podiam virar um modelo inválido no firmware."""

import importlib.util
from pathlib import Path

import numpy as np
import pytest

from queda import avaliacao, modelos
from queda.pipeline import Conjunto

EXPERIMENTOS = Path(__file__).resolve().parents[1] / "experimentos"


def conjunto(y, pessoas=None):
    y = np.asarray(y)
    n = len(y)
    pessoas = np.asarray(pessoas if pessoas is not None else [f"p{i % 4}" for i in range(n)])
    ensaios = [{"queda": bool(v), "duracao_s": 10.0, "codigo": "x", "pessoa": p} for v, p in zip(y, pessoas)]
    return Conjunto(np.zeros((n, 4, 6), np.float32), np.zeros((n, 2, 1), np.float32), y, pessoas,
                    np.array(["x"] * n), np.arange(n), ensaios)


def test_metricas_sem_janela_de_queda_falha_alto():
    with pytest.raises(ValueError, match="queda"):
        avaliacao.metricas(np.array([0.1, 0.2, 0.3]), conjunto([0, 0, 0]))


def test_metricas_sem_janela_de_atividade_falha_alto():
    with pytest.raises(ValueError, match="atividade"):
        avaliacao.metricas(np.array([0.7, 0.8]), conjunto([1, 1]))


def test_treino_com_uma_classe_so_falha_com_mensagem():
    with pytest.raises(ValueError, match="uma classe"):
        modelos.Sklearn("random_forest").ajustar(conjunto([0, 0, 0, 0]), np.random.rand(4, 3))


def carregar_script(nome, monkeypatch):
    monkeypatch.syspath_prepend(str(EXPERIMENTOS))      # os scripts importam o _comum, que mora lá
    spec = importlib.util.spec_from_file_location(nome.removesuffix(".py"), EXPERIMENTOS / nome)
    modulo = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(modulo)
    return modulo


def test_gatilho_sem_l_on_que_passe_99_por_cento_para(monkeypatch):
    import pandas as pd
    m02 = carregar_script("02_gatilho.py", monkeypatch)
    grade = pd.DataFrame({"l_on_g": [1.5, 2.0, 2.5], "quedas_que_passam": [0.985, 0.97, 0.90]})
    with pytest.raises(SystemExit, match="99"):
        m02.escolher_l_on(grade, "A")
    grade.loc[0, "quedas_que_passam"] = 0.995
    assert m02.escolher_l_on(grade, "A") == 1.5


def test_numero_nao_finito_nunca_vira_literal_c(monkeypatch):
    monkeypatch.syspath_prepend(str(EXPERIMENTOS))      # o 09 importa o _comum, que mora lá
    spec = importlib.util.spec_from_file_location("m09", EXPERIMENTOS / "09_exportar_pulseira.py")
    m09 = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m09)
    assert m09.c_float(0.5) == "0.5f"
    for ruim in (np.inf, -np.inf, np.nan):
        with pytest.raises(ValueError):
            m09.c_float(ruim)
