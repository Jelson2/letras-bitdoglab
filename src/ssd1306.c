#include "ssd1306.h"

#include <string.h>

#include "config.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

static bool cmd(uint8_t c) {
    uint8_t b[2] = {0x80, c};  // 0x80 = byte de controle "comando"
    return i2c_write_blocking(OLED_I2C, OLED_I2C_ADDR, b, 2, false) == 2;
}

bool ssd1306_init(void) {
    i2c_init(OLED_I2C, OLED_I2C_FREQ);
    gpio_set_function(OLED_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(OLED_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(OLED_SDA_PIN);
    gpio_pull_up(OLED_SCL_PIN);

    sleep_ms(20);  // o controlador precisa de um instante depois de ligar
    if (!cmd(0xAE)) return false;  // display desligado; sem resposta = sem display

    static const uint8_t seq[] = {
        0x20, 0x00,        // endereçamento horizontal
        0x40,              // linha inicial 0
        0xA1,              // espelha as colunas (orientação da BitDogLab)
        0xA8, 0x3F,        // 64 linhas
        0xC8,              // espelha as linhas
        0xD3, 0x00,        // sem deslocamento vertical
        0xDA, 0x12,        // configuração dos pinos COM
        0xD5, 0x80,        // clock
        0xD9, 0xF1,        // pré-carga
        0xDB, 0x30,        // nível VCOM
        0x81, 0xFF,        // contraste máximo
        0xA4,              // mostra o conteúdo da RAM
        0xA6,              // sem inversão
        0x8D, 0x14,        // bomba de carga ligada
        0xAF,              // display ligado
    };
    for (size_t i = 0; i < sizeof seq; i++) {
        if (!cmd(seq[i])) return false;
    }
    return true;
}

void ssd1306_show(const oled_fb_t *fb) {
    static uint8_t tx[1 + OLED_BUF_SIZE];
    cmd(0x21); cmd(0); cmd(OLED_W - 1);        // colunas 0..127
    cmd(0x22); cmd(0); cmd(OLED_H / 8 - 1);    // páginas 0..7
    tx[0] = 0x40;                              // 0x40 = byte de controle "dados"
    memcpy(tx + 1, fb->buf, OLED_BUF_SIZE);
    i2c_write_blocking(OLED_I2C, OLED_I2C_ADDR, tx, sizeof tx, false);
}
