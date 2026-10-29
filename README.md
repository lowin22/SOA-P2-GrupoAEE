# SOA-P2-GrupoAEE

## Proyecto 2 — Buffer compartido entre procesos

Proyecto de Sistemas Operativos Avanzados, II semestre de 2026. Se implementarán
cuatro programas C17 sobre GNU/Linux con memoria compartida POSIX: `creator`,
`producer`, `consumer` y `finisher`.

**Estado: validación de argumentos implementada (P2-04).** Los cuatro ejecutables
validan opciones antes de cualquier operación y ofrecen `--help`. Los argumentos
válidos todavía terminan con un diagnóstico explícito de operación pendiente:
no se crean instancias ni se intercambian mensajes.

## Integrantes

| Nombre completo | Carné | Usuario de GitHub |
|---|---:|---|
| Adrián Vinicio Campos León | 2025800296 | @Adrianvc94 |
| Esteban Chaves Morera | 2024800687 | @davidchaves04-code |
| Edwin Loria Fonseca | 2026801284 | @lowin22 |

## Compilación nativa

Requisitos: GNU/Linux, GCC con C17, GNU Make y Bash.

```bash
make -j
make test
make smoke
./creator --help
./producer --help
./consumer --help
./finisher --help
make clean
```

Se compila con `-std=c17 -Wall -Wextra -Wpedantic -Werror -pthread`.
Los binarios se generan en la raíz y los objetos/dependencias en `build/`.
`make test` ejecuta la cobertura disponible de argumentos e infraestructura y falla
ante incumplimientos. No acredita todavía el buffer ni el cierre de P2-12.
`make test-options` ejecuta la suite de P2-04. `make help` describe los objetivos.

## Opciones de los ejecutables

```bash
./creator --name /grupo_aee --capacity 8
./producer --name /grupo_aee --id 1 --mean-ms 5 --seed 21 --messages 1000 --log results/p1.csv
./consumer --name /grupo_aee --id 1 --mean-ms 10 --seed 11 --log results/c1.csv
./finisher --name /grupo_aee --summary results/resumen.csv
```

Son ejemplos de sintaxis validada; las operaciones IPC aún están pendientes.
Solo `--messages` es opcional: omitirla significa producir hasta cierre.
Capacidad e IDs: 1..1024; media finita: 0..1000 ms; semilla: 0..`UINT64_MAX`;
cuota explícita: 1..`UINT64_MAX`. Nombres: `/` y caracteres ASCII alfanuméricos,
`_` o `-`, máximo 255 bytes totales. Opciones completas, en cualquier orden,
sin repeticiones; `--help` se usa solo. Los IDs usados se comprueban en P2-07.
El [contrato de opciones](docs/OPCIONES.md) detalla formatos, errores, API y pruebas.

## Docker

Requiere Docker Engine y Compose v2. En Linux, exportar el UID/GID para que los
archivos generados en el montaje pertenezcan al usuario local:

```bash
export LOCAL_UID="$(id -u)"
export LOCAL_GID="$(id -g)"
docker compose build
docker compose up -d dev
docker compose exec dev make clean
docker compose exec dev make -j
docker compose exec dev make smoke
docker compose exec dev ./creator --help
docker compose down
```

El repositorio se monta en `/workspace`; los archivos persisten en el anfitrión.
Ejecutar cada participante con `docker compose exec dev ...` permite usar procesos
independientes dentro del mismo contenedor. Una vez implementado el sistema, cerrar
con `finisher` antes de detener Docker. El tamaño de `/dev/shm` configurado es 64 MiB
y debe revisarse contra la representación compartida aprobada.

Al alternar entre compilador nativo y contenedor, ejecutar `make clean` antes de
recompilar para no mezclar objetos de entornos distintos.

## Estructura y responsabilidades

| Ruta | Contenido |
|---|---|
| `src/apps/` | Cuatro puntos de entrada independientes. |
| `src/common/`, `include/soa/` | Módulos compartidos e interfaces. |
| `tests/` | Parser público, argumentos CLI, aislamiento e infraestructura; futura suite del buffer. |
| `scripts/` | Futuro verificador y experimentos. |
| `results/` | Instrucciones y futura evidencia seleccionada. |
| `docs/` | Diseño, planificación y contratos. |

- Edwin: memoria, admisión, registro y cierre.
- Adrián: opciones, buffer, trabajadores y pausas.
- Esteban: entorno, métricas, verificador y experimentos.
- Cada autor aporta pruebas y documentación; arquitectura y cierre se revisan entre los tres.

## Documentación y siguiente hito

- [Plan de implementación e Issues](docs/PLAN_IMPLEMENTACION.md)
- [Plan de la cáscara](docs/PLAN_CASCARA.md)
- [Arquitectura](docs/ARQUITECTURA.md)
- [Protocolo](docs/PROTOCOLO.md)
- [Contrato CSV](docs/FORMATOS_CSV.md)
- [Pruebas](docs/PRUEBAS.md)
- [Flujo de ramas y PR](docs/FLUJO_GIT.md)

Primero aprobar publicación, admisión, cierre e interfaces en P2-01 y el contrato
CSV en P2-03. Luego integrar creación, intercambio 1/1, verificación, cierre y
recreación del nombre. Los documentos técnicos actuales identifican decisiones
pendientes; no constituyen un protocolo ya aprobado.
