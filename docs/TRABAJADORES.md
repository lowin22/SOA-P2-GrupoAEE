# Productores consumidores y pausas

## Estado actual

P2-09 conserva el generador, el payload, las pausas y el ciclo de trabajo
La ejecución de producer y consumer está pendiente de integrar el escritor CSV de P2-08
No se agregó otro escritor CSV ni una implementación temporal de logs

El archivo worker.c conserva la integración preparada pero no se compila ni se enlaza
La CLI sigue mostrando la operación pendiente cuando recibe argumentos válidos
P2-09 no se considera terminado hasta conectar sus trabajadores con el escritor real

## Implementación preparada

Cada proceso conserva un estado SplitMix64 inicializado una sola vez con su semilla
Las pausas exponenciales evitan log de cero y una media cero evita el sorteo
El plazo se calcula una sola vez con CLOCK_MONOTONIC
Las notificaciones no reinician la pausa sorteada
El productor pausa antes de insertar y despierta con el cierre
El consumidor extrae y después completa el procesamiento aunque comience el cierre

El payload contiene 32 bytes y usa un estado independiente de las pausas
Combina producer_id desplazado 32 bits con sequence mediante XOR
Se generan cuatro valores SplitMix64 en orden little endian
Las mismas semillas repiten los sorteos locales, no los tiempos ni el orden entre procesos

El ciclo preparado abre instancia y log, registra el participante y escribe JOIN
Producer admite cuota positiva o ejecución continua
Consumer permanece esperando cuando el buffer está vacío
Los eventos se escriben fuera del bloqueo y los datos se vacían antes de la retirada
LEAVE se conserva como evento privado y se escribe antes de cerrar la conexión
Los errores de log intentan retirar al participante antes de devolver error

El buffer acumula operaciones y esperas en los registros históricos existentes
operations representa aceptados para productores y extraídos para consumidores
mutex_wait_ns mide las adquisiciones explícitas del buffer
condition_wait_ns incluye la espera por datos o espacio y la readquisición del mutex
Las pausas simuladas no se contabilizan como falta de datos o espacio

## Integración pendiente

Cuando se integre P2-08 se requiere

- Confirmar las interfaces reales de csv_writer y el contrato de errores
- Agregar worker.o y csv_writer.o a los objetos comunes del Makefile
- Conectar producer y consumer con worker_run desde soa_app_main
- Actualizar la ayuda y las pruebas de argumentos para reflejar la ejecución real
- Activar test-workers y ejecutar sus nueve casos con los logs reales
- Repetir la suite completa y los sanitizers

El auxiliar worker_control solo solicita CLOSING dentro de las pruebas

## Validación disponible

```bash
make clean
make -j4 test
make test-random
```

Se pueden verificar el generador, el payload, el buffer y las suites ya integradas
Los nueve casos de trabajadores quedan omitidos explícitamente por falta de P2-08
El objetivo test-workers devuelve error para no presentar esa cobertura como aprobada

Antes de retirar la dependencia se ejecutaron los nueve casos de trabajadores y
la suite completa con ASan y UBSan, esa evidencia es histórica y debe repetirse al integrar
LeakSanitizer permanece limitado por el entorno bajo ptrace