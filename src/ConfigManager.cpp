#include "ConfigManager.h"

ConfigManager::ConfigManager() {
    // Set Defaults
    strcpy(mqtt_server, "");
    strcpy(mqtt_port, "1883");
    strcpy(mqtt_user, "");
    strcpy(mqtt_pass, "");
    strcpy(mqtt_discovery_prefix, "homeassistant");
    strcpy(timezone, "CET-1CEST,M3.5.0,M10.5.0/3");

    strcpy(master_rooms_str, "2-16");
    strcpy(boost_rooms_str, "2-16");
    strcpy(vacation_rooms_str, "1-16");
    strcpy(maint_rooms_str, "1-16");
    strcpy(lock_rooms_str, "1-16");
    strcpy(eco_rooms_str, "1-16");
    strcpy(comfort_rooms_str, "1-16");

    enable_maintenance = true;
    enable_boost = true;
    enable_vacation = true;
    enable_floor_sensors = false;
    
    enable_master_alarm = true;
    enable_master_hysteresis = true;
    enable_master_lock = true;
    enable_master_minmax = true;
    enable_master_standby = true;
    
    enable_telnet = true;
    enable_dark_mode = false;

    maint_day = 0;
    maint_hour = 3;
    maint_minute = 0;
    maint_duration = 15;
    last_maint_str = "Never";

    boost_temp = 24.0;
    master_eco_temp = 18.0;
    master_comfort_temp = 22.0;
}

void ConfigManager::begin() {
    preferences.begin("wavin", false);

    if(preferences.isKey("mqtt_server")) preferences.getString("mqtt_server", "").toCharArray(mqtt_server, 40);
    if(preferences.isKey("mqtt_port")) preferences.getString("mqtt_port", "1883").toCharArray(mqtt_port, 6);
    if(preferences.isKey("mqtt_user")) preferences.getString("mqtt_user", "").toCharArray(mqtt_user, 32);
    if(preferences.isKey("mqtt_pass")) preferences.getString("mqtt_pass", "").toCharArray(mqtt_pass, 32);
    if(preferences.isKey("mqtt_prefix")) preferences.getString("mqtt_prefix", "homeassistant").toCharArray(mqtt_discovery_prefix, 32);
    if(preferences.isKey("timezone")) preferences.getString("timezone", timezone).toCharArray(timezone, 64);

    if(preferences.isKey("master_rooms")) preferences.getString("master_rooms", "2-16").toCharArray(master_rooms_str, 64);
    if(preferences.isKey("boost_rooms")) preferences.getString("boost_rooms", "2-16").toCharArray(boost_rooms_str, 64);
    if(preferences.isKey("vac_rooms")) preferences.getString("vac_rooms", "1-16").toCharArray(vacation_rooms_str, 64);
    if(preferences.isKey("maint_rooms")) preferences.getString("maint_rooms", "1-16").toCharArray(maint_rooms_str, 64);
    if(preferences.isKey("lock_rooms")) preferences.getString("lock_rooms", "1-16").toCharArray(lock_rooms_str, 64);
    if(preferences.isKey("eco_rooms")) preferences.getString("eco_rooms", "1-16").toCharArray(eco_rooms_str, 64);
    if(preferences.isKey("comf_rooms")) preferences.getString("comf_rooms", "1-16").toCharArray(comfort_rooms_str, 64);

    enable_maintenance = preferences.getBool("en_maint", true);
    enable_boost = preferences.getBool("en_boost", true);
    enable_vacation = preferences.getBool("en_vac", true);
    enable_floor_sensors = preferences.getBool("en_floor", false);

    enable_master_alarm = preferences.getBool("en_m_alarm", true);
    enable_master_hysteresis = preferences.getBool("en_m_hyst", true);
    enable_master_lock = preferences.getBool("en_m_lock", true);
    enable_master_minmax = preferences.getBool("en_m_minmax", true);
    enable_master_standby = preferences.getBool("en_m_standby", true);
    
    enable_telnet = preferences.getBool("en_telnet", true);
    enable_dark_mode = preferences.getBool("en_dark", false);

    maint_day = preferences.getInt("maint_day", 0);
    maint_hour = preferences.getInt("maint_hour", 3);
    maint_minute = preferences.getInt("maint_minute", 0);
    maint_duration = preferences.getInt("maint_dur", 15);
    if(preferences.isKey("last_maint")) last_maint_str = preferences.getString("last_maint", "Never");

    boost_temp = preferences.getFloat("boost_temp", 24.0);
    master_eco_temp = preferences.getFloat("m_eco_temp", 18.0);
    master_comfort_temp = preferences.getFloat("m_comf_temp", 22.0);

    // Load Room Names
    const char* default_names[] = {
        "Værksted", "Victor", "Gæstebadeværelse", "Bryggers", "Kontor", 
        "Køkken", "Køkken-alrum", "Badeværelse", "Soveværelse", "Stue", "Emil"
    };

    for(int i=0; i<WAVIN_CHANNELS; i++) {
        char key[10]; sprintf(key, "room_%d", i);
        String val = preferences.getString(key, "");
        if (val.length() == 0) {
            if (i < 11) strcpy(room_names[i], default_names[i]);
            else sprintf(room_names[i], "Room %d", i + 1);
        } else {
            val.toCharArray(room_names[i], 32);
        }
    }

    updateMasks();
}

void ConfigManager::save() {
    // Note: preferences.begin() is called in begin(), but we can ensure it's open here if needed.
    // However, keeping it open consumes heap. It's often better to open/close on demand or keep open if frequent.
    // Here we assume it's open or we reopen.
    
    preferences.putString("mqtt_server", mqtt_server);
    preferences.putString("mqtt_port", mqtt_port);
    preferences.putString("mqtt_user", mqtt_user);
    preferences.putString("mqtt_pass", mqtt_pass);
    preferences.putString("mqtt_prefix", mqtt_discovery_prefix);
    preferences.putString("timezone", timezone);

    preferences.putString("master_rooms", master_rooms_str);
    preferences.putString("boost_rooms", boost_rooms_str);
    preferences.putString("vac_rooms", vacation_rooms_str);
    preferences.putString("maint_rooms", maint_rooms_str);
    preferences.putString("lock_rooms", lock_rooms_str);
    preferences.putString("eco_rooms", eco_rooms_str);
    preferences.putString("comf_rooms", comfort_rooms_str);

    preferences.putBool("en_maint", enable_maintenance);
    preferences.putBool("en_boost", enable_boost);
    preferences.putBool("en_vac", enable_vacation);
    preferences.putBool("en_floor", enable_floor_sensors);

    preferences.putBool("en_m_alarm", enable_master_alarm);
    preferences.putBool("en_m_hyst", enable_master_hysteresis);
    preferences.putBool("en_m_lock", enable_master_lock);
    preferences.putBool("en_m_minmax", enable_master_minmax);
    preferences.putBool("en_m_standby", enable_master_standby);
    
    preferences.putBool("en_telnet", enable_telnet);
    preferences.putBool("en_dark", enable_dark_mode);

    preferences.putInt("maint_day", maint_day);
    preferences.putInt("maint_hour", maint_hour);
    preferences.putInt("maint_minute", maint_minute);
    preferences.putInt("maint_dur", maint_duration);
    
    preferences.putFloat("boost_temp", boost_temp);
    preferences.putFloat("m_eco_temp", master_eco_temp);
    preferences.putFloat("m_comf_temp", master_comfort_temp);
    
    for(int i=0; i<WAVIN_CHANNELS; i++) {
        char key[10]; sprintf(key, "room_%d", i);
        preferences.putString(key, room_names[i]);
    }
    
    updateMasks();
}

void ConfigManager::reset() {
    preferences.clear();
}

void ConfigManager::updateMasks() {
    master_mask = parseMask(String(master_rooms_str));
    boost_mask = parseMask(String(boost_rooms_str));
    vacation_mask = parseMask(String(vacation_rooms_str));
    maint_mask = parseMask(String(maint_rooms_str));
    lock_mask = parseMask(String(lock_rooms_str));
    eco_mask = parseMask(String(eco_rooms_str));
    comfort_mask = parseMask(String(comfort_rooms_str));
}

uint16_t ConfigManager::parseMask(String str) {
    uint16_t mask = 0;
    int len = str.length();
    int p = 0;
    while (p < len) {
        int comma = str.indexOf(',', p);
        if (comma == -1) comma = len;
        String part = str.substring(p, comma);
        part.trim();
        int dash = part.indexOf('-');
        if (dash != -1) {
            int start = part.substring(0, dash).toInt();
            int end = part.substring(dash + 1).toInt();
            for (int k = start; k <= end; k++) {
                if (k >= 1 && k <= 16) mask |= (1 << (k - 1));
            }
        } else {
            int val = part.toInt();
            if (val >= 1 && val <= 16) mask |= (1 << (val - 1));
        }
        p = comma + 1;
    }
    return mask;
}