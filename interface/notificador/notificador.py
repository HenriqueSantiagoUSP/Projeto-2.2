#!/usr/bin/env python3
"""
notificador.py — Envia ao Telegram do cuidador cada alerta publicado pelos hubs.

Assina idoso/+/+/alerta (docs/protocolo.md §1). Cada alerta é um episódio: chega
como "ativo" quando começa e como "resolvido" ou "cancelado" quando termina, e as
duas mensagens viram notificação. Com QoS 1 a mesma mensagem pode chegar duas
vezes, então o par (id, estado) já enviado é descartado por alguns minutos.

O envio ao Telegram roda numa thread própria, para que uma API lenta nunca segure
a conexão MQTT. Sem TELEGRAM_TOKEN o notificador só imprime o que enviaria, o que
basta para testar com o simulador.

Variáveis de ambiente:
    MQTT_HOST, MQTT_PORT            broker (padrão: localhost, 1883)
    MQTT_USER, MQTT_PASSWORD        usuário "notificador" do broker
    TELEGRAM_TOKEN, TELEGRAM_CHAT_ID bot e conversa do cuidador
    UTC_OFFSET_H                    fuso das mensagens (padrão: -3, horário de Brasília)
"""

import json
import os
import queue
import sys
import threading
import time
import urllib.parse
import urllib.request
from datetime import datetime, timedelta, timezone

TOPICO = "idoso/+/+/alerta"
JANELA_DUPLICATA_S = 300

TIPOS = {
    "queda": "Queda detectada",
    "saida_noturna": "Saída noturna sem retorno",
    "banheiro_prolongado": "Permanência prolongada no banheiro",
    "inatividade": "Inatividade prolongada",
    "temperatura": "Temperatura fora da faixa",
    "vestivel_offline": "Pulseira sem sinal",
    "bateria_baixa": "Bateria da pulseira baixa",
}

ESTADOS = {
    "ativo": "ALERTA",
    "resolvido": "Resolvido",
    "cancelado": "Cancelado pelo residente",
}


# -----------------------------------------------------------------------------
# Lógica pura (testada em test_notificador.py)
# -----------------------------------------------------------------------------
def interpretar(topico, payload):
    """Topico e payload de um alerta -> dict com site, quarto e os campos do JSON.
    Levanta ValueError se não for um alerta válido do contrato."""
    partes = topico.split("/")
    if len(partes) != 4 or partes[0] != "idoso" or partes[3] != "alerta":
        raise ValueError(f"tópico inesperado: {topico}")
    try:
        dados = json.loads(payload)
    except (UnicodeDecodeError, json.JSONDecodeError) as e:
        raise ValueError(f"JSON inválido: {e}") from e
    if not isinstance(dados, dict):
        raise ValueError("o alerta deve ser um objeto JSON")
    for campo in ("id", "tipo", "estado"):
        if not isinstance(dados.get(campo), str):
            raise ValueError(f"campo '{campo}' ausente")
    dados["site"], dados["quarto"] = partes[1], partes[2]
    return dados


def detalhe_legivel(tipo, detalhe):
    """O objeto "detalhe" (docs/protocolo.md §1) em uma linha para o cuidador."""
    d = detalhe if isinstance(detalhe, dict) else {}
    if tipo == "queda":
        partes = []
        if "a2" in d:
            partes.append("Pico de |a|²: {:.2f} g²".format(d["a2"]))
        if isinstance(d.get("prob"), (int, float)):
            # Saída do classificador da pulseira (não calibrada); 0 numa queda simulada pelo botão.
            partes.append("confiança do modelo: {:.0f} %".format(100 * d["prob"]))
        if d.get("via") == "vestivel":
            # A pulseira publicou sozinha: o hub não confirmou a queda (docs/protocolo.md §1).
            partes.append("enviado pela própria pulseira, o hub do quarto não respondeu")
        return "; ".join(partes)
    if tipo == "saida_noturna":
        return "Porta {} aberta e {} min sem presença no quarto".format(d.get("porta", "?"), d.get("min_sem_presenca", "?"))
    if tipo == "banheiro_prolongado":
        return "Porta {} fechada há {} min".format(d.get("porta", "?"), d.get("min", "?"))
    if tipo == "inatividade":
        return "{} min sem movimento no quarto".format(d.get("min", "?"))
    if tipo == "temperatura" and "valor" in d:
        return "{:.1f} °C (limite {:.1f} °C)".format(d["valor"], d.get("limite", float("nan")))
    if tipo == "vestivel_offline":
        return "Pulseira {} sem sinal há {} s".format(d.get("vestivel", "?"), d.get("ultimo_contato_s", "?"))
    if tipo == "bateria_baixa":
        return "Pulseira {}: {} mV".format(d.get("vestivel", "?"), d.get("bateria_mV", "?"))
    return ""


def formatar(alerta, offset_h=-3, recebido_em=None):
    """Texto da notificação. Usa o "ts" do hub; sem ele, a hora em que chegou."""
    fuso = timezone(timedelta(hours=offset_h))
    if isinstance(alerta.get("ts"), (int, float)):
        quando = datetime.fromtimestamp(alerta["ts"] / 1000, fuso)
    else:
        quando = datetime.fromtimestamp(recebido_em if recebido_em is not None else time.time(), fuso)

    estado = ESTADOS.get(alerta["estado"], alerta["estado"])
    tipo = TIPOS.get(alerta["tipo"], alerta["tipo"])
    marca = "🚨" if alerta["estado"] == "ativo" else "✅"
    linhas = [
        f"{marca} {estado}: {tipo}",
        f"Quarto {alerta['quarto']} ({alerta['site']})",
    ]
    detalhe = detalhe_legivel(alerta["tipo"], alerta.get("detalhe"))
    if detalhe:
        linhas.append(detalhe)
    linhas.append(quando.strftime("%d/%m %H:%M:%S"))
    return "\n".join(linhas)


class Deduplicador:
    """Lembra (site, quarto, id, estado) já notificados por `janela_s` segundos."""

    def __init__(self, janela_s=JANELA_DUPLICATA_S, relogio=time.monotonic):
        self.janela_s = janela_s
        self.relogio = relogio
        self.vistos = {}

    def novo(self, alerta):
        agora = self.relogio()
        self.vistos = {k: t for k, t in self.vistos.items() if agora - t < self.janela_s}
        chave = (alerta["site"], alerta["quarto"], alerta["id"], alerta["estado"])
        if chave in self.vistos:
            return False
        self.vistos[chave] = agora
        return True


# -----------------------------------------------------------------------------
# Telegram
# -----------------------------------------------------------------------------
def enviar_telegram(token, chat_id, texto, tentativas=3):
    url = f"https://api.telegram.org/bot{token}/sendMessage"
    corpo = urllib.parse.urlencode({"chat_id": chat_id, "text": texto}).encode()
    for tentativa in range(1, tentativas + 1):
        try:
            with urllib.request.urlopen(url, data=corpo, timeout=5) as resposta:
                if resposta.status == 200:
                    return True
        except OSError as e:
            print(f"Telegram falhou (tentativa {tentativa}/{tentativas}): {e}", flush=True)
        time.sleep(1)
    return False


def trabalhador_envio(fila, token, chat_id):
    while True:
        texto = fila.get()
        if token and chat_id:
            enviar_telegram(token, chat_id, texto)
        else:
            print("(sem TELEGRAM_TOKEN: só impresso)", flush=True)


# -----------------------------------------------------------------------------
# MQTT
# -----------------------------------------------------------------------------
def main():
    try:
        import paho.mqtt.client as mqtt
    except ImportError:
        sys.exit("Biblioteca paho-mqtt não encontrada. Instale com:  pip install paho-mqtt")

    host = os.environ.get("MQTT_HOST", "localhost")
    porta = int(os.environ.get("MQTT_PORT", "1883"))
    offset_h = float(os.environ.get("UTC_OFFSET_H", "-3"))
    token = os.environ.get("TELEGRAM_TOKEN", "")
    chat_id = os.environ.get("TELEGRAM_CHAT_ID", "")

    fila = queue.Queue()
    threading.Thread(target=trabalhador_envio, args=(fila, token, chat_id), daemon=True).start()
    dedup = Deduplicador()

    def ao_conectar(cliente, userdata, flags, reason_code, properties):
        if reason_code.is_failure:
            print(f"Broker recusou a conexão: {reason_code}", flush=True)
            return
        print(f"Conectado a {host}:{porta}, assinando {TOPICO}", flush=True)
        cliente.subscribe(TOPICO, qos=1)

    def ao_receber(cliente, userdata, msg):
        try:
            alerta = interpretar(msg.topic, msg.payload)
        except ValueError as e:
            print(f"Ignorado ({msg.topic}): {e}", flush=True)
            return
        if not dedup.novo(alerta):
            return
        texto = formatar(alerta, offset_h)
        print(texto.replace("\n", " | "), flush=True)
        fila.put(texto)

    cliente = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="notificador")
    cliente.username_pw_set(os.environ.get("MQTT_USER", "notificador"), os.environ.get("MQTT_PASSWORD", ""))
    cliente.on_connect = ao_conectar
    cliente.on_message = ao_receber
    cliente.reconnect_delay_set(min_delay=1, max_delay=30)

    while True:
        try:
            cliente.connect(host, porta, keepalive=30)
            break
        except OSError as e:
            print(f"Broker indisponível ({e}); tentando de novo em 5 s", flush=True)
            time.sleep(5)
    cliente.loop_forever(retry_first_connection=True)


if __name__ == "__main__":
    main()
