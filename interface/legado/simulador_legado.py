#!/usr/bin/env python3
"""
simulador.py — Simula o hub do quarto (ESP32-S3) publicando dados no broker MQTT.

Projeto: Sistema de Monitoramento de Idosos — Escola Politécnica da USP
Camada:  Interface aberta de dados (Q1 do relatório), lado "publicador".

O que este script faz
---------------------
Enquanto o gateway real (ESP32-S3) não está conectado, este script ocupa o lugar
dele: conecta-se ao Mosquitto pela porta 1883 (MQTT/TCP, igual ao firmware) e
publica, nos MESMOS tópicos e com o MESMO formato JSON, os quatro sensores:

    idoso/quarto101/temperatura  (QoS 0)  {"sensor":"LM35DZ","valor":22.5,"ts":244316}
    idoso/quarto101/porta        (QoS 1)  {"sensor":"MC-38","estado":"aberta","ts":250316}
    idoso/quarto101/presenca     (QoS 1)  {"sensor":"HC-SR501","evento":"presenca","ts":257196}
    idoso/quarto101/queda        (QoS 1)  {"sensor":"MPU-6050","evento":"queda","a2":7.88,"ts":243946}

Também reproduz o Last Will and Testament (LWT) descrito no relatório (§4.7):
registra no broker a mensagem "offline" para o tópico idoso/quarto101/status,
que o PRÓPRIO broker publica se a conexão cair sem aviso, e publica "online"
(retida) assim que conecta.

Uso
---
    pip install paho-mqtt                      # uma única vez
    python simulador.py                        # broker em localhost:1883, temperatura a cada 30 s
    python simulador.py --intervalo-temp 5     # demonstração mais dinâmica
    python simulador.py --host 192.168.0.10    # broker em outra máquina
    python simulador.py --auto-queda 60        # além do ENTER, uma queda automática a cada 60 s

Durante a execução:   ENTER = publica uma QUEDA        Ctrl+C = encerra
"""

import argparse
import json
import random
import sys
import threading
import time

try:
    import paho.mqtt.client as mqtt
except ImportError:  # biblioteca ausente: orienta em vez de mostrar um traceback
    sys.exit("Biblioteca paho-mqtt não encontrada. Instale com:  pip install paho-mqtt")

# -----------------------------------------------------------------------------
# 1) Parâmetros da simulação
# -----------------------------------------------------------------------------
TEMP_INICIAL = 22.5            # °C — mesmo valor do exemplo do relatório
TEMP_FAIXA = (19.0, 26.0)      # °C — o passeio aleatório fica preso a esta faixa
TEMP_PASSO = 0.3               # °C — variação máxima entre leituras consecutivas
PORTA_INTERVALO = (20, 60)     # s  — a porta abre/fecha em intervalos aleatórios nesta faixa
PRESENCA_INTERVALO = (10, 40)  # s  — idem para eventos de presença (PIR)
QUEDA_A2_FAIXA = (6.3, 12.0)   # g² — |a|² acima do limiar de 6,25 g² (|a| > 2,5 g, relatório §6.2)

# Nível de QoS de cada tópico (relatório, Tabela 2): telemetria tolera perda
# (QoS 0); eventos precisam chegar "pelo menos uma vez" (QoS 1).
QOS = {"temperatura": 0, "porta": 1, "presenca": 1, "queda": 1}

# Quais mensagens levam a flag RETAIN (relatório §4.7): o broker guarda a última
# mensagem retida de cada tópico e a entrega imediatamente a quem assinar
# depois — assim um dashboard recém-aberto já mostra o estado atual. Estados
# são retidos; a QUEDA nunca é, pois um alarme antigo reapresentado a cada
# nova conexão seria enganoso (o dashboard trata "queda retida" como histórico).
RETAIN = {"temperatura": True, "porta": True, "presenca": True, "queda": False}


# -----------------------------------------------------------------------------
# 2) Utilitários
# -----------------------------------------------------------------------------
T0 = time.monotonic()


def ts_ms():
    """Imita o campo "ts" do ESP32: milissegundos desde o boot (uptime).
    Não é hora civil — por isso o dashboard exibe a hora de recepção pelo relógio do PC."""
    return int((time.monotonic() - T0) * 1000)


def hora():
    return time.strftime("%H:%M:%S")


# -----------------------------------------------------------------------------
# 3) O simulador
# -----------------------------------------------------------------------------
class Simulador:
    def __init__(self, args):
        self.args = args
        self.base = f"idoso/{args.quarto}"       # prefixo da hierarquia de tópicos
        self.temp = TEMP_INICIAL
        self.porta_aberta = False
        self.encerrando = False
        self.pedido_queda = threading.Event()    # sinalizado pela thread do teclado

        # paho-mqtt 2.x exige declarar a versão da API de callbacks.
        self.cli = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                               client_id=f"simulador-{args.quarto}")
        self.cli.on_connect = self.on_connect
        self.cli.on_disconnect = self.on_disconnect

        # LWT (Last Will and Testament): o broker publica isto EM NOSSO NOME se a
        # conexão cair sem um DISCONNECT (queda de rede, processo morto...).
        # Precisa ser registrado ANTES de conectar, pois viaja dentro do CONNECT.
        self.cli.will_set(f"{self.base}/status", payload="offline", qos=1, retain=True)

    # --- callbacks do cliente MQTT ------------------------------------------
    def on_connect(self, client, userdata, flags, reason_code, properties):
        if reason_code.is_failure:
            print(f"[{hora()}] Conexão recusada pelo broker: {reason_code}")
            return
        print(f"[{hora()}] Conectado ao broker {self.args.host}:{self.args.port}")
        # "Estou vivo": mensagem retida, o oposto do LWT.
        client.publish(f"{self.base}/status", "online", qos=1, retain=True)

    def on_disconnect(self, client, userdata, flags, reason_code, properties):
        if not self.encerrando:
            print(f"[{hora()}] Conexão perdida ({reason_code}). O paho tentará reconectar sozinho.")

    # --- publicação ----------------------------------------------------------
    def publicar(self, sensor, payload):
        """Serializa o dicionário como JSON compacto (mesma forma do firmware) e publica."""
        topico = f"{self.base}/{sensor}"
        qos, retain = QOS[sensor], RETAIN[sensor]
        texto = json.dumps(payload, separators=(",", ":"))
        self.cli.publish(topico, texto, qos=qos, retain=retain)
        flag = "retain" if retain else ""
        print(f"[{hora()}] PUB {topico:<28} qos={qos} {flag:<6} {texto}")

    def temperatura(self):
        # Passeio aleatório: cada leitura difere pouco da anterior, como num ambiente real.
        self.temp += random.uniform(-TEMP_PASSO, TEMP_PASSO)
        self.temp = min(max(self.temp, TEMP_FAIXA[0]), TEMP_FAIXA[1])
        self.publicar("temperatura", {"sensor": "LM35DZ", "valor": round(self.temp, 1), "ts": ts_ms()})

    def porta(self):
        self.porta_aberta = not self.porta_aberta
        estado = "aberta" if self.porta_aberta else "fechada"
        self.publicar("porta", {"sensor": "MC-38", "estado": estado, "ts": ts_ms()})

    def presenca(self):
        self.publicar("presenca", {"sensor": "HC-SR501", "evento": "presenca", "ts": ts_ms()})

    def queda(self):
        a2 = round(random.uniform(*QUEDA_A2_FAIXA), 2)   # |a|² em g², já acima do limiar
        self.publicar("queda", {"sensor": "MPU-6050", "evento": "queda", "a2": a2, "ts": ts_ms()})

    # --- teclado -------------------------------------------------------------
    def ler_teclado(self):
        """Roda numa thread separada: cada ENTER pede uma queda (quem publica é o laço principal)."""
        while True:
            try:
                input()
            except EOFError:        # stdin fechado (ex.: executando sem terminal)
                return
            self.pedido_queda.set()

    # --- laço principal ------------------------------------------------------
    def executar(self):
        a = self.args
        try:
            self.cli.connect(a.host, a.port, keepalive=30)
        except OSError as e:
            sys.exit(f"Não foi possível conectar em {a.host}:{a.port} ({e}).\n"
                     f"O Mosquitto está rodando? Veja o README.md, seção 'Executar o broker'.")
        self.cli.loop_start()   # thread de rede do paho: envia/recebe, keepalive e reconexão

        threading.Thread(target=self.ler_teclado, daemon=True).start()

        agora = time.monotonic()
        proximo = {                                  # instante da próxima publicação de cada sensor
            "temperatura": agora,                    # a primeira leitura sai imediatamente
            "porta": agora + random.uniform(*PORTA_INTERVALO),
            "presenca": agora + random.uniform(*PRESENCA_INTERVALO),
            "queda": (agora + a.auto_queda) if a.auto_queda else None,
        }

        print(f"\nSimulador do hub do quarto — publicando em {self.base}/...")
        print(f"  temperatura a cada {a.intervalo_temp:g} s | porta a cada "
              f"{PORTA_INTERVALO[0]}-{PORTA_INTERVALO[1]} s | presença a cada "
              f"{PRESENCA_INTERVALO[0]}-{PRESENCA_INTERVALO[1]} s")
        print("  ENTER = publicar uma QUEDA      Ctrl+C = encerrar\n")

        try:
            while True:
                agora = time.monotonic()
                if agora >= proximo["temperatura"]:
                    self.temperatura()
                    proximo["temperatura"] = agora + a.intervalo_temp
                if agora >= proximo["porta"]:
                    self.porta()
                    proximo["porta"] = agora + random.uniform(*PORTA_INTERVALO)
                if agora >= proximo["presenca"]:
                    self.presenca()
                    proximo["presenca"] = agora + random.uniform(*PRESENCA_INTERVALO)
                if proximo["queda"] is not None and agora >= proximo["queda"]:
                    self.queda()
                    proximo["queda"] = agora + a.auto_queda
                if self.pedido_queda.is_set():       # alguém apertou ENTER
                    self.pedido_queda.clear()
                    self.queda()
                time.sleep(0.2)
        except KeyboardInterrupt:
            print(f"\n[{hora()}] Encerrando: publicando status 'offline' e desconectando...")
        finally:
            self.encerrando = True
            # Encerramento "limpo": como enviaremos um DISCONNECT, o broker NÃO
            # publica o LWT — por isso avisamos 'offline' explicitamente antes de sair.
            try:
                info = self.cli.publish(f"{self.base}/status", "offline", qos=1, retain=True)
                info.wait_for_publish(timeout=2)
            except (RuntimeError, ValueError):
                pass                                 # sem conexão no momento: o LWT cobre o caso
            self.cli.disconnect()
            self.cli.loop_stop()


# -----------------------------------------------------------------------------
# 4) Linha de comando
# -----------------------------------------------------------------------------
def parse_args():
    p = argparse.ArgumentParser(
        description="Simula o gateway ESP32-S3 publicando os 4 sensores no broker MQTT.")
    p.add_argument("--host", default="localhost", help="endereço do broker (padrão: localhost)")
    p.add_argument("--port", type=int, default=1883, help="porta MQTT/TCP do broker (padrão: 1883)")
    p.add_argument("--quarto", default="quarto101",
                   help="identificador do quarto nos tópicos (padrão: quarto101)")
    p.add_argument("--intervalo-temp", type=float, default=30.0,
                   help="segundos entre leituras de temperatura (firmware real: 30)")
    p.add_argument("--auto-queda", type=float, default=0,
                   help="publica uma queda automaticamente a cada N segundos (0 = só com ENTER)")
    p.add_argument("--seed", type=int, help="semente do gerador aleatório, para repetir a mesma sequência")
    args = p.parse_args()
    if args.intervalo_temp <= 0:
        p.error("--intervalo-temp deve ser maior que zero")
    return args


if __name__ == "__main__":
    args = parse_args()
    if args.seed is not None:
        random.seed(args.seed)
    Simulador(args).executar()
