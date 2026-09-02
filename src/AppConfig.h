#ifndef APPCONFIG_H
#define APPCONFIG_H

#include <Arduino.h>
#define FIRMWARE_VERSION "2.4.2"

// Board Specific Configuration
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(ESP32C3)
    // ESP32-C3 Rev. 2 Configuration
    #define RS485_RX_PIN 20
    #define RS485_TX_PIN 21
    #define RS485_DE_RE_PIN 3
    #define RS485_UART_NUM 0
    #define BOOT_BUTTON_PIN 9
    #define WAVIN_BAUD_RATE 38400
    #define WAVIN_CONFIG SERIAL_8N1
#else
    // ESP32 Pico / Standard ESP32 Configuration
    #define RS485_RX_PIN 13
    #define RS485_TX_PIN 14
    #define RS485_DE_RE_PIN 26
    #define STATUS_LED_PIN 33
    #define RS485_UART_NUM 2
    #define BOOT_BUTTON_PIN 0
    #define WAVIN_BAUD_RATE 38400
    #define WAVIN_CONFIG SERIAL_8N1
#endif

// MQTT Settings
#define MQTT_RECONNECT_DELAY 5000

#define GITHUB_REPO "jascdk/wavin_ahc9000_advanced_mqtt"

#endif