"""Testes dos blocos do pipeline contra valores conhecidos e os exemplos numéricos dos slides."""

import numpy as np
import pytest

from queda import atributos, modelos, pipeline
from queda.dados import FS


def test_ar2_recupera_o_exemplo_do_slide():
    # slides 9-10: x = [2, 3, 4, 5, 6, 7] → a1 = 2, a2 = -1
    x = np.array([[2, 3, 4, 5, 6, 7]], dtype=float)
    coef, residuo = atributos.ajustar_ar(x, 2)
    assert coef[0] == pytest.approx([2.0, -1.0], abs=1e-6)
    assert residuo[0] == pytest.approx(0.0, abs=1e-6)


def test_dtw_do_slide_da_distancia_zero():
    # slides 46-47: r = [1, 3, 1] e x = [1, 1, 3, 3, 1] têm a mesma forma → DTW = 0
    d = modelos.dtw_lote(np.array([1.0, 3.0, 1.0]), np.array([[1.0, 1.0, 3.0, 3.0, 1.0]]), raio=5)
    assert d[0] == pytest.approx(0.0)


def test_dtw_de_sinais_diferentes_e_positiva():
    d = modelos.dtw_lote(np.array([0.0, 0.0, 0.0]), np.array([[1.0, 1.0, 1.0]]), raio=2)
    assert d[0] == pytest.approx(3.0)


def test_correlacao_normalizada_de_um_sinal_consigo_mesmo_e_1():
    x = np.sin(np.linspace(0, 6, 90))[None, :] * 3 + 1
    assert modelos.correlacao_maxima(x, x[0], 10, normalizada=True)[0] == pytest.approx(1.0)
    # a forma importa, a escala não: 5·x ainda dá 1 na normalizada, mas muda a não normalizada
    assert modelos.correlacao_maxima(5 * x, x[0], 10, normalizada=True)[0] == pytest.approx(1.0)
    assert modelos.correlacao_maxima(5 * x, x[0], 10, normalizada=False)[0] == pytest.approx(
        5 * modelos.correlacao_maxima(x, x[0], 10, normalizada=False)[0])


def test_momentos_iguais_ao_signal_proc():
    t = np.arange(1000) / FS
    x = (np.sin(2 * np.pi * 5 * t))[None, :]
    m = atributos._momentos(x)
    assert m["rms"][0] == pytest.approx(1 / np.sqrt(2), rel=1e-3)
    assert m["crest"][0] == pytest.approx(np.sqrt(2), rel=1e-3)
    assert m["mean"][0] == pytest.approx(0.0, abs=1e-3)


def test_curtose_da_normal_e_perto_de_3():
    x = np.random.default_rng(0).normal(size=(1, 200_000))
    assert atributos.curtose(x)[0] == pytest.approx(3.0, abs=0.05)


def test_zcr_de_seno_de_5_hz():
    t = np.arange(400) / FS                 # 2 s → 20 cruzamentos em 399 intervalos
    x = np.sin(2 * np.pi * 5 * t + 0.1)[None, :]
    assert atributos.zcr(x)[0] == pytest.approx(20 / 399, abs=2 / 399)


def test_fft_acha_a_frequencia_dominante():
    t = np.arange(360) / FS
    acc = np.zeros((1, 360, 6))
    acc[0, :, 2] = 1 + 0.5 * np.sin(2 * np.pi * 3 * t)
    f = atributos.frequencia(acc)
    assert f["fft_dominante_hz"][0] == pytest.approx(3.0, abs=FS / 512)


def test_histerese_da_um_pico_por_evento():
    a = np.ones(1000)
    a[100:110] = [1.5, 2.2, 2.8, 3.1, 2.5, 2.1, 2.3, 1.9, 1.2, 1.0]    # um impacto que oscila
    a[600:603] = [2.6, 3.4, 2.0]
    picos = pipeline.gatilho_histerese(a, l_on=2.0, l_off=1.3)
    assert picos == [103, 601]


def test_histerese_respeita_o_periodo_refratario():
    a = np.ones(1000)
    a[100], a[150] = 3.0, 3.0                # 0,25 s depois: ainda dentro de 1 s
    assert pipeline.gatilho_histerese(a, l_on=2.0, refratario_s=1.0) == [100]


def test_gatilho_nao_esconde_o_impacto_atras_de_um_disparo_anterior():
    # passos de 2,2 g e, 0,3 s depois do último, um impacto de 6 g: o impacto tem de ser disparado
    a = np.ones(1000)
    for i in (100, 200, 300, 400):
        a[i] = 2.2
    a[460] = 6.0
    picos = pipeline.gatilho(a, l_on=2.0)
    assert 460 in picos
    assert 400 not in picos                 # dentro de 0,5 s do impacto, fica só o mais alto
    assert pipeline.gatilho(a, l_on=7.0) == []


def test_gatilho_causal_emite_o_impacto_e_respeita_a_distancia():
    a = np.ones(1000)
    for i in (100, 200, 300, 400):
        a[i] = 2.2
    a[460] = 6.0
    picos = pipeline.gatilho_causal(a, l_on=2.0)
    assert picos == [100, 200, 300, 460]    # o passo em 400 é engolido pelo impacto, 60 amostras depois
    assert pipeline.gatilho_causal(a, l_on=7.0) == []


def test_gatilho_causal_fica_so_com_o_maior_de_uma_sequencia_crescente():
    # 3, 4 e 5 g a 90 amostras um do outro: o find_peaks fica com 3 e 5 (180 amostras entre eles); o causal,
    # que não pode voltar atrás, só com o 5
    a = np.ones(600)
    a[[0 + 50, 90 + 50, 180 + 50]] = [3.0, 4.0, 5.0]
    assert pipeline.gatilho(a, l_on=2.0) == [50, 230]
    assert pipeline.gatilho_causal(a, l_on=2.0) == [230]


def test_gatilho_causal_nao_comeca_numa_descida():
    # o sinal começa acima de L_on e só desce: não há pico a emitir
    a = np.linspace(4.0, 1.0, 300)
    assert pipeline.gatilho_causal(a, l_on=2.0) == []
    # e um pico que ainda não completou 0,5 s não é emitido
    b = np.ones(150)
    b[100] = 3.0
    assert pipeline.gatilho_causal(b, l_on=2.0) == []


def _gatilho_laco(a, l_on, l_off, max_amostras, refratario):
    """Laço amostra a amostra, como o fall_detect_feed() do firmware antigo: a referência da versão vetorizada."""
    picos, em_evento, inicio, pico, fim = [], False, 0, 0, -10 ** 9
    for i, v in enumerate(a):
        if em_evento:
            if v > a[pico]:
                pico = i
            if v < l_off or i - inicio >= max_amostras:
                em_evento, fim = False, i
                picos.append(pico)
            continue
        if v > l_on and i - fim >= refratario:
            em_evento, inicio, pico = True, i, i
    if em_evento:
        picos.append(pico)
    return picos


def test_gatilho_vetorizado_igual_ao_laco_do_firmware():
    rng = np.random.default_rng(1)
    for _ in range(50):
        a = np.abs(1 + 0.8 * rng.standard_normal(3000))
        esperado = _gatilho_laco(a, 2.0, 1.3, max_amostras=200, refratario=150)
        assert pipeline.gatilho_histerese(a, 2.0, 1.3, max_evento_s=1.0, refratario_s=0.75) == esperado


def test_janela_repete_a_borda_no_fim_do_ensaio():
    x = np.arange(10)[:, None].astype(float)
    w = pipeline.recortar(x, 8, 3, 4)
    assert w[:, 0].tolist() == [5, 6, 7, 8, 9, 9, 9]
