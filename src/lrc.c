#include "lrc.h"

#include <string.h>

static int is_digit(char c) { return c >= '0' && c <= '9'; }

// Conteúdo entre colchetes ("mm:ss.xx") -> milissegundos.
static int parse_tag(const char *s, const char *end, uint32_t *ms) {
    const char *p = s;
    uint32_t minutes = 0, seconds = 0, frac = 0;
    int digits = 0;

    while (p < end && is_digit(*p)) {
        minutes = minutes * 10 + (uint32_t)(*p - '0');
        p++;
        if (++digits > 4) return 0;
    }
    if (digits == 0 || p >= end || *p != ':') return 0;
    p++;

    digits = 0;
    while (p < end && is_digit(*p)) {
        seconds = seconds * 10 + (uint32_t)(*p - '0');
        p++;
        if (++digits > 3) return 0;
    }
    if (digits == 0) return 0;

    if (p < end && (*p == '.' || *p == ':')) {
        p++;
        int fd = 0;
        uint32_t mult = 100;
        while (p < end && is_digit(*p)) {
            if (fd < 3) {
                frac += (uint32_t)(*p - '0') * mult;
                mult /= 10;
            }
            fd++;
            p++;
        }
        if (fd == 0) return 0;
    }
    if (p != end) return 0;

    *ms = (minutes * 60 + seconds) * 1000 + frac;
    return 1;
}

size_t lrc_parse(char *buf, lrc_line_t *out, size_t max) {
    size_t n = 0;
    char *line = buf;

    while (line && n < max) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        char *p = line;
        while (*p == ' ' || *p == '\t' || *p == '\r') p++;

        uint32_t times[8];
        int nt = 0;
        while (*p == '[') {
            char *close = strchr(p, ']');
            if (!close) break;
            uint32_t ms;
            if (!parse_tag(p + 1, close, &ms)) break;  // metadado ou tag inválida
            if (nt < 8) times[nt++] = ms;
            p = close + 1;
        }

        if (nt > 0) {
            while (*p == ' ' || *p == '\t') p++;
            char *e = p + strlen(p);
            while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = '\0';
            for (int i = 0; i < nt && n < max; i++) {
                out[n].ms = times[i];
                out[n].text = p;
                n++;
            }
        }

        line = nl ? nl + 1 : NULL;
    }

    // ordenação por inserção (estável; a entrada já costuma vir ordenada)
    for (size_t i = 1; i < n; i++) {
        lrc_line_t key = out[i];
        size_t j = i;
        while (j > 0 && out[j - 1].ms > key.ms) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = key;
    }
    return n;
}

int lrc_find(const lrc_line_t *lines, size_t n, int64_t progress_ms) {
    int lo = 0, hi = (int)n - 1, found = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if ((int64_t)lines[mid].ms <= progress_ms) {
            found = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return found;
}
