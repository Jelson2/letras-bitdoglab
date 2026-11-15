# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# A BitDogLab usa um Raspberry Pi Pico W (RP2040 com Wi-Fi).
set(PICO_BOARD pico_w CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(letras_spotify C CXX ASM)

pico_sdk_init()

if(NOT EXISTS ${CMAKE_CURRENT_LIST_DIR}/src/secrets.h)
    message(FATAL_ERROR
        "Falta o arquivo src/secrets.h.\n"
        "Copie src/secrets.example.h para src/secrets.h e preencha o Wi-Fi e as credenciais do Spotify.")
endif()

add_executable(letras_spotify
    src/main.c
    src/https_client.c
    src/spotify.c
    src/lrclib.c
    src/ssd1306.c
    src/oled_gfx.c
    src/http_parse.c
    src/stream_field.c
    src/json_scan.c
    src/lrc.c
    src/text.c
    src/leds.c
)

# src/ tem o lwipopts.h e o mbedtls_config.h que o SDK procura, além do secrets.h.
target_include_directories(letras_spotify PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)

target_link_libraries(letras_spotify
    pico_stdlib
    hardware_i2c
    hardware_pio
    hardware_adc
    pico_cyw43_arch_lwip_poll
    pico_lwip_mbedtls
    pico_mbedtls
)

# printf pela USB (aparece no terminal serial do VS Code / Thonny); UART desligada.
pico_enable_stdio_usb(letras_spotify 1)
pico_enable_stdio_uart(letras_spotify 0)

# Avisos do código do lwIP, fora do nosso controle.
set_source_files_properties(
    ${PICO_LWIP_PATH}/src/apps/altcp_tls/altcp_tls_mbedtls.c
    PROPERTIES COMPILE_OPTIONS "-Wno-unused-result")

pico_generate_pio_header(letras_spotify ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)

pico_add_extra_outputs(letras_spotify)
