#include "stream_field.h"

#include <string.h>

#include "json_scan.h"  // json_utf8_encode

enum { ST_SEARCH, ST_COLON, ST_AFTER_KEY, ST_STR, ST_ESC, ST_UNI, ST_DONE };

static bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

void sf_init(stream_field_t *s, const char *key, char *out, size_t cap) {
    memset(s, 0, sizeof *s);
    // padrão procurado: "chave" (os dois-pontos e os espaços são conferidos depois)
    size_t n = 0;
    s->pat[n++] = '"';
    for (const char *k = key; *k && n < sizeof s->pat - 2; k++) s->pat[n++] = *k;
    s->pat[n++] = '"';
    s->pat_len = (uint8_t)n;

    // tabela de falha do KMP (o padrão repete aspas, então não dá para só reiniciar)
    s->fail[0] = 0;
    for (size_t i = 1, k = 0; i < n; i++) {
        while (k > 0 && s->pat[i] != s->pat[k]) k = s->fail[k - 1];
        if (s->pat[i] == s->pat[k]) k++;
        s->fail[i] = (uint8_t)k;
    }

    s->state = ST_SEARCH;
    s->out = out;
    s->cap = cap;
    if (cap) out[0] = '\0';
}

bool sf_done(const stream_field_t *s) { return s->state == ST_DONE; }

static void finish(stream_field_t *s) {
    s->out[s->len] = '\0';
    s->found = true;
    s->state = ST_DONE;
}

// Acrescenta bytes ao destino; se não couber, encerra marcando truncamento.
static bool put(stream_field_t *s, const char *b, size_t n) {
    if (s->len + n + 1 > s->cap) {
        s->truncated = true;
        finish(s);
        return false;
    }
    memcpy(s->out + s->len, b, n);
    s->len += n;
    s->out[s->len] = '\0';
    return true;
}

static void put_cp(stream_field_t *s, uint32_t cp) {
    char b[4];
    size_t n = json_utf8_encode(cp, b);
    put(s, b, n);
}

static void uni_done(stream_field_t *s) {
    uint32_t cp = s->uni_val;
    if (cp >= 0xD800 && cp <= 0xDBFF) {  // metade alta: espera a baixa
        s->high = (uint16_t)cp;
        s->state = ST_STR;
        return;
    }
    if (cp >= 0xDC00 && cp <= 0xDFFF) {
        if (s->high) {
            uint32_t full = 0x10000 + (((uint32_t)s->high - 0xD800) << 10) + (cp - 0xDC00);
            s->high = 0;
            s->state = ST_STR;
            put_cp(s, full);
        } else {
            s->state = ST_STR;  // metade baixa solta: descarta
        }
        return;
    }
    s->high = 0;
    s->state = ST_STR;
    put_cp(s, cp);
}

void sf_feed(stream_field_t *s, const uint8_t *d, size_t n) {
    for (size_t i = 0; i < n && s->state != ST_DONE; i++) {
        char c = (char)d[i];
        switch (s->state) {
            case ST_SEARCH:
                while (s->matched > 0 && c != s->pat[s->matched]) s->matched = s->fail[s->matched - 1];
                if (c == s->pat[s->matched]) s->matched++;
                if (s->matched == s->pat_len) {
                    s->matched = 0;
                    s->state = ST_COLON;
                }
                break;
            case ST_COLON:
                if (is_ws(c)) break;
                if (c == ':') {
                    s->state = ST_AFTER_KEY;
                } else {
                    s->state = ST_SEARCH;  // era um valor igual ao nome da chave: segue procurando
                    i--;                   // reprocessa este caractere na busca
                }
                break;
            case ST_AFTER_KEY:
                if (is_ws(c)) break;
                if (c == '"') {
                    s->state = ST_STR;
                    s->len = 0;
                    s->high = 0;
                } else {
                    s->state = ST_SEARCH;  // null, número, objeto...: não serve, continua procurando
                    i--;
                }
                break;
            case ST_STR:
                if (c == '"') {
                    finish(s);
                } else if (c == '\\') {
                    s->state = ST_ESC;
                } else {
                    s->high = 0;
                    put(s, &c, 1);
                }
                break;
            case ST_ESC: {
                char r = 0;
                switch (c) {
                    case 'n': r = '\n'; break;
                    case 't': r = '\t'; break;
                    case 'r': r = '\r'; break;
                    case 'b': r = '\b'; break;
                    case 'f': r = '\f'; break;
                    case 'u':
                        s->state = ST_UNI;
                        s->uni_digits = 0;
                        s->uni_val = 0;
                        continue;
                    default: r = c; break;  // \" \\ \/
                }
                s->high = 0;
                s->state = ST_STR;
                put(s, &r, 1);
                break;
            }
            case ST_UNI: {
                int v;
                if (c >= '0' && c <= '9') v = c - '0';
                else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
                else {  // escape malformado: abandona
                    s->state = ST_STR;
                    break;
                }
                s->uni_val = (uint16_t)((s->uni_val << 4) | (uint16_t)v);
                if (++s->uni_digits == 4) uni_done(s);
                break;
            }
            default:
                break;
        }
    }
}
