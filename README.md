
# SOA-P2-GrupoAEE

## Proyecto 2 - Sistemas Operativos Avanzados

**Tecnológico de Costa Rica**
**II semestre de 2026**

### Comunicación y sincronización entre procesos mediante un buffer compartido

Este proyecto consiste en desarrollar un sistema de productores y consumidores independientes, utilizando memoria compartida POSIX y un buffer circular.

El sistema permitirá intercambiar mensajes de forma sincronizada, mantener el orden FIFO y realizar un cierre ordenado sin perder mensajes previamente aceptados.

## Integrantes

| Nombre completo | Carné | Usuario de GitHub |
|---|---|---|
| Adrián Vinicio Campos León | 2025800296 | @Adrianvc94 |
| Esteban Chaves Morera | 2024800687 | @davidchaves04-code |
| Edwin Loria Fonseca | 2026801284 | @lowin22 |

## Estado del proyecto

En desarrollo.

Actualmente se está preparando la estructura y el entorno de compilación correspondiente al Issue P2-02.

Los programas creator, producer, consumer y finisher serán incorporados durante las siguientes etapas.

## Requisitos

- GNU/Linux de 64 bits.
- GCC compatible con C17.
- GNU Make.
- Docker y Docker Compose V2 (opcionales).
- Python 3 para futuras herramientas auxiliares.

## Estructura del proyecto

- `src/`: código fuente en C.
- `include/`: cabeceras e interfaces.
- `tests/`: pruebas automatizadas.
- `scripts/`: herramientas auxiliares.
- `results/`: resultados experimentales.
- `docs/`: documentación técnica.
- `Makefile`: compilación y pruebas.
- `Dockerfile`: entorno de desarrollo reproducible.
- `compose.yaml`: configuración del contenedor.

## Compilación directa en GNU/Linux

Desde la raíz del proyecto:

```bash
make
```

Para limpiar los archivos generados:

```bash
make clean
```

Para ejecutar las pruebas automatizadas:

```bash
make test
```

**Nota:** la suite de pruebas todavía no está implementada. Por ahora `make test` devuelve un código de error intencional.

La compilación inicial solamente reconoce los archivos fuente que ya estén disponibles. Esto no significa que el sistema esté completamente implementado.

## Entorno Docker

Construir la imagen:

```bash
sudo docker compose build
```

Iniciar el contenedor:

```bash
sudo docker compose up -d
```

Comprobar su estado:

```bash
sudo docker compose ps
```

Compilar dentro del contenedor:

```bash
sudo docker compose exec -T soa-p2 make
```

Entrar a una terminal del contenedor:

```bash
sudo docker compose exec soa-p2 bash
```

Los productores, consumidores y el finalizador se ejecutarán como procesos independientes dentro del mismo contenedor.

Cuando estén implementados, podrán iniciarse desde distintas terminales mediante `docker compose exec`.

Detener el entorno:

```bash
sudo docker compose down
```

**Importante:** detener Docker no sustituye el cierre ordenado que deberá implementar `finisher`.

## Compilación

Se utilizarán las opciones:

`-std=c17 -Wall -Wextra -Wpedantic -Werror -pthread`

El proyecto utilizará APIs POSIX y memoria compartida mediante `shm_open` y `mmap`.

## Organización del trabajo

Las actividades se gestionan mediante Issues y Pull Requests de GitHub.

Cada integrante desarrolla sus tareas en una rama independiente. Los cambios deben revisarse antes de incorporarse a `main`.

El diseño de sincronización, las interfaces y el cierre deben acordarse entre los tres integrantes.

## Pendiente de integración

- Implementación de los cuatro ejecutables.
- Integración de módulos compartidos.
- Protocolo de sincronización y cierre.
- Formatos CSV y métricas.
- Suite de pruebas automatizadas.
- Experimentos y análisis.
- Informe final y etiqueta de entrega.

Este README se actualizará conforme avance el proyecto.
