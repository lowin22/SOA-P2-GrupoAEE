"""Pruebas unitarias de métricas del Issue P2-08."""

import unittest

from scripts.metricas import (
    calcular_latencia_promedio,
    calcular_percentil_95,
)


class TestMetricas(unittest.TestCase):

    def test_promedio(self):
        latencias = [10, 20, 30, 40, 100]
        self.assertEqual(calcular_latencia_promedio(latencias), 40.0)

    def test_percentil_95(self):
        latencias = [10, 20, 30, 40, 100]
        self.assertEqual(calcular_percentil_95(latencias), 100)

    def test_lista_vacia(self):
        self.assertIsNone(calcular_latencia_promedio([]))
        self.assertIsNone(calcular_percentil_95([]))

    def test_un_mensaje(self):
        self.assertEqual(calcular_latencia_promedio([250]), 250.0)
        self.assertEqual(calcular_percentil_95([250]), 250)

    def test_percentil_sin_interpolacion(self):
        latencias = list(range(1, 101))
        self.assertEqual(calcular_percentil_95(latencias), 95)


if __name__ == "__main__":
    unittest.main()
