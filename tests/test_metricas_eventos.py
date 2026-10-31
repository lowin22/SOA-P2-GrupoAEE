"""Pruebas de extracción de latencias desde eventos CSV."""

import unittest

from scripts.metricas import extraer_latencias


class TestMetricasEventos(unittest.TestCase):

    def test_dos_extracciones(self):
        registros = [
            {"event": "ENQUEUE", "timestamp_ns": "100",
             "enqueue_time_ns": "100"},
            {"event": "DEQUEUE", "timestamp_ns": "150",
             "enqueue_time_ns": "100"},
            {"event": "DEQUEUE", "timestamp_ns": "280",
             "enqueue_time_ns": "200"},
        ]

        self.assertEqual(extraer_latencias(registros), [50, 80])

    def test_sin_extracciones(self):
        registros = [
            {"event": "JOIN"},
            {"event": "LEAVE"},
        ]

        self.assertEqual(extraer_latencias(registros), [])

    def test_extraccion_anterior_a_insercion(self):
        registros = [
            {"event": "DEQUEUE", "timestamp_ns": "100",
             "enqueue_time_ns": "200"},
        ]

        with self.assertRaises(ValueError):
            extraer_latencias(registros)

    def test_tiempo_invalido(self):
        registros = [
            {"event": "DEQUEUE", "timestamp_ns": "abc",
             "enqueue_time_ns": "100"},
        ]

        with self.assertRaises(ValueError):
            extraer_latencias(registros)


if __name__ == "__main__":
    unittest.main()
