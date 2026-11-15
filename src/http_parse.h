// Interpretador incremental de respostas HTTP/1.x: lê o status e entrega o
// corpo aos poucos, aceitando Content-Length, "Transfer-Encoding: chunked" e
// corpo até o servidor fechar. Sem dependências do Pico SDK.
#ifndef HTTP_PARSE_H
#define HTTP_PARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*http_body_fn)(const uint8_t *data, size_t len, void *ud);

typedef struct {
    uint8_t state;
    int status;
    bool first_line;
    bool chunked;
    bool has_len;
    bool in_ext;
    uint8_t digits;
    uint8_t line_len;
    uint32_t remaining;
    uint32_t chunk_size;
    char line[96];
    http_body_fn on_body;
    void *ud;
} http_parser_t;

void http_parser_init(http_parser_t *p, http_body_fn on_body, void *ud);

// Alimenta com bytes recebidos (em qualquer fatiamento). Devolve false se a
// resposta for inválida.
bool http_parser_feed(http_parser_t *p, const uint8_t *data, size_t len);

// true quando o corpo terminou (Content-Length atingido ou último chunk).
bool http_parser_done(const http_parser_t *p);

// Chamar quando a conexão fechar: corpo sem tamanho conhecido termina aí.
void http_parser_eof(http_parser_t *p);

// Código HTTP (200, 404...) ou 0 se os cabeçalhos ainda não chegaram.
int http_parser_status(const http_parser_t *p);

#endif
