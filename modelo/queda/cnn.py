"""
CNN 1D sobre a janela reduzida (|a| e |ω| a 50 Hz, 90 amostras).

Cada camada de convolução é um banco de filtros FIR aprendidos: a rede aprende os próprios "templates",
em vez de recebê-los prontos como no filtro casado. Rede pequena (~6 mil parâmetros) para caber no ESP32-S3
depois (TFLite Micro / ESP-DL).
"""

from __future__ import annotations

import numpy as np

SEMENTE = 0


def _rede(canais: int):
    import torch.nn as nn
    return nn.Sequential(
        nn.Conv1d(canais, 16, 7, padding=3), nn.BatchNorm1d(16), nn.ReLU(), nn.MaxPool1d(2),
        nn.Conv1d(16, 32, 5, padding=2), nn.BatchNorm1d(32), nn.ReLU(), nn.MaxPool1d(2),
        nn.Conv1d(32, 32, 3, padding=1), nn.ReLU(),
        nn.AdaptiveAvgPool1d(1), nn.Flatten(), nn.Linear(32, 1),
    )


class CNN1D:
    """
    entrada="modulos": |a| e |ω| (não depende de como a placa está presa; é a que vale para os dados próprios).
    entrada="eixos": os 6 eixos a 50 Hz; enxerga a mudança de postura, mas só serve com a placa sempre na
    mesma orientação (dentro da SisFall).
    """

    def __init__(self, entrada: str = "modulos", epocas: int = 15, lote: int = 256, taxa: float = 1e-3):
        self.entrada, self.epocas, self.lote, self.taxa = entrada, epocas, lote, taxa

    def _bruta(self, conj):
        if self.entrada == "eixos":
            passo = conj.janelas.shape[1] // conj.reduzidas.shape[2]
            return np.transpose(conj.janelas[:, ::passo, :], (0, 2, 1)).astype(np.float32)
        return conj.reduzidas.astype(np.float32)

    def _entrada(self, conj):
        return (self._bruta(conj) - self.media) / self.desvio

    def ajustar(self, conj, F=None):
        import torch
        torch.manual_seed(SEMENTE)
        bruta = self._bruta(conj)
        self.media = bruta.mean(axis=(0, 2), keepdims=True)
        self.desvio = bruta.std(axis=(0, 2), keepdims=True) + 1e-6
        x = torch.from_numpy(self._entrada(conj))
        y = torch.from_numpy(conj.y.astype(np.float32))
        self.rede = _rede(x.shape[1])
        positivos = max(1.0, float(y.sum()))
        perda = torch.nn.BCEWithLogitsLoss(pos_weight=torch.tensor((len(y) - positivos) / positivos))
        otim = torch.optim.Adam(self.rede.parameters(), lr=self.taxa)
        gerador = torch.Generator().manual_seed(SEMENTE)
        self.rede.train()
        for _ in range(self.epocas):
            ordem = torch.randperm(len(y), generator=gerador)
            for i in range(0, len(y), self.lote):
                idx = ordem[i:i + self.lote]
                if len(idx) < 2:
                    continue
                otim.zero_grad()
                perda(self.rede(x[idx])[:, 0], y[idx]).backward()
                otim.step()
        return self

    def escore(self, conj, F=None):
        import torch
        self.rede.eval()
        with torch.no_grad():
            return self.rede(torch.from_numpy(self._entrada(conj)))[:, 0].numpy().astype(np.float64)

    def custo(self, n: int) -> tuple[int, int]:
        """MACs das convoluções e da camada linear, e bytes dos parâmetros em float32."""
        import torch.nn as nn
        macs, comprimento = 0, n
        for camada in self.rede:
            if isinstance(camada, nn.Conv1d):
                macs += camada.in_channels * camada.out_channels * camada.kernel_size[0] * comprimento
            elif isinstance(camada, nn.MaxPool1d):
                comprimento //= 2
            elif isinstance(camada, nn.Linear):
                macs += camada.in_features * camada.out_features
        params = sum(p.numel() for p in self.rede.parameters())
        return macs, 4 * params
