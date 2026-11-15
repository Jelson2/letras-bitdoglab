// Matriz 5x5 de LEDs NeoPixel (GPIO 7) que reage ao som captado pelo microfone da placa.
#ifndef LEDS_H
#define LEDS_H
#include <stdbool.h>

void leds_init(void);
// Chamar a cada volta do laço principal; só redesenha ~30 vezes por segundo.
// `active` = há música tocando (senão a matriz fica em "respiração" suave).
void leds_update(bool active);
// Pulso de luz extra (usado a cada nova linha da letra).
void leds_kick(void);
#endif
