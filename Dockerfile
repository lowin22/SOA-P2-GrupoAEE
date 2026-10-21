# Utilizar Ubuntu 24.04 como sistema base.
FROM ubuntu:24.04

# Evitar preguntas interactivas durante la instalación.
ENV DEBIAN_FRONTEND=noninteractive

# Instalar el compilador y herramientas de desarrollo.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       build-essential \
       make \
       gdb \
       python3 \
       ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Definir el directorio de trabajo dentro del contenedor.
WORKDIR /workspace

# Mantener disponible la terminal para trabajar.
CMD ["/bin/bash"]