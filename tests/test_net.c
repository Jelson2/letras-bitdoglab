// Testes de http_parse, stream_field, base64 e oled_gfx (lógica pura, sem a placa).
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http_parse.h"
#include "oled_gfx.h"
#include "stream_field.h"
#include "text.h"

// ---------- http_parse ----------

typedef struct {
    char data[4096];
    size_t len;
} sink_t;

static void sink_fn(const uint8_t *d, size_t n, void *ud) {
    sink_t *s = ud;
    assert(s->len + n < sizeof s->data);
    memcpy(s->data + s->len, d, n);
    s->len += n;
}

// Alimenta `resp` em pedaços de `step` bytes e confere status e corpo.
static void run_http(const char *resp, size_t step, int status, const char *body, bool done_expected) {
    http_parser_t p;
    sink_t s;
    memset(&s, 0, sizeof s);
    http_parser_init(&p, sink_fn, &s);
    size_t n = strlen(resp);
    for (size_t i = 0; i < n; i += step) {
        size_t k = (n - i < step) ? n - i : step;
        assert(http_parser_feed(&p, (const uint8_t *)resp + i, k));
    }
    http_parser_eof(&p);
    assert(http_parser_status(&p) == status);
    assert(s.len == strlen(body) && memcmp(s.data, body, s.len) == 0);
    assert(http_parser_done(&p) == done_expected);
}

static void test_http(void) {
    const char *len_resp =
        "HTTP/1.1 200 OK\r\nServer: x\r\nCONTENT-length:  11\r\nSet-Cookie: a=b\r\n\r\nhello world EXTRA";
    const char *chunk_resp =
        "HTTP/1.1 200 OK\r\ntransfer-encoding: Chunked\r\nX: y\r\n\r\n"
        "5\r\nhello\r\n6;ext=1\r\n world\r\nA\r\n0123456789\r\n0\r\nTrailer: t\r\n\r\n";
    const char *raw_resp = "HTTP/1.0 200 OK\r\nConnection: close\r\n\r\nsem tamanho conhecido";
    const char *nocontent = "HTTP/1.1 204 No Content\r\nServer: x\r\n\r\n";
    const char *notfound = "HTTP/1.1 404 Not Found\r\nContent-Length: 15\r\n\r\n{\"code\":\"nope\"}";
    const char *cont = "HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";

    for (size_t step = 1; step <= 64; step++) {  // todos os fatiamentos, inclusive byte a byte
        run_http(len_resp, step, 200, "hello world", true);
        run_http(chunk_resp, step, 200, "hello world0123456789", true);
        run_http(raw_resp, step, 200, "sem tamanho conhecido", true);
        run_http(nocontent, step, 204, "", true);
        run_http(notfound, step, 404, "{\"code\":\"nope\"}", true);
        run_http(cont, step, 200, "ok", true);
    }

    // conexão fecha antes do fim do Content-Length: não "termina", mas entregou o que veio
    run_http("HTTP/1.1 200 OK\r\nContent-Length: 50\r\n\r\ncortado", 7, 200, "cortado", false);

    // respostas inválidas
    http_parser_t p;
    http_parser_init(&p, NULL, NULL);
    assert(!http_parser_feed(&p, (const uint8_t *)"GARBAGE\r\n", 9));
    http_parser_init(&p, NULL, NULL);
    const char *ruim = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nZZ\r\n";
    assert(!http_parser_feed(&p, (const uint8_t *)ruim, strlen(ruim)));

    // cabeçalho gigante não estoura o buffer interno
    char big[1200];
    strcpy(big, "HTTP/1.1 200 OK\r\nX-Big: ");
    size_t l = strlen(big);
    memset(big + l, 'a', 1000);
    strcpy(big + l + 1000, "\r\nContent-Length: 2\r\n\r\nhi");
    run_http(big, 100, 200, "hi", true);
}

// ---------- stream_field ----------

static void run_field(const char *json, size_t step, const char *key, bool found, const char *value) {
    stream_field_t s;
    char out[256];
    sf_init(&s, key, out, sizeof out);
    size_t n = strlen(json);
    for (size_t i = 0; i < n; i += step) {
        size_t k = (n - i < step) ? n - i : step;
        sf_feed(&s, (const uint8_t *)json + i, k);
    }
    assert(s.found == found);
    if (found) assert(strcmp(out, value) == 0);
}

static void test_stream_field(void) {
    const char *j1 =
        "{\"id\":1,\"plainLyrics\":\"diz \\\"syncedLyrics\\\":\\\"falso\\\" aqui\","
        "\"syncedLyrics\":\"[00:01.00] Ol\\u00e1\\n[00:02.00] \\\"Oi\\\" \\\\ \\ud83d\\ude00 \\/fim\",\"outro\":2}";
    const char *want1 = "[00:01.00] Ol\xc3\xa1\n[00:02.00] \"Oi\" \\ \xf0\x9f\x98\x80 /fim";
    const char *j2 = "{\"syncedLyrics\": null, \"plainLyrics\": \"x\"}";
    const char *j3 = "[{\"syncedLyrics\":null},{\"syncedLyrics\" : \"segunda\"},{\"syncedLyrics\":\"terceira\"}]";
    const char *j4 = "{\"syncedLyrics\":\"cortado no mei";
    const char *j6 = "{\"a\":\"syncedLyrics\",\"b\":{\"syncedLyrics\"\n:\n\"achou\"}}";  // valor com o nome da chave + espaços

    for (size_t step = 1; step <= 40; step++) {
        run_field(j1, step, "syncedLyrics", true, want1);
        run_field(j2, step, "syncedLyrics", false, "");
        run_field(j3, step, "syncedLyrics", true, "segunda");  // pula o null, para no primeiro texto
        run_field(j4, step, "syncedLyrics", false, "");       // string que nunca fecha
        run_field("{}", step, "syncedLyrics", false, "");
        run_field(j6, step, "syncedLyrics", true, "achou");
    }

    // buffer pequeno: trunca sem estourar e ainda termina em NUL
    stream_field_t s;
    char peq[8];
    const char *j5 = "{\"syncedLyrics\":\"0123456789abcdef\"}";
    sf_init(&s, "syncedLyrics", peq, sizeof peq);
    sf_feed(&s, (const uint8_t *)j5, strlen(j5));
    assert(s.found && s.truncated && strlen(peq) < sizeof peq);
    assert(sf_done(&s));
}

// ---------- base64 ----------

static void test_base64(void) {
    char out[64];
    assert(text_base64((const uint8_t *)"", 0, out, sizeof out) == 0 && out[0] == '\0');
    assert(text_base64((const uint8_t *)"f", 1, out, sizeof out) == 4 && !strcmp(out, "Zg=="));
    assert(text_base64((const uint8_t *)"fo", 2, out, sizeof out) == 4 && !strcmp(out, "Zm8="));
    assert(text_base64((const uint8_t *)"foo", 3, out, sizeof out) == 4 && !strcmp(out, "Zm9v"));
    assert(text_base64((const uint8_t *)"foobar", 6, out, sizeof out) == 8 && !strcmp(out, "Zm9vYmFy"));
    assert(text_base64((const uint8_t *)"id:secret", 9, out, sizeof out) == 12 && !strcmp(out, "aWQ6c2VjcmV0"));
    assert(text_base64((const uint8_t *)"foobar", 6, out, 8) == 0);  // não cabe
}

// ---------- oled_gfx ----------

static int px(const oled_fb_t *fb, int x, int y) { return (fb->buf[(y >> 3) * OLED_W + x] >> (y & 7)) & 1; }

static void test_gfx(void) {
    static oled_fb_t fb;
    gfx_clear(&fb);
    gfx_pixel(&fb, 3, 9, true);  // página 1, bit 1
    assert(fb.buf[1 * OLED_W + 3] == 0x02);
    gfx_pixel(&fb, 3, 9, false);
    assert(fb.buf[1 * OLED_W + 3] == 0);
    gfx_pixel(&fb, -1, 0, true);  // fora da tela: ignora
    gfx_pixel(&fb, 128, 0, true);
    gfx_pixel(&fb, 0, 64, true);
    for (size_t i = 0; i < sizeof fb.buf; i++) assert(fb.buf[i] == 0);

    // 'A' em escala 1 deve bater com a fonte: bit 0 de cada linha = pixel mais à esquerda
    gfx_char(&fb, 0, 0, 'A', 1);
    static const unsigned char A[8] = {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00};
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) assert(px(&fb, c, r) == ((A[r] >> c) & 1));

    // escala 2: cada pixel vira 2x2 e o texto avança 16 px por caractere
    gfx_clear(&fb);
    gfx_text(&fb, 10, 20, "AB", 2);
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            for (int sy = 0; sy < 2; sy++)
                for (int sx = 0; sx < 2; sx++) assert(px(&fb, 10 + c * 2 + sx, 20 + r * 2 + sy) == ((A[r] >> c) & 1));
    assert(gfx_text_width(5, 2) == 80);

    // desenhar além da borda não corrompe memória (rodado com sanitizer)
    gfx_text(&fb, 120, 60, "ABCDEFG", 3);
    gfx_text(&fb, -20, -5, "xyz", 2);
}

int main(void) {
    test_http();
    test_stream_field();
    test_base64();
    test_gfx();
    puts("Todos os testes de rede/grafico passaram.");
    return 0;
}
