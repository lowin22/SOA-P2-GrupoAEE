# Productores consumidores y pausas

## Estado actual

Producer y consumer ejecutan el ciclo completo con el escritor CSV
El Makefile compila y enlaza worker.c y la CLI llama a worker_run
Los nueve casos de trabajadores están activos y también se ejecutan con make test

## Implementación

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

El ciclo abre instancia y log, registra el participante y escribe JOIN
Producer admite cuota positiva o ejecución continua
Consumer permanece esperando cuando el buffer está vacío
La secuencia comienza en 1 y aumenta por cada mensaje aceptado del productor
El ID del productor y la secuencia identifican cada mensaje
Los eventos se escriben fuera del bloqueo y los datos se vacían antes de la retirada
LEAVE se conserva como evento privado y se escribe antes de cerrar la conexión
Los errores de log intentan retirar al participante antes de devolver error

El buffer acumula operaciones y esperas en los registros históricos existentes
operations representa aceptados para productores y extraídos para consumidores
mutex_wait_ns mide las adquisiciones explícitas del buffer
condition_wait_ns incluye la espera por datos o espacio y la readquisición del mutex
Las pausas simuladas no se contabilizan como falta de datos o espacio

## Validación

```bash
make -j4 test
make test-workers
make test-random
```

La revisión pasó la suite completa con 63 pruebas Python sin omisiones
También pasaron los nueve casos de make test-workers

Se cubren cuota con media cero y positiva, producción continua y espera del consumidor en vacío
Se comprueba cierre durante pausa y procesamiento, notificaciones sin reiniciar el plazo y fallos del log
El generador se prueba con la misma semilla y el payload se verifica antes y después de alterarlo

El auxiliar worker_control solicita CLOSING y genera notificaciones dentro de las pruebas
El cierre completo se comprobó además con creator, producer, consumer y finisher reales
Los CSV de ambos trabajadores pasaron el verificador con la opción --validar-ciclo

Se intercambiaron 1000 mensajes con capacidad 8 y media cero
También 100 mensajes con capacidad 1 y media de 1 ms
Ambos recorridos usaron semilla 21 para el productor y 11 para el consumidor
Se verificaron las secuencias crecientes y la eliminación del recurso después del cierre
