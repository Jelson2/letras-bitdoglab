#include "json_scan.h"

#include <stdint.h>
#include <string.h>

typedef struct {
    const char *p;
    const char *end;
    char path[JSON_PATH_MAX];
    json_leaf_cb cb;
    void *ud;
    bool stop;
    bool err;
} scan_t;

static void skip_ws(scan_t *s) {
    while (s->p < s->end && (*s->p == ' ' || *s->p == '\n' || *s->p == '\r' || *s->p == '\t')) s->p++;
}

// s->p aponta para a aspa de abertura. Deixa s->p logo após a aspa de fechamento.
static bool read_string(scan_t *s, const char **start, size_t *len) {
    if (s->p >= s->end || *s->p != '"') return false;
    const char *begin = s->p + 1;
    const char *q = begin;
    while (q < s->end) {
        if (*q == '\\') {
            q += 2;
            continue;
        }
        if (*q == '"') {
            *start = begin;
            *len = (size_t)(q - begin);
            s->p = q + 1;
            return true;
        }
        q++;
    }
    s->p = s->end;
    return false;
}

static size_t path_push(scan_t *s, size_t base, const char *key, size_t klen) {
    size_t n = base;
    if (n > 0 && n < JSON_PATH_MAX - 1) s->path[n++] = '.';
    for (size_t i = 0; i < klen && n < JSON_PATH_MAX - 1; i++) s->path[n++] = key[i];
    s->path[n] = '\0';
    return n;
}

static void u32_to_text(unsigned v, char *out) {
    char tmp[12];
    int i = 0;
    do {
        tmp[i++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (i) *out++ = tmp[--i];
    *out = '\0';
}

static void parse_value(scan_t *s, size_t base, int depth) {
    if (s->stop || s->err) return;
    skip_ws(s);
    if (s->p >= s->end) {
        s->err = true;
        return;
    }
    char c = *s->p;

    if (c == '{') {
        if (depth >= JSON_MAX_DEPTH) { s->err = true; return; }
        s->p++;
        skip_ws(s);
        if (s->p < s->end && *s->p == '}') { s->p++; return; }
        for (;;) {
            skip_ws(s);
            const char *key;
            size_t klen;
            if (!read_string(s, &key, &klen)) { s->err = true; return; }
            skip_ws(s);
            if (s->p >= s->end || *s->p != ':') { s->err = true; return; }
            s->p++;
            size_t n = path_push(s, base, key, klen);
            parse_value(s, n, depth + 1);
            s->path[base] = '\0';
            if (s->stop || s->err) return;
            skip_ws(s);
            if (s->p >= s->end) { s->err = true; return; }
            if (*s->p == ',') { s->p++; continue; }
            if (*s->p == '}') { s->p++; return; }
            s->err = true;
            return;
        }
    }

    if (c == '[') {
        if (depth >= JSON_MAX_DEPTH) { s->err = true; return; }
        s->p++;
        skip_ws(s);
        if (s->p < s->end && *s->p == ']') { s->p++; return; }
        for (unsigned idx = 0;; idx++) {
            char num[12];
            u32_to_text(idx, num);
            size_t n = path_push(s, base, num, strlen(num));
            parse_value(s, n, depth + 1);
            s->path[base] = '\0';
            if (s->stop || s->err) return;
            skip_ws(s);
            if (s->p >= s->end) { s->err = true; return; }
            if (*s->p == ',') { s->p++; continue; }
            if (*s->p == ']') { s->p++; return; }
            s->err = true;
            return;
        }
    }

    if (c == '"') {
        const char *val;
        size_t vlen;
        if (!read_string(s, &val, &vlen)) { s->err = true; return; }
        if (!s->cb(s->path, val, vlen, true, s->ud)) s->stop = true;
        return;
    }

    // número, true, false, null
    const char *val = s->p;
    while (s->p < s->end && *s->p != ',' && *s->p != '}' && *s->p != ']' && *s->p != ' ' &&
           *s->p != '\n' && *s->p != '\r' && *s->p != '\t') {
        s->p++;
    }
    if (s->p == val || s->p >= s->end) {  // vazio ou encostou no fim (pode estar cortado)
        s->err = true;
        return;
    }
    if (!s->cb(s->path, val, (size_t)(s->p - val), false, s->ud)) s->stop = true;
}

bool json_scan(const char *json, size_t len, json_leaf_cb cb, void *ud) {
    scan_t s;
    s.p = json;
    s.end = json + len;
    s.path[0] = '\0';
    s.cb = cb;
    s.ud = ud;
    s.stop = false;
    s.err = false;
    parse_value(&s, 0, 0);
    return s.stop || !s.err;
}

static bool hex4(const char *s, size_t avail, uint32_t *out) {
    if (avail < 4) return false;
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
        else return false;
    }
    *out = v;
    return true;
}

size_t json_utf8_encode(uint32_t cp, char *out) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

size_t json_unescape(const char *src, size_t len, char *dst, size_t cap) {
    size_t n = 0, i = 0;
    if (cap == 0) return 0;
    while (i < len && n + 4 < cap) {
        char c = src[i++];
        if (c != '\\') {
            dst[n++] = c;
            continue;
        }
        if (i >= len) break;
        char e = src[i++];
        switch (e) {
            case 'n': dst[n++] = '\n'; break;
            case 't': dst[n++] = '\t'; break;
            case 'r': dst[n++] = '\r'; break;
            case 'b': dst[n++] = '\b'; break;
            case 'f': dst[n++] = '\f'; break;
            case 'u': {
                uint32_t cp;
                if (!hex4(src + i, len - i, &cp)) break;
                i += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= len && src[i] == '\\' && src[i + 1] == 'u') {
                    uint32_t lo;
                    if (hex4(src + i + 2, len - i - 2, &lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        i += 6;
                    }
                }
                if (cp >= 0xD800 && cp <= 0xDFFF) break;  // surrogate solto: descarta
                n += json_utf8_encode(cp, dst + n);
                break;
            }
            default: dst[n++] = e; break;  // \" \\ \/ e desconhecidos
        }
    }
    dst[n] = '\0';
    return n;
}
