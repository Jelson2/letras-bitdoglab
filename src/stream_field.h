// Extrai, em streaming, o valor de um campo string de um JSON (ex.:
// "syncedLyrics") sem precisar guardar o JSON inteiro: o texto já sai
// desescapado, em UTF-8, direto no buffer de destino. Aceita a entrada
// fatiada em qualquer ponto. Se o campo for null ou não for string, segue
// procurando (útil em respostas que são arrays de objetos).
// Sem dependências do Pico SDK.
#ifndef STREAM_FIELD_H
#define STREAM_FIELD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char pat[40];
    uint8_t fail[40];
    uint8_t pat_len;
    uint8_t matched;
    uint8_t state;
    uint8_t uni_digits;
    uint16_t uni_val;
    uint16_t high;  // metade alta de um par substituto, à espera da baixa
    char *out;
    size_t cap;
    size_t len;
    bool found;      // achou o campo e leu a string até o fim (ou até encher o buffer)
    bool truncated;  // o buffer encheu antes do fim da string
} stream_field_t;

// `key` sem aspas (ex.: "syncedLyrics"). `out` recebe o texto com NUL no fim.
void sf_init(stream_field_t *s, const char *key, char *out, size_t cap);
void sf_feed(stream_field_t *s, const uint8_t *data, size_t len);
bool sf_done(const stream_field_t *s);

#endif
