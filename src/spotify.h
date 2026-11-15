// Cliente da API do Spotify: renova o token e lê a faixa que está tocando.
#ifndef SPOTIFY_H
#define SPOTIFY_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool valid;      // true se a resposta trouxe uma faixa (id preenchido)
    bool playing;
    uint32_t progress_ms;
    uint32_t duration_ms;
    char id[40];
    char name[128];
    char artist[96];
    char album[128];
    uint32_t sampled_at_ms;  // relógio (ms desde o boot) em que `progress_ms` vale
} track_info_t;

// Devolve 200 se renovou, outro código HTTP (400 = credenciais ruins) ou um HTTPS_ERR_*.
int spotify_refresh_token(void);

// true se há token e ele ainda não venceu.
bool spotify_token_ok(void);
void spotify_invalidate_token(void);

// Consulta /v1/me/player. Devolve o código HTTP (200, 204, 401, 403, 429...) ou um
// HTTPS_ERR_*. Com 200, `out` é preenchido (veja `out->valid`).
int spotify_get_player(track_info_t *out);

// Texto curto sobre o último erro devolvido pela API (para depuração no terminal).
const char *spotify_last_body(void);

#endif
