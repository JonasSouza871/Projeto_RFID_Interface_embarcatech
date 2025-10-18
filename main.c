/*
 * Projeto RFID - Raspberry Pi Pico
 * Leitura de cartões RFID usando módulo MFRC522
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "mfrc522.h"

// Pinagem do MFRC522
#define PIN_MISO    4   // GP4 - Master In Slave Out
#define PIN_CS      5   // GP5 - Chip Select (SDA)
#define PIN_SCK     2   // GP2 - Clock SPI
#define PIN_MOSI    3   // GP3 - Master Out Slave In
#define PIN_RST     0   // GP0 - Reset

// Protótipos de funções
void setup_gpio(void);

int main() {
    // Inicializar stdio
    stdio_init_all();
    sleep_ms(2000);

    // Configurar GPIO
    setup_gpio();

    // Inicializar MFRC522
    MFRC522Ptr_t mfrc = MFRC522_Init();

    if (mfrc == NULL) {
        while(1) {
            sleep_ms(1000);
        }
    }

    // Inicializar comunicação SPI e MFRC522
    PCD_Init(mfrc, spi0);

    // Loop principal
    while (1) {
        // Verificar se há um novo cartão presente
        if (PICC_IsNewCardPresent(mfrc)) {
            // Tentar ler o UID do cartão
            if (PICC_ReadCardSerial(mfrc)) {
                // Exibir UID do cartão
                printf("UID: ");
                for (uint8_t i = 0; i < mfrc->uid.size; i++) {
                    printf("%02X", mfrc->uid.uidByte[i]);
                    if (i < mfrc->uid.size - 1) printf(":");
                }
                printf("\n");

                // Parar a criptografia
                PCD_StopCrypto1(mfrc);

                // Aguardar remoção do cartão
                sleep_ms(1000);
            }
        }

        sleep_ms(100);
    }

    return 0;
}

/**
 * Configura os pinos GPIO necessários
 */
void setup_gpio(void) {
    // Configurar pino de reset do MFRC522
    gpio_init(PIN_RST);
    gpio_set_dir(PIN_RST, GPIO_OUT);
    gpio_put(PIN_RST, 1);  // Reset inativo (high)

    // Configurar SPI
    spi_init(spi0, 1000000);  // 1 MHz

    // Configurar pinos SPI
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    // Configurar Chip Select
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);  // CS inativo (high)
}
