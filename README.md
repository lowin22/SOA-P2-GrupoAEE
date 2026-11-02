# SOA-P2-GrupoAEE

## Proyecto 2 — Buffer compartido entre procesos

Sistema de múltiples productores y consumidores implementado en C17 para GNU/Linux. Cuatro programas independientes (`creator`, `producer`, `consumer` y `finisher`) intercambian mensajes mediante un buffer circular FIFO en memoria compartida POSIX. El protocolo usa mutex y variables de condición compartidos entre procesos, registro de participantes y cierre cooperativo con drenaje.

El proyecto incluye validación de argumentos, verificación de identidad, contenido y FIFO, métricas CSV, pruebas de intercalados, estrés 32/32, ASan/UBSan y un experimento de 45 corridas.

## Integrantes

| Nombre completo | Carné | Usuario de GitHub |
|---|---:|---|
| Adrián Vinicio Campos León | 2025800296 | [@Adrianvc94](https://github.com/Adrianvc94) |
| Esteban Chaves Morera | 2024800687 | [@davidchaves04-code](https://github.com/davidchaves04-code) |
| Edwin Loria Fonseca | 2026801284 | [@lowin22](https://github.com/lowin22) |

## Entorno probado

- Ubuntu 26.04.1 LTS de 64 bits sobre WSL2.
- Kernel Linux 6.18.33.2-microsoft-standard-WSL2.
- GCC 15.2.0.
- Intel Core i7-13700H, con 20 procesadores lógicos visibles.
- GNU Make, Bash y Python 3.

También se proporciona un entorno Docker Compose. Los detalles y las limitaciones de la ejecución instrumentada están en [`docs/ESTRES.md`](docs/ESTRES.md).

## Compilación y pruebas

```bash
make clean
make -j4
make -j4 test
```

Se compila con `-std=c17 -Wall -Wextra -Wpedantic -Werror -pthread`. Los binarios se generan en la raíz y los objetos en `build/`. `make test` falla ante cualquier incumplimiento e incluye argumentos, memoria, registro, buffer, trabajadores, cierre, verificadores y estrés con 32 productores y 32 consumidores.

Objetivos adicionales:

```bash
make smoke
make test-buffer
make test-shutdown
make test-stress
make -j4 test-asan ASAN_OPTIONS=detect_leaks=0:halt_on_error=1
make -j4 test-ubsan
make help
```

LeakSanitizer se deshabilitó porque no funciona bajo `ptrace` en el entorno WSL2 probado. ASan mantiene la detección de accesos inválidos; UBSan comprueba comportamiento indefinido. Ninguno demuestra ausencia de carreras entre procesos.

## Ejecución completa

El siguiente ejemplo usa un productor y un consumidor. El productor tiene cuota finita; por ello se espera su terminación antes de solicitar el cierre.

```bash
mkdir -p results/demo

./creator --name /grupo_aee --capacity 8

./consumer --name /grupo_aee --id 1 --mean-ms 10 --seed 11 \
  --log results/demo/c1.csv &
consumer_pid=$!

./producer --name /grupo_aee --id 1 --mean-ms 5 --seed 21 \
  --messages 1000 --log results/demo/p1.csv

./finisher --name /grupo_aee --summary results/demo/resumen.csv
wait "$consumer_pid"

python3 scripts/verificar_csv.py \
  results/demo/p1.csv results/demo/c1.csv \
  --validar-ciclo --summary results/demo/resumen.csv
```

El orden del protocolo es:

1. `creator` crea y publica la instancia.
2. Consumidores y productores se conectan y registran IDs únicos por rol.
3. Los productores insertan; los consumidores extraen FIFO y procesan fuera del mutex.
4. Después de terminar los productores, `finisher` cambia a `CLOSING`.
5. Los consumidores drenan los mensajes aceptados y se retiran.
6. `finisher` espera participantes y conexiones, destruye las primitivas, elimina el nombre POSIX y escribe el resumen.

Un productor sin `--messages` continúa hasta que `finisher` solicite el cierre. El cierre se rechaza y la instancia permanece `OPEN` si existen productores o datos pendientes pero no hay consumidores activos.

## Opciones

```text
./creator  --name NOMBRE --capacity CAPACIDAD
./producer --name NOMBRE --id ID --mean-ms MS --seed SEMILLA
           [--messages CUOTA] --log ARCHIVO
./consumer --name NOMBRE --id ID --mean-ms MS --seed SEMILLA --log ARCHIVO
./finisher --name NOMBRE --summary ARCHIVO
```

- Capacidad e IDs: `1..1024`.
- Media exponencial: valor finito entre `0..1000` ms; cero desactiva pausas.
- Semilla: `0..UINT64_MAX`.
- Cuota explícita: `1..UINT64_MAX`.
- Nombre: `/` seguido de caracteres ASCII alfanuméricos, `_` o `-`, con máximo de 255 bytes.
- Los IDs no se reutilizan dentro del mismo rol durante la vida de una instancia.
- `--help` muestra la ayuda de cada ejecutable.

El contrato completo está en [`docs/OPCIONES.md`](docs/OPCIONES.md).

## CSV y métricas

Cada trabajador escribe su propio CSV. Los eventos incluyen rol, ID, tipo, tiempo monotónico, estado, identidad del mensaje, secuencias globales, ocupación y payload. La escritura ocurre fuera de la región crítica; por eso FIFO se verifica mediante `enqueue_seq` y `dequeue_seq`, no mediante el orden físico de las líneas.

El resumen de `finisher` contiene aceptados, extraídos, pendientes, marcas temporales, throughput y duración del cierre. Su esquema reserva columnas de latencia, pero el ejecutable no las completa: el análisis reconstruye la latencia media y el P95 cruzando los eventos de inserción y extracción. Los esquemas y unidades están documentados en [`docs/FORMATOS_CSV.md`](docs/FORMATOS_CSV.md).

## Experimentos

La matriz publicada contiene 45 corridas: capacidades 1, 8 y 64; tres relaciones entre tasas; cinco repeticiones; cuatro productores y cuatro consumidores; 800 mensajes por corrida.

```bash
# Mostrar la planificación sin ejecutar procesos
python3 scripts/ejecutar_experimentos.py

# Ejecutar una configuración
python3 scripts/ejecutar_experimentos.py --ejecutar --corrida 1

# Ejecutar las 45 configuraciones
python3 scripts/ejecutar_experimentos.py --ejecutar --todas
```

Los parámetros, semillas, consolidados y gráficas están en [`results/experimentos/`](results/experimentos/). El análisis publicado no conserva las métricas cuantitativas de espera por participante ni el manifiesto completo del entorno original; esta limitación se declara en el informe.

## Docker

```bash
export LOCAL_UID="$(id -u)"
export LOCAL_GID="$(id -g)"
docker compose build
docker compose up -d dev
docker compose exec dev make clean
docker compose exec dev make -j4 test
docker compose down
```

El repositorio se monta en `/workspace` y `/dev/shm` dispone de 64 MiB. Al alternar entre compilación nativa y contenedor se debe ejecutar `make clean` para no mezclar objetos.

## Estructura y responsabilidades

| Ruta | Contenido |
|---|---|
| `src/apps/` | Puntos de entrada de los cuatro ejecutables. |
| `src/common/` | Memoria, buffer, participantes, trabajadores, cierre y métricas. |
| `include/soa/` | Interfaces y tipos públicos. |
| `tests/` | Pruebas C, Python, integración, carreras y estrés. |
| `scripts/` | Verificación CSV, experimentos, análisis y gráficas. |
| `results/` | Configuraciones y evidencia experimental seleccionada. |
| `docs/` | Arquitectura, protocolo, pruebas, estrés e informe. |

- **Edwin:** memoria, admisión, registro, cierre e intercalados.
- **Adrián:** opciones, buffer, trabajadores, pausas, estrés y sanitizers.
- **Esteban:** CSV, métricas, verificador, suite y experimentos.
- **Equipo completo:** arquitectura, revisión integral, informe y defensa.

## Documentación

- [Informe](docs/informe.pdf)
- [Guía de defensa individual](docs/DEFENSA.md)
- [Arquitectura](docs/ARQUITECTURA.md)
- [Protocolo de sincronización y cierre](docs/PROTOCOLO.md)
- [Formato de CSV y métricas](docs/FORMATOS_CSV.md)
- [Pruebas y evidencia](docs/PRUEBAS.md)
- [Estrés y sanitizers](docs/ESTRES.md)
- [Herramientas y scripts](scripts/README.md)
- [Resultados experimentales](results/experimentos/analisis_final/README.md)

El PDF del informe se regenera, si se dispone de LaTeX e IEEEtran, mediante:

```bash
cd docs
latexmk -pdf -interaction=nonstopmode -halt-on-error informe.tex
```

## Procedencia y licencia

El código, las pruebas, los scripts, las gráficas y la documentación fueron elaborados por el equipo para este proyecto. No se incorporó una implementación externa del buffer o de su sincronización. Las lecturas de Dijkstra y Birrell se utilizan como fundamento conceptual y se citan en el informe. GCC, GNU Make, Python, Docker y las interfaces POSIX se usan bajo sus licencias respectivas y no se redistribuyen como parte del código del proyecto.

Este repositorio académico no concede una licencia general de reutilización. La etiqueta anotada `p2-entrega`, el hash definitivo y el paquete de entrega se incorporarán en P2-17.
