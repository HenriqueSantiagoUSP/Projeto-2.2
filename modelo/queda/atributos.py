"""
Atributos de cada janela (slides "Segmentação e atributos", "TDF" e "Modelos Autorregressivos").

A janela tem o impacto na amostra `pre` (1,0 s): antes dele está a queda livre, depois a reação do corpo.
Os atributos usam módulos (|a|, |ω|) e ângulos entre vetores, que não dependem de como a placa está
presa: a SisFall e os nossos dados têm a placa orientada de jeitos diferentes.

- Tempo: as mesmas definições do signal_proc.c da pulseira (mean, rms, std, min, max, crest) mais curtose,
  assimetria, ZCR, SMA, pico a pico, queda livre, tempo entre a queda livre e o impacto, atividade depois
  do impacto, mudança de inclinação, correlação entre eixos e giroscópio.
- Frequência: FFT de |a| — fração de energia por banda, frequência dominante e centroide espectral.
- AR: coeficientes de um AR(p) ajustado a |a| na janela, por mínimos quadrados, e o desvio do resíduo.
"""

from __future__ import annotations

import numpy as np

from .dados import FS

BANDAS_HZ = [(0.5, 3.0), (3.0, 6.0), (6.0, 12.0), (12.0, 25.0)]
ORDEM_AR = 4


def _momentos(x: np.ndarray) -> dict[str, np.ndarray]:
    """Atributos do signal_proc.c (mesmas fórmulas), em lote: x é (K, N)."""
    media = x.mean(axis=1)
    media_q = (x * x).mean(axis=1)
    rms = np.sqrt(media_q)
    desvio = np.sqrt(np.maximum(media_q - media * media, 0.0))
    maximo = x.max(axis=1)
    return {"mean": media, "rms": rms, "std": desvio, "min": x.min(axis=1), "max": maximo,
            "crest": np.divide(maximo, rms, out=np.zeros_like(rms), where=rms > 0)}


def curtose(x: np.ndarray) -> np.ndarray:
    c = x - x.mean(axis=1, keepdims=True)
    m2 = (c ** 2).mean(axis=1)
    return np.divide((c ** 4).mean(axis=1), m2 ** 2, out=np.zeros_like(m2), where=m2 > 1e-12)


def assimetria(x: np.ndarray) -> np.ndarray:
    c = x - x.mean(axis=1, keepdims=True)
    m2 = (c ** 2).mean(axis=1)
    return np.divide((c ** 3).mean(axis=1), m2 ** 1.5, out=np.zeros_like(m2), where=m2 > 1e-12)


def zcr(x: np.ndarray) -> np.ndarray:
    """Taxa de cruzamentos por zero do sinal sem a média (cruzamentos por amostra)."""
    c = x - x.mean(axis=1, keepdims=True)
    return (np.diff(np.signbit(c), axis=1) != 0).mean(axis=1)


def _correlacao(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    a = a - a.mean(axis=1, keepdims=True)
    b = b - b.mean(axis=1, keepdims=True)
    den = np.sqrt((a * a).sum(axis=1) * (b * b).sum(axis=1))
    return np.divide((a * b).sum(axis=1), den, out=np.zeros(len(a)), where=den > 1e-12)


def _angulo_graus(u: np.ndarray, v: np.ndarray) -> np.ndarray:
    cos = (u * v).sum(axis=1) / np.maximum(np.linalg.norm(u, axis=1) * np.linalg.norm(v, axis=1), 1e-9)
    return np.degrees(np.arccos(np.clip(cos, -1.0, 1.0)))


def tempo(janelas: np.ndarray, pre: int) -> dict[str, np.ndarray]:
    acc, gyr = janelas[:, :, 0:3], janelas[:, :, 3:6]
    a = np.linalg.norm(acc, axis=2)
    w = np.linalg.norm(gyr, axis=2)
    f = {f"a_{k}": v for k, v in _momentos(a).items()}
    f["a_curtose"] = curtose(a)
    f["a_assimetria"] = assimetria(a)
    f["a_zcr"] = zcr(a)
    f["a_pico_a_pico"] = a.max(axis=1) - a.min(axis=1)
    f["sma"] = np.abs(acc).sum(axis=2).mean(axis=1)
    antes, depois = a[:, :pre], a[:, pre:]
    f["queda_livre_min"] = antes.min(axis=1)
    f["queda_livre_ate_impacto_s"] = (pre - antes.argmin(axis=1)) / FS
    fim = int(0.3 * FS)
    f["pos_std"] = depois[:, -fim:].std(axis=1)
    f["pos_media"] = depois[:, -fim:].mean(axis=1)
    inicio = int(0.5 * FS)
    f["inclinacao_graus"] = _angulo_graus(acc[:, :inicio].mean(axis=1), acc[:, -fim:].mean(axis=1))
    f["corr_xy"] = np.abs(_correlacao(acc[:, :, 0], acc[:, :, 1]))
    f["corr_xz"] = np.abs(_correlacao(acc[:, :, 0], acc[:, :, 2]))
    f["corr_yz"] = np.abs(_correlacao(acc[:, :, 1], acc[:, :, 2]))
    f.update({f"w_{k}": v for k, v in _momentos(w).items() if k in ("mean", "rms", "max")})
    f["w_rotacao_graus"] = w.sum(axis=1) / FS
    return f


def frequencia(janelas: np.ndarray, nfft: int = 512) -> dict[str, np.ndarray]:
    a = np.linalg.norm(janelas[:, :, 0:3], axis=2)
    a = (a - a.mean(axis=1, keepdims=True)) * np.hanning(a.shape[1])
    espectro = np.abs(np.fft.rfft(a, n=nfft, axis=1)) ** 2
    freqs = np.fft.rfftfreq(nfft, 1 / FS)
    total = np.maximum(espectro[:, freqs >= 0.5].sum(axis=1), 1e-12)
    f = {}
    for lo, hi in BANDAS_HZ:
        f[f"fft_{lo:g}_{hi:g}hz"] = espectro[:, (freqs >= lo) & (freqs < hi)].sum(axis=1) / total
    util = freqs >= 0.5
    f["fft_dominante_hz"] = freqs[util][np.argmax(espectro[:, util], axis=1)]
    f["fft_centroide_hz"] = (espectro[:, util] * freqs[util]).sum(axis=1) / total
    return f


def ajustar_ar(x: np.ndarray, p: int) -> tuple[np.ndarray, np.ndarray]:
    """
    AR(p) por mínimos quadrados em lote (slide 8: â = (AᵀA)⁻¹Aᵀb), sem termo constante.
    x é (K, N); devolve os coeficientes (K, p) e o desvio do resíduo (K,).
    """
    n = x.shape[1]
    A = np.stack([x[:, p - k - 1:n - k - 1] for k in range(p)], axis=2)    # colunas x[n-1], ..., x[n-p]
    b = x[:, p:]
    AtA = np.einsum("kni,knj->kij", A, A) + 1e-9 * np.eye(p)
    Atb = np.einsum("kni,kn->ki", A, b)
    coef = np.linalg.solve(AtA, Atb[..., None])[..., 0]
    residuo = b - np.einsum("kni,ki->kn", A, coef)
    return coef, residuo.std(axis=1)


def ar(janelas: np.ndarray, p: int = ORDEM_AR) -> dict[str, np.ndarray]:
    a = np.linalg.norm(janelas[:, :, 0:3], axis=2)
    a = a - a.mean(axis=1, keepdims=True)
    coef, desvio = ajustar_ar(a, p)
    f = {f"ar{p}_a{k + 1}": coef[:, k] for k in range(p)}
    f[f"ar{p}_residuo"] = desvio
    return f


def calcular(janelas: np.ndarray, pre: int, grupos=("tempo", "freq", "ar"), p: int = ORDEM_AR,
             bloco: int = 2000):
    """Matriz de atributos (K, F) e os nomes, na ordem dos grupos pedidos (em blocos, para caber na RAM)."""
    partes, nomes = [], []
    for i in range(0, max(len(janelas), 1), bloco):
        w = janelas[i:i + bloco].astype(np.float64)
        f: dict[str, np.ndarray] = {}
        if "tempo" in grupos:
            f.update(tempo(w, pre))
        if "freq" in grupos:
            f.update(frequencia(w))
        if "ar" in grupos:
            f.update(ar(w, p))
        nomes = list(f)
        partes.append(np.column_stack([f[k] for k in nomes]) if len(w) else np.zeros((0, len(nomes))))
    return np.vstack(partes), nomes
