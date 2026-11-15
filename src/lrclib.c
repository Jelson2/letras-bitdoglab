#include "lrclib.h"

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "https_client.h"
#include "stream_field.h"
#include "text.h"

static char g_lrc[LRC_BUF_SIZE];
static lrc_line_t g_lines[MAX_LYRIC_LINES];
static size_t g_count;

const lrc_line_t *lrclib_lines(void) { return g_lines; }
size_t lrclib_count(void) { return g_count; }
void lrclib_clear(void) { g_count = 0; }

static void field_sink(const uint8_t *d, size_t n, void *ud) { sf_feed((stream_field_t *)ud, d, n); }

// Faz um GET e tenta extrair "syncedLyrics". Devolve o código HTTP/erro; em `*lines`,
// o número de linhas se a letra veio.
static int try_url(const char *path, int *lines) {
    static char req[2048];
    int rl = snprintf(req, sizeof req,
                      "GET %s HTTP/1.1\r\n"
                      "Host: lrclib.net\r\n"
                      "Accept: application/json\r\n"
                      "User-Agent: BitDogLab-Letras/1.0 (https://github.com/Jelson2)\r\n"
                      "Connection: close\r\n\r\n",
                      path);
    if (rl <= 0 || (size_t)rl >= sizeof req) return HTTPS_ERR_MEM;

    stream_field_t sf;
    sf_init(&sf, "syncedLyrics", g_lrc, sizeof g_lrc);
    int status = https_request("lrclib.net", req, HTTPS_TIMEOUT_MS, field_sink, &sf);
    *lines = 0;
    if (status == 200 && sf.found && g_lrc[0]) {
        g_count = lrc_parse(g_lrc, g_lines, MAX_LYRIC_LINES);
        *lines = (int)g_count;
        printf("LRCLIB: %d linhas%s\n", *lines, sf.truncated ? " (letra cortada: buffer cheio)" : "");
    }
    return status;
}

int lrclib_fetch(const char *track, const char *artist, const char *album, uint32_t duration_s) {
    static char path[1400];
    static char t[400], a[300], al[400];
    g_count = 0;
    text_url_encode(track, t, sizeof t);
    text_url_encode(artist, a, sizeof a);
    text_url_encode(album, al, sizeof al);

    // 1) busca exata (com álbum e duração); 2) sem álbum, que no Spotify nem sempre
    // bate com o do LRCLIB; 3) busca por texto, usando o primeiro resultado com letra.
    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt == 0) {
            if (!album[0]) continue;
            snprintf(path, sizeof path, "/api/get?track_name=%s&artist_name=%s&album_name=%s&duration=%u", t, a, al,
                     (unsigned)duration_s);
        } else if (attempt == 1) {
            snprintf(path, sizeof path, "/api/get?track_name=%s&artist_name=%s&duration=%u", t, a,
                     (unsigned)duration_s);
        } else {
            snprintf(path, sizeof path, "/api/search?track_name=%s&artist_name=%s", t, a);
        }

        int lines = 0;
        int status = try_url(path, &lines);
        printf("LRCLIB tentativa %d: HTTP %d\n", attempt + 1, status);
        if (lines > 0) return lines;
        if (status < 0) return status;                        // erro de rede: tentar depois
        if (status == 429 || status >= 500) return -status;   // servidor ocupado: tentar depois
        // 200 sem letra sincronizada ou 404: passa para a próxima forma de busca
    }
    return 0;
}
