#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd -- "$root"

expect_failure() {
    local output
    if output=$("$@" 2>&1); then
        printf 'ERROR: se esperaba fallo: %s\n' "$*" >&2
        exit 1
    fi
    if [[ -z "$output" ]]; then
        printf 'ERROR: falta diagnóstico: %s\n' "$*" >&2
        exit 1
    fi
}

for program in creator producer consumer finisher; do
    [[ -x "./$program" ]] || { printf 'Falta %s\n' "$program" >&2; exit 1; }
    output=$("./$program" --help)
    [[ "$output" == *"$program"* && "$output" == *'Uso previsto:'* &&
       "$output" == *'pendiente de implementación'* ]] || {
        printf 'ERROR: ayuda incompleta de %s\n' "$program" >&2
        exit 1
    }
    expect_failure "./$program"
    expect_failure "./$program" --unknown
    expect_failure "./$program" --help --unknown
done

expect_failure ./creator --name /soa_p2_smoke --capacity 8
expect_failure ./producer --name /soa_p2_smoke --id 1 --mean-ms 0 --seed 1 --messages 1 --log unused.csv
expect_failure ./consumer --name /soa_p2_smoke --id 1 --mean-ms 0 --seed 2 --log unused.csv
expect_failure ./finisher --name /soa_p2_smoke --summary unused.csv

printf 'OK: infraestructura de la cáscara (no valida buffer ni cierre).\n'
