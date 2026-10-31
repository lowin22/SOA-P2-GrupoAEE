"""Pruebas de duración del cierre del Issue P2-08."""

import unittest

from scripts.metricas import calcular_duracion_cierre


class TestMetricasCierre(unittest.TestCase):

    def test_cierre_correcto(self):
        self.assertEqual(
            calcular_duracion_cierre(1_000_000_000, 3_500_000_000),
            2_500_000_000,
        )

    def test_cierre_inmediato(self):
        self.assertEqual(calcular_duracion_cierre(100, 100), 0)

    def test_tiempo_invertido(self):
        with self.assertRaises(ValueError):
            calcular_duracion_cierre(200, 100)


if __name__ == "__main__":
    unittest.main()
