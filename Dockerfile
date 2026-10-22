FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       build-essential bash python3 ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

# Mantiene disponible el entorno de desarrollo para docker compose exec.
# Este proceso no participa en el protocolo concurrente de la aplicación.
CMD ["sleep", "infinity"]
