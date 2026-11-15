// Driver mínimo do display OLED SSD1306 128x64 por I2C.
#ifndef SSD1306_H
#define SSD1306_H

#include <stdbool.h>

#include "oled_gfx.h"

// Configura o I2C e inicializa o display. Devolve false se ele não responder.
bool ssd1306_init(void);

// Envia o framebuffer para o display.
void ssd1306_show(const oled_fb_t *fb);

#endif
