"""Leitura dos ensaios próprios: os dois formatos de arquivo e os defeitos que não podem passar calados."""

import numpy as np
import pytest

from queda import dados


def gravar(pasta, nome, linhas):
    pasta.mkdir(parents=True, exist_ok=True)
    (pasta / nome).write_text("\n".join(linhas) + "\n", encoding="utf-8")


def parado(n, t0=0, passo_ms=5, bruto=False):
    """n amostras paradas, deitadas no eixo z: 1 g (4096 contagens no formato bruto)."""
    az = "4096" if bruto else "1.0000"
    return [f"{t0 + i * passo_ms},0,0,{az},0,0,0" for i in range(n)]


def test_formato_antigo_em_g(tmp_path):
    gravar(tmp_path / "andando_1", "andando_1.csv", parado(400, passo_ms=10))
    (e,) = dados.carregar_proprios(tmp_path)
    assert (e.codigo, e.pessoa, e.queda) == ("andando", "p01", False)
    assert np.median(np.linalg.norm(e.acc, axis=1)) == pytest.approx(1.0, abs=1e-3)


def test_formato_do_protocolo_de_coleta_em_contagens(tmp_path):
    # docs/coleta-de-dados.md: p03_queda_frente_2.csv, valores brutos (4096 LSB/g), com o prefixo CSV:
    linhas = ["CSV:t_ms,ax,ay,az,gx,gy,gz"] + ["CSV:" + l for l in parado(400, bruto=True)]
    gravar(tmp_path / "sessao", "p03_queda_frente_2.csv", linhas)
    (e,) = dados.carregar_proprios(tmp_path)
    assert (e.codigo, e.pessoa, e.rep, e.queda) == ("queda_frente", "p03", 2, True)
    assert np.median(np.linalg.norm(e.acc, axis=1)) == pytest.approx(1.0, abs=1e-3)


def test_linha_nan_e_descartada(tmp_path):
    linhas = parado(400, passo_ms=10)
    linhas[200] = "2000,nan,0,1.0,0,0,0"
    gravar(tmp_path / "andando_1", "andando_1.csv", linhas)
    (e,) = dados.carregar_proprios(tmp_path)
    assert np.isfinite(e.acc).all() and np.isfinite(e.gyr).all()


def test_tempo_que_recomeca_fica_com_o_trecho_mais_longo(tmp_path):
    # O firmware recomeça a contagem a cada cabeçalho (sensor reconectado no meio do ensaio).
    linhas = parado(100, bruto=True) + parado(500, bruto=True)
    gravar(tmp_path / "s", "p01_andar_1.csv", linhas)
    (e,) = dados.carregar_proprios(tmp_path)
    assert e.duracao_s == pytest.approx(499 * 0.005, abs=0.01)


def test_arquivo_desconhecido_e_avisado(tmp_path, capsys):
    gravar(tmp_path / "s", "ensaio_estranho.csv", parado(400))
    gravar(tmp_path / "s", "metadados.csv", ["pessoa,idade", "p03,22"])
    assert dados.carregar_proprios(tmp_path) == []
    saida = capsys.readouterr().out
    assert "ensaio_estranho.csv" in saida
    assert "metadados.csv" not in saida


def test_pasta_ausente_e_avisada(tmp_path, capsys):
    assert dados.carregar_proprios(tmp_path / "nao_existe") == []
    assert "nao_existe" in capsys.readouterr().out
