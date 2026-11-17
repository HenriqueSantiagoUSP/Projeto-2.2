"""
Prepara o relatório do Q2 (relatorios/Semestre 2/Projeto 3/Q2/latex/), a primeira versão do relatório final:
gráficos, tabelas, números e exportações da PCB. Rode depois de 09 e 11, que ele lê:

    uv run python experimentos/12_relatorio_q2.py

Depois, em Q2/latex/: `pdflatex main.tex`, duas vezes.

Gera em Q2/latex/figuras/:
- fig_lm35_caracterizacao: os 22 pontos do LM35DZ (S1 P2, Tabela 11) com o ajuste por mínimos quadrados, do
  sensor e da cadeia condicionada, e os resíduos em °C;
- fig_lm35_faixa_adc: quanto da faixa do ADC (12 dB) o sinal ocupa com e sem o ganho de 5,5;
- fig_rc_bode: resposta do filtro RC de 10 kΩ e 1 µF na entrada do ADC;
- fig_biquad: resposta do passa-baixa de 20 Hz da pulseira (os coeficientes de fall_model.c) e o DLPF do MPU;
- fig_mpu_cenarios: um ensaio próprio de cada cenário (eixos, |a| e |ω|);
- cópias de figuras do Q1 (relatório e apresentação) usadas no capítulo de modelos;
- da PCB: renders 3D e camadas de cobre em PDF (kicad-cli, que também roda o ERC e o DRC) e cada folha do
  esquema em PNG, recortada no circuito (pdftoppm).

E em Q2/latex/tabelas/: a lista de materiais, a comparação dos modelos da pulseira e numeros.tex (macros com os
números do texto que vêm de dados: ajuste do LM35, modelo, DRC/ERC, contagens).
"""

import csv
import json
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

import _comum
from _estilo import AZUL, COR_ATIVIDADE, COR_QUEDA, CRITICO, LARANJA, MUDO, TINTA2, VERDE_AGUA, VIOLETA, br
import matplotlib.pyplot as plt
import numpy as np
from scipy import signal

from queda.dados import carregar_proprios
from queda.pipeline import FC_PASSA_BAIXA, FS, coeficientes_passa_baixa

RAIZ = _comum.RAIZ_MODELO.parent
P3 = RAIZ / "relatorios" / "Semestre 2" / "Projeto 3"
Q1 = P3 / "Q1"
LATEX = P3 / "Q2" / "latex"
FIG = LATEX / "figuras"
TAB = LATEX / "tabelas"
R = _comum.RESULTADOS
PLACA = RAIZ / "hardware" / "placas" / "v1"
FONTE_PCB = RAIZ / "hardware" / "fontes" / "esquematico"
FALL_MODEL_C = RAIZ / "firmware" / "gadget" / "components" / "fall_pipeline" / "fall_model.c"
KICAD_CLI = Path("C:/Users/henri/AppData/Local/Programs/KiCad/10.0/bin/kicad-cli.exe")
L = 6.3                                   # polegadas: a largura do texto no A4 com margens de 3 e 2 cm

# --- LM35DZ: 22 pontos da caracterização do S1 P2 (Tabela 11 de "Projeto 2.pdf") ------------------------------
T_REF = np.array([0.0, 1.8, 4.6, 6.9, 9.0, 12.1, 14.2, 16.5, 18.3, 20.0, 21.7, 24.1, 25.7, 28.8, 30.5, 33.3,
                  38.2, 43.2, 47.7, 53.6, 58.4, 62.2])
V_SENSOR = np.array([38.95, 48.34, 72.20, 91.80, 108.80, 135.60, 157.40, 177.30, 191.60, 206.30, 221.60, 243.40,
                     269.90, 288.50, 305.90, 332.10, 380.10, 426.70, 468.50, 523.60, 569.90, 603.60])
V_COND = np.array([213.0, 265.1, 398.7, 507.1, 602.2, 751.0, 871.7, 982.9, 1062.5, 1143.7, 1228.7, 1350.4,
                   1447.2, 1601.0, 1697.5, 1843.6, 2111.2, 2370.0, 2605.6, 2910.5, 3167.3, 3354.5])
# Curvas usadas desde o S1 (firmware/room/components/sensors/include/lm35.h): o script confere que o ajuste as
# reproduz.
ALFA_S1, BETA_S1 = 9.2246, 27.1227               # sensor: V = alfa T + beta (mV)
ALFA_CADEIA, BETA_CADEIA = 51.3335, 144.739      # cadeia: T = (V - beta) / alfa
R_RC, C_RC = 10e3, 1e-6                          # filtro na entrada do ADC
ADC_FUNDO_MV, ADC_BITS = 3100.0, 12              # faixa útil a 12 dB (hardware/README.md) e resolução
T_MAX_UTIL = 57.0                                # LM35_MAX_VALID_C
DLPF_MPU_HZ = 44.0                               # CONFIG_DLPF_44HZ do mpu6050.c

plt.rcParams.update({"savefig.dpi": 200})
numeros: dict[str, str] = {}


def num(nome, valor):
    """Guarda um número para numeros.tex (\\nome)."""
    numeros[nome] = valor


def salvar(fig, nome):
    fig.savefig(FIG / nome, bbox_inches="tight")
    plt.close(fig)
    print("  ", nome)


def ajuste(x, y):
    a, b = np.polyfit(x, y, 1)
    r2 = 1 - np.sum((y - (a * x + b)) ** 2) / np.sum((y - y.mean()) ** 2)
    return a, b, r2


# --- figuras do LM35 --------------------------------------------------------------------------------------------
def lm35():
    a_s, b_s, r2_s = ajuste(T_REF, V_SENSOR)
    a_c, b_c, r2_c = ajuste(T_REF, V_COND)
    for medido, esperado, nome in ((a_s, ALFA_S1, "alfa do sensor"), (b_s, BETA_S1, "beta do sensor"),
                                   (a_c, ALFA_CADEIA, "alfa da cadeia"), (b_c, BETA_CADEIA, "beta da cadeia")):
        assert abs(medido - esperado) < 1e-3 * max(1, abs(esperado)), (nome, medido, esperado)
    res_s = (V_SENSOR - (a_s * T_REF + b_s)) / a_s          # em °C
    res_c = (V_COND - (a_c * T_REF + b_c)) / a_c
    ganho = a_c / a_s
    num("lmPontos", str(len(T_REF)))
    num("lmAlfa", br(a_s, 4)), num("lmBeta", br(b_s, 4)), num("lmRdoisSensor", br(r2_s, 4))
    num("lmAlfaCadeia", br(a_c, 4)), num("lmBetaCadeia", br(b_c, 3)), num("lmRdoisCadeia", br(r2_c, 4))
    num("lmGanhoMedido", br(ganho, 2))
    num("lmResMaxSensor", br(np.abs(res_s).max(), 2)), num("lmResMaxCadeia", br(np.abs(res_c).max(), 2))
    num("lmResRmsCadeia", br(np.sqrt(np.mean(res_c ** 2)), 2))

    fig, eixos = plt.subplots(2, 2, figsize=(L, 4.4), height_ratios=[2.2, 1], sharex=True)
    t = np.linspace(0, 65, 2)
    for col, (v, a, b, r2, res, cor, titulo) in enumerate((
            (V_SENSOR, a_s, b_s, r2_s, res_s, AZUL, "Saída do LM35DZ"),
            (V_COND, a_c, b_c, r2_c, res_c, LARANJA, "Saída do LM358 (G = 5,5)"))):
        ax = eixos[0, col]
        ax.plot(t, a * t + b, color=MUDO, lw=1.0, zorder=1,
                label=f"V = {br(a, 3)} T + {br(b, 1)} mV\nR² = {br(r2, 4)}")
        ax.scatter(T_REF, v, s=14, color=cor, zorder=2, label=f"{len(T_REF)} pontos medidos")
        ax.set_title(titulo)
        ax.set_ylabel("tensão (mV)")
        ax.legend(loc="upper left", handlelength=1.2)
        ax = eixos[1, col]
        ax.axhline(0, color=MUDO, lw=0.8)
        ax.stem(T_REF, res, linefmt=cor, markerfmt="o", basefmt=" ")
        ax.set_ylabel("resíduo (°C)")
        ax.set_xlabel("temperatura de referência (°C)")
    for ax in eixos[1]:
        ax.set_ylim(-1.6, 1.6)
    fig.align_ylabels(eixos[:, 0])
    fig.tight_layout()
    salvar(fig, "fig_lm35_caracterizacao.png")

    # faixa do ADC ocupada em 0–57 °C, com e sem o amplificador
    lsb = ADC_FUNDO_MV / 2 ** ADC_BITS
    faixas = [("sem ganho (LM35 direto)", a_s * 0 + b_s, a_s * T_MAX_UTIL + b_s, a_s, AZUL),
              ("com o LM358 (G = 5,5)", b_c, a_c * T_MAX_UTIL + b_c, a_c, LARANJA)]
    fig, ax = plt.subplots(figsize=(L, 1.9))
    for i, (rotulo, v0, v1, inclinacao, cor) in enumerate(faixas):
        ax.barh(i, ADC_FUNDO_MV, color="#eeede8", height=0.5)
        ax.barh(i, v1 - v0, left=v0, color=cor, height=0.5)
        ocup = (v1 - v0) / ADC_FUNDO_MV * 100
        ax.text(ADC_FUNDO_MV + 40, i, f"{br(ocup, 0)} % da faixa; {br(lsb / inclinacao * 1000, 0)} m°C por LSB",
                va="center", fontsize=8.5, color=TINTA2)
        num("lmOcupSemGanho" if i == 0 else "lmOcupComGanho", br(ocup, 0))
        num("lmResolSemGanho" if i == 0 else "lmResolComGanho", br(lsb / inclinacao, 3))
    num("adcLsb", br(lsb, 2))
    num("lmVmaxCadeia", br((a_c * T_MAX_UTIL + b_c) / 1000, 2))
    ax.set_yticks([0, 1], [f[0] for f in faixas])
    ax.set_xlim(0, ADC_FUNDO_MV * 1.62)
    ax.set_xticks([0, 1000, 2000, 3000])
    ax.set_xlabel("tensão na entrada do ADC (mV), de 0 a 57 °C; fundo de escala ~3,1 V a 12 dB")
    ax.grid(axis="y", visible=False)
    ax.invert_yaxis()
    fig.tight_layout()
    salvar(fig, "fig_lm35_faixa_adc.png")


def rc_bode():
    fc = 1 / (2 * np.pi * R_RC * C_RC)
    f = np.logspace(-1, 4, 600)
    h = 1 / (1 + 1j * f / fc)
    db = 20 * np.log10(np.abs(h))

    def atenuacao(freq):
        return 20 * np.log10(abs(1 / (1 + 1j * freq / fc)))

    num("rcFc", br(fc, 1)), num("rcSessenta", br(-atenuacao(60), 0)), num("rcMil", br(-atenuacao(1000), 0))
    num("rcAssentaMs", br(5 * R_RC * C_RC * 1000, 0))
    fig, ax = plt.subplots(figsize=(L, 2.5))
    ax.semilogx(f, db, color=AZUL)
    ax.axvline(50, color=VIOLETA, lw=0.9, ls="--")
    ax.text(50 * 0.9, -57, "Nyquist das rajadas\ndo ADC (50 Hz)", fontsize=7.8, color=VIOLETA, ha="right")
    for freq, rot, cor, desloc in ((fc, f"fc = {br(fc, 1)} Hz (−3 dB)", AZUL, (-95, -6)),
                                   (60, f"rede, 60 Hz: {br(atenuacao(60), 0)} dB", CRITICO, (10, 8)),
                                   (1000, f"1 kHz: {br(atenuacao(1000), 0)} dB", TINTA2, (10, 6))):
        ax.plot(freq, atenuacao(freq), "o", color=cor, ms=4.5, zorder=3)
        ax.annotate(rot, (freq, atenuacao(freq)), xytext=desloc, textcoords="offset points", fontsize=7.8,
                    color=cor)
    ax.text(0.12, -50, "a temperatura varia em minutos\n(< 0,01 Hz): passa inteira", fontsize=7.8, color=TINTA2)
    ax.set_ylim(-60, 5)
    ax.set_xlim(0.1, 1e4)
    ax.set_xlabel("frequência (Hz)")
    ax.set_ylabel("ganho (dB)")
    fig.tight_layout()
    salvar(fig, "fig_rc_bode.png")


def biquad():
    b, a = coeficientes_passa_baixa()
    # fall_model_biquad = {b0, b1, b2, a1, a2}, gerado pelo 09 a partir da mesma função
    texto = FALL_MODEL_C.read_text(encoding="utf-8")
    bloco = re.search(r"fall_model_biquad\[5\]\s*=\s*\{([^}]*)\}", texto).group(1)
    firmware = np.array([float(x.strip().rstrip("f")) for x in bloco.split(",") if x.strip()])
    assert np.allclose(firmware, [b[0], b[1], b[2], a[1], a[2]], atol=1e-6), firmware
    f, h = signal.freqz(b, a, worN=2048, fs=FS)
    db = 20 * np.log10(np.maximum(np.abs(h), 1e-6))
    num("bqFc", br(FC_PASSA_BAIXA, 0)), num("bqFs", br(FS, 0))
    num("bqCoefs", ", ".join(br(v, 5).replace("-", "$-$") for v in firmware))
    fig, ax = plt.subplots(figsize=(L, 2.5))
    ax.plot(f, db, color=LARANJA, label="biquad da pulseira (Butterworth de 2ª ordem, 20 Hz)")
    ax.axvline(FC_PASSA_BAIXA, color=LARANJA, ls="--", lw=0.9)
    ax.axvline(DLPF_MPU_HZ, color=VIOLETA, ls="--", lw=0.9, label="DLPF do MPU-6050 (44 Hz, filtro digital dentro do sensor)")
    ax.axvspan(0.5, 12, color=VERDE_AGUA, alpha=0.18, label="banda dos movimentos do corpo (~0,5–12 Hz)")
    ax.set_xlim(0, FS / 2)
    ax.set_ylim(-50, 5)
    ax.set_xlabel("frequência (Hz); 100 Hz = Nyquist de 200 Hz")
    ax.set_ylabel("ganho (dB)")
    ax.legend(loc="upper right")
    fig.tight_layout()
    salvar(fig, "fig_biquad.png")


def mpu_cenarios():
    ensaios = {(e.codigo, e.rep): e for e in carregar_proprios()}
    cenarios = [("andando", "andando"), ("correndo", "correndo"), ("caindo_frente", "queda de frente"),
                ("caindo_joelho", "queda de joelhos"), ("caindo_costas", "queda de costas")]
    num("mpuEnsaios", str(len(ensaios)))
    fig, eixos = plt.subplots(3, 5, figsize=(L, 4.6), sharey="row")
    for c, (codigo, titulo) in enumerate(cenarios):
        e = ensaios[(codigo, 1)]
        t = np.arange(len(e.acc)) / FS
        ax = eixos[0, c]
        for j, (cor, nome) in enumerate(zip((AZUL, LARANJA, VERDE_AGUA), "xyz")):
            ax.plot(t, e.acc[:, j], color=cor, lw=0.8, label=f"a{nome}")
        ax.axhline(2, color=MUDO, lw=0.7, ls=":")
        ax.axhline(-2, color=MUDO, lw=0.7, ls=":")
        ax.set_title(titulo, fontsize=8.5)
        amag = np.linalg.norm(e.acc, axis=1)
        eixos[1, c].plot(t, amag, color=COR_QUEDA if e.queda else COR_ATIVIDADE, lw=0.9)
        eixos[1, c].axhline(np.sqrt(3) * 2, color=MUDO, lw=0.7, ls=":")
        eixos[2, c].plot(t, np.linalg.norm(e.gyr, axis=1), color=VIOLETA, lw=0.9)
        eixos[2, c].set_xlabel("tempo (s)", fontsize=8)
        for ax in eixos[:, c]:
            ax.tick_params(labelsize=7)
    eixos[0, 0].set_ylabel("eixos (g)", fontsize=8)
    eixos[1, 0].set_ylabel("|a| (g)", fontsize=8)
    eixos[2, 0].set_ylabel("|ω| (°/s)", fontsize=8)
    eixos[0, 0].legend(loc="lower left", fontsize=6.5, ncols=3, handlelength=0.8, columnspacing=0.6)
    fig.tight_layout(h_pad=0.6, w_pad=0.4)
    salvar(fig, "fig_mpu_cenarios.png")


# --- figuras reaproveitadas do Q1 --------------------------------------------------------------------------------
COPIAS = {
    Q1 / "latex" / "figuras" / "fig_pipeline.png": "fig_pipeline.png",
    Q1 / "latex" / "figuras" / "fig_saturacao.png": "fig_saturacao.png",
    Q1 / "latex" / "figuras" / "fig_resumo.png": "fig_resumo.png",
    Q1 / "latex" / "figuras" / "fig_proprios_ensaios.png": "fig_proprios_ensaios.png",
    Q1 / "apresentacao" / "figuras" / "ap_janela.png": "fig_janela.png",
    Q1 / "apresentacao" / "figuras" / "ap_importancia.png": "fig_importancia.png",
    Q1 / "apresentacao" / "figuras" / "ap_funil.png": "fig_funil_pulseira.png",
}


def copiar():
    for origem, destino in COPIAS.items():
        shutil.copyfile(origem, FIG / destino)
        print("  ", destino, "(cópia)")


# --- PCB pelo kicad-cli ------------------------------------------------------------------------------------------
def kicad(*args, **kw):
    r = subprocess.run([str(KICAD_CLI), *map(str, args)], capture_output=True, text=True, encoding="utf-8",
                       errors="replace", **kw)
    if r.returncode != 0:
        raise RuntimeError(f"kicad-cli {' '.join(map(str, args[:3]))}: {r.stderr or r.stdout}")
    return r


def recorte_camadas(placa: Path, margem=3.0, acima=7.0):
    """Recorte (trim do \\includegraphics, em mm) das camadas plotadas em A4 paisagem na escala 1:1: o KiCad põe
    a placa nas coordenadas dela na folha; a camada espelhada fica refletida em torno do centro da página.
    `acima` deixa à vista o desenho da antena da DevKit, que passa da borda de cima."""
    texto = placa.read_text(encoding="utf-8")
    m = re.search(r"\(gr_rect\s*\(start ([\d.]+) ([\d.]+)\)\s*\(end ([\d.]+) ([\d.]+)\)(?:(?!\(gr_).)*?"
                  r"\(layer \"Edge\.Cuts\"\)", texto, re.S)
    x0, y0, x1, y1 = map(float, m.groups())
    num("pcbLargura", br(x1 - x0, 1)), num("pcbAltura", br(y1 - y0, 1))
    largura, altura = 297.0, 210.0
    topo, base = y0 - acima, altura - y1 - margem
    frente = (x0 - margem, base, largura - x1 - margem, topo)
    verso = (largura - x1 - margem, base, x0 - margem, topo)
    for nome, (l, b, r, t) in (("pcbTrimF", frente), ("pcbTrimB", verso)):
        num(nome, f"{l:.1f}mm {b:.1f}mm {r:.1f}mm {t:.1f}mm")


def folhas_esquema(pdf: Path, dpi=250, folga_mm=4):
    """Cada folha do esquema em PNG, recortada no circuito: o desenho ocupa um canto pequeno da A4, e a moldura
    e o carimbo (canto de baixo à direita) não interessam no relatório."""
    from PIL import Image
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(["pdftoppm", "-r", str(dpi), "-png", str(pdf), str(Path(tmp) / "f")], check=True,
                       capture_output=True)
        for png in sorted(Path(tmp).glob("f-*.png")):
            pagina = int(png.stem.split("-")[1])
            im = Image.open(png).convert("RGB")
            a = np.asarray(im).astype(int)
            alt, larg = a.shape[:2]
            tinta = (a.min(axis=2) < 170) | (a.max(axis=2) - a.min(axis=2) > 60)    # traço escuro ou colorido
            m = int(15 / 25.4 * dpi)                                                 # moldura (~10–12 mm) e coordenadas
            tinta[:m, :] = tinta[-m:, :] = False
            tinta[:, :m] = tinta[:, -m:] = False
            tinta[int(0.75 * alt):, int(0.55 * larg):] = False                       # carimbo (~108 x 32 mm)
            ys, xs = np.nonzero(tinta)
            f = int(folga_mm / 25.4 * dpi)
            caixa = (max(xs.min() - f, 0), max(ys.min() - f, 0), min(xs.max() + f, larg), min(ys.max() + f, alt))
            im.crop(caixa).save(FIG / f"esquema_p{pagina}.png", optimize=True, dpi=(dpi, dpi))   # 1:1 no LaTeX
            print(f"   esquema_p{pagina}.png")


def pcb():
    folhas_esquema(PLACA / "esquema.pdf")
    recorte_camadas(FONTE_PCB / "esquematico.kicad_pcb")
    if not KICAD_CLI.exists():
        print("AVISO: kicad-cli não encontrado; ficam os renders e camadas de uma rodada anterior, se houver")
        return
    placa = FONTE_PCB / "esquematico.kicad_pcb"
    renders = {"pcb_3d_cima.png": ["--side", "top"], "pcb_3d_baixo.png": ["--side", "bottom"],
               "pcb_3d_perspectiva.png": ["--side", "top", "--perspective", "--rotate", "-50,0,20", "--zoom",
                                          "1.3"]}
    for nome, extra in renders.items():
        kicad("pcb", "render", "--quality", "high", "--width", "2400", "--height", "1500", "--background",
              "opaque", *extra, "-o", FIG / nome, placa)
        print("  ", nome)
    for nome, camadas, extra in (("pcb_fcu.pdf", "F.Cu,F.Silkscreen,Edge.Cuts", []),
                                 ("pcb_bcu.pdf", "B.Cu,Edge.Cuts", ["--mirror"])):
        kicad("pcb", "export", "pdf", "--mode-single", "--layers", camadas, "--drill-shape-opt", "2", *extra,
              "-o", FIG / nome, placa)
        print("  ", nome)
    with tempfile.TemporaryDirectory() as tmp:
        drc_json = Path(tmp) / "drc.json"
        kicad("pcb", "drc", "--format", "json", "--schematic-parity", "-o", drc_json, placa)
        drc = json.loads(drc_json.read_text(encoding="utf-8"))
        erc_json = Path(tmp) / "erc.json"
        kicad("sch", "erc", "--format", "json", "-o", erc_json, FONTE_PCB / "esquematico.kicad_sch")
        erc = json.loads(erc_json.read_text(encoding="utf-8"))
    viol = drc.get("violations", [])
    erros = sum(1 for v in viol if v.get("severity") == "error")
    avisos = sum(1 for v in viol if v.get("severity") == "warning")
    erc_viol = sum(len(s.get("violations", [])) for s in erc.get("sheets", []))
    num("drcErros", str(erros)), num("drcAvisos", str(avisos))
    num("drcSemLigacao", str(len(drc.get("unconnected_items", []))))
    num("drcParidade", str(len(drc.get("schematic_parity", []))))
    num("ercViolacoes", str(erc_viol))
    print(f"   DRC: {erros} erros, {avisos} avisos, {len(drc.get('unconnected_items', []))} sem ligação, "
          f"{len(drc.get('schematic_parity', []))} divergências; ERC: {erc_viol}")


# --- tabelas -----------------------------------------------------------------------------------------------------
def tex(s):
    return (s.replace("\\", "\\textbackslash{}").replace("_", "\\_").replace("&", "\\&").replace("%", "\\%")
            .replace("#", "\\#"))


# Função de cada linha da lista de materiais, pela primeira referência (o CSV do KiCad não a traz).
FUNCAO = {
    "C1": "desacoplamento (LM35, LM358, CAN, PIR, botão, MPU)", "C3": "filtro RC na entrada do ADC (com R3)",
    "C8": "reservatório na entrada de 5 V", "D1": "impede juntar duas fontes no 5 V da DevKit",
    "J1": "módulo transceptor CAN SN65HVD230", "J2": "módulo PIR HC-SR501", "J3": "portas 1 e 2 (reed MC-38)",
    "J5": "módulo MPU-6050 (GY-521)", "J6": "módulo USB-C (entrada de 5 V)", "J7": "barramento CAN (CANH, CANL, GND)",
    "R1": "ganho do LM358 (para o GND)", "R2": "ganho do LM358 (realimentação)", "R3": "filtro RC (com C3)",
    "R4": "série na saída do PIR", "SW1": "botão ``estou bem''", "TP1": "ponto de teste", "TP2": "ponto de teste",
    "TP3": "ponto de teste", "TP4": "ponto de teste (saída do LM358)", "TP5": "ponto de teste", "TP6": "ponto de teste",
    "U1": "ESP32-S3-DevKitC-1 encaixada", "U2": "sensor de temperatura LM35DZ", "U3": "amp-op LM358 (soquete DIP-8)",
}


def tabela_bom():
    with open(PLACA / "lista-de-materiais.csv", encoding="utf-8", newline="") as f:
        linhas = list(csv.DictReader(f))
    total = sum(int(r["Qtd"]) for r in linhas)
    num("bomItens", str(len(linhas))), num("bomPecas", str(total))
    corpo = []
    for r in linhas:
        footprint = r["Footprint"].split(":", 1)[-1]
        ref = r["Referência"]
        funcao = FUNCAO[ref.split(",")[0].split("-")[0]]
        valor = tex(r["Valor"]).replace("uF", "\\,µF").replace("nF", "\\,nF").replace("\\_", "\\_\\allowbreak{}")
        quebravel = tex(footprint).replace("\\_", "\\_\\allowbreak{}")
        corpo.append(f"{tex(ref)} & {valor} & {funcao} & \\texttt{{\\scriptsize {quebravel}}} & "
                     f"{r['Qtd']} \\\\")
    (TAB / "bom.tex").write_text(
        "% Gerado por modelo/experimentos/12_relatorio_q2.py a partir de hardware/placas/v1/lista-de-materiais.csv\n"
        "\\begin{tabular}{@{}l >{\\raggedright\\arraybackslash}p{2.3cm} >{\\raggedright\\arraybackslash}p{4.4cm} "
        ">{\\raggedright\\arraybackslash}p{4.4cm} r@{}}\n\\toprule\n"
        "Referência & Valor & Função & Footprint & Qtd \\\\\n\\midrule\n"
        + "\n".join(corpo) + f"\n\\midrule\n\\multicolumn{{4}}{{@{{}}l}}{{Total}} & {total} \\\\\n"
        "\\bottomrule\n\\end{tabular}\n", encoding="utf-8", newline="\n")
    print("   bom.tex")


def tabela_modelos():
    vest = json.loads((R / "pulseira.json").read_text(encoding="utf-8"))
    svm = json.loads((R / "svm_pulseira.json").read_text(encoding="utf-8"))["modelos"]
    rf = vest["rf"]
    num("rfAlarmes", br(rf["alarmes_h"], 2)), num("rfQuedas", br(rf["sens_1alarme_h"] * 100, 1))
    num("rfLimiar", br(rf["limiar_prob"], 2)), num("rfBytes", f"{rf['bytes_flash']:,}".replace(",", "\\,"))
    num("rfKB", br(rf["bytes_flash"] / 1e3, 0)), num("rfNos", f"{rf['nos']:,}".replace(",", "\\,"))
    num("lrAlarmes", br(vest["lr"]["alarmes_h"], 2)), num("lrQuedas", br(vest["lr"]["sens_1alarme_h"] * 100, 1))
    num("gatilhoLon", br(vest["l_on_g"], 1)), num("gatilhoPassam", br(vest["gatilho_quedas_que_passam"] * 100, 1))
    num("gatilhoDisparosH", br(vest["gatilho_disparos_h"], 0)), num("sisPessoas", str(vest["pessoas"]))
    num("nAtributos", str(len(vest["atributos"]))), num("janelasTreino", f"{vest['janelas']:,}".replace(",", "\\,"))
    linhas = []
    for chave, nome in (("lr", "Regressão logística"), ("rf", "Random Forest (60 árvores, prof. 10)"),
                        ("svm_todas", "SVM (RBF)")):
        m = svm[chave]
        kb = (rf["bytes_flash"] if chave == "rf" else m["bytes"]) / 1e3
        ops = f"{m['ops']:,}".replace(",", "\\,")
        linhas.append(f"{nome} & {br(m['alarmes_h'], 2)} & {br(m['sens_1alarme_h'] * 100, 1)}\\,\\% & "
                      f"{br(kb, 1)} & {ops} \\\\")
    (TAB / "modelos_pulseira.tex").write_text(
        "% Gerado por modelo/experimentos/12_relatorio_q2.py a partir de resultados/pulseira.json e svm_pulseira.json\n"
        "\\begin{tabular}{@{}lrrrr@{}}\n\\toprule\n"
        "Classificador & \\makecell[r]{Alarmes\\\\falsos/h} & \\makecell[r]{Quedas com\\\\$\\le$1 alarme/h} & "
        "\\makecell[r]{Memória\\\\(kB)} & \\makecell[r]{Operações\\\\por janela} \\\\\n\\midrule\n"
        + "\n".join(linhas) + "\n\\bottomrule\n\\end{tabular}\n", encoding="utf-8", newline="\n")
    print("   modelos_pulseira.tex")


def gravar_numeros():
    corpo = "\n".join(f"\\newcommand{{\\{k}}}{{{v}}}" for k, v in sorted(numeros.items()))
    (TAB / "numeros.tex").write_text(
        "% Gerado por modelo/experimentos/12_relatorio_q2.py: números do texto que vêm de dados.\n" + corpo + "\n",
        encoding="utf-8", newline="\n")
    print(f"   numeros.tex ({len(numeros)} números)")


def main():
    FIG.mkdir(parents=True, exist_ok=True)
    TAB.mkdir(parents=True, exist_ok=True)
    print("Figuras:")
    lm35()
    rc_bode()
    biquad()
    mpu_cenarios()
    copiar()
    print("PCB:")
    pcb()
    print("Tabelas:")
    tabela_bom()
    tabela_modelos()
    gravar_numeros()


if __name__ == "__main__":
    main()
