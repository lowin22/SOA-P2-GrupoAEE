#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd -- "$root"
test_dir=$(mktemp -d)
creator_name="/soa_p2_args_$$"
trap 'rm -f -- "/dev/shm/${creator_name#/}"; rm -rf -- "$test_dir"' EXIT
printf 'existing log must survive\n' > "$test_dir/existing.csv"
cp -- "$test_dir/existing.csv" "$test_dir/expected.csv"

expect_error() {
    local fragment=$1
    shift
    if "$@" > "$test_dir/stdout" 2> "$test_dir/stderr"; then
        printf 'ERROR: se esperaba error: %s\n' "$*" >&2
        exit 1
    fi
    [[ ! -s "$test_dir/stdout" ]]
    if ! [[ -s "$test_dir/stderr" ]] ||
       ! LC_ALL=C grep -Fq -- "$fragment" "$test_dir/stderr"; then
        cat "$test_dir/stderr" >&2
        printf 'ERROR: falta diagnostico "%s": %s\n' "$fragment" "$*" >&2
        exit 1
    fi
}

for program in creator producer consumer finisher; do
    "./$program" --help > "$test_dir/stdout" 2> "$test_dir/stderr"
    [[ ! -s "$test_dir/stderr" ]]
    LC_ALL=C grep -Fq -- '--name' "$test_dir/stdout"
    expect_error 'Faltan argumentos' "./$program"
    expect_error 'desconocida' "./$program" --unknown
    expect_error '--help' "./$program" --help --unknown
    expect_error 'repetida' "./$program" --name /uno --name /dos
    expect_error 'valor' "./$program" --name --help
done

# Creator debe aceptar sus argumentos y crear la instancia.
./creator --capacity 1024 --name "$creator_name" > "$test_dir/stdout" \
    2> "$test_dir/stderr"
[[ ! -s "$test_dir/stderr" ]]
LC_ALL=C grep -Fq -- 'creada con capacidad 1024' "$test_dir/stdout"

# Productores y consumidores ya están implementados.
# Usamos una instancia inexistente para comprobar que los argumentos
# son válidos sin iniciar procesos de intercambio de mensajes.
expect_error 'instancia' ./producer --name /args_valid --id 1024 \
    --mean-ms 1000 --seed 18446744073709551615 --messages 1 --log "$test_dir/new.csv"
expect_error 'instancia' ./producer --name /args_valid --id 1 \
    --mean-ms 0 --seed 0 --log "$test_dir/new.csv"
expect_error 'instancia' ./consumer --name /args_valid --id 1 \
    --mean-ms 2.5 --seed 1 --log "$test_dir/new.csv"

# Finisher todavía no tiene activada su implementación.
expect_error 'argumentos validos' ./finisher --name /args_valid --summary "$test_dir/new.csv"

for value in 0 1025 -1 8abc 18446744073709551616; do
    expect_error '--capacity' ./creator --name /args_invalid --capacity "$value"
done
for program in producer consumer; do
    for value in NaN inf 1001 -1 1e309 1ms; do
        expect_error '--mean-ms' "./$program" --name /args_invalid --id 1 \
            --mean-ms "$value" --seed 1 --log "$test_dir/existing.csv"
    done
    expect_error '--id' "./$program" --name /args_invalid --id 0 \
        --mean-ms 0 --seed 1 --log "$test_dir/new.csv"
    expect_error '--seed' "./$program" --name /args_invalid --id 1 \
        --mean-ms 0 --seed -1 --log "$test_dir/new.csv"
done
expect_error '--messages' ./producer --name /args_invalid --id 1 \
    --mean-ms 0 --seed 1 --messages 0 --log "$test_dir/existing.csv"
expect_error '--name' ./finisher --name /bad/name --summary "$test_dir/existing.csv"
expect_error 'no permitida' ./consumer --name /args_invalid --messages 1
expect_error 'no permitida' ./creator --name /args_invalid --log "$test_dir/new.csv"

cmp -- "$test_dir/existing.csv" "$test_dir/expected.csv"
[[ ! -e "$test_dir/new.csv" ]]
printf 'OK: argumentos CLI, diagnosticos y archivos de resultados intactos.\n'
