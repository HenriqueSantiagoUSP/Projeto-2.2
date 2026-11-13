"""O cache da SisFall: relido quando os arquivos mudam, nunca reaproveitado de outra pasta, e à prova de truncamento."""

import pytest

from queda import dados

LINHA = "  17,-179, -99, -18,-504,-352,  76,-697,-279;\n"


@pytest.fixture
def sisfall(tmp_path, monkeypatch):
    monkeypatch.setattr(dados, "CACHE", tmp_path / "cache")
    raiz = tmp_path / "publicos"
    (raiz / "SA01").mkdir(parents=True)
    (raiz / "SA01" / "D01_SA01_R01.txt").write_text(LINHA * 50)
    return raiz


def test_arquivo_alterado_e_relido(sisfall):
    assert len(dados.carregar_sisfall(sisfall)[0].acc) == 50
    (sisfall / "SA01" / "D01_SA01_R01.txt").write_text(LINHA * 80)
    assert len(dados.carregar_sisfall(sisfall)[0].acc) == 80


def test_outra_pasta_nao_usa_o_cache(sisfall, tmp_path):
    dados.carregar_sisfall(sisfall)
    outra = tmp_path / "outra"
    (outra / "SE02").mkdir(parents=True)
    (outra / "SE02" / "F01_SE02_R03.txt").write_text(LINHA * 20)
    (e,) = dados.carregar_sisfall(outra)
    assert (e.pessoa, e.codigo, e.rep, len(e.acc)) == ("SE02", "F01", 3, 20)


def test_cache_truncado_e_relido(sisfall, capsys):
    dados.carregar_sisfall(sisfall)
    (cache,) = list(dados.CACHE.glob("*.pkl"))
    cache.write_bytes(cache.read_bytes()[:40])
    assert len(dados.carregar_sisfall(sisfall)[0].acc) == 50
    assert "cache" in capsys.readouterr().out.lower()
    assert not list(dados.CACHE.glob("*.tmp"))
