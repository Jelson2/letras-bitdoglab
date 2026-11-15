// Texto para o OLED: conversão UTF-8 -> ASCII, quebra de linha e URL encode.
// Sem dependências do Pico SDK (roda também no computador, para testes).
#ifndef TEXT_H
#define TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WRAP_MAX_COLS 21

// Converte UTF-8 para ASCII imprimível: tira acentos, troca aspas curvas e
// travessões e descarta o que a fonte 8x8 não tem. Devolve o tamanho de `out`.
size_t text_to_ascii(const char *utf8, char *out, size_t cap);

// Quebra `s` em linhas de até `cols` caracteres (por palavra; palavras maiores
// que a linha são cortadas). Devolve o número de linhas escritas em `out`;
// `*truncated` indica que sobrou texto além de `max_lines`.
int text_wrap(const char *s, int cols, char out[][WRAP_MAX_COLS + 1], int max_lines, bool *truncated);

// Percent-encoding de UTF-8. Devolve o tamanho de `out`.
size_t text_url_encode(const char *s, char *out, size_t cap);

// Base64 padrão (com '='). Devolve o tamanho de `out`, ou 0 se não couber.
size_t text_base64(const uint8_t *in, size_t n, char *out, size_t cap);

#endif
