// =====================================================
// Sistema RFID - Raspberry Pi Pico W com MQTT
// Integração com Dashboard AGV
// Publica leituras RFID em tempo real via MQTT
// =====================================================

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/spi.h"
#include "lwip/apps/mqtt.h"
#include "lwip/dns.h"
#include "mfrc522.h"

// ========== CONFIGURAÇÕES DO PROJETO ==========

// 🔧 CONFIGURAÇÕES DE WIFI
#define WIFI_SSID       "TP-Link_29B5"           // ⚠️ ALTERE AQUI: Nome da sua rede WiFi
#define WIFI_PASSWORD   "23438651"          // ⚠️ ALTERE AQUI: Senha da sua rede WiFi

// 🔧 CONFIGURAÇÕES DO BROKER MQTT
#define MQTT_BROKER_IP  "192.168.0.103"      // ⚠️ ALTERE AQUI: IP do computador rodando o broker
#define MQTT_BROKER_PORT 1883                // Porta padrão do MQTT
#define MQTT_CLIENT_ID  "PicoW-RFID-Reader"  // ID único deste dispositivo

// 📡 TÓPICOS MQTT (conforme documentação do projeto)
#define MQTT_TOPIC_RFID     "agv/rfid"       // Publicar leituras RFID
#define MQTT_TOPIC_STATUS   "agv/sensors/rfid/status" // Status do leitor

// 🔌 PINAGEM DO LEITOR RFID MFRC522
#define PIN_MISO    4                        // SPI MISO (Master In Slave Out)
#define PIN_CS      5                        // SPI CS (Chip Select)
#define PIN_SCK     2                        // SPI Clock
#define PIN_MOSI    3                        // SPI MOSI (Master Out Slave In)
#define PIN_RST     0                        // Reset do MFRC522

// ⚙️ CONFIGURAÇÕES DE OPERAÇÃO
#define SCAN_INTERVAL_MS    500              // Intervalo entre leituras (ms)
#define DEBOUNCE_TIME_MS    3000             // Tempo para ignorar mesma tag (ms)
#define RECONNECT_DELAY_MS  5000             // Delay antes de reconectar MQTT

// ========== VARIÁVEIS GLOBAIS ==========

// Cliente MQTT
mqtt_client_t *mqtt_client = NULL;
bool mqtt_connected = false;
ip_addr_t mqtt_broker_ip;

// Controle de leitura RFID
volatile uint8_t last_uid[10] = {0};
volatile uint8_t last_uid_size = 0;
volatile absolute_time_t last_read_time;

// Status da conexão
bool wifi_connected = false;
absolute_time_t last_reconnect_attempt;

// ========== PROTÓTIPOS DE FUNÇÕES ==========

// Funções de inicialização
void setup_gpio(void);
void connect_wifi(void);
void mqtt_init_and_connect(void);

// Callbacks MQTT
void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status);
void mqtt_pub_request_cb(void *arg, err_t result);
void dns_found_cb(const char *hostname, const ip_addr_t *ipaddr, void *arg);

// Funções de operação
void publish_rfid_tag(const uint8_t *uid, uint8_t uid_size);
void publish_status(const char *status);
bool is_same_tag(const uint8_t *uid, uint8_t uid_size);
void uid_to_hex_string(const uint8_t *uid, uint8_t size, char *output);

// ========== IMPLEMENTAÇÃO ==========

/**
 * Configura os pinos GPIO e inicializa SPI para o MFRC522
 */
void setup_gpio(void) {
    // Configura pino de reset
    gpio_init(PIN_RST);
    gpio_set_dir(PIN_RST, GPIO_OUT);
    gpio_put(PIN_RST, 1);  // Reset inativo (HIGH)

    // Inicializa SPI0 a 1MHz
    spi_init(spi0, 1000000);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    // Configura Chip Select (CS)
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);  // CS inativo (HIGH)

    printf("[RFID] GPIO configurado\n");
}

/**
 * Conecta ao WiFi
 */
void connect_wifi(void) {
    printf("[WiFi] Conectando a: %s\n", WIFI_SSID);

    if (cyw43_arch_init()) {
        printf("[WiFi] ERRO: Falha ao inicializar CYW43!\n");
        return;
    }

    cyw43_arch_enable_sta_mode();

    printf("[WiFi] Aguardando conexao...\n");

    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD,
                                           CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("[WiFi] ERRO: Falha ao conectar!\n");
        printf("[WiFi] Verifique SSID e senha!\n");
        wifi_connected = false;
        return;
    }

    wifi_connected = true;
    printf("[WiFi] Conectado com sucesso!\n");
    printf("[WiFi] IP: %s\n", ip4addr_ntoa(netif_ip4_addr(netif_list)));
}

/**
 * Callback chamado quando a resolução DNS é concluída
 */
void dns_found_cb(const char *hostname, const ip_addr_t *ipaddr, void *arg) {
    if (ipaddr != NULL) {
        mqtt_broker_ip = *ipaddr;
        printf("[MQTT] Broker resolvido: %s\n", ip4addr_ntoa(ipaddr));
    } else {
        printf("[MQTT] ERRO: Falha ao resolver hostname!\n");
    }
}

/**
 * Callback de conexão MQTT
 */
void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status) {
    if (status == MQTT_CONNECT_ACCEPTED) {
        mqtt_connected = true;
        printf("[MQTT] Conectado ao broker!\n");

        // Publica status de inicialização
        publish_status("online");

        // LED integrado: aceso = conectado
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    } else {
        mqtt_connected = false;
        printf("[MQTT] Conexao falhou! Status: %d\n", status);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
    }
}

/**
 * Callback de confirmação de publicação MQTT
 */
void mqtt_pub_request_cb(void *arg, err_t result) {
    if (result == ERR_OK) {
        printf("[MQTT] Mensagem publicada com sucesso!\n");

        // Pisca LED para indicar publicação
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        sleep_ms(50);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    } else {
        printf("[MQTT] ERRO ao publicar! Codigo: %d\n", result);
    }
}

/**
 * Inicializa e conecta ao broker MQTT
 */
void mqtt_init_and_connect(void) {
    printf("[MQTT] Inicializando cliente...\n");

    // Se já existe um cliente, desconecta e libera recursos
    if (mqtt_client != NULL) {
        printf("[MQTT] Liberando cliente antigo...\n");
        mqtt_disconnect(mqtt_client);
        mqtt_client_free(mqtt_client);
        mqtt_client = NULL;
        mqtt_connected = false;
        sleep_ms(500); // Aguarda liberação de recursos
    }

    mqtt_client = mqtt_client_new();
    if (mqtt_client == NULL) {
        printf("[MQTT] ERRO: Falha ao criar cliente!\n");
        return;
    }

    // Tenta converter IP do broker
    if (!ip4addr_aton(MQTT_BROKER_IP, &mqtt_broker_ip)) {
        printf("[MQTT] IP invalido, tentando resolver DNS...\n");

        // Se não for IP válido, tenta resolver via DNS
        err_t err = dns_gethostbyname(MQTT_BROKER_IP, &mqtt_broker_ip, dns_found_cb, NULL);

        if (err == ERR_INPROGRESS) {
            printf("[MQTT] Aguardando resolucao DNS...\n");
            // Aguarda até 5 segundos pela resolução
            for (int i = 0; i < 50; i++) {
                sleep_ms(100);
                cyw43_arch_poll();
                if (mqtt_broker_ip.addr != 0) break;
            }
        }

        if (mqtt_broker_ip.addr == 0) {
            printf("[MQTT] ERRO: Nao foi possivel resolver o broker!\n");
            return;
        }
    }

    printf("[MQTT] Conectando ao broker %s:%d...\n",
           ip4addr_ntoa(&mqtt_broker_ip), MQTT_BROKER_PORT);

    // Configuração de conexão
    struct mqtt_connect_client_info_t ci;
    memset(&ci, 0, sizeof(ci));
    ci.client_id = MQTT_CLIENT_ID;
    ci.keep_alive = 60;  // Keep-alive de 60 segundos

    // Conecta ao broker
    err_t err = mqtt_client_connect(mqtt_client, &mqtt_broker_ip, MQTT_BROKER_PORT,
                                    mqtt_connection_cb, NULL, &ci);

    if (err != ERR_OK) {
        printf("[MQTT] ERRO ao iniciar conexao! Codigo: %d\n", err);
        mqtt_connected = false;
    } else {
        printf("[MQTT] Conexao iniciada, aguardando confirmacao...\n");
    }
}

/**
 * Converte UID para string hexadecimal (ex: "A1B2C3D4")
 */
void uid_to_hex_string(const uint8_t *uid, uint8_t size, char *output) {
    for (uint8_t i = 0; i < size; i++) {
        sprintf(output + (i * 2), "%02X", uid[i]);
    }
    output[size * 2] = '\0';
}

/**
 * Verifica se é a mesma tag lida recentemente (debounce)
 */
bool is_same_tag(const uint8_t *uid, uint8_t uid_size) {
    // Verifica se é a mesma tag
    if (last_uid_size != uid_size) return false;

    for (uint8_t i = 0; i < uid_size; i++) {
        if (last_uid[i] != uid[i]) return false;
    }

    // Verifica se passou tempo suficiente desde a última leitura
    absolute_time_t now = get_absolute_time();
    int64_t diff_ms = absolute_time_diff_us(last_read_time, now) / 1000;

    return diff_ms < DEBOUNCE_TIME_MS;
}

/**
 * Publica leitura de tag RFID no broker MQTT
 * Formato JSON: {"tag":"A1B2C3D4","timestamp":1234567890}
 */
void publish_rfid_tag(const uint8_t *uid, uint8_t uid_size) {
    if (!mqtt_connected) {
        printf("[MQTT] Nao conectado, pulando publicacao...\n");
        return;
    }

    // Converte UID para string hexadecimal
    char uid_str[32] = {0};
    uid_to_hex_string(uid, uid_size, uid_str);

    // Cria payload JSON conforme especificação do projeto
    char payload[128];
    uint32_t timestamp = to_ms_since_boot(get_absolute_time());

    snprintf(payload, sizeof(payload),
             "{\"tag\":\"%s\",\"timestamp\":%lu,\"reader\":\"PicoW\"}",
             uid_str, timestamp);

    printf("[RFID] Tag detectada: %s\n", uid_str);
    printf("[MQTT] Publicando: %s\n", payload);

    // Publica no tópico agv/rfid
    err_t err = mqtt_publish(mqtt_client, MQTT_TOPIC_RFID, payload, strlen(payload),
                            1,  // QoS 1 (pelo menos uma entrega)
                            0,  // Retain: false
                            mqtt_pub_request_cb, NULL);

    if (err != ERR_OK) {
        printf("[MQTT] ERRO ao publicar! Codigo: %d\n", err);
    }

    // Salva última tag lida
    memcpy((void*)last_uid, uid, uid_size);
    last_uid_size = uid_size;
    last_read_time = get_absolute_time();
}

/**
 * Publica status do leitor RFID
 */
void publish_status(const char *status) {
    if (!mqtt_connected) return;

    char payload[64];
    snprintf(payload, sizeof(payload),
             "{\"status\":\"%s\",\"reader\":\"PicoW\"}", status);

    mqtt_publish(mqtt_client, MQTT_TOPIC_STATUS, payload, strlen(payload),
                0, 0, mqtt_pub_request_cb, NULL);
}

/**
 * Reconecta ao MQTT se desconectado
 */
void mqtt_reconnect(void) {
    if (mqtt_connected || !wifi_connected) return;

    // Verifica se já passou tempo suficiente desde a última tentativa
    absolute_time_t now = get_absolute_time();
    int64_t diff_ms = absolute_time_diff_us(last_reconnect_attempt, now) / 1000;

    if (diff_ms < RECONNECT_DELAY_MS) {
        return; // Ainda não é hora de tentar reconectar
    }

    printf("[MQTT] Tentando reconectar...\n");
    last_reconnect_attempt = now;
    mqtt_init_and_connect();
}

// ========== FUNÇÃO PRINCIPAL ==========

int main() {
    // Inicializa comunicação serial (USB)
    stdio_init_all();
    sleep_ms(3000);  // Aguarda estabilização

    printf("\n");
    printf("========================================\n");
    printf("  Leitor RFID com MQTT para AGV\n");
    printf("  Dashboard Integration\n");
    printf("========================================\n\n");

    // PASSO 1: Conectar ao WiFi
    connect_wifi();
    if (!wifi_connected) {
        printf("\n[ERRO] WiFi nao conectou! Verifique credenciais.\n");
        printf("Encerrando...\n");
        return 1;
    }

    // PASSO 2: Conectar ao broker MQTT
    mqtt_init_and_connect();
    sleep_ms(2000);  // Aguarda callback de conexão

    if (!mqtt_connected) {
        printf("\n[AVISO] MQTT nao conectou! Tentando continuar...\n");
        printf("Verifique se o broker esta rodando em: %s:%d\n",
               MQTT_BROKER_IP, MQTT_BROKER_PORT);
    }

    // PASSO 3: Configurar hardware do leitor RFID
    printf("\n[RFID] Configurando hardware...\n");
    setup_gpio();

    // PASSO 4: Inicializar biblioteca MFRC522
    MFRC522Ptr_t mfrc = MFRC522_Init();
    if (mfrc == NULL) {
        printf("[ERRO] Falha ao inicializar MFRC522!\n");
        printf("Verifique conexoes do modulo RFID:\n");
        printf("  MISO -> GP%d\n", PIN_MISO);
        printf("  MOSI -> GP%d\n", PIN_MOSI);
        printf("  SCK  -> GP%d\n", PIN_SCK);
        printf("  CS   -> GP%d\n", PIN_CS);
        printf("  RST  -> GP%d\n", PIN_RST);
        printf("  VCC  -> 3.3V\n");
        printf("  GND  -> GND\n");
        while(1) sleep_ms(1000);
    }

    PCD_Init(mfrc, spi0);
    printf("[RFID] Leitor inicializado com sucesso!\n\n");

    printf("========================================\n");
    printf("  Sistema pronto!\n");
    printf("========================================\n");
    printf("Topico MQTT: %s\n", MQTT_TOPIC_RFID);
    printf("Formato: {\"tag\":\"HEX\",\"timestamp\":MS}\n");
    printf("\nAproxime tags RFID do leitor...\n\n");

    // Inicializa controle de tempo
    last_read_time = get_absolute_time();
    last_reconnect_attempt = get_absolute_time();

    // ========== LOOP PRINCIPAL ==========
    // Escaneia continuamente por tags RFID e publica via MQTT

    uint32_t loop_count = 0;
    absolute_time_t last_status = get_absolute_time();

    while (1) {
        // Processa eventos de rede (WiFi + MQTT)
        cyw43_arch_poll();

        // Reconecta MQTT se necessário
        if (!mqtt_connected && wifi_connected) {
            mqtt_reconnect();
        }

        // Verifica se há cartão RFID próximo
        if (PICC_IsNewCardPresent(mfrc)) {
            if (PICC_ReadCardSerial(mfrc)) {

                // Verifica se não é a mesma tag (debounce)
                if (!is_same_tag(mfrc->uid.uidByte, mfrc->uid.size)) {

                    // PUBLICA TAG NO MQTT!
                    publish_rfid_tag(mfrc->uid.uidByte, mfrc->uid.size);

                    printf("----------------------------------------\n");
                }

                // Finaliza comunicação com o cartão
                PCD_StopCrypto1(mfrc);
            }
        }

        // Publica status periodicamente (a cada 30 segundos)
        absolute_time_t now = get_absolute_time();
        if (absolute_time_diff_us(last_status, now) > 30000000) {
            publish_status("online");
            last_status = now;
            printf("[INFO] Status publicado (loop: %lu)\n", loop_count);
        }

        loop_count++;
        sleep_ms(SCAN_INTERVAL_MS);
    }

    // Cleanup (nunca alcançado neste código)
    if (wifi_connected) {
        mqtt_disconnect(mqtt_client);
        cyw43_arch_deinit();
    }

    return 0;
}
