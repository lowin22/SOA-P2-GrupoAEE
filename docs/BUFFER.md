# Buffer y sincronización P2-06

Responsable Adrián, revisor Edwin

## Implementación

`src/common/buffer.c` implementa las funciones existentes `soa_buffer_put` y
`soa_buffer_take` sin cambiar sus nombres ni firmas públicas

Cada operación toma el mutex compartido y lo libera antes de retornar
Las condiciones `not_full` y `not_empty` se esperan dentro de ciclos que comprueban
el estado y la ocupación, `pthread_cond_wait` libera el mutex mientras espera y lo
recupera antes de continuar

La inserción solo avanza en `OPEN`, asigna secuencia global y tiempo monotónico bajo
el mismo mutex que protege el corte a `CLOSING`
La extracción mantiene FIFO y permite drenar durante `CLOSING`
En `CLOSED` se rechazan ambas operaciones
Los índices avanzan con módulo de la capacidad y los contadores se comprueban antes
de incrementarlos para evitar overflow
Las secuencias siguientes deben inicializarse en uno, al alcanzar `UINT64_MAX` se
rechaza la operación para no desbordar el siguiente incremento

Los eventos son copias privadas completas con rol, participante, tipo, estado,
tiempo y ocupación posterior a la operación
La inserción copia el borrador e ignora sus campos de secuencia global y tiempo,
también pone `reserved` en cero
La extracción captura `dequeue_seq` y conserva la identidad y la carga útil
La escritura de CSV y el procesamiento pertenecen al llamador después del retorno
El buffer no genera pausas ni realiza E/S de archivos
El consumidor debe permanecer registrado durante su procesamiento posterior

Cada inserción avisa a `not_empty` y cada extracción avisa a `not_full`
Al vaciar el buffer se notifica también `state_changed` para que el finalizador
pueda volver a comprobar su predicado de drenaje
El finalizador deberá cambiar el estado y despertar las tres condiciones bajo el
mismo mutex, esas operaciones pertenecen a P2-10

## Retornos

| Código | Significado |
| --- | --- |
| `SOA_OK` | Operación realizada y salidas disponibles |
| `SOA_CLOSED` | Inserción después del corte o extracción sin posibilidad de datos, salidas intactas |
| `SOA_INVALID_ARGUMENT` | Punteros nulos, rol incorrecto, participante no registrado, ID fuera de rango o borrador sin identidad válida |
| `SOA_INCOMPATIBLE_INSTANCE` | Capacidad, índices, ocupación, estado o secuencias iniciales incompatibles |
| `SOA_OVERFLOW` | Secuencia, contador o marca temporal fuera de rango, sin modificar el buffer |
| `SOA_SYSTEM_ERROR` | Fallo del reloj o de una primitiva POSIX, errno permite consultar la causa |

Los fallos anteriores a la modificación conservan mensajes y eventos de salida
Si una notificación o el desbloqueo falla después de realizar la operación, los
datos ya fueron aceptados o extraídos y el evento ya contiene esa operación
No se debe reintentar ciegamente porque se podría duplicar una inserción
Estos fallos se reportan como errores del sistema, no se promete recuperación ante
primitivas corruptas o procesos que mueren dentro de una sección crítica

## Integración pendiente con memoria y participantes

En esta rama no hay implementación de P2-05 ni una representación interna existente
de `soa_instance_t`, el issue de P2-05 está abierto
Por eso `src/common/instance_internal.h` define únicamente el almacenamiento que
necesita el buffer y los campos privados de rol, ID y registro
No constituye una ABI final ni reemplaza el diseño aprobado de publicación y admisión

Antes de integrar con P2-05 y P2-07 se requiere revisión de Edwin para unificar esta
cabecera con su representación de memoria

- Incorporar cabecera de publicación, tamaño y versión, participantes y métricas según P2-01
- Calcular el mapeo con `offsetof` y la capacidad, comprobando overflow
- Inicializar mutex y condiciones con `PTHREAD_PROCESS_SHARED`
- Configurar las condiciones con `CLOCK_MONOTONIC`
- Inicializar estado en `OPEN`, índices y contadores en cero y secuencias en uno
- Entregar handles registrados con el rol y el ID correctos
- Mantener la reserva de vida desde antes del mapeo hasta después del último uso
- No destruir ni desmontar el segmento mientras una operación pueda usarlo

No se implementan aquí creator, apertura, registro ni finisher
El indicador privado de registro no sustituye la admisión sincronizada de P2-07
Las validaciones del buffer tampoco sustituyen la validación de ABI antes de tocar
el mutex ni el bloqueo de descriptor acordado en P2-01
La instrumentación acumulada de esperas por participante se integra con P2-08,
este módulo entrega los eventos exigidos por el contrato de P2-03

## Pruebas

```bash
make clean
make -j4 test
make test-buffer
```

`tests/buffer_test.c` crea objetos POSIX exclusivos y los desvincula inmediatamente
para que no queden nombres después de una prueba fallida
Los procesos hijos vuelven a mapear el objeto en otra dirección y usan las
funciones públicas del buffer, las primitivas son compartidas entre procesos
El fixture inicializa el segmento y handles privados porque los módulos de memoria
y registro todavía no están disponibles
No se presenta como una ejecución completa de los cuatro programas del proyecto

| Caso | Comprobación |
| --- | --- |
| Capacidades 1, 2, 8 y 1024 | FIFO, índices circulares, ocupación y conservación |
| Inserción y extracción intercaladas | Reutilización de posiciones e identidad estable |
| Eventos | Identidad, carga, tiempos, secuencias, rol, estado y ocupación |
| Lleno y vacío | Sin resultado prematuro, reanudación al aparecer espacio o datos |
| Notificación sin cambiar el predicado | El trabajador continúa esperando |
| Cierre con productor o consumidor esperando | Despertar y retorno sin nuevas inserciones |
| Cierre con pendientes | Drenaje y retorno de cierre después de vaciar |
| Cerrado con pendientes | Ninguna extracción en `CLOSED` |
| Cuatro productores y cuatro consumidores | 400 mensajes con capacidad 1 y 8, cada identidad exactamente una vez |
| Errores | Punteros, rol, registro, estado interno y overflow sin modificar datos |

Los pipes confirman que el hijo está por llamar a la operación y transmiten su
resultado, `poll` comprueba ausencia de respuesta y progreso con plazos acotados
Las alarmas solo detectan estancamiento en pruebas, no coordinan el buffer
No hay sondeo ni pausas artificiales en las operaciones de producción
El segmento de pruebas reserva al final un vector de identidades independiente de
las celdas del buffer para detectar duplicados entre consumidores
La carga conocida del fixture prueba integridad, no implementa el generador de
payload de los trabajadores que corresponde a P2-09
La integración de las aplicaciones reales y de sus CSV con el verificador queda
pendiente de P2-05, P2-07, P2-08, P2-09 y P2-10

La implementación y las pruebas se realizaron con asistencia de Codex
La revisión de Edwin y el PR integrado siguen pendientes

## Validación obtenida

Pasaron la reconstrucción limpia con `make -j4 test`, la suite del buffer,
smoke y las 30 pruebas Python existentes
También pasó la suite del buffer instrumentada con ASan y UBSan

```bash
make clean
ASAN_OPTIONS=detect_leaks=0 make -j4 CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' LDFLAGS='-fsanitize=address,undefined -no-pie' test-buffer
```

Se desactivó la detección de fugas por la limitación de LeakSanitizer bajo ptrace
ya observada en este entorno, no se afirma validación de fugas ni ausencia de carreras
Al terminar se reconstruyó con los flags normales y se ejecutó nuevamente la suite
