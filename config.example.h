/**
 * config.example.h
 *
 * Arquivo de exemplo de configuração
 *
 * INSTRUÇÕES:
 * 1. Copie este arquivo para "config.h"
 * 2. Preencha suas credenciais
 * 3. No main_mqtt.c, substitua as defines por #include "config.h"
 */

#ifndef CONFIG_H
#define CONFIG_H

// ========== CONFIGURAÇÕES DE WIFI ==========
#define WIFI_SSID       "SUA_REDE_WIFI"
#define WIFI_PASSWORD   "SUA_SENHA_WIFI"

// ========== CONFIGURAÇÕES DE MQTT ==========
#define MQTT_BROKER_IP  "192.168.1.100"      // IP do computador com o broker
#define MQTT_BROKER_PORT 1883
#define MQTT_CLIENT_ID  "PicoW-RFID-Reader"

// ========== TÓPICOS MQTT ==========
#define MQTT_TOPIC_RFID     "agv/rfid"
#define MQTT_TOPIC_STATUS   "agv/sensors/rfid/status"

// ========== PINAGEM RFID MFRC522 ==========
#define PIN_MISO    4
#define PIN_CS      5
#define PIN_SCK     2
#define PIN_MOSI    3
#define PIN_RST     0

// ========== CONFIGURAÇÕES DE OPERAÇÃO ==========
#define SCAN_INTERVAL_MS    500
#define DEBOUNCE_TIME_MS    3000
#define RECONNECT_DELAY_MS  5000

#endif // CONFIG_H
