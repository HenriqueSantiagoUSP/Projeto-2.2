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

Nenhum alerta pode se perder em silêncio:
- a sessão MQTT é persistente: o broker guarda os alertas (QoS 1) enquanto o
  notificador está fora do ar e os entrega quando ele volta;
- um alerta com dados inesperados vira um texto simples, em vez de derrubar o processo;
- um alerta que o Telegram não aceitou agora (rede, servidor, limite de taxa) espera
  e é enviado de novo até passar; só uma mensagem recusada pelo Telegram (400) é descartada.

Variáveis de ambiente:
    MQTT_HOST, MQTT_PORT            broker (padrão: localhost, 1883)
    MQTT_USER, MQTT_PASSWORD        usuário "notificador" do broker
    TELEGRAM_TOKEN, TELEGRAM_CHAT_ID bot e conversa do cuidador
    UTC_OFFSET_H                    fuso das mensagens (padrão: -3, horário de Brasília)
"""

import http.client
import json
import math
import os
import queue
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timedelta, timezone

TOPICO = "idoso/+/+/alerta"
JANELA_DUPLICATA_S = 300
ESPERA_MAX_S = 60          # entre novas tentativas de um alerta que o Telegram ainda não aceitou

# Resultado de uma tentativa de envio ao Telegram
ENVIADA, TENTAR_DE_NOVO, RECUSADA = "enviada", "tentar de novo", "recusada"

TIPOS = {
    "queda": "Queda detectada",
    "ajuda": "Pedido de ajuda (botão da pulseira)",
    "saida_noturna": "Saída noturna sem retorno",
    "banheiro_prolongado": "Permanência prolongada no banheiro",
    "inatividade": "Inatividade prolongada",
    "temperatura": "Temperatura fora da faixa",
    "pulseira_offline": "Pulseira sem sinal",
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


def _numero(valor):
    """O valor como float, ou None se não for um número finito (um JSON pode trazer qualquer coisa)."""
    if isinstance(valor, bool) or not isinstance(valor, (int, float)):
        return None
    return float(valor) if math.isfinite(valor) else None


PELA_PULSEIRA = "enviado pela própria pulseira, o hub do quarto não respondeu"


def detalhe_legivel(tipo, detalhe):
    """O objeto "detalhe" (docs/protocolo.md §1) em uma linha para o cuidador."""
    d = detalhe if isinstance(detalhe, dict) else {}
    # A pulseira publicou sozinha: o hub não confirmou a queda ou o pedido de ajuda (docs/protocolo.md §1).
    pela_pulseira = d.get("via") == "pulseira"
    if tipo == "queda":
        partes = []
        a2, prob = _numero(d.get("a2")), _numero(d.get("prob"))
        if a2 is not None:
            partes.append("Pico de |a|²: {:.2f} g²".format(a2))
        if prob is not None:
            # Saída do classificador da pulseira (não calibrada); 0 numa queda simulada pelo botão.
            partes.append("confiança do modelo: {:.0f} %".format(100 * prob))
        if pela_pulseira:
            partes.append(PELA_PULSEIRA)
        return "; ".join(partes)
    if tipo == "ajuda":
        return PELA_PULSEIRA if pela_pulseira else ""
    if tipo == "saida_noturna":
        return "Porta {} aberta e {} min sem presença no quarto".format(d.get("porta", "?"), d.get("min_sem_presenca", "?"))
    if tipo == "banheiro_prolongado":
        return "Porta {} fechada há {} min".format(d.get("porta", "?"), d.get("min", "?"))
    if tipo == "inatividade":
        return "{} min sem movimento no quarto".format(d.get("min", "?"))
    if tipo == "temperatura" and _numero(d.get("valor")) is not None:
        limite = _numero(d.get("limite"))
        texto = "{:.1f} °C".format(_numero(d["valor"]))
        return texto + (" (limite {:.1f} °C)".format(limite) if limite is not None else "")
    if tipo == "pulseira_offline":
        return "Pulseira {} sem sinal há {} s".format(d.get("pulseira", "?"), d.get("ultimo_contato_s", "?"))
    if tipo == "bateria_baixa":
        return "Pulseira {}: {} mV".format(d.get("pulseira", "?"), d.get("bateria_mV", "?"))
    return ""


def formatar(alerta, offset_h=-3, recebido_em=None):
    """Texto da notificação. Usa o "ts" do hub; sem ele, a hora em que chegou."""
    fuso = timezone(timedelta(hours=offset_h))
    quando = None
    ts = _numero(alerta.get("ts"))
    if ts is not None:
        try:
            quando = datetime.fromtimestamp(ts / 1000, fuso)
        except (ValueError, OverflowError, OSError):
            quando = None          # fora da faixa do calendário: vale a hora em que chegou
    if quando is None:
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


def texto_simples(alerta):
    """O alerta sem formatação, para quando os dados vieram de um jeito que formatar() não esperava."""
    marca = "🚨" if alerta.get("estado") == "ativo" else "✅"
    return "\n".join([f"{marca} {alerta.get('estado')}: {alerta.get('tipo')}",
                      f"Quarto {alerta.get('quarto')} ({alerta.get('site')})",
                      f"Dados: {json.dumps(alerta.get('detalhe'), ensure_ascii=False, default=str)[:300]}"])


def processar(topico, payload, dedup, offset_h, recebido_em=None):
    """
    Uma mensagem do broker -> o texto a enviar, ou None (não é alerta, ou já foi enviado).
    Nunca levanta: um alerta com dados inesperados ainda chega ao cuidador, como texto simples.
    """
    try:
        alerta = interpretar(topico, payload)
    except ValueError as e:
        print(f"Ignorado ({topico}): {e}", flush=True)
        return None
    if not dedup.novo(alerta):
        return None
    try:
        return formatar(alerta, offset_h, recebido_em)
    except Exception as e:  # noqa: BLE001 — a notificação vale mais que a formatação
        print(f"Formatação falhou ({e!r}); enviando o alerta como texto simples", flush=True)
        return texto_simples(alerta)


# -----------------------------------------------------------------------------
# Telegram
# -----------------------------------------------------------------------------
def enviar_telegram(token, chat_id, texto, tentativas=3, espera_s=1.0):
    """
    Algumas tentativas seguidas. ENVIADA; RECUSADA se o Telegram rejeitou a própria mensagem
    (400: tentar de novo não adianta, e ela não pode travar a fila); TENTAR_DE_NOVO no resto
    (rede, servidor, limite de taxa, token ou conversa errados, resposta cortada).
    """
    url = f"https://api.telegram.org/bot{token}/sendMessage"
    corpo = urllib.parse.urlencode({"chat_id": chat_id, "text": texto}).encode()
    for tentativa in range(1, tentativas + 1):
        try:
            with urllib.request.urlopen(url, data=corpo, timeout=5) as resposta:
                if resposta.status == 200:
                    return ENVIADA
        except urllib.error.HTTPError as e:
            if e.code == 400:
                print(f"Telegram recusou a mensagem (400): {e}", flush=True)
                return RECUSADA
            print(f"Telegram falhou (tentativa {tentativa}/{tentativas}): HTTP {e.code}", flush=True)
        except (OSError, http.client.HTTPException, ValueError) as e:
            # ValueError cobre http.client.InvalidURL (um token com espaço ou \r, por exemplo)
            print(f"Telegram falhou (tentativa {tentativa}/{tentativas}): {e!r}", flush=True)
        if tentativa < tentativas:
            time.sleep(espera_s)
    return TENTAR_DE_NOVO


def entregar(texto, token, chat_id, enviar=enviar_telegram, dormir=time.sleep):
    """
    Insiste até o Telegram aceitar, com espera crescente até ESPERA_MAX_S: uma queda de internet
    não pode sumir com um alerta. True se enviado, False se o Telegram recusou a mensagem.
    """
    espera = 2.0
    while True:
        try:
            resultado = enviar(token, chat_id, texto)
        except Exception as e:  # noqa: BLE001 — o envio nunca pode matar a thread
            print(f"Erro inesperado no envio ao Telegram: {e!r}", flush=True)
            resultado = TENTAR_DE_NOVO
        if resultado != TENTAR_DE_NOVO:
            return resultado == ENVIADA
        print(f"Telegram indisponível: o alerta vai de novo em {espera:.0f} s", flush=True)
        dormir(espera)
        espera = min(espera * 2, ESPERA_MAX_S)


def trabalhador_envio(fila, token, chat_id):
    while True:
        texto = fila.get()
        try:
            if token and chat_id:
                entregar(texto, token, chat_id)
            else:
                print("(sem TELEGRAM_TOKEN: só impresso)", flush=True)
        except Exception as e:  # noqa: BLE001 — se esta thread morrer, nenhum alerta sai mais
            print(f"Erro no envio, o alerta foi descartado: {e!r}", flush=True)


# -----------------------------------------------------------------------------
# MQTT
# -----------------------------------------------------------------------------
def novo_cliente(mqtt):
    """
    Sessão persistente (clean_session=False), como o Telegraf: com o mesmo client_id, o broker
    guarda os alertas QoS 1 que chegarem enquanto o notificador reinicia ou perde a conexão.
    """
    return mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="notificador", clean_session=False)


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
        # O paho relança no loop_forever o que escapar daqui: um alerta esquisito derrubaria o processo.
        try:
            texto = processar(msg.topic, msg.payload, dedup, offset_h)
        except Exception as e:  # noqa: BLE001
            print(f"Erro ao tratar {msg.topic}: {e!r}", flush=True)
            return
        if texto:
            print(texto.replace("\n", " | "), flush=True)
            fila.put(texto)

    cliente = novo_cliente(mqtt)
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
