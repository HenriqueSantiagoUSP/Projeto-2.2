"""Testes da lógica do notificador (sem broker e sem Telegram).

    python -m unittest discover -s interface/notificador
"""

import http.client
import json
import types
import unittest
import urllib.error
from unittest import mock

import notificador
from notificador import Deduplicador, formatar, interpretar, processar


def alerta_json(**campos):
    base = {"v": 1, "id": "a1b2c3-42", "tipo": "queda", "estado": "ativo",
            "origem": "vestivel/a1b2c3", "detalhe": {"a2": 7.88}, "ts": 1792497600000}
    base.update(campos)
    return json.dumps(base).encode()


class TestInterpretar(unittest.TestCase):
    def test_alerta_valido(self):
        a = interpretar("idoso/butanta/quarto101/alerta", alerta_json())
        self.assertEqual(a["site"], "butanta")
        self.assertEqual(a["quarto"], "quarto101")
        self.assertEqual(a["tipo"], "queda")

    def test_topico_errado(self):
        with self.assertRaises(ValueError):
            interpretar("idoso/butanta/quarto101/porta/quarto", alerta_json())

    def test_json_invalido(self):
        with self.assertRaises(ValueError):
            interpretar("idoso/butanta/quarto101/alerta", b"{nao e json")

    def test_campo_obrigatorio(self):
        with self.assertRaises(ValueError):
            interpretar("idoso/butanta/quarto101/alerta", json.dumps({"tipo": "queda"}).encode())


class TestFormatar(unittest.TestCase):
    def test_queda_ativa(self):
        a = interpretar("idoso/butanta/quarto101/alerta", alerta_json())
        texto = formatar(a, offset_h=-3)
        self.assertIn("ALERTA: Queda detectada", texto)
        self.assertIn("Quarto quarto101 (butanta)", texto)
        self.assertIn("7.88 g²", texto)
        self.assertIn("20/10 09:00:00", texto)   # ts = 2026-10-20 12:00 UTC, mostrado em UTC-3

    def test_queda_enviada_pela_pulseira(self):
        a = interpretar("idoso/butanta/quarto101/alerta",
                        alerta_json(detalhe={"a2": 9.5, "prob": 0.97, "via": "vestivel"}))
        texto = formatar(a)
        self.assertIn("9.50 g²", texto)
        self.assertIn("confiança do modelo: 97 %", texto)
        self.assertIn("pela própria pulseira", texto)

    def test_queda_sem_prob_continua_legivel(self):
        # alertas de antes do modelo na pulseira (ou de outro emissor) não têm "prob"
        texto = formatar(interpretar("idoso/butanta/quarto101/alerta", alerta_json()))
        self.assertIn("7.88 g²", texto)
        self.assertNotIn("confiança", texto)

    def test_cancelado(self):
        a = interpretar("idoso/butanta/quarto101/alerta", alerta_json(estado="cancelado", detalhe={}))
        self.assertIn("Cancelado pelo residente", formatar(a))

    def test_temperatura(self):
        a = interpretar("idoso/butanta/quarto101/alerta",
                        alerta_json(tipo="temperatura", detalhe={"valor": 29.14, "limite": 28}))
        self.assertIn("29.1 °C (limite 28.0 °C)", formatar(a))

    def test_sem_ts_usa_hora_de_recepcao(self):
        dados = json.loads(alerta_json())
        del dados["ts"]
        a = interpretar("idoso/butanta/quarto101/alerta", json.dumps(dados).encode())
        self.assertIn("20/10 09:00:00", formatar(a, offset_h=-3, recebido_em=1792497600))


class TestDeduplicador(unittest.TestCase):
    def test_descarta_repeticao_dentro_da_janela(self):
        agora = [0.0]
        d = Deduplicador(janela_s=300, relogio=lambda: agora[0])
        a = interpretar("idoso/butanta/quarto101/alerta", alerta_json())
        self.assertTrue(d.novo(a))
        self.assertFalse(d.novo(a))
        agora[0] = 301.0
        self.assertTrue(d.novo(a))

    def test_estados_diferentes_do_mesmo_episodio(self):
        d = Deduplicador()
        ativo = interpretar("idoso/butanta/quarto101/alerta", alerta_json())
        cancelado = interpretar("idoso/butanta/quarto101/alerta", alerta_json(estado="cancelado"))
        self.assertTrue(d.novo(ativo))
        self.assertTrue(d.novo(cancelado))


class TestAlertaEsquisito(unittest.TestCase):
    """JSON válido com tipos inesperados: o alerta chega ao cuidador mesmo assim, nada derruba o processo."""

    def test_detalhe_com_tipos_inesperados(self):
        casos = [alerta_json(tipo="temperatura", detalhe={"valor": 29, "limite": None}),
                 alerta_json(detalhe={"a2": "7.9", "prob": "alta"}),
                 alerta_json(ts=10 ** 20),
                 alerta_json(ts=-10 ** 20),
                 alerta_json(detalhe=[1, 2, 3])]
        for payload in casos:
            texto = formatar(interpretar("idoso/butanta/quarto101/alerta", payload), recebido_em=1792497600)
            self.assertIn("Quarto quarto101 (butanta)", texto)

    def test_temperatura_sem_limite(self):
        a = interpretar("idoso/butanta/quarto101/alerta",
                        alerta_json(tipo="temperatura", detalhe={"valor": 29, "limite": None}))
        self.assertIn("29.0 °C", formatar(a))

    def test_processar_nunca_levanta(self):
        d = Deduplicador()
        self.assertIsNone(processar("idoso/butanta/quarto101/alerta", b"\xff\xfe", d, -3))
        texto = processar("idoso/butanta/quarto101/alerta", alerta_json(ts="ontem"), d, -3)
        self.assertIn("Queda detectada", texto)
        self.assertIsNone(processar("idoso/butanta/quarto101/alerta", alerta_json(ts="ontem"), d, -3))  # repetido


class TestEnvio(unittest.TestCase):
    def test_erro_http_inesperado_nao_derruba_o_envio(self):
        def urlopen(*_a, **_k):
            raise http.client.IncompleteRead(b"")
        with mock.patch.object(notificador.urllib.request, "urlopen", urlopen):
            self.assertEqual(notificador.enviar_telegram("t", "c", "oi", tentativas=1, espera_s=0),
                             notificador.TENTAR_DE_NOVO)

    def test_mensagem_recusada_nao_trava_a_fila(self):
        def urlopen(*_a, **_k):
            raise urllib.error.HTTPError("u", 400, "Bad Request", {}, None)
        with mock.patch.object(notificador.urllib.request, "urlopen", urlopen):
            self.assertEqual(notificador.enviar_telegram("t", "c", "oi", tentativas=1, espera_s=0),
                             notificador.RECUSADA)

    def test_alerta_espera_a_internet_voltar(self):
        respostas = [http.client.IncompleteRead(b""), notificador.TENTAR_DE_NOVO, notificador.ENVIADA]
        esperas = []

        def enviar(*_a, **_k):
            r = respostas.pop(0)
            if isinstance(r, Exception):
                raise r
            return r
        self.assertTrue(notificador.entregar("oi", "t", "c", enviar=enviar, dormir=esperas.append))
        self.assertEqual(respostas, [])
        self.assertEqual(len(esperas), 2)
        self.assertLess(esperas[0], esperas[1])

    def test_sessao_persistente(self):
        criados = []

        class ClienteFalso:
            def __init__(self, *args, **kwargs):
                criados.append(kwargs)

        falso = types.SimpleNamespace(Client=ClienteFalso, CallbackAPIVersion=types.SimpleNamespace(VERSION2=2))
        notificador.novo_cliente(falso)
        self.assertEqual(criados[0]["client_id"], "notificador")
        self.assertFalse(criados[0]["clean_session"])


if __name__ == "__main__":
    unittest.main()
