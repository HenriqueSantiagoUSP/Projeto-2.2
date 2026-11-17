"""Testes do demo_pulseira.py sem placa, sem janela e sem broker.

    python -m unittest test_demo_pulseira          (de dentro de interface/)
"""

import contextlib
import io
import json
import re
import sys
import threading
import types
import unittest
from pathlib import Path
from unittest import mock

import demo_pulseira as demo

AQUI = Path(__file__).resolve().parent
sys.path.insert(0, str(AQUI / "notificador"))
from notificador import formatar, interpretar  # noqa: E402

FALL_MODEL_C = AQUI.parent / "firmware" / "gadget" / "components" / "fall_pipeline" / "fall_model.c"


class TestLinhas(unittest.TestCase):
    def test_cabecalho_e_amostra(self):
        self.assertEqual(demo.interpretar_linha("CSV:t_ms,ax,ay,az,gx,gy,gz  (raw counts; 4096 LSB/g)\n"),
                         ("inicio", None))
        tipo, (t_ms, ax, ay, az) = demo.interpretar_linha("CSV:1000,4096,0,-4096,12,-3,7\r\n")
        self.assertEqual((tipo, t_ms, ax, ay, az), ("amostra", 1000, 1.0, 0.0, -1.0))

    def test_feat_com_cor_ansi(self):
        linha = ("\x1b[0;32mFEAT:pico=600,a_pico=7.5854,a_mean=1.47,pos_std=0.0353,inclinacao_graus=90.2,"
                 "prob=0.9935,queda=1,cycles=123456\x1b[0m\n")
        tipo, dados = demo.interpretar_linha(linha)
        self.assertEqual(tipo, "janela")
        self.assertEqual(dados["pico"], 600)
        self.assertEqual(dados["queda"], 1)
        self.assertAlmostEqual(dados["prob"], 0.9935)
        self.assertAlmostEqual(dados["inclinacao_graus"], 90.2)

    def test_boot(self):
        self.assertEqual(demo.interpretar_linha("I (345) main: wearable a1b2c3, MAC 24:0a:c4:a1:b2:c3 (configure "
                                                "it in the hub)"), ("pulseira", "a1b2c3"))
        tipo, modelo = demo.interpretar_linha("I (350) main: fall pipeline: trigger 2.00 g, random forest, fall "
                                              "when P >= 0.73 (trained at +-8 g)")
        self.assertEqual(tipo, "modelo")
        self.assertEqual(modelo, {"l_on": 2.0, "classificador": "random forest", "limiar": 0.73})

    def test_avisos_da_placa(self):
        self.assertEqual(demo.interpretar_linha("\x1b[0;33mW (500) main: MPU-6050 not connected (I2C SDA GPIO1, "
                                                "SCL GPIO2): no fall detection\x1b[0m"),
                         ("aviso", "MPU-6050 not connected (I2C SDA GPIO1, SCL GPIO2): no fall detection"))
        # A linha da queda já aparece como disparo; não vira aviso.
        self.assertIsNone(demo.interpretar_linha("W (600) main: FALL (a2 = 57.54 g2, P = 99 %), seq 3"))
        self.assertIsNone(demo.interpretar_linha("I (700) main: trigger at sample 10: peak 2.10 g"))

    def test_botao_da_placa(self):
        self.assertEqual(demo.interpretar_linha("\x1b[0;33mW (900) main: HELP (button held 3 s), seq 5\x1b[0m"),
                         ("ajuda", None))
        self.assertEqual(demo.interpretar_linha("I (950) main: HELP_CANCEL for seq 5"), ("cancelamento", "ajuda"))
        self.assertEqual(demo.interpretar_linha("I (960) main: FALL_CANCEL for seq 3"), ("cancelamento", "queda"))
        # Um reenvio atrasado não é um novo "estou bem"; uma falha de envio é só um aviso.
        self.assertIsNone(demo.interpretar_linha("I (980) main: FALL_CANCEL for seq 3 delivered late"))
        tipo, _ = demo.interpretar_linha("W (970) main: FALL_CANCEL not delivered to the hub: trying again from 2 s on")
        self.assertEqual(tipo, "aviso")


class TestFiltro(unittest.TestCase):
    def test_coeficientes_iguais_aos_do_firmware(self):
        texto = FALL_MODEL_C.read_text(encoding="utf-8")
        bloco = re.search(r"fall_model_biquad\[5\] = \{(.*?)\};", texto, re.S).group(1)
        firmware = [float(v.rstrip("f")) for v in re.findall(r"-?[\d.]+(?:e-?\d+)?f", bloco)]
        for nosso, deles in zip(demo.coeficientes_butterworth(), firmware):
            self.assertAlmostEqual(nosso, deles, places=6)

    def test_parte_do_repouso(self):
        filtro = demo.Biquad(demo.coeficientes_butterworth())
        saidas = [filtro(1.0) for _ in range(50)]
        for y in saidas:
            self.assertAlmostEqual(y, 1.0, places=9)


class TestPonte(unittest.TestCase):
    def test_alerta_como_o_do_hub_e_o_notificador_entende(self):
        alerta = demo.alerta_queda("a1b2c3", 42, "ativo", a_pico=3.0, prob=0.97, ts=1792497600000)
        self.assertEqual(alerta, {"v": 1, "id": "a1b2c3-42", "tipo": "queda", "estado": "ativo",
                                  "origem": "pulseira/a1b2c3", "detalhe": {"a2": 9.0, "prob": 0.97},
                                  "ts": 1792497600000})
        a = interpretar("idoso/butanta/quarto101/alerta", json.dumps(alerta).encode())
        texto = formatar(a)
        self.assertIn("9.00 g²", texto)
        self.assertIn("confiança do modelo: 97 %", texto)

    def test_cancelado_sem_detalhe(self):
        alerta = demo.alerta_queda("a1b2c3", 42, "cancelado", ts=1)
        self.assertEqual((alerta["estado"], alerta["detalhe"]), ("cancelado", {}))

    def test_pedido_de_ajuda_como_o_do_hub(self):
        alerta = demo.alerta_ajuda("a1b2c3", 43, "ativo", ts=1792497600000)
        self.assertEqual(alerta, {"v": 1, "id": "a1b2c3-43", "tipo": "ajuda", "estado": "ativo",
                                  "origem": "pulseira/a1b2c3", "detalhe": {}, "ts": 1792497600000})
        texto = formatar(interpretar("idoso/butanta/quarto101/alerta", json.dumps(alerta).encode()))
        self.assertIn("Pedido de ajuda", texto)

    def test_estou_bem_cancela_o_ultimo_alerta(self):
        ponte = object.__new__(demo.Ponte)      # sem broker: só a lógica dos alertas
        ponte.e = demo.Estado(janela_s=10.0)
        ponte.e.pulseira = "a1b2c3"
        ponte.seq, ponte.aberto = 10, None
        publicados = []
        ponte.publicar = lambda sufixo, dados, retain: publicados.append(dados)
        ponte.queda(3.0, 0.97)
        ponte.ajuda()
        ponte.cancelar()
        self.assertEqual([(a["id"], a["tipo"], a["estado"]) for a in publicados],
                         [("a1b2c3-11", "queda", "ativo"), ("a1b2c3-12", "ajuda", "ativo"),
                          ("a1b2c3-12", "ajuda", "cancelado")])
        with contextlib.redirect_stdout(io.StringIO()):
            ponte.cancelar()                    # nada mais aberto
        self.assertEqual(len(publicados), 3)


class TestLeitor(unittest.TestCase):
    def test_feat_vai_para_o_tempo_da_amostra_e_continua_depois_de_um_reinicio(self):
        estado = demo.Estado(janela_s=10.0)
        leitor = demo.Leitor(estado)
        leitor.linha("CSV:t_ms,ax,ay,az,gx,gy,gz")
        for i in range(400):
            leitor.linha(f"CSV:{i * 5},0,0,4096,0,0,0")
        leitor.linha("FEAT:pico=100,a_pico=2.5,prob=0.10,queda=0,cycles=1")
        self.assertAlmostEqual(estado.disparos[-1].t, 0.5)
        self.assertAlmostEqual(estado.sinal[-1][1], 1.0, places=6)     # 1 g parado, filtrado

        # O sensor reconecta: a contagem recomeça, o tempo da sessão não volta atrás.
        leitor.linha("CSV:t_ms,ax,ay,az,gx,gy,gz")
        leitor.linha("CSV:0,0,0,4096,0,0,0")
        self.assertAlmostEqual(estado.t_ultimo, 2.0, places=6)
        leitor.linha("FEAT:pico=0,a_pico=6.0,prob=0.95,queda=1,cycles=1")
        self.assertTrue(estado.disparos[-1].queda)
        self.assertAlmostEqual(estado.disparos[-1].t, 2.0, places=6)
        self.assertEqual(estado.n_quedas, 1)

    def test_botao_da_placa_vai_para_a_ponte_e_para_a_faixa(self):
        estado = demo.Estado(janela_s=10.0)
        chamadas = []
        ponte = types.SimpleNamespace(queda=lambda *a: chamadas.append("queda"),
                                      ajuda=lambda: chamadas.append("ajuda"),
                                      cancelar=lambda: chamadas.append("cancelar"))
        leitor = demo.Leitor(estado, ponte)
        leitor.linha("CSV:t_ms,ax,ay,az,gx,gy,gz")
        leitor.linha("CSV:0,0,0,4096,0,0,0")
        leitor.linha("W (900) main: HELP (button held 3 s), seq 5")
        self.assertEqual(estado.faixa_tipo, "ajuda")
        self.assertGreater(estado.faixa_ate, estado.t_ultimo)
        leitor.linha("I (950) main: HELP_CANCEL for seq 5")
        self.assertEqual(estado.faixa_ate, 0.0)
        self.assertEqual(chamadas, ["ajuda", "cancelar"])

    def test_sinal_limitado_depois_de_um_tempo_absurdo(self):
        # Bytes perdidos na serial podem juntar dois tempos numa linha só ("CSV:1234" + "123455,...").
        estado = demo.Estado(janela_s=10.0)
        leitor = demo.Leitor(estado)
        leitor.linha("CSV:t_ms,ax,ay,az,gx,gy,gz")
        for i in range(2000):
            leitor.linha(f"CSV:{i * 5},0,0,4096,0,0,0")
        leitor.linha("CSV:1234123455,0,0,4096,0,0,0")
        for i in range(2000, 8000):
            leitor.linha(f"CSV:{i * 5},0,0,4096,0,0,0")
        self.assertLessEqual(len(estado.sinal), int((estado.janela_s + 2.0) * demo.FS_HZ))
        self.assertAlmostEqual(estado.sinal[-1][0], 7999 * 0.005, places=6)


class TestLinhasEstragadas(unittest.TestCase):
    def test_modelo_com_numeros_emendados_e_ignorado(self):
        linha = "I (350) main: fall pipeline: trigger 2.00 g, random forest, fall when P >= 0.732.5123"
        self.assertIsNone(demo.interpretar_linha(linha))

    def test_feat_com_pico_infinito_e_ignorado(self):
        self.assertIsNone(demo.interpretar_linha("FEAT:pico=inf,a_pico=2.5,prob=0.10,queda=0"))


class SerialFalsa:
    """Imita o pyserial: cada abertura segue um plano, uma lista de linhas (bytes ou exceção) ou "falha"."""

    planos = []
    abertas = []
    parar = None

    def __init__(self):
        self.port = self.baudrate = self.timeout = None
        self.dtr = self.rts = False
        self.rts_ligado = False
        self.linhas = []

    def __setattr__(self, nome, valor):
        if nome == "rts" and valor:
            object.__setattr__(self, "rts_ligado", True)
        object.__setattr__(self, nome, valor)

    def open(self):
        plano = SerialFalsa.planos.pop(0)
        SerialFalsa.abertas.append(self)
        if plano == "falha":
            raise ErroSerialFalso("a porta ainda não voltou")
        self.linhas = list(plano)

    def readline(self):
        if not self.linhas:
            SerialFalsa.parar.set()
            return b""
        item = self.linhas.pop(0)
        if isinstance(item, Exception):
            raise item
        return item

    def close(self):
        pass


class ErroSerialFalso(Exception):
    pass


class TestLeituraSerial(unittest.TestCase):
    def ler(self, planos, leitor, reiniciar=False):
        SerialFalsa.planos, SerialFalsa.abertas = list(planos), []
        SerialFalsa.parar = threading.Event()
        falso = types.SimpleNamespace(Serial=SerialFalsa, SerialException=ErroSerialFalso)
        with mock.patch.dict(sys.modules, {"serial": falso}), \
                mock.patch.object(demo, "REABRIR_S", 0.0, create=True), \
                contextlib.redirect_stderr(io.StringIO()):
            demo.ler_serial("COM9", 921600, leitor, None, SerialFalsa.parar, reiniciar)

    def test_a_porta_cai_e_volta_sem_reiniciar_a_placa(self):
        # O que aconteceu na bancada: GetOverlappedResult failed (Acesso negado) no meio da leitura.
        estado = demo.Estado(janela_s=10.0)
        planos = [[b"CSV:t_ms,ax,ay,az,gx,gy,gz\n", b"CSV:0,0,0,4096,0,0,0\n", b"CSV:5,0,0,4096,0,0,0\n",
                   ErroSerialFalso("GetOverlappedResult failed (Acesso negado)")],
                  "falha",
                  [b"CSV:10,0,0,4096,0,0,0\n", b"CSV:15,0,0,4096,0,0,0\n"]]
        self.ler(planos, demo.Leitor(estado), reiniciar=True)
        self.assertEqual(len(estado.sinal), 4)
        self.assertEqual(len(SerialFalsa.abertas), 3)
        self.assertTrue(SerialFalsa.abertas[0].rts_ligado)                 # o reset só na primeira abertura
        self.assertFalse(any(s.rts_ligado for s in SerialFalsa.abertas[1:]))
        self.assertFalse(estado.serial_perdida)

    def test_porta_ocupada_no_inicio_espera_ficar_livre(self):
        # A COM está com o idf.py monitor: a janela mostra a perda e conecta quando a porta for solta.
        estado = demo.Estado(janela_s=10.0)
        planos = ["falha", "falha", [b"CSV:t_ms,ax,ay,az,gx,gy,gz\n", b"CSV:0,0,0,4096,0,0,0\n"]]
        self.ler(planos, demo.Leitor(estado), reiniciar=True)
        self.assertEqual(len(estado.sinal), 1)
        self.assertEqual(len(SerialFalsa.abertas), 3)
        self.assertTrue(SerialFalsa.abertas[2].rts_ligado)     # o reset vai na abertura que deu certo
        self.assertFalse(estado.serial_perdida)

    def test_uma_linha_que_da_erro_nao_para_a_leitura(self):
        class LeitorFragil(demo.Leitor):
            def linha(self, texto):
                if texto.startswith("BOOM"):
                    raise RuntimeError("linha inesperada")
                super().linha(texto)

        estado = demo.Estado(janela_s=10.0)
        planos = [[b"CSV:t_ms,ax,ay,az,gx,gy,gz\n", b"CSV:0,0,0,4096,0,0,0\n", b"BOOM\n",
                   b"CSV:5,0,0,4096,0,0,0\n"]]
        self.ler(planos, LeitorFragil(estado))
        self.assertEqual(len(estado.sinal), 2)


if __name__ == "__main__":
    unittest.main()
