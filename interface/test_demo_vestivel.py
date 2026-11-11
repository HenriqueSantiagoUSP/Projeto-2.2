"""Testes do demo_vestivel.py sem placa, sem janela e sem broker.

    python -m unittest test_demo_vestivel          (de dentro de interface/)
"""

import json
import re
import sys
import unittest
from pathlib import Path

import demo_vestivel as demo

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
                                                "it in the hub)"), ("vestivel", "a1b2c3"))
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
                                  "origem": "vestivel/a1b2c3", "detalhe": {"a2": 9.0, "prob": 0.97},
                                  "ts": 1792497600000})
        a = interpretar("idoso/butanta/quarto101/alerta", json.dumps(alerta).encode())
        texto = formatar(a)
        self.assertIn("9.00 g²", texto)
        self.assertIn("confiança do modelo: 97 %", texto)

    def test_cancelado_sem_detalhe(self):
        alerta = demo.alerta_queda("a1b2c3", 42, "cancelado", ts=1)
        self.assertEqual((alerta["estado"], alerta["detalhe"]), ("cancelado", {}))


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


if __name__ == "__main__":
    unittest.main()
