// Parser de letras sincronizadas no formato LRC.
// Sem dependências do Pico SDK (roda também no computador, para testes).
#ifndef LRC_H
#define LRC_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t ms;       // instante em que a linha começa
    const char *text;  // aponta para dentro do buffer passado a lrc_parse
} lrc_line_t;

// Interpreta o texto .lrc. O buffer é MODIFICADO (as linhas viram strings) e
// precisa continuar válido enquanto `out` for usado. Aceita várias tags de
// tempo por linha e ignora metadados ([ar:...], [ti:...]). Devolve a
// quantidade de linhas, ordenadas por tempo.
size_t lrc_parse(char *buf, lrc_line_t *out, size_t max);

// Índice da última linha com ms <= progress_ms, ou -1 se ainda não começou.
int lrc_find(const lrc_line_t *lines, size_t n, int64_t progress_ms);

#endif
