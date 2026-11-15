#include "https_client.h"

#include <stdio.h>
#include <string.h>

#include "lwip/altcp_tcp.h"
#include "lwip/altcp_tls.h"
#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "mbedtls/ssl.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

// Cada altcp_write precisa caber num registro TLS (MBEDTLS_SSL_OUT_CONTENT_LEN = 2048):
// se passar, o lwIP envia só uma parte e devolve erro. Por isso o envio é em pedaços.
#define SEND_CHUNK 1024
#define CLOSE_GRACE_MS 300

typedef struct {
    struct altcp_pcb *pcb;
    ip_addr_t ip;
    http_parser_t parser;
    volatile bool dns_done;
    volatile bool dns_ok;
    volatile bool connected;
    volatile bool done;
    volatile bool remote_closed;
    volatile int error;
} conn_t;

static struct altcp_tls_config *g_tls_cfg;

static void pump(void) {
    cyw43_arch_poll();
    cyw43_arch_wait_for_work_until(make_timeout_time_ms(5));
}

static void close_conn(conn_t *c) {
    if (!c->pcb) return;
    cyw43_arch_lwip_begin();
    altcp_arg(c->pcb, NULL);
    altcp_recv(c->pcb, NULL);
    altcp_err(c->pcb, NULL);
    if (altcp_close(c->pcb) != ERR_OK) altcp_abort(c->pcb);
    c->pcb = NULL;
    cyw43_arch_lwip_end();
}

static void on_dns(const char *name, const ip_addr_t *ip, void *arg) {
    (void)name;
    conn_t *c = arg;
    if (ip) {
        c->ip = *ip;
        c->dns_ok = true;
    }
    c->dns_done = true;
}

static err_t on_connected(void *arg, struct altcp_pcb *pcb, err_t err) {
    (void)pcb;
    conn_t *c = arg;
    if (err != ERR_OK) {
        c->error = HTTPS_ERR_CONNECT;
        c->done = true;
    } else {
        c->connected = true;  // o envio é feito fora do callback
    }
    return ERR_OK;
}

static void on_err(void *arg, err_t err) {
    (void)err;
    conn_t *c = arg;
    c->pcb = NULL;  // o lwIP já liberou o pcb: não pode mais ser usado
    if (!c->error) c->error = c->connected ? HTTPS_ERR_PROTO : HTTPS_ERR_CONNECT;
    c->done = true;
}

static err_t on_recv(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err) {
    conn_t *c = arg;
    if (!p) {  // o servidor fechou a conexão
        c->remote_closed = true;
        c->done = true;
        return ERR_OK;
    }
    if (err != ERR_OK) {
        pbuf_free(p);
        return err;
    }
    for (struct pbuf *q = p; q; q = q->next) {
        if (!http_parser_feed(&c->parser, (const uint8_t *)q->payload, q->len)) {
            c->error = HTTPS_ERR_PROTO;
            break;
        }
    }
    altcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    if (c->error || http_parser_done(&c->parser)) c->done = true;
    return ERR_OK;
}

static bool expired(absolute_time_t deadline) { return absolute_time_diff_us(get_absolute_time(), deadline) <= 0; }

// Envia o pedido em pedaços, esperando o lwIP liberar espaço quando precisar.
static bool send_all(conn_t *c, const char *req, size_t len, absolute_time_t deadline) {
    size_t sent = 0;
    while (sent < len) {
        if (c->done || !c->pcb || expired(deadline)) return false;
        size_t n = len - sent;
        if (n > SEND_CHUNK) n = SEND_CHUNK;
        cyw43_arch_lwip_begin();
        err_t e = altcp_write(c->pcb, req + sent, (u16_t)n, TCP_WRITE_FLAG_COPY);
        if (e == ERR_OK) altcp_output(c->pcb);
        cyw43_arch_lwip_end();
        if (e == ERR_OK) sent += n;
        else if (e == ERR_MEM) pump();  // sem espaço agora: deixa o lwIP esvaziar e tenta de novo
        else return false;
    }
    return true;
}

int https_request(const char *host, const char *request, uint32_t timeout_ms, http_body_fn on_body, void *ud) {
    static conn_t c;  // uma requisição por vez
    memset(&c, 0, sizeof c);
    http_parser_init(&c.parser, on_body, ud);

    if (!g_tls_cfg) {
        // Sem CA: a conexão é cifrada, mas o certificado do servidor não é verificado
        // (equivale ao setInsecure() do projeto ESP32).
        g_tls_cfg = altcp_tls_create_config_client(NULL, 0);
        if (!g_tls_cfg) return HTTPS_ERR_TLS_CONFIG;
    }

    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);

    // ---- pcb + DNS ----
    err_t e = ERR_MEM;
    cyw43_arch_lwip_begin();
    c.pcb = altcp_tls_new(g_tls_cfg, IPADDR_TYPE_V4);
    if (c.pcb) {
        altcp_arg(c.pcb, &c);
        altcp_recv(c.pcb, on_recv);
        altcp_err(c.pcb, on_err);
        mbedtls_ssl_set_hostname((mbedtls_ssl_context *)altcp_tls_context(c.pcb), host);  // SNI
        e = dns_gethostbyname(host, &c.ip, on_dns, &c);
    }
    cyw43_arch_lwip_end();
    if (!c.pcb) return HTTPS_ERR_MEM;
    if (e == ERR_OK) {
        c.dns_ok = c.dns_done = true;
    } else if (e != ERR_INPROGRESS) {
        close_conn(&c);
        return HTTPS_ERR_DNS;
    }
    while (!c.dns_done && !expired(deadline)) pump();
    if (!c.dns_ok) {
        close_conn(&c);
        return c.dns_done ? HTTPS_ERR_DNS : HTTPS_ERR_TIMEOUT;
    }

    // ---- conexão TCP + handshake TLS ----
    cyw43_arch_lwip_begin();
    e = altcp_connect(c.pcb, &c.ip, 443, on_connected);
    cyw43_arch_lwip_end();
    if (e != ERR_OK) {
        close_conn(&c);
        return HTTPS_ERR_CONNECT;
    }
    while (!c.connected && !c.done && !expired(deadline)) pump();
    if (!c.connected) {
        int err = c.error ? c.error : HTTPS_ERR_TIMEOUT;
        close_conn(&c);
        return err;
    }

    // ---- pedido e resposta ----
    if (!send_all(&c, request, strlen(request), deadline)) {
        int err = c.error ? c.error : (expired(deadline) ? HTTPS_ERR_TIMEOUT : HTTPS_ERR_SEND);
        close_conn(&c);
        return err;
    }
    while (!c.done && !expired(deadline)) pump();

    // Resposta completa mas o servidor ainda não fechou: dá um instante para ele
    // fechar primeiro (evita acumular conexões em TIME_WAIT no lado do Pico).
    if (c.done && !c.remote_closed && !c.error) {
        absolute_time_t grace = make_timeout_time_ms(CLOSE_GRACE_MS);
        while (!c.remote_closed && !c.error && !expired(grace)) pump();
    }
    if (c.remote_closed) http_parser_eof(&c.parser);

    bool timed_out = !c.done;
    int err = c.error;
    close_conn(&c);
    for (int i = 0; i < 4; i++) pump();  // deixa o lwIP enviar o fechamento

    int status = http_parser_status(&c.parser);
    if (http_parser_done(&c.parser)) return status;
    if (status > 0 && c.remote_closed) return status;  // veio cortado, mas veio
    if (err) return err;
    return timed_out ? HTTPS_ERR_TIMEOUT : HTTPS_ERR_NO_RESPONSE;
}

const char *https_error_text(int code) {
    switch (code) {
        case HTTPS_ERR_TLS_CONFIG: return "config TLS";
        case HTTPS_ERR_MEM: return "sem memoria";
        case HTTPS_ERR_DNS: return "falha no DNS";
        case HTTPS_ERR_CONNECT: return "falha ao conectar";
        case HTTPS_ERR_TIMEOUT: return "tempo esgotado";
        case HTTPS_ERR_PROTO: return "resposta invalida";
        case HTTPS_ERR_NO_RESPONSE: return "sem resposta";
        case HTTPS_ERR_SEND: return "falha ao enviar";
        default: return "erro";
    }
}
