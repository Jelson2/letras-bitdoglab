#include "leds.h"

#include <math.h>
#include <stdint.h>

#include "config.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "ws2812.pio.h"

#define N 5

static PIO g_pio = pio0;
static uint g_sm;
static float g_hist[N];      // "altura" de cada coluna (0..1); a mais nova fica à direita
static float g_level, g_peak = 0.02f, g_dc = 2048.f, g_kick;
static float g_hue;
static uint32_t g_last_frame;

static inline void put(uint8_t r, uint8_t g, uint8_t b) {
    pio_sm_put_blocking(g_pio, g_sm, ((uint32_t)g << 24) | ((uint32_t)r << 16) | ((uint32_t)b << 8));
}

void leds_init(void) {
    uint off = pio_add_program(g_pio, &ws2812_program);
    g_sm = pio_claim_unused_sm(g_pio, true);
    pio_gpio_init(g_pio, LED_MATRIX_PIN);
    pio_sm_set_consecutive_pindirs(g_pio, g_sm, LED_MATRIX_PIN, 1, true);
    pio_sm_config c = ws2812_program_get_default_config(off);
    sm_config_set_sideset_pins(&c, LED_MATRIX_PIN);
    sm_config_set_out_shift(&c, false, true, 24);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    const int cycles = ws2812_T1 + ws2812_T2 + ws2812_T3;
    sm_config_set_clkdiv(&c, clock_get_hz(clk_sys) / (800000.f * cycles));
    pio_sm_init(g_pio, g_sm, off, &c);
    pio_sm_set_enabled(g_pio, g_sm, true);

    adc_init();
    adc_gpio_init(MIC_PIN);
    adc_select_input(MIC_ADC_INPUT);
}

void leds_kick(void) { g_kick = 1.0f; }

// Intensidade do som agora (0..1), auto-ajustada ao volume do ambiente.
static float read_level(void) {
    adc_select_input(MIC_ADC_INPUT);
    uint16_t mn = 4095, mx = 0;
    uint32_t sum = 0;
    for (int i = 0; i < 64; i++) {
        uint16_t v = adc_read();
        sum += v;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    g_dc += ((sum / 64.f) - g_dc) * 0.02f;  // acompanha o nível médio do microfone
    float amp = (float)(mx - mn) / 4095.f;  // amplitude pico a pico
    if (amp > g_peak) g_peak = amp;
    g_peak *= 0.9995f;                      // referência cai devagar: adapta a volume baixo
    if (g_peak < 0.02f) g_peak = 0.02f;     // piso: ruído de fundo não vira "música"
    float l = (amp - 0.008f) / g_peak;
    return l < 0 ? 0 : (l > 1 ? 1 : l);
}

static void hsv(float h, float s, float v, uint8_t *r, uint8_t *g, uint8_t *b) {
    h -= floorf(h);
    float k = h * 6.f;
    int i = (int)k;
    float f = k - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    float R, G, B;
    switch (i % 6) {
        case 0: R = v; G = t; B = p; break;
        case 1: R = q; G = v; B = p; break;
        case 2: R = p; G = v; B = t; break;
        case 3: R = p; G = q; B = v; break;
        case 4: R = t; G = p; B = v; break;
        default: R = v; G = p; B = q; break;
    }
    *r = (uint8_t)(R * 255); *g = (uint8_t)(G * 255); *b = (uint8_t)(B * 255);
}

// Posição do LED na fita a partir de (coluna x, linha y), y=0 em cima.
static int led_index(int x, int y) {
#if LED_MATRIX_SERPENTINE
    return 24 - (y * N + ((y % 2 == 0) ? x : (N - 1 - x)));
#else
    return 24 - (y * N + x);
#endif
}

void leds_update(bool active) {
    uint32_t t = to_ms_since_boot(get_absolute_time());
    if (t - g_last_frame < 33) return;
    g_last_frame = t;

    float lvl = active ? read_level() : 0.f;
    // sobe rápido, desce devagar
    g_level = lvl > g_level ? lvl : g_level * 0.85f;
    float shown = g_level + g_kick * 0.5f;
    if (shown > 1) shown = 1;
    g_kick *= 0.8f;

    // Histórico: a cada ~3 quadros desliza uma coluna para a esquerda.
    static int tick;
    if (++tick >= 3) {
        tick = 0;
        for (int i = 0; i < N - 1; i++) g_hist[i] = g_hist[i + 1];
        g_hist[N - 1] = shown;
    } else {
        g_hist[N - 1] = shown > g_hist[N - 1] ? shown : g_hist[N - 1];
    }
    g_hue += 0.002f + 0.01f * g_level;

    uint8_t px[25][3] = {{0}};
    float breathe = 0.5f + 0.5f * sinf(t / 700.f);
    for (int x = 0; x < N; x++) {
        float h = g_hist[x] * N;  // altura em LEDs
        for (int y = 0; y < N; y++) {
            int rowFromBottom = N - 1 - y;
            float v = h - rowFromBottom;
            v = v < 0 ? 0 : (v > 1 ? 1 : v);
            if (!active && rowFromBottom == 0) v = 0.15f + 0.25f * breathe;  // modo espera
            if (v <= 0) continue;
            uint8_t r, g, b;
            hsv(g_hue + x * 0.07f + rowFromBottom * 0.05f, 1.f, v * LED_BRIGHTNESS / 255.f, &r, &g, &b);
            int i = led_index(x, y);
            px[i][0] = r; px[i][1] = g; px[i][2] = b;
        }
    }
    for (int i = 0; i < 25; i++) put(px[i][0], px[i][1], px[i][2]);
}
