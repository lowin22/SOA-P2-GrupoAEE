# Estrés y sanitizers

## Estrés

```bash
make test-stress
```

Se ejecutan creator, 32 productores, 32 consumidores y finisher como procesos independientes
Cada productor usa una cuota de 1000 mensajes y cada trabajador usa su ID como semilla
Las pausas están desactivadas y se prueban capacidades 1 y 8
Cada caso debe conservar 32000 mensajes y terminar sin pendientes

Primero se registran los 32 productores sin consumidores para llenar el buffer y bloquearlos
Después se incorporan los consumidores y se confirma el registro de los 32
El auxiliar worker_control espera esos registros con state_changed bajo el mutex compartido
Los predicados se revisan en ciclos y la espera libera el mutex, sin sondeo

Se espera que terminen las cuotas antes de ejecutar finisher
Se comprueban las secuencias de cada productor, la ocupación y los eventos JOIN y LEAVE
El verificador existente cruza los 64 CSV y el resumen con --validar-ciclo y --summary
Se exigen identidades y payload íntegros, FIFO, aceptados iguales a extraídos y pendientes cero
También se comprueba que el nombre POSIX haya sido eliminado después del cierre

El caso está incluido en make test y tiene un timeout externo de 120 segundos
Un error de proceso, de archivo o del verificador hace fallar la prueba
El timeout detecta estancamiento, no participa en la sincronización del programa
La limpieza de una prueba fallida termina solo sus procesos y retira solo su nombre

## Compilación instrumentada

```bash
make -j4 test-asan
make -j4 test-ubsan
```

Los objetivos asan y ubsan solo reconstruyen los cuatro ejecutables
Los objetivos test-asan y test-ubsan reconstruyen y ejecutan toda la suite con timeout externo de 300 segundos
test-sanitizers ejecuta ambos en orden
Cada reconstrucción elimina primero los objetos anteriores para no mezclar configuraciones
Ejecutar estos objetivos por separado de otras compilaciones porque comparten build y los ejecutables

Se usa -O1 -g -fno-omit-frame-pointer con -fsanitize=address o -fsanitize=undefined
Se conservan C17, las advertencias estrictas y pthread
ASAN_OPTIONS usa halt_on_error=1 y UBSAN_OPTIONS usa halt_on_error=1:print_stacktrace=1
Ambas opciones pueden indicarse desde el entorno o como variables de make

En este entorno LeakSanitizer falla bajo ptrace
La suite ASan se validó con la detección de fugas desactivada

```bash
make -j4 test-sanitizers ASAN_OPTIONS=detect_leaks=0:halt_on_error=1
```

Esto mantiene la detección de accesos inválidos de ASan, pero no comprueba fugas

Para volver a la compilación normal

```bash
make clean
make -j4 test
```

## Entorno y resultados

Validación sobre d27bf8b con los cambios locales de estrés y sanitizers

- Ubuntu 26.04.1 LTS de 64 bits sobre WSL2
- Kernel Linux 6.18.33.2-microsoft-standard-WSL2
- GCC 15.2.0 de Ubuntu
- Intel Core i7 13700H con 20 procesadores lógicos visibles
- Compilación nativa con -O2 -g

| Configuración | Suite | Estrés capacidad 1 | Estrés capacidad 8 | Código final |
| --- | --- | --- | --- | --- |
| Nativa | 71 pruebas Python, 264 casos del parser y suites C aprobadas | 32000 mensajes verificados | 32000 mensajes verificados | 0 |
| ASan con detect_leaks=0 | Misma suite aprobada | 32000 mensajes verificados | 32000 mensajes verificados | 0 |
| UBSan | Misma suite aprobada | 32000 mensajes verificados | 32000 mensajes verificados | 0 |

La primera ejecución de ASan con detección de fugas activa terminó con código 2 de make
LeakSanitizer informó que no puede funcionar bajo ptrace
Con detect_leaks=0 no se detectaron errores de accesos a memoria en los casos ejecutados
UBSan tampoco detectó comportamiento indefinido
Después se restauró la compilación normal con make clean y pasó make -j4 test

También se comprobó un timeout externo de 0,2 segundos con código 124 y retirada del nombre de prueba
La ejecución con make test-stress PYTHON=false terminó con código 2 de make
Así se comprobó que el objetivo propaga un fallo del ejecutor

Los CSV, comandos por proceso y diagnósticos se generan en results/runs/stress con un sufijo por ejecución
Cada ejecución conserva sus propios archivos y esos directorios están ignorados por Git
Esta documentación conserva los resultados seleccionados

## Límites

ASan y UBSan no demuestran ausencia de carreras entre procesos ni garantizan progreso en todos los intercalados
La evidencia se complementa con los [invariantes y predicados](PROTOCOLO.md) y las [pruebas de intercalados y cierre](PRUEBAS.md#8-intercalados-y-carreras-de-cierre--p2-13)

El mutex protege índices, ocupación, secuencias y el corte de cierre
Cada aceptación incrementa ocupación y aceptados, cada extracción reduce ocupación e incrementa extraídos
Así se conserva aceptados igual a extraídos más pendientes y el avance de head y tail mantiene FIFO
Las esperas liberan el mutex y el cierre despierta las condiciones
El finalizador espera procesamiento, retiradas y conexiones antes de destruir las primitivas
Estas garantías suponen procesos cooperantes y planificación que permita avanzar, no prometen equidad

Los resultados instrumentados validan corrección y no se usan para el experimento de rendimiento
