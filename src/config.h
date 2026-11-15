// Configurações do projeto: pinos da BitDogLab, tempos e tamanhos de buffer.
#ifndef CONFIG_H
#define CONFIG_H

// ---- Display OLED SSD1306 128x64 (I2C1 da BitDogLab) ----
#define OLED_I2C i2c1
#define OLED_SDA_PIN 14
#define OLED_SCL_PIN 15
#define OLED_I2C_ADDR 0x3C
#define OLED_I2C_FREQ 400000

// ---- Botões da BitDogLab (ligam ao GND, usam pull-up interno) ----
#define BTN_A_PIN 5  // atrasa a letra
#define BTN_B_PIN 6  // adianta a letra
#define ADJUST_STEP_MS 250

// ---- Rede ----
#define SPOTIFY_POLL_MS 4000   // intervalo entre consultas ao Spotify
#define HTTPS_TIMEOUT_MS 15000 // limite total de cada requisição HTTPS
#define WIFI_TIMEOUT_MS 30000

// ---- Buffers (estáticos, para não fragmentar a heap) ----
#define BODY_BUF_SIZE 16384  // resposta JSON do Spotify
#define LRC_BUF_SIZE 16384   // texto .lrc da letra
#define MAX_LYRIC_LINES 400

// Matriz de LEDs e microfone (BitDogLab)
#define LED_MATRIX_PIN 7
#define LED_MATRIX_SERPENTINE 1  // 1 = linhas em zigue-zague; se a animação sair espelhada/torta, troque para 0
#define LED_BRIGHTNESS 40        // 0..255 (baixo de propósito: LEDs fortes ofuscam e puxam muita corrente)
#define MIC_PIN 28
#define MIC_ADC_INPUT 2

#endif
