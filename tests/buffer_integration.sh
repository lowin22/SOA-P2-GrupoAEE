#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd -- "$root"
python=${1:-python3}
test_dir=$(mktemp -d)
name="/buffer_integration_$$"
pids=()
cleanup() {
    for pid in "${pids[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
        fi
        wait "$pid" 2>/dev/null || true
    done
    rm -f -- "/dev/shm/${name#/}"
    rm -rf -- "$test_dir"
}
trap cleanup EXIT

./build/tests/buffer_integration drain "$name"

for configuration in '8 1 1000' '1 4 1000' '8 4 1000'; do
    read -r capacity workers count <<< "$configuration"
    ./creator --name "$name" --capacity "$capacity"
    pids=()
    logs=()
    for role in consumer producer; do
        for ((id = 1; id <= workers; ++id)); do
            log="$test_dir/${role}_${id}.csv"
            ./build/tests/buffer_integration worker "$name" "$role" "$id" "$count" "$log" &
            pids+=("$!")
            logs+=("$log")
        done
    done
    failed=0
    for pid in "${pids[@]}"; do
        wait "$pid" || failed=1
    done
    pids=()
    [[ $failed == 0 ]]
    "$python" scripts/verificar_csv.py --validar-ciclo "${logs[@]}"
    ./build/tests/buffer_integration check "$name" "$((workers * count))"
    printf 'OK integracion capacidad %s con %s productores y consumidores\n' "$capacity" "$workers"
done
