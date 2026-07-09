#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include "WavinController.h" // For WAVIN_CHANNELS

class ConfigManager {
public:
    ConfigManager();
    void begin();
    void save();
    void reset();
    void updateMasks();

    // --- Configuration Variables ---
    
    // MQTT
    char mqtt_server[40];
    char mqtt_port[6];
    char mqtt_user[32];
    char mqtt_pass[32];
    char mqtt_discovery_prefix[32];
    
    // System
    char timezone[64];
    char room_names[WAVIN_CHANNELS][32];
    bool enable_telnet;
    bool enable_dark_mode;

    // Room Masks (Strings)
    char master_rooms_str[64];
    char boost_rooms_str[64];
    char vacation_rooms_str[64];
    char maint_rooms_str[64];
    char lock_rooms_str[64];
    char eco_rooms_str[64];
    char comfort_rooms_str[64];

    // Calculated Masks (Bitfields)
    uint16_t master_mask;
    uint16_t boost_mask;
    uint16_t vacation_mask;
    uint16_t maint_mask;
    uint16_t lock_mask;
    uint16_t eco_mask;
    uint16_t comfort_mask;

    // Feature Flags
    bool enable_maintenance;
    bool enable_boost;
    bool enable_vacation;
    bool enable_floor_sensors;
    
    // Master Settings
    bool enable_master_alarm;
    bool enable_master_hysteresis;
    bool enable_master_lock;
    bool enable_master_minmax;
    bool enable_master_standby;

    // Feature Settings
    int maint_day;
    int maint_hour;
    int maint_minute;
    int maint_duration;
    String last_maint_str;
    float boost_temp;
    float master_eco_temp;
    float master_comfort_temp;

private:
    Preferences preferences;
    uint16_t parseMask(String str);
};

#endif