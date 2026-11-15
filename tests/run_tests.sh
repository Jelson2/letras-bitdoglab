#!/bin/sh
# Compila e roda os testes da lógica pura no computador (precisa de gcc ou clang).
# Uso, a partir da pasta do projeto:  sh tests/run_tests.sh
set -e
cd "$(dirname "$0")/.."
CC="${CC:-gcc}"
FLAGS="-std=c11 -Wall -Wextra -g -fsanitize=address,undefined -fno-sanitize-recover=all -Isrc"

$CC $FLAGS tests/test_host.c src/json_scan.c src/lrc.c src/text.c -o /tmp/letras_test_host
/tmp/letras_test_host

$CC $FLAGS tests/test_net.c src/http_parse.c src/stream_field.c src/json_scan.c src/oled_gfx.c src/text.c -o /tmp/letras_test_net
/tmp/letras_test_net
