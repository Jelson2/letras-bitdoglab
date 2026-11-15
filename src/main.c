// Letras sincronizadas do Spotify na BitDogLab (Raspberry Pi Pico W + OLED SSD1306).
//
// Fluxo: Wi-Fi -> token do Spotify -> a cada SPOTIFY_POLL_MS consulta a faixa tocando
// -> ao trocar de faixa busca a letra sincronizada no LRCLIB -> mostra no OLED a linha
// certa no momento certo, estimando o progresso entre as consultas.

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "hardware/gpio.h"
#include "https_client.h"
#include "leds.h"
#include "lrclib.h"
#include "oled_gfx.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "secrets.h"
#include "spotify.h"
#include "ssd1306.h"
#include "text.h"

// ---------- Display ----------

static oled_fb_t g_fb;
static bool g_oled_ok;
static char g_screen_key[56];  // o que está desenhado agora: evita redesenhar à toa

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

static void screen_flush(void) {
    if (g_oled_ok) ssd1306_show(&g_fb);
}

// Mostra até 8 linhas de texto pequeno (16 colunas). Se `key` for igual à da tela
// atual, não faz nada. Linhas vazias viram espaço.
static void show_msg(const char *key, int n, const char *const lines[]) {
    if (key && !strcmp(key, g_screen_key)) return;
    snprintf(g_screen_key, sizeof g_screen_key, "%s", key ? key : "");

    gfx_clear(&g_fb);
    int row = 0;
    for (int i = 0; i < n && row < 8; i++) {
        char ascii[160];
        text_to_ascii(lines[i], ascii, sizeof ascii);
        if (!ascii[0]) {
            row++;
            continue;
        }
        char wrapped[8][WRAP_MAX_COLS + 1];
        bool trunc;
        int k = text_wrap(ascii, 16, wrapped, 8 - row, &trunc);
        for (int j = 0; j < k; j++, row++) gfx_text(&g_fb, 0, row * 8, wrapped[j], 1);
    }
    screen_flush();
    for (int i = 0; i < n; i++) {
        if (lines[i][0]) printf("[tela] %s\n", lines[i]);
    }
}

static int longest_word(const char *s) {
    int best = 0, cur = 0;
    for (; *s; s++) {
        if (*s == ' ') cur = 0;
        else if (++cur > best) best = cur;
    }
    return best;
}

// Mostra uma linha da letra centralizada, em fonte grande (8 colunas x 4 linhas)
// ou, se não couber, pequena (16 x 8).
static void show_lyric(size_t idx) {
    char key[24];
    snprintf(key, sizeof key, "L%u", (unsigned)idx);
    if (!strcmp(key, g_screen_key)) return;
    snprintf(g_screen_key, sizeof g_screen_key, "%s", key);
    leds_kick();  // pulso de luz a cada nova linha da letra

    char ascii[200];
    text_to_ascii(lrclib_lines()[idx].text, ascii, sizeof ascii);

    char lines[8][WRAP_MAX_COLS + 1];
    bool trunc = false;
    int scale = 2;
    int n = 0;
    if (longest_word(ascii) <= 8) n = text_wrap(ascii, 8, lines, 4, &trunc);
    if (n == 0 || trunc || longest_word(ascii) > 8) {
        scale = 1;
        n = text_wrap(ascii, 16, lines, 8, &trunc);
    }
    if (n == 0) {  // trecho instrumental (linha vazia)
        strcpy(lines[0], "...");
        n = 1;
    }

    int h = 8 * scale;
    int y0 = (OLED_H - n * h) / 2;
    gfx_clear(&g_fb);
    for (int i = 0; i < n; i++) {
        int x = (OLED_W - gfx_text_width((int)strlen(lines[i]), scale)) / 2;
        gfx_text(&g_fb, x, y0 + i * h, lines[i], scale);
    }
    screen_flush();
    printf("[%02u:%02u] %s\n", (unsigned)(lrclib_lines()[idx].ms / 60000), (unsigned)(lrclib_lines()[idx].ms / 1000 % 60),
           lrclib_lines()[idx].text);
}

// ---------- Estado do app ----------

typedef enum { LY_NONE, LY_OK, LY_ERR } lyrics_state_t;

typedef struct {
    bool active;   // a última consulta trouxe uma faixa válida
    bool playing;
    track_info_t track;
    int64_t base_prog;   // progresso (ms) no instante base_t
    uint32_t base_t;
    int32_t adjust_ms;   // ajuste manual pelos botões
    lyrics_state_t ly;
    uint32_t retry_at;   // quando tentar a letra de novo, se deu erro de rede
} app_t;

static app_t g_app;
static uint32_t g_next_poll;

static bool reached(uint32_t t) { return (int32_t)(now_ms() - t) >= 0; }

static void load_lyrics(void) {
    const track_info_t *t = &g_app.track;
    char key[56];
    snprintf(key, sizeof key, "B:%s", t->id);
    show_msg(key, 6, (const char *const[]){"Buscando letra...", "", t->name, "", t->artist, ""});

    int n = lrclib_fetch(t->name, t->artist, t->album, t->duration_ms / 1000);
    if (n > 0) {
        g_app.ly = LY_OK;
    } else if (n == 0) {
        g_app.ly = LY_NONE;
    } else {
        g_app.ly = LY_ERR;
        g_app.retry_at = now_ms() + 15000;
        printf("LRCLIB: erro %d, tenta de novo em 15 s\n", n);
    }
    g_screen_key[0] = '\0';  // força redesenhar
}

static void handle_track(const track_info_t *t) {
    g_app.active = true;
    g_app.playing = t->playing;
    g_app.base_prog = t->progress_ms;
    g_app.base_t = t->sampled_at_ms;

    bool new_track = strcmp(t->id, g_app.track.id) != 0;
    if (new_track) {
        g_app.track = *t;
        g_app.adjust_ms = 0;
        g_app.ly = LY_NONE;
        lrclib_clear();
        printf("Nova musica: %s - %s\n", t->name, t->artist);
        load_lyrics();
    } else if (g_app.ly == LY_ERR && reached(g_app.retry_at)) {
        load_lyrics();
    }
}

static void poll_spotify(void) {
    if (!spotify_token_ok()) {
        show_msg("auth", 2, (const char *const[]){"Spotify", "Autenticando..."});
        int r = spotify_refresh_token();
        if (r != 200) {
            printf("Token: erro %d  %s\n", r, spotify_last_body());
            char key[24];
            snprintf(key, sizeof key, "tok%d", r);
            if (r == 400 || r == 401) {
                show_msg(key, 5, (const char *const[]){"Erro Spotify", "", "Token invalido.", "Confira o", "secrets.h"});
            } else {
                show_msg(key, 4, (const char *const[]){"Sem acesso ao", "Spotify:", r > 0 ? "erro do servidor" : https_error_text(r), ""});
            }
            g_app.active = false;
            g_next_poll = now_ms() + 10000;
            return;
        }
    }

    track_info_t t;
    int st = spotify_get_player(&t);
    printf("Spotify: HTTP %d\n", st);

    if (st == 200 && t.valid) {
        handle_track(&t);
        return;
    }

    g_app.active = false;
    char key[24];
    snprintf(key, sizeof key, "st%d", st);
    if (st == 200) {
        show_msg(key, 4, (const char *const[]){"Sem faixa", "", "(anuncio ou", "conteudo local)"});
    } else if (st == 204) {
        show_msg(key, 4, (const char *const[]){"Nenhuma musica", "", "tocando no", "momento."});
    } else if (st == 401) {
        spotify_invalidate_token();  // renova no próximo ciclo
        g_next_poll = now_ms();
    } else if (st == 403) {
        show_msg(key, 4, (const char *const[]){"Spotify bloqueou", "", "Verifique Premium", "e permissoes."});
    } else if (st == 429) {
        show_msg(key, 4, (const char *const[]){"Spotify: muitas", "requisicoes.", "", "Aguarde..."});
        g_next_poll = now_ms() + 10000;
    } else if (st < 0) {
        show_msg(key, 4, (const char *const[]){"Erro de rede:", https_error_text(st), "", "Tentando de novo"});
    } else {
        char code[24];
        snprintf(code, sizeof code, "Codigo: %d", st);
        show_msg(key, 3, (const char *const[]){"Erro Spotify", "", code});
    }
}

static void update_screen(void) {
    if (!g_app.active) return;
    const track_info_t *t = &g_app.track;
    char key[56];

    if (!g_app.playing) {
        snprintf(key, sizeof key, "P:%s", t->id);
        show_msg(key, 5, (const char *const[]){"PAUSADO", "", t->name, "", t->artist});
        return;
    }
    if (g_app.ly != LY_OK) {
        snprintf(key, sizeof key, "N%d:%s", (int)g_app.ly, t->id);
        if (g_app.ly == LY_ERR) {
            show_msg(key, 4, (const char *const[]){"Erro ao buscar", "a letra.", "", t->name});
        } else {
            show_msg(key, 6, (const char *const[]){"Letra sincronizada", "nao encontrada.", "", t->name, "", t->artist});
        }
        return;
    }

    int64_t prog = g_app.base_prog + (int32_t)(now_ms() - g_app.base_t) + g_app.adjust_ms;
    int idx = lrc_find(lrclib_lines(), lrclib_count(), prog);
    if (idx < 0) {
        snprintf(key, sizeof key, "I:%s", t->id);
        show_msg(key, 5, (const char *const[]){"Tocando:", "", t->name, "", t->artist});
    } else {
        show_lyric((size_t)idx);
    }
}

// ---------- Botões ----------

static void buttons_init(void) {
    gpio_init(BTN_A_PIN);
    gpio_set_dir(BTN_A_PIN, GPIO_IN);
    gpio_pull_up(BTN_A_PIN);
    gpio_init(BTN_B_PIN);
    gpio_set_dir(BTN_B_PIN, GPIO_IN);
    gpio_pull_up(BTN_B_PIN);
}

static void read_buttons(void) {
    static bool a_prev = true, b_prev = true;  // true = solto (pull-up)
    static uint32_t last_ms;
    bool a = gpio_get(BTN_A_PIN), b = gpio_get(BTN_B_PIN);
    uint32_t t = now_ms();
    if (t - last_ms > 150) {
        if (!a && a_prev) {
            g_app.adjust_ms -= ADJUST_STEP_MS;
            last_ms = t;
            printf("Ajuste da letra: %ld ms\n", (long)g_app.adjust_ms);
        } else if (!b && b_prev) {
            g_app.adjust_ms += ADJUST_STEP_MS;
            last_ms = t;
            printf("Ajuste da letra: %ld ms\n", (long)g_app.adjust_ms);
        }
    }
    a_prev = a;
    b_prev = b;
}

// ---------- Wi-Fi ----------

static bool wifi_up(void) { return cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP; }

static bool wifi_connect(void) {
    show_msg("wifi", 3, (const char *const[]){"Conectando WiFi", "", WIFI_SSID});
    int r = cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_MIXED_PSK, WIFI_TIMEOUT_MS);
    if (r != 0) {
        char code[24];
        snprintf(code, sizeof code, "Erro %d", r);
        printf("WiFi: falha (%d)\n", r);
        show_msg("wifi_fail", 6, (const char *const[]){"Falha no WiFi", "", code, "Confira SSID e", "senha em", "secrets.h"});
        return false;
    }
    printf("WiFi conectado, IP %s\n", ip4addr_ntoa(netif_ip4_addr(netif_default)));
    g_screen_key[0] = '\0';
    return true;
}

int main(void) {
    stdio_init_all();
    g_oled_ok = ssd1306_init();
    buttons_init();
    leds_init();
    printf("\n=== Letras Spotify - BitDogLab ===\n");
    if (!g_oled_ok) printf("AVISO: display OLED nao respondeu (I2C1, GPIO %d/%d, 0x%02X)\n", OLED_SDA_PIN, OLED_SCL_PIN, OLED_I2C_ADDR);
    show_msg("boot", 4, (const char *const[]){"BitDogLab", "Letras Spotify", "", "Iniciando..."});

    if (cyw43_arch_init_with_country(CYW43_COUNTRY_BRAZIL)) {
        show_msg("cyw_fail", 3, (const char *const[]){"Erro no chip", "WiFi (CYW43)", "Reinicie a placa"});
        while (true) sleep_ms(1000);
    }
    cyw43_arch_enable_sta_mode();
    cyw43_wifi_pm(&cyw43_state, CYW43_PERFORMANCE_PM);  // economia de energia mais leve: menos latência

    g_next_poll = now_ms();
    while (true) {
        cyw43_arch_poll();

        if (!wifi_up()) {
            g_app.active = false;
            if (!wifi_connect()) {
                sleep_ms(5000);
                continue;
            }
            g_next_poll = now_ms();
        }

        if (reached(g_next_poll)) {
            g_next_poll = now_ms() + SPOTIFY_POLL_MS;
            poll_spotify();  // bloqueia durante a requisição HTTPS (de 1 a alguns segundos)
        }

        update_screen();
        read_buttons();
        leds_update(g_app.active && g_app.playing);
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(20));
    }
}
