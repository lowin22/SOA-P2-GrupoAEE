# Buffer y sincronización P2-06

Responsable Adrián, revisor Edwin

## Operaciones

`soa_buffer_put` inserta y `soa_buffer_take` extrae mensajes FIFO
Las funciones conservan sus nombres y firmas públicas

Ambas toman el mutex compartido y lo liberan antes de retornar
Las esperas por espacio y datos usan condiciones dentro de ciclos
Mientras esperan liberan el mutex, al despertar vuelven a comprobar el predicado

La inserción solo avanza en `OPEN`, asigna secuencia global y tiempo monotónico
bajo el mismo mutex que protege el cambio a `CLOSING`
La extracción permite drenar durante `CLOSING`
En `CLOSED` se rechazan ambas operaciones
Los índices avanzan con módulo de la capacidad
Los contadores y las secuencias se comprueban antes de incrementarse
Las secuencias empiezan en uno y se rechaza el incremento que desbordaría su rango

Los eventos son copias privadas con rol, participante, tipo, estado, tiempo,
ocupación posterior a la operación y mensaje completo
El borrador conserva su identidad y carga útil, la inserción asigna su secuencia
global y tiempo y pone `reserved` en cero
La extracción captura también `dequeue_seq`

Cada inserción avisa a `not_empty` y cada extracción avisa a `not_full`
Al vaciar el buffer se notifica `state_changed` para que el finalizador compruebe
su predicado de drenaje
El finalizador debe cambiar el estado y despertar las tres condiciones bajo el
mismo mutex, esa coordinación pertenece a P2-10

Las pausas, el procesamiento y la escritura de archivos ocurren fuera del buffer
El consumidor debe permanecer registrado hasta completar su procesamiento

## Retornos

| Código | Significado |
| --- | --- |
| `SOA_OK` | Operación realizada y salidas disponibles |
| `SOA_CLOSED` | El estado impide la operación, salidas intactas |
| `SOA_INVALID_ARGUMENT` | Punteros, identidad o handle inválidos |
| `SOA_INCOMPATIBLE_INSTANCE` | Estado interno incompatible con el buffer |
| `SOA_OVERFLOW` | Secuencia, contador o tiempo fuera de rango |
| `SOA_SYSTEM_ERROR` | Fallo del reloj o de una primitiva POSIX, errno indica la causa |

Los errores anteriores a la modificación dejan intactos el buffer y las salidas
Si una notificación o el desbloqueo falla después de la operación, los datos ya
fueron aceptados o extraídos y el evento refleja ese resultado
No se debe reintentar sin comprobarlo porque se podría duplicar una inserción
No se promete recuperación ante primitivas corruptas o procesos que mueren dentro
de la región crítica

## Integración

Se utiliza la representación de P2-05 y P2-07 sin cambiar su ABI
El handle debe conservar descriptor, reserva de lectura y registro con el rol correcto
La apertura valida publicación y ABI, el registro controla admisión e IDs históricos
El buffer mantiene la reserva de vida y usa el mutex y las condiciones de la instancia

P2-08 incorpora las métricas acumuladas de esperas y la escritura de eventos
P2-09 incorpora los trabajadores completos y P2-10 el cierre de la instancia

## Pruebas

```bash
make clean
make -j4 test
make test-buffer
```

`buffer_test.c` usa memoria POSIX y procesos con mapeos en direcciones distintas
Los datos de prueba controlan los límites y fallos internos sin depender del registro
Comprueba capacidades 1, 2, 8 y 1024, vuelta de índices, eventos, bloqueo por lleno
y vacío, notificaciones sin cambio del predicado, cierre y overflow
Los pipes confirman el intento de operación y transmiten el resultado
Los plazos de poll y las alarmas solo detectan estancamiento en pruebas

`buffer_integration.sh` usa creator real y procesos independientes registrados
Cada proceso usa las funciones públicas y escribe sus eventos fuera del mutex
El verificador comprueba identidad, contenido, FIFO y registro con los CSV generados
La integración también comprueba drenaje y rechazo de operaciones sin registro

| Integración | Mensajes verificados |
| --- | --- |
| Capacidad 8 con un productor y un consumidor | 1000 |
| Capacidad 1 con cuatro productores y cuatro consumidores | 4000 |
| Capacidad 8 con cuatro productores y cuatro consumidores | 4000 |

La carga conocida y el escritor CSV son auxiliares de prueba
No sustituyen los trabajadores ni el módulo de logs
La instancia de integración se libera cuando todos sus procesos terminaron,
con bloqueo exclusivo del descriptor antes de destruir las primitivas
Los archivos temporales se eliminan al terminar

Pasaron la reconstrucción limpia, las pruebas del buffer, la integración,
los argumentos, memoria, registro, smoke y las 30 pruebas Python
También pasó la integración instrumentada con ASan y UBSan

```bash
make clean
ASAN_OPTIONS=detect_leaks=0 make -j4 CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' LDFLAGS='-fsanitize=address,undefined -no-pie' test-buffer
```

La detección de fugas se desactivó por la limitación de LeakSanitizer bajo ptrace
No se afirma comprobación de fugas ni ausencia de carreras
Después se restauró la compilación normal

La implementación y las pruebas se realizaron con asistencia de Codex
La revisión de Edwin y el PR integrado siguen pendientes
