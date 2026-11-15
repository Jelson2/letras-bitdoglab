#include "spotify.h"

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "https_client.h"
#include "json_scan.h"
#include "pico/stdlib.h"
#include "secrets.h"
#include "text.h"

static char g_body[BODY_BUF_SIZE];
static size_t g_body_len;

static char g_token[640];
static bool g_have_token;
static uint32_t g_token_expire_ms;

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

static void body_sink(const uint8_t *d, size_t n, void *ud) {
    (void)ud;
    size_t room = sizeof g_body - 1 - g_body_len;
    if (n > room) n = room;  // resposta maior que o buffer: o JSON fica cortado, e tudo bem
    memcpy(g_body + g_body_len, d, n);
    g_body_len += n;
    g_body[g_body_len] = '\0';
}

static uint32_t parse_u32(const char *v, size_t n) {
    uint32_t r = 0;
    for (size_t i = 0; i < n && v[i] >= '0' && v[i] <= '9'; i++) r = r * 10 + (uint32_t)(v[i] - '0');
    return r;
}

const char *spotify_last_body(void) { return g_body; }

// ---------- token ----------

typedef struct {
    bool got;
    uint32_t expires_in;
} token_ctx_t;

static bool token_cb(const char *path, const char *val, size_t len, bool is_str, void *ud) {
    token_ctx_t *t = ud;
    if (is_str && !strcmp(path, "access_token")) {
        json_unescape(val, len, g_token, sizeof g_token);
        t->got = g_token[0] != '\0';
    } else if (!is_str && !strcmp(path, "expires_in")) {
        t->expires_in = parse_u32(val, len);
    }
    return true;
}

int spotify_refresh_token(void) {
    static char req[1536];

    // Authorization: Basic base64(client_id:client_secret)
    char cred[160], auth[240];
    int cl = snprintf(cred, sizeof cred, "%s:%s", SPOTIFY_CLIENT_ID, SPOTIFY_CLIENT_SECRET);
    if (cl <= 0 || (size_t)cl >= sizeof cred || !text_base64((const uint8_t *)cred, (size_t)cl, auth, sizeof auth)) {
        return HTTPS_ERR_MEM;
    }

    char rt[420], form[480];
    text_url_encode(SPOTIFY_REFRESH_TOKEN, rt, sizeof rt);
    int fl = snprintf(form, sizeof form, "grant_type=refresh_token&refresh_token=%s", rt);
    if (fl <= 0 || (size_t)fl >= sizeof form) return HTTPS_ERR_MEM;

    int rl = snprintf(req, sizeof req,
                      "POST /api/token HTTP/1.1\r\n"
                      "Host: accounts.spotify.com\r\n"
                      "Authorization: Basic %s\r\n"
                      "Content-Type: application/x-www-form-urlencoded\r\n"
                      "Content-Length: %d\r\n"
                      "User-Agent: BitDogLab-Letras/1.0\r\n"
                      "Connection: close\r\n\r\n%s",
                      auth, fl, form);
    if (rl <= 0 || (size_t)rl >= sizeof req) return HTTPS_ERR_MEM;

    g_have_token = false;
    g_body_len = 0;
    g_body[0] = '\0';
    int status = https_request("accounts.spotify.com", req, HTTPS_TIMEOUT_MS, body_sink, NULL);
    if (status != 200) return status;

    token_ctx_t ctx = {false, 3600};
    json_scan(g_body, g_body_len, token_cb, &ctx);
    if (!ctx.got) return HTTPS_ERR_PROTO;

    uint32_t validity = ctx.expires_in > 180 ? ctx.expires_in - 120 : ctx.expires_in / 2;
    g_token_expire_ms = now_ms() + validity * 1000u;
    g_have_token = true;
    return 200;
}

bool spotify_token_ok(void) { return g_have_token && (int32_t)(now_ms() - g_token_expire_ms) < 0; }

void spotify_invalidate_token(void) { g_have_token = false; }

// ---------- player ----------

static bool player_cb(const char *path, const char *val, size_t len, bool is_str, void *ud) {
    track_info_t *t = ud;
    if (is_str) {
        if (!strcmp(path, "item.id")) json_unescape(val, len, t->id, sizeof t->id);
        else if (!strcmp(path, "item.name")) json_unescape(val, len, t->name, sizeof t->name);
        else if (!strcmp(path, "item.artists.0.name")) json_unescape(val, len, t->artist, sizeof t->artist);
        else if (!strcmp(path, "item.album.name")) json_unescape(val, len, t->album, sizeof t->album);
    } else {
        if (!strcmp(path, "is_playing")) t->playing = (len == 4 && !memcmp(val, "true", 4));
        else if (!strcmp(path, "progress_ms")) t->progress_ms = parse_u32(val, len);
        else if (!strcmp(path, "item.duration_ms")) t->duration_ms = parse_u32(val, len);
    }
    return true;
}

int spotify_get_player(track_info_t *out) {
    static char req[1024];
    memset(out, 0, sizeof *out);

    int rl = snprintf(req, sizeof req,
                      "GET /v1/me/player HTTP/1.1\r\n"
                      "Host: api.spotify.com\r\n"
                      "Authorization: Bearer %s\r\n"
                      "Accept: application/json\r\n"
                      "User-Agent: BitDogLab-Letras/1.0\r\n"
                      "Connection: close\r\n\r\n",
                      g_token);
    if (rl <= 0 || (size_t)rl >= sizeof req) return HTTPS_ERR_MEM;

    g_body_len = 0;
    g_body[0] = '\0';
    uint32_t t0 = now_ms();
    int status = https_request("api.spotify.com", req, HTTPS_TIMEOUT_MS, body_sink, NULL);
    uint32_t t1 = now_ms();
    if (status != 200) return status;

    json_scan(g_body, g_body_len, player_cb, out);  // se vier cortado, o que já foi lido vale
    out->valid = out->id[0] != '\0';
    if (!out->name[0]) strcpy(out->name, "Desconhecido");

    // O progresso foi medido pelo servidor em algum ponto entre t0 e t1; assumir o
    // meio do intervalo e referir tudo ao instante t1 (fim da resposta).
    out->progress_ms += (t1 - t0) / 2;
    out->sampled_at_ms = t1;
    return 200;
}

