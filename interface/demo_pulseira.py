#!/usr/bin/env python3
"""
demo_pulseira.py — o modelo de queda ao vivo, com uma placa só ligada ao computador.

Lê a serial da pulseira gravada com o perfil de demonstração (firmware/gadget/sdkconfig.demo.defaults):
- linhas CSV: cada amostra bruta, a 200 Hz, que viram o gráfico do |a| filtrado;
- linhas FEAT: cada disparo do gatilho, com os atributos, a P(queda) e a decisão do Random Forest da placa.

Mostra tudo numa janela. Com --mqtt-host, também faz o papel do hub para a central, no contrato de
docs/protocolo.md:
- status online;
- pulseira/<id> com a atividade, como o hub: o primeiro logo e depois um resumo a cada 5 min;
- o alerta de queda com a P(queda), que aparece no Grafana e no Telegram;
- o botão da placa: segurar 3 s vira o alerta "ajuda", e dois toques cancelam o último alerta.

A placa decide; este programa só mostra e repassa.

Uso
---
    pip install -r requirements-demo.txt                              # uma vez
    python demo_pulseira.py --porta COM5                              # só a janela
    python demo_pulseira.py --porta COM5 --mqtt-host 192.168.0.10 --senha <MQTT_HUB_PASSWORD>
    python demo_pulseira.py --porta COM5 --gravar sessao.log          # guarda a sessão
    python demo_pulseira.py --reproduzir sessao.log                   # ensaio sem a placa

Na janela: c = "estou bem" (cancela o último alerta na central, como os dois toques); q = sai.
"""

import argparse
import importlib.util
import json
import math
import os
import re
import sys
import threading
import time
from collections import deque
from dataclasses import dataclass, field

FS_HZ = 200.0                   # taxa do MPU-6050 na pulseira
PERIODO_S = 1.0 / FS_HZ
LSB_POR_G = 4096.0              # ±8 g
FC_HZ = 20.0                    # o passa-baixa do pipeline (modelo/queda/pipeline.py, FC_PASSA_BAIXA)
INTERVALO_PULSEIRA_S = 300.0    # como o hub: um resumo da pulseira a cada 5 min (docs/protocolo.md §1)
PRIMEIRO_RESUMO_S = 30.0        # o primeiro sai logo, como o hub faz com o primeiro heartbeat
FAIXA_QUEDA_S = 4.0             # quanto tempo a faixa "QUEDA DETECTADA" fica na tela
REABRIR_S = 1.0                 # intervalo entre as tentativas de reabrir a porta quando ela cai

ANSI = re.compile(r"\x1b\[[0-9;]*m")
LOG_ESP = re.compile(r"^([IWE]) \((\d+)\) ([\w.-]+): (.*)$")
PULSEIRA = re.compile(r"wearable ([0-9a-f]{6}), MAC")
MODELO = re.compile(r"fall pipeline: trigger ([\d.]+) g, (.+?), fall when P >= ([\d.]+)")
CANCELAMENTO = re.compile(r"(FALL|HELP)_CANCEL for seq \d+")   # os dois toques no botão da placa

# Os atributos que o painel mostra, com o nome do Python (modelo/queda/atributos.py).
ATRIBUTOS_PAINEL = [("a_max", "pico de |a|", "g", 2), ("queda_livre_min", "queda livre (mín. de |a|)", "g", 2),
                    ("pos_std", "quietude no fim (desvio)", "g", 3), ("inclinacao_graus", "mudança de inclinação",
                                                                       "°", 0)]


def br(valor, casas=2):
    return f"{valor:.{casas}f}".replace(".", ",")


# ---------------------------------------------------------------------------------------------------------
# Leitura das linhas da serial (funções puras, testadas em test_demo_pulseira.py)
# ---------------------------------------------------------------------------------------------------------

def interpretar_linha(linha):
    """
    Uma linha da serial vira (tipo, dados), ou None se não interessa:
    - ("inicio", None): cabeçalho CSV, o sensor (re)começou e a contagem de amostras recomeça;
    - ("amostra", (t_ms, ax, ay, az)): aceleração em g;
    - ("janela", {...}): uma linha FEAT, com os números convertidos;
    - ("pulseira", "a1b2c3"), ("modelo", {...}): do log de boot;
    - ("ajuda", None): o botão foi segurado (pedido de ajuda);
    - ("cancelamento", "queda" ou "ajuda"): dois toques no botão cancelaram o último alerta da placa;
    - ("aviso", "texto"): um aviso (W) ou erro (E) do log, para mostrar na janela.
    """
    linha = ANSI.sub("", linha).strip()
    if linha.startswith("CSV:"):
        corpo = linha[4:]
        if corpo.startswith("t_ms"):
            return ("inicio", None)
        partes = corpo.split(",")
        if len(partes) != 7:
            return None
        try:
            t_ms, ax, ay, az = (int(p) for p in partes[:4])
        except ValueError:
            return None
        return ("amostra", (t_ms, ax / LSB_POR_G, ay / LSB_POR_G, az / LSB_POR_G))
    if linha.startswith("FEAT:"):
        dados = {}
        for par in linha[5:].split(","):
            chave, _, valor = par.partition("=")
            if not valor:
                continue
            try:
                dados[chave] = float(valor)
            except ValueError:
                return None
        if "pico" not in dados or "prob" not in dados:
            return None
        try:
            dados["pico"] = int(dados["pico"])
            dados["queda"] = int(dados.get("queda", 0))
        except (ValueError, OverflowError):      # inf ou nan numa linha estragada
            return None
        return ("janela", dados)
    m = PULSEIRA.search(linha)
    if m:
        return ("pulseira", m.group(1))
    m = MODELO.search(linha)
    if m:
        try:
            return ("modelo", {"l_on": float(m.group(1)), "classificador": m.group(2),
                               "limiar": float(m.group(3))})
        except ValueError:                       # números emendados por bytes perdidos ("0.732.5123")
            return None
    m = LOG_ESP.match(linha)
    if not m:
        return None
    mensagem = m.group(4)
    if mensagem.startswith("HELP ("):
        return ("ajuda", None)
    cancelamento = CANCELAMENTO.fullmatch(mensagem)
    if cancelamento:
        return ("cancelamento", "queda" if cancelamento.group(1) == "FALL" else "ajuda")
    if m.group(1) in "WE" and not mensagem.startswith("FALL ("):
        return ("aviso", mensagem)
    return None


def coeficientes_butterworth(fc=FC_HZ, fs=FS_HZ):
    """Passa-baixa Butterworth de 2ª ordem pela transformação bilinear: (b0, b1, b2, a1, a2), como o firmware."""
    k = math.tan(math.pi * fc / fs)
    norma = 1.0 / (1.0 + math.sqrt(2.0) * k + k * k)
    b0 = k * k * norma
    return (b0, 2.0 * b0, b0, 2.0 * (k * k - 1.0) * norma, (1.0 - math.sqrt(2.0) * k + k * k) * norma)


class Biquad:
    """Forma direta II, como o dsps_biquad_f32, partindo do repouso no primeiro valor (sem transitório)."""

    def __init__(self, coef):
        self.b0, self.b1, self.b2, self.a1, self.a2 = coef
        self.w = None

    def __call__(self, x):
        if self.w is None:
            w0 = x / (1.0 + self.a1 + self.a2)
            self.w = [w0, w0]
        d0 = x - self.a1 * self.w[0] - self.a2 * self.w[1]
        y = self.b0 * d0 + self.b1 * self.w[0] + self.b2 * self.w[1]
        self.w = [d0, self.w[0]]
        return y


def agora_ms():
    return int(time.time() * 1000)


def alerta_queda(pulseira, seq, estado, a_pico=None, prob=None, ts=None):
    """O alerta de queda como o hub o publica (docs/protocolo.md §1)."""
    detalhe = {}
    if estado == "ativo":
        detalhe = {"a2": round(a_pico * a_pico, 2), "prob": round(prob, 2)}
    return {"v": 1, "id": f"{pulseira}-{seq}", "tipo": "queda", "estado": estado,
            "origem": f"pulseira/{pulseira}", "detalhe": detalhe, "ts": ts if ts is not None else agora_ms()}


def alerta_ajuda(pulseira, seq, estado, ts=None):
    """O pedido de ajuda (botão segurado) como o hub o publica: sem detalhe."""
    return {"v": 1, "id": f"{pulseira}-{seq}", "tipo": "ajuda", "estado": estado,
            "origem": f"pulseira/{pulseira}", "detalhe": {}, "ts": ts if ts is not None else agora_ms()}


def rotulo_disparo(prob, queda, limiar):
    """
    O que a placa decidiu num disparo. "queda" é o que ela avisou; com P acima do limiar e sem aviso, o disparo
    caiu na espera depois de uma queda avisada (CONFIG_GADGET_FALL_COOLDOWN_S, 5 s no perfil de demonstração):
    a placa achou queda, mas não avisa duas vezes a mesma.
    """
    if queda:
        return "QUEDA"
    return "já avisada" if prob >= limiar else "não"


# ---------------------------------------------------------------------------------------------------------
# Estado compartilhado entre a leitura (thread) e a janela
# ---------------------------------------------------------------------------------------------------------

@dataclass
class Disparo:
    t: float
    a_pico: float
    prob: float
    queda: bool
    atributos: dict


@dataclass
class Estado:
    janela_s: float
    trava: threading.Lock = field(default_factory=threading.Lock)
    pulseira: str = "demo"
    modelo: dict = field(default_factory=lambda: {"l_on": 2.0, "classificador": "random forest", "limiar": 0.73})
    sinal: deque = field(default_factory=deque)          # (t em s, |a| filtrado)
    disparos: deque = field(default_factory=lambda: deque(maxlen=200))
    ultima: dict = None
    faixa_ate: float = 0.0                                # tempo da sessão até quando mostrar a faixa
    faixa_tipo: str = "queda"                             # a faixa é da queda ou do pedido de ajuda
    avisos: deque = field(default_factory=lambda: deque(maxlen=3))
    t_ultimo: float = 0.0                                 # tempo da sessão da última amostra
    ativ_soma: float = 0.0                                # para a atividade (RMS de |a| bruto − 1 g)
    ativ_n: int = 0
    n_quedas: int = 0
    serial_perdida: bool = False                          # a porta caiu e a leitura tenta reabri-la

    def __post_init__(self):
        # Teto em pontos, além do corte por tempo: um tempo absurdo numa linha estragada não faz o sinal crescer.
        self.sinal = deque(self.sinal, maxlen=int((self.janela_s + 2.0) * FS_HZ))


class Leitor:
    """Transforma as linhas no estado: filtra o sinal, mapeia cada FEAT para o tempo e avisa a ponte."""

    def __init__(self, estado, ponte=None):
        self.e = estado
        self.ponte = ponte
        self.filtros = None
        self.deslocamento = 0.0      # o tempo da sessão continua quando a contagem do sensor recomeça
        self.t_segmento = 0.0

    def linha(self, texto):
        r = interpretar_linha(texto)
        if r is None:
            return
        tipo, dados = r
        e = self.e
        with e.trava:
            if tipo == "inicio":
                self.deslocamento = e.t_ultimo + PERIODO_S if e.sinal else 0.0
                self.filtros = None
            elif tipo == "amostra":
                t_ms, ax, ay, az = dados
                if self.filtros is None:
                    coef = coeficientes_butterworth()
                    self.filtros = [Biquad(coef) for _ in range(3)]
                fx, fy, fz = (f(v) for f, v in zip(self.filtros, (ax, ay, az)))
                t = self.deslocamento + t_ms / 1000.0
                e.sinal.append((t, math.sqrt(fx * fx + fy * fy + fz * fz)))
                while e.sinal and e.sinal[0][0] < t - e.janela_s - 1.0:
                    e.sinal.popleft()
                e.t_ultimo = t
                bruto = math.sqrt(ax * ax + ay * ay + az * az)
                e.ativ_soma += (bruto - 1.0) ** 2
                e.ativ_n += 1
            elif tipo == "janela":
                t = self.deslocamento + dados["pico"] * PERIODO_S
                d = Disparo(t, dados.get("a_pico", 0.0), dados["prob"], bool(dados["queda"]), dados)
                e.disparos.append(d)
                e.ultima = dados
                if d.queda:
                    e.n_quedas += 1
                    e.faixa_ate = e.t_ultimo + FAIXA_QUEDA_S
                    e.faixa_tipo = "queda"
            elif tipo == "ajuda":
                e.faixa_ate = e.t_ultimo + FAIXA_QUEDA_S
                e.faixa_tipo = "ajuda"
            elif tipo == "cancelamento":
                e.faixa_ate = 0.0                         # "estou bem": a faixa sai
            elif tipo == "pulseira":
                e.pulseira = dados
            elif tipo == "modelo":
                e.modelo = dados
            elif tipo == "aviso":
                e.avisos.append(dados)
        if self.ponte is None:
            return
        if tipo == "janela" and dados["queda"]:
            self.ponte.queda(dados.get("a_pico", 0.0), dados["prob"])
        elif tipo == "ajuda":
            self.ponte.ajuda()
        elif tipo == "cancelamento":
            self.ponte.cancelar()


# ---------------------------------------------------------------------------------------------------------
# Fontes: a porta serial ou uma sessão gravada
# ---------------------------------------------------------------------------------------------------------

def abrir_serial(serial, porta, baud):
    conexao = serial.Serial()
    conexao.port, conexao.baudrate, conexao.timeout = porta, baud, 0.2
    # DTR e RTS desligados ao abrir: com eles ligados, a placa pode ficar em reset ou no modo de gravação.
    conexao.dtr = False
    conexao.rts = False
    conexao.open()
    return conexao


def reabrir_serial(serial, conexao, porta, baud, estado, parar, erro):
    """
    A porta caiu (no Windows: "GetOverlappedResult failed", quando o link USB pisca). A placa continua rodando, então
    a leitura tenta reabrir a mesma porta a cada REABRIR_S, sem o pulso de reset, até conseguir ou a janela fechar.
    """
    with estado.trava:
        estado.serial_perdida = True
    print(f"Serial {porta} perdida ({erro}); tentando reabrir a cada {REABRIR_S:g} s.", file=sys.stderr)
    if conexao is not None:
        try:
            conexao.close()
        except (serial.SerialException, OSError):
            pass
    while not parar.is_set():
        parar.wait(REABRIR_S)
        try:
            nova = abrir_serial(serial, porta, baud)
        except (serial.SerialException, OSError):
            continue
        with estado.trava:
            estado.serial_perdida = False
        print(f"Serial {porta} de volta.", file=sys.stderr)
        return nova
    return None


def ler_serial(porta, baud, leitor, gravar, parar, reiniciar=True):
    # Roda numa thread: um sys.exit aqui sumiria sem mensagem (o main confere o pyserial antes).
    import serial
    try:
        conexao = abrir_serial(serial, porta, baud)
    except (serial.SerialException, OSError) as erro:
        # Porta ocupada (o idf.py monitor, por exemplo) ou ainda não ligada: a janela mostra e espera.
        conexao = reabrir_serial(serial, None, porta, baud, leitor.e, parar, erro)
        if conexao is None:
            return
    if reiniciar:
        # Um pulso no EN (RTS), como o idf.py monitor: a placa reinicia e o log de boot diz o id e o modelo.
        conexao.rts = True
        time.sleep(0.1)
        conexao.rts = False
    arquivo = open(gravar, "w", encoding="utf-8", newline="\n") if gravar else None
    ja_avisados = set()
    try:
        while not parar.is_set():
            try:
                bruta = conexao.readline()
            except (serial.SerialException, OSError) as erro:
                conexao = reabrir_serial(serial, conexao, porta, baud, leitor.e, parar, erro)
                if conexao is None:
                    break
                continue
            if not bruta:
                continue
            texto = bruta.decode("utf-8", errors="replace")
            if arquivo:
                arquivo.write(texto if texto.endswith("\n") else texto + "\n")
            try:
                leitor.linha(texto)
            except Exception as erro:  # noqa: BLE001 — uma linha estranha não pode parar a leitura
                tipo = type(erro).__name__
                if tipo not in ja_avisados:
                    ja_avisados.add(tipo)
                    print(f"Linha ignorada ({tipo}: {erro}): {texto.strip()[:120]}", file=sys.stderr)
    finally:
        if conexao is not None:
            conexao.close()
        if arquivo:
            arquivo.close()


def reproduzir(caminho, leitor, parar, velocidade=1.0):
    """Toca uma sessão gravada no ritmo das amostras (t_ms), recomeçando a cada cabeçalho CSV."""
    with open(caminho, encoding="utf-8", errors="replace") as arquivo:
        linhas = arquivo.readlines()
    inicio = time.monotonic()
    base = 0.0
    for texto in linhas:
        if parar.is_set():
            return
        r = interpretar_linha(texto)
        if r and r[0] == "inicio":
            base = leitor.e.t_ultimo + PERIODO_S if leitor.e.sinal else 0.0
        elif r and r[0] == "amostra":
            alvo = inicio + (base + r[1][0] / 1000.0) / velocidade
            espera = alvo - time.monotonic()
            if espera > 0:
                time.sleep(espera)
        leitor.linha(texto)


def porta_unica():
    """A única porta serial à vista, ou None (com a lista impressa)."""
    try:
        from serial.tools import list_ports
    except ImportError:
        sys.exit("Biblioteca pyserial não encontrada. Instale com:  pip install -r requirements-demo.txt")
    portas = [p.device for p in list_ports.comports()]
    if len(portas) == 1:
        return portas[0]
    print("Portas encontradas:", ", ".join(portas) if portas else "nenhuma")
    return None


# ---------------------------------------------------------------------------------------------------------
# Ponte para a central: este programa no papel do hub
# ---------------------------------------------------------------------------------------------------------

class Ponte:
    def __init__(self, args, estado):
        try:
            import paho.mqtt.client as mqtt
        except ImportError:
            sys.exit("Biblioteca paho-mqtt não encontrada. Instale com:  pip install -r requirements-demo.txt")
        self.e = estado
        self.base = f"idoso/{args.site}/{args.quarto}"
        # Um seq por sessão que não repete o da anterior: o notificador descarta id + estado repetidos.
        self.seq = int(time.time()) % 50000
        self.aberto = None           # (seq, tipo) do último alerta da pulseira: o que o "estou bem" cancela
        self.conectado = False
        self.cli = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"demo-usb-{args.site}-{args.quarto}")
        self.cli.username_pw_set(args.usuario, args.senha)
        self.cli.will_set(f"{self.base}/status", json.dumps({"v": 1, "estado": "offline"}), qos=1, retain=True)
        self.cli.on_connect = self._ao_conectar
        self.cli.on_disconnect = self._ao_desconectar
        try:
            self.cli.connect(args.mqtt_host, args.mqtt_porta, keepalive=30)
        except OSError as erro:
            sys.exit(f"Não foi possível conectar em {args.mqtt_host}:{args.mqtt_porta} ({erro}). A central está no "
                     "ar? (No Windows, o serviço Mosquitto local captura localhost: use o IP da máquina.)")
        self.cli.loop_start()
        threading.Thread(target=self._pulseira_periodico, daemon=True).start()

    def publicar(self, sufixo, dados, retain):
        texto = json.dumps(dados, separators=(",", ":"), ensure_ascii=False)
        self.cli.publish(f"{self.base}/{sufixo}", texto, qos=1, retain=retain)
        print(f"[{time.strftime('%H:%M:%S')}] {sufixo:<18} {texto}")

    def _ao_conectar(self, client, userdata, flags, reason_code, properties):
        if reason_code.is_failure:
            print(f"Conexão recusada pelo broker: {reason_code} (usuário/senha?)")
            return
        self.conectado = True
        self.publicar("status", {"v": 1, "estado": "online", "fw": "demo-usb", "hora": True, "ts": agora_ms()}, True)

    def _ao_desconectar(self, client, userdata, flags, reason_code, properties):
        self.conectado = False

    def _pulseira_periodico(self):
        """pulseira/<id> como o hub a publica: o primeiro logo, depois um resumo a cada 5 min."""
        espera = PRIMEIRO_RESUMO_S
        while True:
            time.sleep(espera)
            with self.e.trava:
                n, soma, pulseira = self.e.ativ_n, self.e.ativ_soma, self.e.pulseira
                self.e.ativ_n, self.e.ativ_soma = 0, 0.0
            if n == 0:
                espera = PRIMEIRO_RESUMO_S   # sem amostras, como sem heartbeat: a pulseira não está viva
                continue
            espera = INTERVALO_PULSEIRA_S
            self.publicar(f"pulseira/{pulseira}", {"v": 1, "estado": "online",
                                                    "atividade": round(math.sqrt(soma / n), 3), "ts": agora_ms()},
                          True)

    def queda(self, a_pico, prob):
        self.seq += 1
        self.aberto = (self.seq, "queda")
        self.publicar("alerta", alerta_queda(self.e.pulseira, self.seq, "ativo", a_pico, prob), False)

    def ajuda(self):
        self.seq += 1
        self.aberto = (self.seq, "ajuda")
        self.publicar("alerta", alerta_ajuda(self.e.pulseira, self.seq, "ativo"), False)

    def cancelar(self):
        """Como na pulseira: o "estou bem" vale para o último alerta dela, queda ou ajuda."""
        if self.aberto is None:
            print("(nenhum alerta aberto para cancelar)")
            return
        seq, tipo = self.aberto
        alerta = alerta_queda(self.e.pulseira, seq, "cancelado") if tipo == "queda" else \
            alerta_ajuda(self.e.pulseira, seq, "cancelado")
        self.publicar("alerta", alerta, False)
        self.aberto = None

    def encerrar(self):
        self.publicar("status", {"v": 1, "estado": "offline", "ts": agora_ms()}, True)
        time.sleep(0.3)
        self.cli.loop_stop()
        self.cli.disconnect()


# ---------------------------------------------------------------------------------------------------------
# Janela
# ---------------------------------------------------------------------------------------------------------

AZUL, LARANJA, TINTA, TINTA2, CINZA = "#2a78d6", "#eb6834", "#0b0b0b", "#52514e", "#c3c2b7"


def janela(estado, ponte, args, parar):
    if args.salvar_quadro:
        import matplotlib
        matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.animation import FuncAnimation

    fig = plt.figure(figsize=(13, 6.5))
    grade = fig.add_gridspec(1, 2, width_ratios=[2.6, 1], left=0.06, right=0.98, top=0.8, bottom=0.11, wspace=0.08)
    ax = fig.add_subplot(grade[0])
    painel = fig.add_subplot(grade[1])
    painel.axis("off")
    linha, = ax.plot([], [], color=TINTA, lw=1.3)
    lon = ax.axhline(2.0, color=LARANJA, ls="--", lw=1.1)
    ax.set_xlim(-args.janela_s, 0)
    ax.set_ylim(0, 4)
    ax.set_xlabel("tempo (s)")
    ax.set_ylabel("|a| filtrado (g)")
    ax.grid(color="#e1e0d9", lw=0.6)
    for lado in ("top", "right"):
        ax.spines[lado].set_visible(False)
    titulo = fig.text(0.06, 0.95, "", fontsize=12, color=TINTA, va="top")
    faixa = fig.text(0.5, 0.86, "", fontsize=22, fontweight="bold", ha="center", va="center", color="white",
                     bbox={"boxstyle": "round,pad=0.4", "fc": LARANJA, "ec": "none"})
    rodape = fig.text(0.06, 0.02, "", fontsize=9, color=TINTA2)
    texto_painel = painel.text(0.0, 1.0, "", va="top", ha="left", fontsize=10, family="monospace", color=TINTA)
    marcas = []

    def quadro(_):
        for m in marcas:
            m.remove()
        marcas.clear()
        with estado.trava:
            pontos = list(estado.sinal)
            disparos = list(estado.disparos)
            ultima = dict(estado.ultima) if estado.ultima else None
            t_fim = estado.t_ultimo
            modelo = dict(estado.modelo)
            pulseira = estado.pulseira
            mostrar_faixa = t_fim < estado.faixa_ate
            faixa_tipo = estado.faixa_tipo
            avisos = list(estado.avisos)
            serial_perdida = estado.serial_perdida
        if pontos:
            linha.set_data([t - t_fim for t, _ in pontos], [a for _, a in pontos])
            topo = max(4.0, max(a for _, a in pontos) * 1.15)
            ax.set_ylim(0, topo)
        lon.set_ydata([modelo["l_on"], modelo["l_on"]])
        for d in disparos:
            x = d.t - t_fim
            if x < -args.janela_s:
                continue
            rotulo = rotulo_disparo(d.prob, d.queda, modelo["limiar"])
            cor = AZUL if rotulo == "não" else LARANJA
            # Já avisada: a mesma cor da queda, com o triângulo vazado.
            preenchimento = "white" if rotulo == "já avisada" else cor
            contorno = cor if rotulo == "já avisada" else "white"
            marcas.append(ax.plot([x], [d.a_pico], "v", color=cor, ms=10, mfc=preenchimento, mec=contorno,
                                  mew=1.5 if rotulo == "já avisada" else 0.8)[0])
            marcas.append(ax.text(x, d.a_pico, f" P={br(d.prob)}", color=cor, fontsize=10, fontweight="bold",
                                  va="bottom"))

        fonte = f"reproduzindo {os.path.basename(args.reproduzir)}" if args.reproduzir else f"porta {args.porta}"
        if serial_perdida:
            fonte += " PERDIDA, reabrindo…"
        central = "sem central" if ponte is None else ("central: conectado" if ponte.conectado else
                                                       "central: desconectado")
        titulo.set_text(f"Pulseira {pulseira} — {modelo['classificador']}, queda quando P ≥ "
                        f"{br(modelo['limiar'])}, gatilho {br(modelo['l_on'], 1)} g   |   {fonte}   |   {central}")
        if mostrar_faixa and faixa_tipo == "ajuda":
            faixa.set_text("PEDIDO DE AJUDA (botão segurado)")
            faixa.set_visible(True)
        elif mostrar_faixa and disparos:
            ultima_queda = next((d for d in reversed(disparos) if d.queda), disparos[-1])
            faixa.set_text(f"QUEDA DETECTADA — P = {br(ultima_queda.prob)}")
            faixa.set_visible(True)
        else:
            faixa.set_visible(False)

        linhas = ["Última janela"]
        if ultima:
            for chave, nome, unidade, casas in ATRIBUTOS_PAINEL:
                if chave in ultima:
                    linhas.append(f"  {nome:<27}{br(ultima[chave], casas):>7} {unidade}")
            decisao = rotulo_disparo(ultima["prob"], ultima["queda"], modelo["limiar"])
            decisao = "não é queda" if decisao == "não" else decisao
            linhas += [f"  {'P(queda)':<27}{br(ultima['prob']):>7}", f"  {'decisão':<27}{decisao:>7}"]
        else:
            linhas.append("  (nenhum disparo ainda)")
        linhas += ["", "Últimos disparos (pico e P)"]
        for d in list(reversed(disparos))[:6]:
            rotulo = rotulo_disparo(d.prob, d.queda, modelo["limiar"])
            linhas.append(f"  {br(d.t - t_fim, 1):>6} s  {br(d.a_pico):>5} g  P {br(d.prob)}  {rotulo}")
        texto_painel.set_text("\n".join(linhas))
        rodape.set_text("Avisos da placa: " + " | ".join(avisos) if avisos else
                        "c = estou bem (cancela o último alerta na central)   q = sair")
        return [linha, lon, titulo, faixa, texto_painel, rodape, *marcas]

    def tecla(evento):
        if evento.key == "c" and ponte is not None:
            ponte.cancelar()
        elif evento.key == "q":
            plt.close(fig)

    fig.canvas.mpl_connect("key_press_event", tecla)
    if args.salvar_quadro:
        fim = time.monotonic() + args.duracao
        while time.monotonic() < fim and not parar.is_set():
            time.sleep(0.2)
        quadro(0)
        fig.savefig(args.salvar_quadro, dpi=100)
        print(f"quadro salvo em {args.salvar_quadro}")
        return
    animacao = FuncAnimation(fig, quadro, interval=100, cache_frame_data=False)  # noqa: F841 (referência viva)
    plt.show()


# ---------------------------------------------------------------------------------------------------------

def parse_args():
    p = argparse.ArgumentParser(description="O modelo de queda da pulseira ao vivo, pela USB, com ponte opcional "
                                            "para a central.")
    p.add_argument("--porta", help="porta serial da placa (ex.: COM5); sem ela, usa a única porta encontrada")
    p.add_argument("--baud", type=int, default=921600, help="velocidade da serial (padrão: 921600, a do perfil "
                                                            "de demonstração)")
    p.add_argument("--sem-reset", action="store_true", help="não reinicia a placa ao abrir a porta (o id do "
                                                             "pulseira só aparece no próximo boot)")
    p.add_argument("--reproduzir", help="toca uma sessão gravada com --gravar, sem a placa")
    p.add_argument("--gravar", help="guarda tudo o que chega da serial neste arquivo")
    p.add_argument("--janela-s", type=float, default=10.0, help="segundos de sinal na tela (padrão: 10)")
    p.add_argument("--mqtt-host", help="endereço do broker da central; sem ele, não publica nada")
    p.add_argument("--mqtt-porta", type=int, default=1883)
    p.add_argument("--usuario", default="hub", help="usuário do broker (padrão: hub)")
    p.add_argument("--senha", default=os.environ.get("MQTT_HUB_PASSWORD", ""),
                   help="senha do usuário hub (padrão: variável MQTT_HUB_PASSWORD)")
    p.add_argument("--site", default="butanta", help="instituição no tópico (padrão: butanta)")
    p.add_argument("--quarto", default="quarto101", help="quarto no tópico (padrão: quarto101)")
    p.add_argument("--salvar-quadro", help=argparse.SUPPRESS)      # testes: salva um quadro e sai
    p.add_argument("--duracao", type=float, default=10.0, help=argparse.SUPPRESS)
    return p.parse_args()


def main():
    args = parse_args()
    if not args.reproduzir and not args.porta:
        args.porta = porta_unica()
        if args.porta is None:
            sys.exit("Informe a porta com --porta (ou use --reproduzir).")

    if not args.reproduzir and importlib.util.find_spec("serial") is None:
        sys.exit("Biblioteca pyserial não encontrada. Instale com:  pip install -r requirements-demo.txt")

    estado = Estado(janela_s=args.janela_s)
    ponte = Ponte(args, estado) if args.mqtt_host else None
    leitor = Leitor(estado, ponte)
    parar = threading.Event()
    if args.reproduzir:
        fonte = threading.Thread(target=reproduzir, args=(args.reproduzir, leitor, parar), daemon=True)
    else:
        fonte = threading.Thread(target=ler_serial, args=(args.porta, args.baud, leitor, args.gravar, parar,
                                                          not args.sem_reset), daemon=True)
    fonte.start()
    try:
        janela(estado, ponte, args, parar)
    finally:
        parar.set()
        fonte.join(timeout=1.0)
        if ponte is not None:
            ponte.encerrar()


if __name__ == "__main__":
    main()
