// Busca de letras sincronizadas no LRCLIB (https://lrclib.net).
#ifndef LRCLIB_H
#define LRCLIB_H

#include <stddef.h>
#include <stdint.h>

#include "lrc.h"

// Procura a letra sincronizada. Devolve o número de linhas carregadas (> 0),
// 0 se não existe letra sincronizada, ou um valor < 0 se houve erro de rede
// (HTTPS_ERR_*, ou -código HTTP para 429/5xx) e vale tentar de novo depois.
int lrclib_fetch(const char *track, const char *artist, const char *album, uint32_t duration_s);

// Linhas da última busca bem-sucedida (válidas até a próxima chamada de lrclib_fetch).
const lrc_line_t *lrclib_lines(void);
size_t lrclib_count(void);
void lrclib_clear(void);

#endif
