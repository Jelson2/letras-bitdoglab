// Testes da lógica pura (json_scan, lrc, text). Rodam no computador, sem a placa:
//   sh tests/run_tests.sh
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json_scan.h"
#include "lrc.h"
#include "text.h"

// ---------- json_scan ----------

typedef struct {
    char id[64], nome[128], artista[64], album[64], device_nome[64];
    int playing;
    unsigned progress, duration;
    int leaves;
} player_t;

static unsigned parse_u(const char *v, size_t n) {
    unsigned r = 0;
    for (size_t i = 0; i < n; i++) r = r * 10 + (unsigned)(v[i] - '0');
    return r;
}

static bool player_cb(const char *path, const char *val, size_t len, bool is_str, void *ud) {
    player_t *p = ud;
    p->leaves++;
    if (is_str) {
        if (!strcmp(path, "item.id")) json_unescape(val, len, p->id, sizeof p->id);
        else if (!strcmp(path, "item.name")) json_unescape(val, len, p->nome, sizeof p->nome);
        else if (!strcmp(path, "item.artists.0.name")) json_unescape(val, len, p->artista, sizeof p->artista);
        else if (!strcmp(path, "item.album.name")) json_unescape(val, len, p->album, sizeof p->album);
        else if (!strcmp(path, "device.name")) json_unescape(val, len, p->device_nome, sizeof p->device_nome);
    } else {
        if (!strcmp(path, "is_playing")) p->playing = (len == 4 && !memcmp(val, "true", 4));
        else if (!strcmp(path, "progress_ms")) p->progress = parse_u(val, len);
        else if (!strcmp(path, "item.duration_ms")) p->duration = parse_u(val, len);
    }
    return true;
}

static const char *SPOTIFY_JSON =
    "{\n \"device\": {\"id\": \"DEV123\", \"name\": \"Meu Celular\", \"volume_percent\": 50},\n"
    " \"shuffle_state\": false, \"progress_ms\": 83211, \"is_playing\": true,\n"
    " \"item\": {\n"
    "  \"album\": {\"id\": \"ALB\", \"name\": \"\\u00c1lbum \\\"Especial\\\"\", \"images\": [{\"url\": \"x\", \"height\": 640}]},\n"
    "  \"artists\": [{\"id\": \"A1\", \"name\": \"Jo\\u00e3o & Maria\"}, {\"id\": \"A2\", \"name\": \"Segundo\"}],\n"
    "  \"available_markets\": [\"AD\", \"AE\", \"AR\", \"BR\"],\n"
    "  \"duration_ms\": 215000, \"id\": \"TRACK42\", \"name\": \"Can\\u00e7\\u00e3o\\nTeste\", \"popularity\": 77,\n"
    "  \"nested\": {\"a\": {\"b\": {\"c\": [1, 2, [3, 4], {\"d\": null}]}}}\n"
    " },\n \"currently_playing_type\": \"track\"\n}";

static void test_json(void) {
    player_t p;
    memset(&p, 0, sizeof p);
    assert(json_scan(SPOTIFY_JSON, strlen(SPOTIFY_JSON), player_cb, &p));
    assert(strcmp(p.id, "TRACK42") == 0);                 // não confunde com device.id
    assert(strcmp(p.device_nome, "Meu Celular") == 0);
    assert(strcmp(p.nome, "Can\xc3\xa7\xc3\xa3o\nTeste") == 0);
    assert(strcmp(p.artista, "Jo\xc3\xa3o & Maria") == 0);  // artists.0, não artists.1
    assert(strcmp(p.album, "\xc3\x81lbum \"Especial\"") == 0);
    assert(p.playing == 1 && p.progress == 83211 && p.duration == 215000);

    // JSON cortado no meio: devolve false, mas o que veio antes já foi entregue
    size_t corte = strstr(SPOTIFY_JSON, "\"duration_ms\"") - SPOTIFY_JSON + 5;
    memset(&p, 0, sizeof p);
    assert(!json_scan(SPOTIFY_JSON, corte, player_cb, &p));
    assert(p.progress == 83211 && p.duration == 0 && p.id[0] == '\0');

    // vazio, lixo e profundidade excessiva não derrubam o parser
    assert(!json_scan("", 0, player_cb, &p));
    assert(!json_scan("{\"a\": }", 7, player_cb, &p));
    char fundo[200];
    memset(fundo, '[', 100);
    fundo[100] = '\0';
    assert(!json_scan(fundo, 100, player_cb, &p));
}

static const char *cap_ptr;
static size_t cap_len;
static bool cap_cb(const char *path, const char *val, size_t len, bool is_str, void *ud) {
    (void)ud;
    if (is_str && !strcmp(path, "syncedLyrics")) {
        cap_ptr = val;
        cap_len = len;
        return false;  // para a leitura
    }
    return true;
}

static void test_unescape_e_parada(void) {
    const char *j = "{\"plainLyrics\":\"x\",\"syncedLyrics\":\"[00:01.00] Oi\\n[00:02.00] Ol\\u00e1 \\ud83d\\ude00\",\"outro\":1";
    cap_ptr = NULL;
    assert(json_scan(j, strlen(j), cap_cb, NULL));  // parou antes do fim (JSON cortado)
    assert(cap_ptr != NULL);
    char out[128];
    size_t n = json_unescape(cap_ptr, cap_len, out, sizeof out);
    assert(strcmp(out, "[00:01.00] Oi\n[00:02.00] Ol\xc3\xa1 \xf0\x9f\x98\x80") == 0);
    assert(n == strlen(out));

    char peq[8];  // destino pequeno: nunca estoura
    json_unescape("abcdefghijkl", 12, peq, sizeof peq);
    assert(strlen(peq) < sizeof peq);
}

// ---------- lrc ----------

static void test_lrc(void) {
    char buf[] =
        "[ar:Fulano]\n[ti:Teste]\n[length: 03:00]\n"
        "[00:12.00] Primeira linha  \r\n"
        "[00:15.5][01:00.25]Refrao\n"
        "\n"
        "[00:20.000]\n"
        "[00:05]Antes de tudo\n"
        "linha sem tempo\n"
        "[xx:yy]ruim\n"
        "[00:30.12]Ultima";
    lrc_line_t l[32];
    size_t n = lrc_parse(buf, l, 32);
    assert(n == 6);
    assert(l[0].ms == 5000 && !strcmp(l[0].text, "Antes de tudo"));
    assert(l[1].ms == 12000 && !strcmp(l[1].text, "Primeira linha"));
    assert(l[2].ms == 15500 && !strcmp(l[2].text, "Refrao"));
    assert(l[3].ms == 20000 && !strcmp(l[3].text, ""));
    assert(l[4].ms == 30120 && !strcmp(l[4].text, "Ultima"));
    assert(l[5].ms == 60250 && !strcmp(l[5].text, "Refrao"));

    assert(lrc_find(l, n, 0) == -1);
    assert(lrc_find(l, n, 5000) == 0);
    assert(lrc_find(l, n, 14999) == 1);
    assert(lrc_find(l, n, 15500) == 2);
    assert(lrc_find(l, n, 99999999) == 5);
    assert(lrc_find(l, 0, 1000) == -1);

    char buf2[] = "[00:01.00]a\n[00:02.00]b\n[00:03.00]c\n";
    assert(lrc_parse(buf2, l, 2) == 2);  // respeita o máximo
}

// ---------- text ----------

static void test_text(void) {
    char out[128];
    text_to_ascii("Cora\xc3\xa7\xc3\xa3o, n\xc3\xa3o \xc3\xa9 \xe2\x80\x9c" "aspas\xe2\x80\x9d \xe2\x80\x94 ok\xe2\x80\xa6", out, sizeof out);
    assert(strcmp(out, "Coracao, nao e \"aspas\" - ok...") == 0);
    text_to_ascii("It\xe2\x80\x99s \xe2\x99\xaa fine \xf0\x9f\x98\x80 \xc3\x87\xc3\x9f", out, sizeof out);
    assert(strcmp(out, "It's  fine  Css") == 0);
    text_to_ascii("a\nb\tc", out, sizeof out);
    assert(strcmp(out, "a b c") == 0);
    text_to_ascii("\xc3", out, sizeof out);  // UTF-8 truncado
    assert(strcmp(out, "") == 0);
    char peq[6];
    text_to_ascii("abcdefghij", peq, sizeof peq);
    assert(strlen(peq) < sizeof peq);

    char l[8][WRAP_MAX_COLS + 1];
    bool tr;
    int n = text_wrap("hello world foo", 10, l, 4, &tr);
    assert(n == 2 && !tr && !strcmp(l[0], "hello") && !strcmp(l[1], "world foo"));
    n = text_wrap("supercalifragilistico", 10, l, 4, &tr);
    assert(n == 3 && !strcmp(l[0], "supercalif") && !strcmp(l[1], "ragilistic") && !strcmp(l[2], "o"));
    n = text_wrap("", 10, l, 4, &tr);
    assert(n == 0 && !tr);
    n = text_wrap("   oi    tudo   bem  ", 16, l, 4, &tr);
    assert(n == 1 && !strcmp(l[0], "oi tudo bem"));
    n = text_wrap("a b c d e f g h i j", 3, l, 2, &tr);  // mais linhas que o limite
    assert(n == 2 && tr);
    n = text_wrap("x", 99, l, 4, &tr);  // cols acima do máximo é limitado
    assert(n == 1);

    char url[128];
    text_url_encode("A\xc3\xa7\xc3\xa3o & Caf\xc3\xa9", url, sizeof url);
    assert(strcmp(url, "A%C3%A7%C3%A3o%20%26%20Caf%C3%A9") == 0);
    text_url_encode("a-b_c.d~e", url, sizeof url);
    assert(strcmp(url, "a-b_c.d~e") == 0);
    char peq2[8];
    text_url_encode("\xc3\xa7\xc3\xa7\xc3\xa7\xc3\xa7", peq2, sizeof peq2);
    assert(strlen(peq2) < sizeof peq2 && strlen(peq2) % 3 == 0);
}

int main(void) {
    test_json();
    test_unescape_e_parada();
    test_lrc();
    test_text();
    puts("Todos os testes passaram.");
    return 0;
}
