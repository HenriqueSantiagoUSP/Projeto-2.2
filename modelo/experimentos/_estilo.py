"""
Estilo comum das figuras explicativas (07_figuras_explicativas.py e 10_figuras_apresentacao.py): paleta,
parâmetros do matplotlib, nomes legíveis dos atributos e o salvamento em resultados/.
"""

import _comum
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

R = _comum.RESULTADOS

# Paleta (categórica validada para daltonismo nas 3 primeiras cores) e tinta neutra.
AZUL, LARANJA, VERDE_AGUA, VIOLETA, AMARELO = "#2a78d6", "#eb6834", "#1baf7a", "#6250d6", "#eda100"
BOM, CRITICO = "#0ca30c", "#d03b3b"
TINTA, TINTA2, MUDO, GRADE, EIXO = "#0b0b0b", "#52514e", "#898781", "#e1e0d9", "#c3c2b7"
COR_ATIVIDADE, COR_QUEDA, COR_BRUSCO = AZUL, LARANJA, VERDE_AGUA

plt.rcParams.update({
    "figure.facecolor": "white", "savefig.facecolor": "white", "savefig.dpi": 150,
    "font.size": 9.5, "axes.titlesize": 10.5, "axes.titlelocation": "left", "axes.titlecolor": TINTA,
    "axes.labelcolor": TINTA2, "axes.edgecolor": EIXO, "axes.spines.top": False, "axes.spines.right": False,
    "axes.grid": True, "grid.color": GRADE, "grid.linewidth": 0.6, "axes.axisbelow": True,
    "xtick.color": MUDO, "ytick.color": MUDO, "xtick.labelcolor": TINTA2, "ytick.labelcolor": TINTA2,
    "legend.frameon": False, "legend.fontsize": 8.5, "lines.linewidth": 1.4,
})

NOMES_ATR = {
    "a_mean": "média de |a|", "a_rms": "RMS de |a|", "a_std": "desvio de |a|", "a_min": "mínimo de |a|",
    "a_max": "pico de |a|", "a_crest": "fator de crista", "a_curtose": "curtose de |a|",
    "a_assimetria": "assimetria de |a|", "a_zcr": "cruzamentos por zero", "a_pico_a_pico": "pico a pico de |a|",
    "sma": "SMA (magnitude média dos eixos)", "queda_livre_min": "mínimo de |a| antes do impacto",
    "queda_livre_ate_impacto_s": "tempo da queda livre ao impacto", "pos_std": "desvio de |a| no fim (quietude)",
    "pos_media": "média de |a| no fim", "inclinacao_graus": "mudança de inclinação",
    "corr_xy": "|corr| entre x e y", "corr_xz": "|corr| entre x e z", "corr_yz": "|corr| entre y e z",
    "w_mean": "média de |ω|", "w_rms": "RMS de |ω|", "w_max": "pico de |ω|", "w_rotacao_graus": "rotação total",
    "fft_0.5_3hz": "energia 0,5–3 Hz", "fft_3_6hz": "energia 3–6 Hz", "fft_6_12hz": "energia 6–12 Hz",
    "fft_12_25hz": "energia 12–25 Hz", "fft_dominante_hz": "frequência dominante",
    "fft_centroide_hz": "centroide espectral",
}


def br(v, casas=1):
    """Número com vírgula decimal."""
    return f"{v:.{casas}f}".replace(".", ",")


def nome_atr(n):
    if n.startswith("ar4_a"):
        return f"coeficiente a{n[-1]} do AR(4)"
    if n == "ar4_residuo":
        return "resíduo do AR(4)"
    return NOMES_ATR.get(n, n)


def grupo_atr(n):
    return "FFT" if n.startswith("fft_") else "AR" if n.startswith("ar") else "tempo"


def salvar(fig, nome):
    fig.savefig(R / nome)
    plt.close(fig)
    print("  ", nome)
