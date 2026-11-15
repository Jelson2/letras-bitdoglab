// Framebuffer monocromático 128x64 (formato de páginas do SSD1306) e texto com
// a fonte 8x8. Sem dependências do Pico SDK.
#ifndef OLED_GFX_H
#define OLED_GFX_H

#include <stdbool.h>
#include <stdint.h>

#define OLED_W 128
#define OLED_H 64
#define OLED_BUF_SIZE (OLED_W * OLED_H / 8)

typedef struct {
    uint8_t buf[OLED_BUF_SIZE];
} oled_fb_t;

void gfx_clear(oled_fb_t *fb);
void gfx_pixel(oled_fb_t *fb, int x, int y, bool on);

// Desenha um caractere ASCII; `scale` amplia por fator inteiro (1, 2, ...).
void gfx_char(oled_fb_t *fb, int x, int y, char c, int scale);
void gfx_text(oled_fb_t *fb, int x, int y, const char *s, int scale);

// Largura em pixels de `n` caracteres.
static inline int gfx_text_width(int n, int scale) { return n * 8 * scale; }

#endif
