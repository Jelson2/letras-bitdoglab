// Leitor de JSON minimalista, sem alocação: percorre o texto e avisa cada valor
// "folha" junto com o seu caminho (ex.: "item.artists.0.name"). Serve para
// pegar poucos campos de respostas grandes e funciona mesmo com JSON cortado.
// Sem dependências do Pico SDK (roda também no computador, para testes).
#ifndef JSON_SCAN_H
#define JSON_SCAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JSON_PATH_MAX 96
#define JSON_MAX_DEPTH 12

// `val`/`len` apontam para o valor dentro do JSON. Para strings, é o conteúdo
// sem as aspas e ainda com escapes (use json_unescape). Para números, true,
// false e null, é o texto literal. Devolva false para parar a leitura.
typedef bool (*json_leaf_cb)(const char *path, const char *val, size_t len, bool is_string, void *ud);

// Devolve false se o JSON estiver malformado ou cortado (os valores lidos até
// ali já foram entregues ao callback).
bool json_scan(const char *json, size_t len, json_leaf_cb cb, void *ud);

// Desfaz escapes JSON (\n, \", \uXXXX...) gerando UTF-8 terminado em NUL.
// Devolve o tamanho escrito em `dst`.
size_t json_unescape(const char *src, size_t len, char *dst, size_t cap);

// Escreve o code point em UTF-8 (1 a 4 bytes, sem NUL). Devolve quantos bytes.
size_t json_utf8_encode(uint32_t cp, char *out);

#endif
