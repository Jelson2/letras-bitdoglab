#include "oled_gfx.h"

#include <string.h>

#include "font8x8_basic.h"

void gfx_clear(oled_fb_t *fb) { memset(fb->buf, 0, sizeof fb->buf); }

void gfx_pixel(oled_fb_t *fb, int x, int y, bool on) {
    if (x < 0 || x >= OLED_W || y < 0 || y >= OLED_H) return;
    uint8_t *b = &fb->buf[(y >> 3) * OLED_W + x];
    if (on) *b |= (uint8_t)(1u << (y & 7));
    else *b &= (uint8_t)~(1u << (y & 7));
}

void gfx_char(oled_fb_t *fb, int x, int y, char c, int scale) {
    unsigned char u = (unsigned char)c;
    if (u >= 128) u = '?';
    if (scale < 1) scale = 1;
    for (int row = 0; row < 8; row++) {
        unsigned char bits = font8x8_basic[u][row];  // bit 0 = pixel mais à esquerda
        if (!bits) continue;
        for (int col = 0; col < 8; col++) {
            if (!(bits & (1u << col))) continue;
            for (int sy = 0; sy < scale; sy++)
                for (int sx = 0; sx < scale; sx++) gfx_pixel(fb, x + col * scale + sx, y + row * scale + sy, true);
        }
    }
}

void gfx_text(oled_fb_t *fb, int x, int y, const char *s, int scale) {
    for (; *s; s++) {
        gfx_char(fb, x, y, *s, scale);
        x += 8 * scale;
    }
}
