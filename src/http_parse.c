#include "http_parse.h"

#include <string.h>

enum { S_HEAD, S_BODY_RAW, S_BODY_LEN, S_CH_SIZE, S_CH_DATA, S_CH_CRLF, S_TRAILER, S_DONE, S_ERROR };

static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

// `prefix` precisa estar em minúsculas.
static bool starts_with_ci(const char *s, const char *prefix) {
    while (*prefix) {
        if (lower((unsigned char)*s) != *prefix) return false;
        s++;
        prefix++;
    }
    return true;
}

static bool contains_ci(const char *s, const char *word) {
    for (; *s; s++) {
        if (starts_with_ci(s, word)) return true;
    }
    return false;
}

void http_parser_init(http_parser_t *p, http_body_fn on_body, void *ud) {
    memset(p, 0, sizeof *p);
    p->state = S_HEAD;
    p->first_line = true;
    p->on_body = on_body;
    p->ud = ud;
}

static void emit(http_parser_t *p, const uint8_t *d, size_t n) {
    if (n && p->on_body) p->on_body(d, n, p->ud);
}

static void headers_finished(http_parser_t *p) {
    if (p->status >= 100 && p->status < 200 && p->status != 101) {
        p->first_line = true;  // "100 Continue": a resposta de verdade vem a seguir
        p->has_len = false;
        p->chunked = false;
        return;
    }
    if (p->status == 204 || p->status == 304) {
        p->state = S_DONE;
    } else if (p->chunked) {
        p->state = S_CH_SIZE;
        p->chunk_size = 0;
        p->digits = 0;
        p->in_ext = false;
    } else if (p->has_len) {
        p->state = p->remaining ? S_BODY_LEN : S_DONE;
    } else {
        p->state = S_BODY_RAW;
    }
}

static void handle_line(http_parser_t *p) {
    p->line[p->line_len] = '\0';

    if (p->first_line) {
        p->first_line = false;
        if (!starts_with_ci(p->line, "http/")) {
            p->state = S_ERROR;
            return;
        }
        const char *sp = strchr(p->line, ' ');
        if (!sp) {
            p->state = S_ERROR;
            return;
        }
        sp++;
        int v = 0, d = 0;
        while (*sp >= '0' && *sp <= '9' && d < 3) {
            v = v * 10 + (*sp - '0');
            sp++;
            d++;
        }
        if (d != 3) {
            p->state = S_ERROR;
            return;
        }
        p->status = v;
        return;
    }

    if (p->line_len == 0) {
        headers_finished(p);
    } else if (starts_with_ci(p->line, "content-length:")) {
        const char *s = p->line + 15;
        while (*s == ' ' || *s == '\t') s++;
        uint32_t v = 0;
        int d = 0;
        while (*s >= '0' && *s <= '9' && d < 10) {
            v = v * 10 + (uint32_t)(*s - '0');
            s++;
            d++;
        }
        if (d > 0) {
            p->has_len = true;
            p->remaining = v;
        }
    } else if (starts_with_ci(p->line, "transfer-encoding:")) {
        if (contains_ci(p->line + 18, "chunked")) p->chunked = true;
    }
}

bool http_parser_feed(http_parser_t *p, const uint8_t *d, size_t n) {
    size_t i = 0;
    while (i < n) {
        switch (p->state) {
            case S_HEAD: {
                uint8_t c = d[i++];
                if (c == '\n') {
                    handle_line(p);
                    p->line_len = 0;
                } else if (c != '\r' && p->line_len < sizeof p->line - 1) {
                    p->line[p->line_len++] = (char)c;
                }
                break;
            }
            case S_BODY_RAW:
                emit(p, d + i, n - i);
                i = n;
                break;
            case S_BODY_LEN: {
                size_t take = n - i;
                if (take > p->remaining) take = p->remaining;
                emit(p, d + i, take);
                i += take;
                p->remaining -= (uint32_t)take;
                if (p->remaining == 0) p->state = S_DONE;
                break;
            }
            case S_CH_SIZE: {
                char c = (char)d[i++];
                if (c == '\n') {
                    if (p->digits == 0) {
                        p->state = S_ERROR;
                    } else if (p->chunk_size == 0) {
                        p->state = S_TRAILER;
                        p->line_len = 0;
                    } else {
                        p->remaining = p->chunk_size;
                        p->state = S_CH_DATA;
                    }
                } else if (c == '\r' || c == ' ') {
                    // ignora
                } else if (p->in_ext) {
                    // extensão do chunk: ignora até o fim da linha
                } else if (c == ';') {
                    p->in_ext = true;
                } else {
                    int v;
                    if (c >= '0' && c <= '9') v = c - '0';
                    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
                    else {
                        p->state = S_ERROR;
                        break;
                    }
                    if (p->chunk_size > 0x7FFFFFF) {
                        p->state = S_ERROR;
                        break;
                    }
                    p->chunk_size = p->chunk_size * 16 + (uint32_t)v;
                    if (p->digits < 255) p->digits++;
                }
                break;
            }
            case S_CH_DATA: {
                size_t take = n - i;
                if (take > p->remaining) take = p->remaining;
                emit(p, d + i, take);
                i += take;
                p->remaining -= (uint32_t)take;
                if (p->remaining == 0) p->state = S_CH_CRLF;
                break;
            }
            case S_CH_CRLF: {
                char c = (char)d[i++];
                if (c == '\n') {
                    p->state = S_CH_SIZE;
                    p->chunk_size = 0;
                    p->digits = 0;
                    p->in_ext = false;
                } else if (c != '\r') {
                    p->state = S_ERROR;
                }
                break;
            }
            case S_TRAILER: {
                char c = (char)d[i++];
                if (c == '\n') {
                    if (p->line_len == 0) p->state = S_DONE;
                    else p->line_len = 0;
                } else if (c != '\r' && p->line_len < 250) {
                    p->line_len++;
                }
                break;
            }
            case S_DONE:
                i = n;  // sobras depois do fim: ignora
                break;
            default:
                return false;
        }
    }
    return p->state != S_ERROR;
}

bool http_parser_done(const http_parser_t *p) { return p->state == S_DONE; }

void http_parser_eof(http_parser_t *p) {
    if (p->state == S_BODY_RAW) p->state = S_DONE;
}

int http_parser_status(const http_parser_t *p) { return p->status; }
