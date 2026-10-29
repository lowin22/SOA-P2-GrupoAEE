# Argumentos y validación — P2-04
## Opciones por ejecutable

```bash
./creator --name /grupo_aee --capacity 8
./producer --name /grupo_aee --id 1 --mean-ms 5 --seed 21 --messages 1000 --log results/p1.csv
./consumer --name /grupo_aee --id 1 --mean-ms 10 --seed 11 --log results/c1.csv
./finisher --name /grupo_aee --summary results/resumen.csv
```

Todas las opciones de cada ejemplo son obligatorias salvo `--messages`.
Cada ejecutable admite también `--help` como único argumento.

| Opción | Formato y rango |
| --- | --- |
| `--name` | `/` seguido de uno o más caracteres ASCII `[A-Za-z0-9_-]`, máximo 255 bytes en total sin terminador |
| `--capacity` | Entero decimal, 1..1024 |
| `--id` | Entero decimal, 1..1024 según P2-01 |
| `--mean-ms` | Decimal finito, 0..1000 ms, cero desactiva pausas |
| `--seed` | Entero decimal, 0..18446744073709551615 |
| `--messages` | Entero decimal, 1..18446744073709551615, omitir significa producir hasta cierre |
| `--log` y `--summary` | Ruta no vacía, sin comprobar existencia o permisos durante el parseo |

Los límites y el conjunto de caracteres del nombre, así como el rango de semillas,
concretan decisiones del proyecto que el enunciado no define por completo.
Determinar si un ID válido ya se utilizó corresponde al registro sincronizado de
P2-07, no al parser. El nombre se conserva sin añadir prefijos.

## Reglas de sintaxis

- Formato `--opcion valor`, en cualquier orden, con nombres completos. No se
  aceptan opciones cortas, abreviaturas, `--opcion=valor`, argumentos sueltos ni
  `--` como separador.
- Se rechazan opciones desconocidas, repetidas o pertenecientes a otro ejecutable.
  El rechazo de repetidas también aplica si los valores coinciden.
- Un valor ausente o vacío es un error. Un token que comienza por `--` no se consume
  como valor. Para una ruta con ese prefijo puede usarse `./--archivo.csv`.
- Los enteros solo admiten dígitos ASCII, sin signos ni espacios. Ceros iniciales
  permitidos, `008` significa ocho. Se rechazan conversiones parciales y overflow
  antes de convertir a tipos de menor ancho.
- La media admite decimales y exponente opcional, como `2.5`, `.5` y `1e2`. También
  admite `+1` y `-0`, que equivale a cero. Se rechazan espacios, hexadecimal,
  `NaN`, infinito y conversiones con overflow o underflow reportado por `strtod`.
  Se utiliza el punto decimal del locale C inicial, sin cambiar el locale.
- Las rutas pueden contener espacios si el shell las entrega como un argumento.
  No se crean directorios ni se abren o truncan archivos al validar.

## API y ejecución

`options_parse` está declarada en `include/soa/options.h` e implementada en
`src/common/options.c`. Reconoce opciones, comprueba cuáles admite el ejecutable,
exige las obligatorias y convierte los valores en una estructura temporal.
Solo actualiza `out` cuando todos los argumentos son válidos.

Devuelve `OPTIONS_OK`, `OPTIONS_HELP` o `OPTIONS_ERROR`. Ayuda y errores dejan
`out` intacto. El diagnóstico queda terminado en nulo si el buffer tiene tamaño
positivo y puede truncarse si es pequeño. Las cadenas de salida pertenecen a
`argv` y no se liberan. El parser no imprime ni accede a archivos o recursos IPC.

Los cuatro puntos de entrada conservan el nombre existente `soa_app_main` y le
pasan el tipo de programa. La ayuda sale por stdout con código cero. Los errores
salen por stderr con código no cero. La operación siempre queda después del parseo.
Actualmente los argumentos válidos alcanzan un diagnóstico explícito de operación
pendiente y código no cero porque aún falta implementar la ejecución IPC.

## Pruebas

```bash
make clean
make -j4 test
make test-options
make test-python
```

`make test` combina argumentos, smoke y las pruebas Python incorporadas por otros
Issues. Un fallo devuelve código no cero. La suite completa del buffer, concurrencia
y cierre sigue pendiente, no se atribuye esa cobertura a las pruebas actuales.