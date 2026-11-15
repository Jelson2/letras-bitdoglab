# Letras do Spotify na BitDogLab (C / Pico SDK)

Mostra no OLED da BitDogLab a letra sincronizada da música que está tocando no Spotify
(letras vindas do LRCLIB). Mesmo princípio do projeto ESP32, agora em C no Pico W.

## Como usar no VS Code
1. Instale a extensão **Raspberry Pi Pico** (oficial) e deixe ela instalar o SDK/toolchain.
2. Copie `src/secrets.example.h` para `src/secrets.h` e preencha Wi-Fi (2,4 GHz), Client ID,
   Client Secret e Refresh Token do Spotify (os mesmos do projeto ESP32).
3. Na extensão: **Import Project** → escolha esta pasta → placa **Pico W** → Import.
4. Clique em **Run** (ou "Compile Project" e copie `build/letras_spotify.uf2` para a placa em modo BOOTSEL:
   segure BOOTSEL, aperte RESET e solte).
5. Veja os logs no monitor serial (USB).

## Hardware
- OLED SSD1306 128x64: I2C1, SDA GPIO14, SCL GPIO15, endereço 0x3C.
- Botão A (GPIO5): atrasa a letra 250 ms. Botão B (GPIO6): adianta 250 ms. O ajuste zera a cada música.

## Matriz de LEDs
A matriz 5x5 (GPIO 7) reage ao som captado pelo microfone da placa (GPIO 28): colunas que sobem e descem com o volume, cores que mudam, e um pulso a cada nova linha de letra. Se a animação sair espelhada, troque `LED_MATRIX_SERPENTINE` em `src/config.h`. O brilho está em `LED_BRIGHTNESS`.

## Observações
- A conexão HTTPS é criptografada, mas **não verifica certificados** (como `setInsecure()` no ESP32).
- A cada consulta (4 s) há um handshake TLS; o RP2040 faz isso em ~1 s e a tela pode "congelar" nesse tempo.
- TLS 1.2 apenas.

## Problemas comuns
- "Falha no WiFi": confira SSID/senha; só funciona em 2,4 GHz.
- "Token invalido": refresh token/Client ID/Secret errados em `secrets.h`.
- "Letra sincronizada nao encontrada": o LRCLIB não tem essa faixa.
- Tela preta: veja no log serial o aviso de OLED sem resposta.

## Testes no computador
`sh tests/run_tests.sh` (lógica de parsing: JSON, LRC, HTTP, texto).
