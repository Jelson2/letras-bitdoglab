// Cliente HTTPS bloqueante para o Pico W (lwIP altcp + mbedTLS, modo "poll").
// Faz uma requisição por vez e entrega o corpo da resposta aos poucos.
#ifndef HTTPS_CLIENT_H
#define HTTPS_CLIENT_H

#include <stdint.h>

#include "http_parse.h"

enum {
    HTTPS_ERR_TLS_CONFIG = -1,
    HTTPS_ERR_MEM = -2,
    HTTPS_ERR_DNS = -3,
    HTTPS_ERR_CONNECT = -4,  // inclui falha no handshake TLS
    HTTPS_ERR_TIMEOUT = -5,
    HTTPS_ERR_PROTO = -6,
    HTTPS_ERR_NO_RESPONSE = -7,
    HTTPS_ERR_SEND = -8,
};

// Envia `request` (texto HTTP completo) para host:443 e chama `on_body` com os
// pedaços do corpo. Devolve o código HTTP (200, 404...) ou um HTTPS_ERR_* (< 0).
int https_request(const char *host, const char *request, uint32_t timeout_ms, http_body_fn on_body, void *ud);

const char *https_error_text(int code);

#endif
