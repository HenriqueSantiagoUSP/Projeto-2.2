"""
Pipeline de detecção, o mesmo que vai para o ESP32 depois:

    200 Hz → [1] passa-baixa → [2] gatilho por picos de |a| → só então [3] janela em torno do pico
           → [4] atributos → [5] classificador → [6] decisão

O gatilho não decide a queda: ele só acorda o classificador quando |a| passa de L_on. Correr, pular ou
sentar com força também passam do limiar, e cabe ao classificador separar esses casos da queda.

Este módulo faz as etapas 1 a 3 e monta o conjunto de janelas para treinar e avaliar:
- de cada ensaio de queda sai **uma** janela positiva, a do disparo mais perto do impacto (o maior |a| do
  ensaio). Os outros disparos do mesmo ensaio (os passos antes de escorregar, por exemplo) são ambíguos e
  ficam de fora;
- de cada ensaio de atividade saem **todas** as janelas dos disparos, todas negativas: cada uma classificada
  como queda é um alarme falso.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from scipy import signal

from .dados import FS, Ensaio, saturar

# Janela em torno do pico: 1,0 s antes (queda livre e impacto) e 0,8 s depois (o que o corpo faz em seguida).
# 0,8 s porque os ensaios próprios têm só 0,82–1,62 s depois do impacto. 360 amostras cabem no buffer
# circular de 512 do vestível.
PRE_S = 1.0
POS_S = 0.8
FC_PASSA_BAIXA = 20.0          # Hz: o impacto tem conteúdo até ~20 Hz; acima é ruído e vibração
FS_REDUZIDA = 50.0             # Hz: versão reduzida da janela para templates, DTW e CNN

# Faixas do sensor (por eixo): o cenário A é o vestível atual; o B, a configuração dos dados próprios.
CENARIOS = {
    "A": {"g_max": 8.0, "dps_max": 500.0, "descricao": "±8 g / ±500 °/s (vestível atual)"},
    "B": {"g_max": 2.0, "dps_max": 250.0, "descricao": "±2 g / ±250 °/s (dados próprios)"},
}


def passa_baixa(x: np.ndarray, fc: float = FC_PASSA_BAIXA, fs: float = FS) -> np.ndarray:
    """Butterworth de 2ª ordem, causal (um biquad, como o dsps_biquad_f32 do ESP-DSP)."""
    b, a = signal.butter(2, fc / (fs / 2))
    zi = signal.lfilter_zi(b, a)
    saida = np.empty_like(x, dtype=np.float32)
    for j in range(x.shape[1]):
        saida[:, j], _ = signal.lfilter(b, a, x[:, j], zi=zi * x[0, j])
    return saida


def gatilho(a_mag: np.ndarray, l_on: float, distancia_s: float = 0.5, fs: float = FS) -> list[int]:
    """
    Gatilho do pipeline: os picos de |a| acima de L_on, separados por pelo menos distancia_s (entre dois
    picos próximos fica o mais alto). O impacto, o maior pico de uma queda, nunca é escondido por um
    disparo anterior, o que acontecia com a histerese + período refratário (ver gatilho_histerese).

    No ESP: ao passar de L_on, acompanha o máximo e o emite quando passar distancia_s sem um valor maior.
    A janela já precisa de 0,8 s depois do pico, então a espera não atrasa a decisão.
    """
    picos, _ = signal.find_peaks(a_mag, height=l_on, distance=max(1, int(distancia_s * fs)))
    return [int(p) for p in picos]


def gatilho_causal(a_mag: np.ndarray, l_on: float, distancia_s: float = 0.5, fs: float = FS) -> list[int]:
    """
    O gatilho como ele roda no vestível (fall_pipeline.c), amostra a amostra e sem olhar o futuro:
    - começa a acompanhar um pico quando |a| sobe (|a|[n] > |a|[n-1]) e chega a L_on;
    - a cada valor maior, o pico passa para ele;
    - emite o pico quando passam distancia_s sem um valor maior.

    Difere do find_peaks só em sequências de picos crescentes a menos de distancia_s uns dos outros: aqui fica
    só o maior. O impacto, o maior pico de uma queda, sempre é emitido. Na SisFall (cenário A, L_on = 2,0 g) os
    dois deixam passar as mesmas 99,2 % das quedas, e este dispara menos (753 × 858 por hora de atividade).
    """
    d = max(1, int(distancia_s * fs))
    picos: list[int] = []
    rastreando, pico_v, pico_i = False, 0.0, 0
    anterior = float(a_mag[0]) if len(a_mag) else 0.0
    for n, x in enumerate(a_mag.tolist()):
        if rastreando:
            if x > pico_v:
                pico_v, pico_i = x, n
            elif n - pico_i >= d:
                picos.append(pico_i)
                rastreando = False
        if not rastreando and x >= l_on and x > anterior:
            rastreando, pico_v, pico_i = True, x, n
        anterior = x
    return picos


def gatilho_histerese(a_mag: np.ndarray, l_on: float, l_off: float | None = None, max_evento_s: float = 1.0,
                      refratario_s: float = 1.0, fs: float = FS) -> list[int]:
    """
    Histerese do detector provisório que o firmware usou até 2026-11-11 (fall_detect.c): o evento começa quando
    o sinal passa de L_on e acaba quando cai abaixo de L_off ou depois de max_evento_s. Devolve o índice do pico
    de cada evento. Depois de um evento, novos disparos esperam refratario_s. Usada na linha de base.
    """
    if l_off is None:
        l_off = max(1.1, 0.6 * l_on)
    l_off = min(l_off, l_on)
    max_amostras, refratario = int(max_evento_s * fs), max(1, int(refratario_s * fs))
    # Mesma lógica do laço amostra a amostra do firmware, pulando direto de evento em evento.
    acima = np.flatnonzero(a_mag > l_on)
    abaixo = np.flatnonzero(a_mag < l_off)
    picos, proximo = [], 0
    while True:
        k = np.searchsorted(acima, proximo)
        if k >= len(acima):
            return picos
        inicio = int(acima[k])
        j = np.searchsorted(abaixo, inicio + 1)
        fim = int(abaixo[j]) if j < len(abaixo) else len(a_mag)
        fim = min(fim, inicio + max_amostras)
        picos.append(inicio + int(np.argmax(a_mag[inicio:fim + 1])))
        proximo = fim + refratario


def recortar(x: np.ndarray, centro: int, pre: int, pos: int) -> np.ndarray:
    """Janela [centro - pre, centro + pos), repetindo a borda quando o ensaio acaba antes."""
    idx = np.clip(np.arange(centro - pre, centro + pos), 0, len(x) - 1)
    return x[idx]


@dataclass
class Conjunto:
    """Janelas prontas para os modelos, com uma linha por janela."""
    janelas: np.ndarray        # (K, L, 6): ax, ay, az (g), gx, gy, gz (°/s), filtrado, a 200 Hz
    reduzidas: np.ndarray      # (K, 2, L/4): |a| e |ω| a 50 Hz (templates, DTW, CNN)
    y: np.ndarray              # 1 = queda
    pessoa: np.ndarray
    codigo: np.ndarray
    ensaio: np.ndarray         # índice do ensaio de origem
    ensaios: list[dict]        # um por ensaio: pessoa, código, queda, duração, se o gatilho pegou o impacto


def montar(ensaios: list[Ensaio], cenario: str, l_on: float, pre_s: float = PRE_S, pos_s: float = POS_S,
           tolerancia_s: float = 0.5, causal: bool = False) -> Conjunto:
    """
    Aplica saturação do cenário, filtro e gatilho a cada ensaio e recorta as janelas. `causal` usa o gatilho do
    vestível (gatilho_causal) no lugar do find_peaks: é o que treina o modelo exportado para o firmware.
    """
    acionar = gatilho_causal if causal else gatilho
    faixa = CENARIOS[cenario]
    pre, pos = int(pre_s * FS), int(pos_s * FS)
    passo = int(FS / FS_REDUZIDA)
    janelas, reduzidas, y, pessoa, codigo, origem, info = [], [], [], [], [], [], []
    for k, e in enumerate(ensaios):
        acc, gyr = saturar(e.acc, e.gyr, faixa["g_max"], faixa["dps_max"])
        x = passa_baixa(np.hstack([acc, gyr]))
        a_mag = np.linalg.norm(x[:, 0:3], axis=1)
        picos = acionar(a_mag, l_on)
        if e.queda:
            impacto = int(np.argmax(a_mag))
            perto = [p for p in picos if abs(p - impacto) <= tolerancia_s * FS]
            escolhidos = [min(perto, key=lambda p: abs(p - impacto))] if perto else []
        else:
            escolhidos = picos
        info.append({"pessoa": e.pessoa, "codigo": e.codigo, "queda": e.queda, "duracao_s": e.duracao_s,
                     "disparos": len(picos), "pegou_impacto": bool(escolhidos) if e.queda else None,
                     "arquivo": e.arquivo, "pico_g": float(a_mag.max())})
        for p in escolhidos:
            w = recortar(x, p, pre, pos)
            janelas.append(w)
            mag = np.stack([np.linalg.norm(w[:, 0:3], axis=1), np.linalg.norm(w[:, 3:6], axis=1)])
            reduzidas.append(mag[:, ::passo])
            y.append(int(e.queda)); pessoa.append(e.pessoa); codigo.append(e.codigo); origem.append(k)
    vazio = np.zeros((0, pre + pos, 6), np.float32)
    return Conjunto(np.asarray(janelas, np.float32) if janelas else vazio,
                    np.asarray(reduzidas, np.float32) if reduzidas else np.zeros((0, 2, (pre + pos) // passo)),
                    np.asarray(y), np.asarray(pessoa), np.asarray(codigo), np.asarray(origem), info)
