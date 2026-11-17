#!/usr/bin/env python3
"""
simulador.py — Faz o papel de um hub do quarto, publicando no broker da central.

Publica no contrato de docs/protocolo.md exatamente o que o firmware do hub publica:
status (com Last Will), presenca, porta/<id>, temperatura, pulseira/<id>, alerta
(episódios "ativo" -> "resolvido"/"cancelado") e config/aplicada. Serve para testar a
central — Telegraf, InfluxDB, Grafana e o notificador do Telegram — sem hardware.

Como o hub, mede a temperatura a cada --intervalo-temp, mas só a publica quando ela muda
0,2 °C ou mais desde o último envio, ou a cada 5 min; e publica pulseira/<id> como um
resumo a cada --intervalo-pulseira (5 min).

Uso
---
    pip install paho-mqtt                                   # uma única vez
    python simulador.py --senha <MQTT_HUB_PASSWORD do .env>
    python simulador.py --senha ... --intervalo-temp 5      # demonstração mais dinâmica
    python simulador.py --senha ... --quarto quarto102      # um segundo quarto

Comandos (digite e tecle ENTER)
-------------------------------
    (vazio)  queda                     a  pedido de ajuda (botão segurado)
    c        cancela a última queda ou o último pedido de ajuda ("estou bem", dois toques)
    s        saída noturna             b  banheiro prolongado
    t        temperatura alta          r  resolve os alertas abertos
    o        pulseira sem sinal / volta
    q        encerra
"""

import argparse
import json
import os
import random
import sys
import threading
import time

try:
    import paho.mqtt.client as mqtt
except ImportError:  # biblioteca ausente: orienta em vez de mostrar um traceback
    sys.exit("Biblioteca paho-mqtt não encontrada. Instale com:  pip install paho-mqtt")

# -----------------------------------------------------------------------------
# Parâmetros da simulação
# -----------------------------------------------------------------------------
TEMP_INICIAL = 22.5          # °C
TEMP_FAIXA = (19.0, 26.0)    # °C — o passeio aleatório fica preso a esta faixa
TEMP_PASSO = 0.1             # °C — variação máxima entre leituras
TEMP_DELTA = 0.2             # °C — como o hub: publica quando muda isto desde o último envio...
ENVIO_MAX_S = 300            # s — ...ou a cada 5 min, como sinal de vida (docs/protocolo.md §1)
PORTA_INTERVALO = (20, 60)   # s — uma porta muda de estado em intervalos aleatórios nesta faixa
PRESENCA_INTERVALO = (8, 30) # s — idem para a presença
PORTAS = ("quarto", "banheiro")
PULSEIRA = "a1b2c3"          # id = 3 últimos bytes do MAC da pulseira

CONFIG_PADRAO = {
    "noite": {"inicio": "22:00", "fim": "06:00"},
    "portas": {"quarto": "saida", "banheiro": "banheiro"},
    "saida_noturna_min": 15,
    "banheiro_min": 40,
    "inatividade_min": 120,
    "temperatura": {"min": 18, "max": 28, "histerese": 0.5, "persistencia_min": 10},
    "pulseira_offline_s": 90,
    "bateria_baixa_mV": 3500,
}


def agora_ms():
    """O hub carimba "ts" em epoch ms (UTC) depois de sincronizar o relógio por SNTP."""
    return int(time.time() * 1000)


def hora():
    return time.strftime("%H:%M:%S")


class Simulador:
    def __init__(self, args):
        self.args = args
        self.base = f"idoso/{args.site}/{args.quarto}"
        self.temp = TEMP_INICIAL
        self.temp_enviada = None                          # o último valor publicado, e quando
        self.temp_enviada_em = 0.0
        self.portas = {p: False for p in PORTAS}          # False = fechada
        self.presente = False
        self.bateria_mv = 4100
        self.pulseira_mudo = False
        self.seq_pulseira = 0                             # o seq das mensagens da pulseira nomeia o alerta
        self.ultimo_da_pulseira = None                    # (id, tipo) da última queda ou ajuda ainda ativa
        self.abertos = {}                                 # id -> alerta ativo (exceto os da pulseira)
        self.config = json.loads(json.dumps(CONFIG_PADRAO))
        self.origem_config = "padrao"                     # como o hub: "padrao", depois "mqtt"
        self.comandos = []
        self.trava = threading.Lock()
        self.encerrar = threading.Event()

        self.cli = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                               client_id=f"simulador-{args.site}-{args.quarto}")
        self.cli.username_pw_set(args.usuario, args.senha)
        # O broker publica isto em nosso nome se a conexão cair sem aviso.
        self.cli.will_set(f"{self.base}/status", json.dumps({"v": 1, "estado": "offline"}),
                          qos=1, retain=True)
        self.cli.on_connect = self.ao_conectar
        self.cli.on_message = self.ao_receber

    # --- publicação ----------------------------------------------------------
    def publicar(self, sufixo, dados, qos=1, retain=True):
        texto = json.dumps({"v": 1, **dados, "ts": agora_ms()}, separators=(",", ":"), ensure_ascii=False)
        self.cli.publish(f"{self.base}/{sufixo}", texto, qos=qos, retain=retain)
        print(f"[{hora()}] {sufixo:<20} {texto}")

    def publicar_config(self, origem, erro=None):
        dados = {"origem": origem, **self.config}
        if erro:
            dados["erro"] = erro
        self.publicar("config/aplicada", dados)

    def publicar_estados(self):
        """Como o hub: tudo o que é retido sai de novo a cada conexão."""
        self.publicar("status", {"estado": "online", "fw": "simulador", "hora": True})
        self.publicar_config(self.origem_config)
        self.publicar("presenca", {"sensor": "HC-SR501", "estado": "detectada" if self.presente else "livre"})
        for porta, aberta in self.portas.items():
            self.publicar(f"porta/{porta}", {"sensor": "MC-38", "estado": "aberta" if aberta else "fechada"})
        if self.temp_enviada is None:
            self.temperatura()
        else:
            self.publicar_temperatura(self.temp_enviada)  # o último valor enviado, como o hub
        self.heartbeat()

    def alerta(self, id_, tipo, estado, origem, detalhe=None):
        self.publicar("alerta", {"id": id_, "tipo": tipo, "estado": estado, "origem": origem,
                                 "detalhe": detalhe or {}}, retain=False)

    # --- sensores ------------------------------------------------------------
    def publicar_temperatura(self, valor):
        self.publicar("temperatura", {"sensor": "LM35DZ", "valor": round(valor, 2)}, qos=0)

    def temperatura(self, agora=None):
        """Uma leitura; publicada só se mudou 0,2 °C desde o último envio ou se já faz 5 min."""
        agora = time.monotonic() if agora is None else agora
        self.temp += random.uniform(-TEMP_PASSO, TEMP_PASSO)
        self.temp = min(max(self.temp, TEMP_FAIXA[0]), TEMP_FAIXA[1])
        if (self.temp_enviada is None or abs(self.temp - self.temp_enviada) >= TEMP_DELTA
                or agora - self.temp_enviada_em >= ENVIO_MAX_S):
            self.publicar_temperatura(self.temp)
            self.temp_enviada, self.temp_enviada_em = self.temp, agora

    def porta(self):
        porta = random.choice(PORTAS)
        self.portas[porta] = not self.portas[porta]
        self.publicar(f"porta/{porta}", {"sensor": "MC-38",
                                         "estado": "aberta" if self.portas[porta] else "fechada"})

    def presenca(self):
        self.presente = not self.presente
        self.publicar("presenca", {"sensor": "HC-SR501", "estado": "detectada" if self.presente else "livre"})

    def heartbeat(self):
        """O resumo da pulseira que o hub publica a cada 5 min (e logo que ela aparece ou volta)."""
        if self.pulseira_mudo:
            return
        self.bateria_mv = max(3300, self.bateria_mv - random.randint(0, 3))
        self.publicar(f"pulseira/{PULSEIRA}", {
            "estado": "online",
            "bateria_mV": self.bateria_mv,
            "rssi": random.randint(-78, -48),
            "atividade": round(random.uniform(0.02, 0.35), 3),
        })

    # --- comandos ------------------------------------------------------------
    def executar_comando(self, cmd):
        epoch_s = int(time.time())
        if cmd in ("", "a"):
            # Como na pulseira: o cancelamento vale para o último alerta dela, queda ou ajuda.
            self.seq_pulseira += 1
            id_ = f"{PULSEIRA}-{self.seq_pulseira}"
            if cmd == "":
                a2 = round(random.uniform(6.3, 12.0), 2)
                prob = round(random.uniform(0.75, 1.0), 2)   # acima do limiar do Random Forest (0,73)
                self.ultimo_da_pulseira = (id_, "queda")
                self.alerta(id_, "queda", "ativo", f"pulseira/{PULSEIRA}", {"a2": a2, "prob": prob})
            else:
                self.ultimo_da_pulseira = (id_, "ajuda")
                self.alerta(id_, "ajuda", "ativo", f"pulseira/{PULSEIRA}")
        elif cmd == "c":
            if self.ultimo_da_pulseira is None:
                print("  (nenhuma queda ou pedido de ajuda aberto para cancelar)")
            else:
                id_, tipo = self.ultimo_da_pulseira
                self.alerta(id_, tipo, "cancelado", f"pulseira/{PULSEIRA}")
                self.ultimo_da_pulseira = None
        elif cmd in ("s", "b", "t"):
            tipo, origem, detalhe = {
                "s": ("saida_noturna", "porta/quarto", {"porta": "quarto", "min_sem_presenca": 15}),
                "b": ("banheiro_prolongado", "porta/banheiro", {"porta": "banheiro", "min": 40}),
                "t": ("temperatura", "temperatura", {"valor": 29.4, "limite": 28}),
            }[cmd]
            id_ = f"{tipo}-{epoch_s}"
            self.abertos[id_] = (tipo, origem, detalhe)
            self.alerta(id_, tipo, "ativo", origem, detalhe)
        elif cmd == "r":
            for id_, (tipo, origem, detalhe) in list(self.abertos.items()):
                self.alerta(id_, tipo, "resolvido", origem, detalhe)
            self.abertos.clear()
        elif cmd == "o":
            self.pulseira_mudo = not self.pulseira_mudo
            id_ = f"pulseira_offline-{PULSEIRA}-{epoch_s}"
            detalhe = {"pulseira": PULSEIRA, "ultimo_contato_s": 90}
            if self.pulseira_mudo:
                self.abertos["offline"] = ("pulseira_offline", f"pulseira/{PULSEIRA}", detalhe)
                self.publicar(f"pulseira/{PULSEIRA}", {"estado": "offline"})
                self.alerta(id_, "pulseira_offline", "ativo", f"pulseira/{PULSEIRA}", detalhe)
                self.offline_id = id_
            else:
                self.abertos.pop("offline", None)
                self.alerta(self.offline_id, "pulseira_offline", "resolvido", f"pulseira/{PULSEIRA}", detalhe)
                self.heartbeat()
        elif cmd == "q":
            self.encerrar.set()
        else:
            print("  comandos: ENTER queda | a ajuda | c cancela | s saída noturna | b banheiro | "
                  "t temperatura | r resolve | o pulseira | q sai")

    def ler_teclado(self):
        while not self.encerrar.is_set():
            try:
                linha = input().strip().lower()
            except EOFError:
                return
            with self.trava:
                self.comandos.append(linha)

    # --- MQTT ----------------------------------------------------------------
    def ao_conectar(self, client, userdata, flags, reason_code, properties):
        if reason_code.is_failure:
            print(f"[{hora()}] Conexão recusada pelo broker: {reason_code} (usuário/senha?)")
            return
        print(f"[{hora()}] Conectado a {self.args.host}:{self.args.port} como {self.args.usuario}")
        client.subscribe(f"{self.base}/config/desejada", qos=1)
        with self.trava:
            self.comandos.append("__estados__")

    def ao_receber(self, client, userdata, msg):
        try:
            recebida = json.loads(msg.payload)
            if not isinstance(recebida, dict):
                raise ValueError("não é um objeto")
        except ValueError as e:
            self.publicar_config("mqtt", erro=f"JSON inválido: {e}")
            return
        recebida.pop("v", None)
        recebida.pop("origem", None)
        self.config.update(recebida)
        self.origem_config = "mqtt"
        print(f"[{hora()}] configuração recebida")
        self.publicar_config("mqtt")

    # --- laço principal ------------------------------------------------------
    def executar(self):
        a = self.args
        try:
            self.cli.connect(a.host, a.port, keepalive=30)
        except OSError as e:
            sys.exit(f"Não foi possível conectar em {a.host}:{a.port} ({e}). A central está no ar? "
                     f"Veja interface/README.md.")
        self.cli.loop_start()
        if not a.sem_teclado:
            threading.Thread(target=self.ler_teclado, daemon=True).start()

        inicio = agora = time.monotonic()
        proximo = {
            "temperatura": agora + a.intervalo_temp,
            "porta": agora + random.uniform(*PORTA_INTERVALO),
            "presenca": agora + random.uniform(*PRESENCA_INTERVALO),
            "pulseira": agora + a.intervalo_pulseira,
            "queda": (agora + a.auto_queda) if a.auto_queda else None,
        }

        print(f"\nSimulador do hub — publicando em {self.base}/...")
        print("  ENTER queda | a ajuda | c cancela | s saída noturna | b banheiro | t temperatura | "
              "r resolve | o pulseira | q sai\n")
        try:
            while not self.encerrar.is_set():
                agora = time.monotonic()
                with self.trava:
                    pendentes, self.comandos = self.comandos, []
                for cmd in pendentes:
                    if cmd == "__estados__":
                        self.publicar_estados()
                    else:
                        self.executar_comando(cmd)

                for chave, intervalo, acao in (
                    ("temperatura", lambda: a.intervalo_temp, self.temperatura),
                    ("porta", lambda: random.uniform(*PORTA_INTERVALO), self.porta),
                    ("presenca", lambda: random.uniform(*PRESENCA_INTERVALO), self.presenca),
                    ("pulseira", lambda: a.intervalo_pulseira, self.heartbeat),
                ):
                    if agora >= proximo[chave]:
                        acao()
                        proximo[chave] = agora + intervalo()
                if proximo["queda"] is not None and agora >= proximo["queda"]:
                    self.executar_comando("")
                    proximo["queda"] = agora + a.auto_queda
                if a.duracao and agora - inicio >= a.duracao:
                    break
                time.sleep(0.2)
        except KeyboardInterrupt:
            pass
        finally:
            print(f"\n[{hora()}] Encerrando: publicando status 'offline' e desconectando...")
            # Saída limpa: o broker não publicaria o Last Will, então avisamos nós mesmos.
            try:
                info = self.cli.publish(f"{self.base}/status",
                                        json.dumps({"v": 1, "estado": "offline", "ts": agora_ms()}),
                                        qos=1, retain=True)
                info.wait_for_publish(timeout=2)
            except (RuntimeError, ValueError):
                pass
            self.cli.disconnect()
            self.cli.loop_stop()


def parse_args():
    p = argparse.ArgumentParser(description="Simula um hub do quarto publicando no contrato de docs/protocolo.md.")
    p.add_argument("--host", default="localhost", help="endereço do broker (padrão: localhost)")
    p.add_argument("--port", type=int, default=1883, help="porta MQTT do broker (padrão: 1883)")
    p.add_argument("--usuario", default="hub", help="usuário do broker (padrão: hub)")
    p.add_argument("--senha", default=os.environ.get("MQTT_HUB_PASSWORD", ""),
                   help="senha do usuário hub (padrão: variável MQTT_HUB_PASSWORD)")
    p.add_argument("--site", default="butanta", help="instituição no tópico (padrão: butanta)")
    p.add_argument("--quarto", default="quarto101", help="quarto no tópico (padrão: quarto101)")
    p.add_argument("--intervalo-temp", type=float, default=60.0,
                   help="segundos entre leituras de temperatura (padrão: 60, como o hub); cada uma só é "
                        "publicada se mudou 0,2 °C ou se já faz 5 min")
    p.add_argument("--intervalo-pulseira", type=float, default=300.0,
                   help="segundos entre os resumos da pulseira em pulseira/<id> (padrão: 300, como o hub)")
    p.add_argument("--auto-queda", type=float, default=0, help="publica uma queda a cada N segundos (0 = só com ENTER)")
    p.add_argument("--duracao", type=float, default=0, help="encerra sozinho após N segundos (0 = até Ctrl+C ou q)")
    p.add_argument("--sem-teclado", action="store_true", help="não lê comandos do teclado (execução automática)")
    p.add_argument("--seed", type=int, help="semente do gerador aleatório, para repetir a mesma sequência")
    args = p.parse_args()
    if args.intervalo_temp <= 0 or args.intervalo_pulseira <= 0:
        p.error("os intervalos devem ser maiores que zero")
    return args


if __name__ == "__main__":
    args = parse_args()
    if args.seed is not None:
        random.seed(args.seed)
    Simulador(args).executar()
