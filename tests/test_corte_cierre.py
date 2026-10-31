
"""Pruebas de auditoría del corte OPEN a CLOSING."""

import unittest

from scripts.verificar_csv import verificar_corte_cierre


class TestCorteCierre(unittest.TestCase):

    def crear_evento(self, evento, tiempo, estado="OPEN"):

        # Construir un evento sencillo para las pruebas.
        return {
            "event": evento,
            "timestamp_ns": str(tiempo),
            "enqueue_time_ns": str(tiempo),
            "state": estado,
            "participant_id": "1",
            "producer_id": "1",
            "sequence": "1",
        }

    def test_insercion_antes_del_cierre(self):

        # El cierre comienza en el instante 1000.
        fila = self.crear_evento("ENQUEUE", 900)

        # La inserción anterior al cierre debe aceptarse.
        self.assertEqual(
            verificar_corte_cierre([fila], 1000), []
        )

    def test_insercion_posterior_al_cierre(self):

        # Simular una inserción después del corte.
        fila = self.crear_evento("ENQUEUE", 1100)

        # El verificador debe encontrar un error.
        errores = verificar_corte_cierre([fila], 1000)

        self.assertTrue(
            any("posterior" in error for error in errores)
        )

    def test_insercion_en_estado_closing(self):

        # Una inserción no puede aceptarse en CLOSING.
        fila = self.crear_evento("ENQUEUE", 900, "CLOSING")

        errores = verificar_corte_cierre([fila], 1000)

        self.assertTrue(
            any("fuera de OPEN" in error for error in errores)
        )

    def test_join_posterior_al_cierre(self):

        # Una incorporación posterior al cierre es inválida.
        fila = self.crear_evento("JOIN", 1200)

        errores = verificar_corte_cierre([fila], 1000)

        self.assertTrue(
            any("JOIN posterior" in error for error in errores)
        )


if __name__ == "__main__":
    unittest.main()
