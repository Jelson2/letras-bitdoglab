#include "text.h"

#include <stdint.h>
#include <string.h>

// U+00C0..U+00FF -> equivalente ASCII
static const char *const LATIN1[64] = {
    /* C0 */ "A", "A", "A", "A", "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I",
    /* D0 */ "D", "N", "O", "O", "O", "O", "O", "x", "O", "U", "U", "U", "U", "Y", "TH", "ss",
    /* E0 */ "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
    /* F0 */ "d", "n", "o", "o", "o", "o", "o", "/", "o", "u", "u", "u", "u", "y", "th", "y",
};

static const char *symbol_replacement(uint32_t cp) {
    switch (cp) {
        case 0x2018: case 0x2019: case 0x201A: case 0x00B4: case 0x0060: return "'";
        case 0x201C: case 0x201D: case 0x201E: return "\"";
        case 0x2013: case 0x2014: case 0x2212: return "-";
        case 0x2026: return "...";
        case 0x00BF: return "?";
        case 0x00A1: return "!";
        case 0x00A0: case '\t': case '\n': case '\r': return " ";
        default: return NULL;
    }
}

size_t text_to_ascii(const char *utf8, char *out, size_t cap) {
    size_t n = 0;
    if (cap == 0) return 0;
    const unsigned char *p = (const unsigned char *)utf8;

    while (*p && n + 4 < cap) {
        unsigned char c = *p;
        uint32_t cp;
        int extra;
        if (c < 0x80) { cp = c; extra = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else { p++; continue; }
        p++;
        for (int i = 0; i < extra; i++) {
            if ((*p & 0xC0) != 0x80) { cp = 0xFFFD; break; }
            cp = (cp << 6) | (*p & 0x3F);
            p++;
        }

        char single[2] = {0, 0};
        const char *rep = NULL;
        if (cp >= 32 && cp < 127 && cp != '`') {
            single[0] = (char)cp;
            rep = single;
        } else if (cp >= 0xC0 && cp <= 0xFF) {
            rep = LATIN1[cp - 0xC0];
        } else {
            rep = symbol_replacement(cp);
        }
        if (rep) {
            while (*rep) out[n++] = *rep++;
        }
    }
    out[n] = '\0';
    return n;
}

int text_wrap(const char *s, int cols, char out[][WRAP_MAX_COLS + 1], int max_lines, bool *truncated) {
    if (cols > WRAP_MAX_COLS) cols = WRAP_MAX_COLS;
    if (cols < 1) cols = 1;

    int n = 0;
    int cur = 0;
    bool trunc = false;
    char line[WRAP_MAX_COLS + 1];
    line[0] = '\0';

#define FLUSH_LINE()                                  \
    do {                                              \
        if (n < max_lines) {                          \
            memcpy(out[n], line, (size_t)cur + 1);    \
            n++;                                      \
        } else {                                      \
            trunc = true;                             \
        }                                             \
        cur = 0;                                      \
        line[0] = '\0';                               \
    } while (0)

    const char *p = s;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        const char *w = p;
        while (*p && *p != ' ') p++;
        int wl = (int)(p - w);

        while (wl > cols) {  // palavra maior que a linha: corta
            if (cur > 0) FLUSH_LINE();
            memcpy(line, w, (size_t)cols);
            cur = cols;
            line[cur] = '\0';
            FLUSH_LINE();
            w += cols;
            wl -= cols;
        }

        if (cur == 0) {
            memcpy(line, w, (size_t)wl);
            cur = wl;
            line[cur] = '\0';
        } else if (cur + 1 + wl <= cols) {
            line[cur++] = ' ';
            memcpy(line + cur, w, (size_t)wl);
            cur += wl;
            line[cur] = '\0';
        } else {
            FLUSH_LINE();
            memcpy(line, w, (size_t)wl);
            cur = wl;
            line[cur] = '\0';
        }
    }
    if (cur > 0) FLUSH_LINE();
#undef FLUSH_LINE

    if (truncated) *truncated = trunc;
    return n;
}

size_t text_url_encode(const char *s, char *out, size_t cap) {
    static const char HEX[] = "0123456789ABCDEF";
    size_t n = 0;
    if (cap == 0) return 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char c = *p;
        bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                    c == '-' || c == '_' || c == '.' || c == '~';
        if (safe) {
            if (n + 1 >= cap) break;
            out[n++] = (char)c;
        } else {
            if (n + 3 >= cap) break;
            out[n++] = '%';
            out[n++] = HEX[c >> 4];
            out[n++] = HEX[c & 0x0F];
        }
    }
    out[n] = '\0';
    return n;
}

size_t text_base64(const uint8_t *in, size_t n, char *out, size_t cap) {
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t need = ((n + 2) / 3) * 4;
    if (cap < need + 1) {
        if (cap) out[0] = '\0';
        return 0;
    }
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < n) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < n) v |= in[i + 2];
        out[o++] = T[(v >> 18) & 63];
        out[o++] = T[(v >> 12) & 63];
        out[o++] = (i + 1 < n) ? T[(v >> 6) & 63] : '=';
        out[o++] = (i + 2 < n) ? T[v & 63] : '=';
    }
    out[o] = '\0';
    return o;
}
