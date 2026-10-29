"""Pruebas de validación y throughput del P2-08."""

import unittest

from scripts.metricas import (
    calcular_latencia_promedio,
    calcular_percentil_95,
    calcular_throughput,
)


class TestMetricasValidacion(unittest.TestCase):

    def test_latencias_invalidas(self):
        for valor in (-10, 1.5, True, 2**64):
            with self.subTest(valor=valor):
                with self.assertRaises(ValueError):
                    calcular_latencia_promedio([valor])
                with self.assertRaises(ValueError):
                    calcular_percentil_95([valor])

    def test_throughput(self):
        # 100 mensajes extraídos durante dos segundos.
        self.assertEqual(
            calcular_throughput(100, 1_000_000_000, 3_000_000_000),
            50.0,
        )

    def test_throughput_sin_mensajes(self):
        self.assertIsNone(calcular_throughput(0, 100, 200))

    def test_throughput_intervalo_cero(self):
        self.assertIsNone(calcular_throughput(1, 100, 100))

    def test_throughput_tiempo_invertido(self):
        with self.assertRaises(ValueError):
            calcular_throughput(1, 200, 100)


if __name__ == "__main__":
    unittest.main()
