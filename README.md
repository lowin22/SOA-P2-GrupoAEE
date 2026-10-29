# SOA-P2-GrupoAEE

## Proyecto 2 — Buffer compartido entre procesos

Proyecto de Sistemas Operativos Avanzados, II semestre de 2026. Se implementarán
cuatro programas C17 sobre GNU/Linux con memoria compartida POSIX: `creator`,
`producer`, `consumer` y `finisher`.

**Estado: cáscara inicial.** Solo se implementa `--help` y el rechazo explícito de
operaciones pendientes. Todavía no se crean recursos IPC ni se intercambian mensajes.

El módulo de buffer de P2-06 está implementado y se comprueba con `make test-buffer`
Incluye operaciones FIFO y pruebas entre procesos con condiciones compartidas
La integración con creación y registro sigue pendiente de P2-05 y P2-07
El [contrato del buffer](docs/BUFFER.md) describe bloqueos, retornos y pruebas

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
make smoke
./creator --help
./producer --help
./consumer --help
./finisher --help
make clean
```

Se compila con `-std=c17 -Wall -Wextra -Wpedantic -Werror -pthread`.
Los binarios se generan en la raíz y los objetos/dependencias en `build/`.
`make test` devuelve error deliberadamente hasta integrar pruebas funcionales;
`make smoke` solo valida infraestructura. `make help` describe los objetivos.

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
| `tests/` | Comprobaciones iniciales; futura suite funcional. |
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
