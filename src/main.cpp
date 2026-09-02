#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <Update.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <TelnetStream.h>
#include <time.h>
#include "WavinController.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h>
#include <Ticker.h>
#include "AppConfig.h"
#include "WebInterface.h"
#include "ConfigManager.h"


// Uncomment the line below to run a hardware loopback test on boot
// #define RUN_LOOPBACK_TEST 

// --- Globals ---
WiFiClient espClient;
WebInterface webInterface(80);
PubSubClient mqtt(espClient);
HardwareSerial RS485(RS485_UART_NUM); // Use UART2 for Pico, UART0 for C3
WavinController wavin(RS485, RS485_DE_RE_PIN, RS485_DE_RE_PIN);
#ifdef STATUS_LED_PIN
Ticker ledTicker;
#endif
Preferences preferences;
ConfigManager configManager;

extern float g_wavinChannelCurrent[];
extern float g_wavinTotalCurrent;

// State tracking
bool discovery_published = false;
bool clock_synced = false;
float dailyMinAvgTemp = 0.0;
float dailyMaxAvgTemp = 0.0;
int statsDay = -1;
int mqtt_reconnect_count = 0;

// Maintenance State
bool maint_active = false;
unsigned long maint_start_millis = 0;
float maint_restore_temps[WAVIN_CHANNELS];
String maint_restore_modes[WAVIN_CHANNELS];
int last_maint_day_triggered = -1;
const char* days_of_week[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Every Day", "Disabled"};

// Boost State
bool boost_active = false;
unsigned long boost_start_millis = 0;
float boost_restore_temps[WAVIN_CHANNELS];
String boost_restore_modes[WAVIN_CHANNELS];

// Vacation State
bool vacation_active = false;
float vacation_restore_temps[WAVIN_CHANNELS];
String vacation_restore_modes[WAVIN_CHANNELS];
unsigned long last_telemetry_time = 0;

// --- Function Prototypes ---
void setup_wifi();
void saveConfigCallback();
void mqtt_callback(char* topic, byte* payload, unsigned int length);
void reconnect_mqtt();
void publish_discovery();
void publish_status();
void publish_telemetry();
void check_maintenance();
void start_maintenance();
void stop_maintenance();
void check_boost();
void start_boost();
void stop_boost();
void start_vacation();
void stop_vacation();
void update_status_led();
#ifdef STATUS_LED_PIN
void toggleLed() {
  digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
}
#endif

void perform_loopback_test() {
    TelnetStream.println("\n--- HARDWARE LOOPBACK TEST ---");
    TelnetStream.println("1. Disconnect the RS485 Module.");
    TelnetStream.printf("2. Connect a wire directly between GPIO %d and GPIO %d.\n", RS485_RX_PIN, RS485_TX_PIN);
    TelnetStream.println("Testing...");

    RS485.end();
    RS485.begin(WAVIN_BAUD_RATE, WAVIN_CONFIG, RS485_RX_PIN, RS485_TX_PIN);
    
    RS485.write(0x55); // Send 'U'
    RS485.flush();
    delay(50);

    if (RS485.available()) {
        int b = RS485.read();
        TelnetStream.printf("Result: Received %02X. ", b);
        if (b == 0x55) TelnetStream.println("SUCCESS! ESP32 UART is working.");
        else TelnetStream.println("FAILURE! Received wrong byte.");
    } else {
        TelnetStream.println("FAILURE! No data received. Check pins.");
    }
    TelnetStream.println("------------------------------\n");
    
    // Restore normal operation
    RS485.end();
}

// --- Setup ---
void setup() {
  // Serial.begin(115200); // UART logging disabled
  // Serial.println("\nStarting Wavin AHC 9000 Gateway...");

  // Check for Factory Reset (Hold Boot Button GPIO 0 on startup)
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
      #ifdef STATUS_LED_PIN
      pinMode(STATUS_LED_PIN, OUTPUT);
      digitalWrite(STATUS_LED_PIN, HIGH); // Solid ON during reset hold
      #endif
      delay(3000); // Wait 3 seconds to confirm hold
      if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
          preferences.begin("wavin", false);
          preferences.clear();
          configManager.reset();
          ESP.restart();
      }
      #ifdef STATUS_LED_PIN
      digitalWrite(STATUS_LED_PIN, LOW);
      #endif
  }

  #ifdef STATUS_LED_PIN
  pinMode(STATUS_LED_PIN, OUTPUT);
  #endif

  // Initialize ConfigManager (loads settings from NVS)
  configManager.begin();
  
  setup_wifi();
  if (configManager.enable_telnet) {
      TelnetStream.begin();
      TelnetStream.println("\nStarting Wavin AHC 9000 Gateway...");
      TelnetStream.printf("RS485 Config: RX=%d, TX=%d, DE=%d\n", RS485_RX_PIN, RS485_TX_PIN, RS485_DE_RE_PIN);
      TelnetStream.println("Loaded Room Names:");
      for (int i = 0; i < WAVIN_CHANNELS; i++) {
          TelnetStream.printf("  CH %d: %s\n", i, configManager.room_names[i]);
      }
  }

  #ifdef RUN_LOOPBACK_TEST
  perform_loopback_test();
  #endif

  // Wavin Controller Init (Initialize Serial AFTER any loopback tests)
  wavin.begin(RS485_RX_PIN, RS485_TX_PIN);

  // MQTT Setup
  if (strlen(configManager.mqtt_server) > 0) {
    mqtt.setServer(configManager.mqtt_server, atoi(configManager.mqtt_port));
    mqtt.setCallback(mqtt_callback);
    mqtt.setBufferSize(8192); // Increase buffer for large HA Discovery payloads
  }

  // OTA Setup
  ArduinoOTA.setHostname("WavinGateway");
  ArduinoOTA.begin();

  webInterface.begin();

  // Configure Time
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", configManager.timezone, 1);
  tzset();
}

// --- Main Loop ---
void loop() {
  ArduinoOTA.handle();
  webInterface.handleClient();
  
  if (!mqtt.connected()) {
    reconnect_mqtt();
  }
  mqtt.loop();

  #ifdef STATUS_LED_PIN
  update_status_led();
  #endif

  // Handle Wavin Communication & Publish on Update
  if (wavin.loop()) { // This runs every 10 seconds
      publish_status();
  }

  if (configManager.enable_maintenance) check_maintenance();
  if (configManager.enable_boost) check_boost();

  // Publish Discovery only after first scan is complete and we know which channels are active
  if (mqtt.connected() && wavin.isInitialized() && !discovery_published) {
      publish_discovery();
      discovery_published = true;
  }

  if (mqtt.connected() && millis() - last_telemetry_time > 300000) { // 5 minutes
      last_telemetry_time = millis();
      publish_telemetry();
  }

  // Sync Clock once
  if (!clock_synced && WiFi.status() == WL_CONNECTED) {
      static unsigned long last_sync_attempt = 0;
      if (millis() - last_sync_attempt > 30000) { // Retry every 30s
          last_sync_attempt = millis();
          
          time_t now = time(nullptr);
          struct tm timeinfo;
          localtime_r(&now, &timeinfo);
          // Check if time is valid (e.g. > 2020)
          if (timeinfo.tm_year > (2020 - 1900)) {
              char timeStr[64];
              strftime(timeStr, sizeof(timeStr), "%A, %B %d %Y %H:%M:%S", &timeinfo);
              TelnetStream.print("Syncing Wavin Clock to: ");
              TelnetStream.println(timeStr);
              // tm_year is years since 1900, tm_mon is 0-11, tm_wday is 0-6 (Sun=0)
              // Wavin expects: Year (full), Month (1-12), Day (1-31), Wday (0-6, Mon=0..Sun=6)
              // Map tm_wday (Sun=0...Sat=6) to Wavin (Mon=0...Sun=6)
              uint16_t wday = (timeinfo.tm_wday + 6) % 7;
              if (wavin.syncClock(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, wday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec)) {
                  TelnetStream.println("Clock Synced Successfully");
                  clock_synced = true;
              } else {
                  TelnetStream.println("Clock Sync Failed");
              }
          }
      }
  }
}

// --- WiFiManager Implementation ---
bool shouldSaveConfig = false;
void saveConfigCallback() {
  shouldSaveConfig = true;
}

void configModeCallback(WiFiManager *myWiFiManager) {
  #ifdef STATUS_LED_PIN
  ledTicker.attach(0.1, toggleLed); // Fast blink (10Hz) for AP Mode
  #endif
}

void setup_wifi() {
  WiFiManager wm;
  wm.setDebugOutput(false); // Disable WiFiManager debug output
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.setAPCallback(configModeCallback);

  // Custom CSS for nicer UI and Help Text
  const char* custom_css = "<meta charset='UTF-8'><style>"
    "body { font-family: sans-serif; background-color: #f0f2f5; padding: 20px; }"
    "h1 { color: #005eb8; }"
    "h2 { color: #005eb8; border-bottom: 1px solid #ccc; padding-bottom: 5px; margin-top: 25px; font-size: 1.2em; }"
    ".help { font-size: 0.85em; color: #666; margin-top: -12px; margin-bottom: 15px; display: block; line-height: 1.4; }"
    "input[type='text'], input[type='password'], input[type='number'] { width: 100%; padding: 8px; margin-bottom: 10px; border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box; }"
    "button { background-color: #005eb8 !important; border-radius: 4px !important; }"
    "</style>";
  wm.setCustomHeadElement(custom_css);

  // --- MQTT Section ---
  WiFiManagerParameter p_mqtt_head("<h2>MQTT Configuration</h2>");
  WiFiManagerParameter custom_mqtt_server("server", "MQTT Broker IP", configManager.mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "MQTT Port", configManager.mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_user("user", "MQTT Username", configManager.mqtt_user, 32);
  WiFiManagerParameter custom_mqtt_pass("pass", "MQTT Password", configManager.mqtt_pass, 32);
  WiFiManagerParameter custom_mqtt_disc("discovery", "MQTT Discovery Prefix", configManager.mqtt_discovery_prefix, 32);
  
  // --- System Section ---
  WiFiManagerParameter p_sys_head("<h2>System Settings</h2>");
  WiFiManagerParameter custom_timezone("timezone", "Timezone (POSIX)", configManager.timezone, 64);
  WiFiManagerParameter p_tz_help("<div class='help'>e.g., CET-1CEST,M3.5.0,M10.5.0/3 for Europe/Copenhagen</div>");
  
  char en_floor_str[2]; sprintf(en_floor_str, "%d", configManager.enable_floor_sensors);
  WiFiManagerParameter custom_en_floor("en_floor", "Enable Floor Sensors (0/1)", en_floor_str, 2);

  // --- Master Control Section ---
  WiFiManagerParameter p_master_head("<h2>Master Climate Entity</h2>");
  WiFiManagerParameter custom_master_rooms("master_rooms", "Master Rooms (e.g. 2-16)", configManager.master_rooms_str, 64);
  WiFiManagerParameter p_master_help("<div class='help'>Define which rooms are controlled by the Master Climate entity in Home Assistant.<br>Supports ranges (e.g., <b>2-16</b>) and lists (e.g., <b>1, 5, 7</b>).</div>");

  // --- Master Features Section ---
  WiFiManagerParameter p_mfeat_head("<h2>Master Features</h2>");
  WiFiManagerParameter p_mfeat_help("<div class='help'>Select which Master controls to expose to Home Assistant.</div>");
  
  char en_m_alarm_str[2]; sprintf(en_m_alarm_str, "%d", configManager.enable_master_alarm);
  WiFiManagerParameter custom_en_m_alarm("en_m_alarm", "Enable Master Alarms (0/1)", en_m_alarm_str, 2);
  
  char en_m_hyst_str[2]; sprintf(en_m_hyst_str, "%d", configManager.enable_master_hysteresis);
  WiFiManagerParameter custom_en_m_hyst("en_m_hyst", "Enable Master Hysteresis (0/1)", en_m_hyst_str, 2);
  
  char en_m_lock_str[2]; sprintf(en_m_lock_str, "%d", configManager.enable_master_lock);
  WiFiManagerParameter custom_en_m_lock("en_m_lock", "Enable Master Lock (0/1)", en_m_lock_str, 2);
  WiFiManagerParameter custom_lock_rooms("lock_rooms", "Lock Rooms Mask (e.g. 1-16)", configManager.lock_rooms_str, 64);
  WiFiManagerParameter custom_eco_rooms("eco_rooms", "Eco Rooms Mask (e.g. 1-16)", configManager.eco_rooms_str, 64);
  WiFiManagerParameter custom_comfort_rooms("comfort_rooms", "Comfort Rooms Mask (e.g. 1-16)", configManager.comfort_rooms_str, 64);
  
  char en_m_minmax_str[2]; sprintf(en_m_minmax_str, "%d", configManager.enable_master_minmax);
  WiFiManagerParameter custom_en_m_minmax("en_m_minmax", "Enable Master Min/Max Temp (0/1)", en_m_minmax_str, 2);

  char en_m_standby_str[2]; sprintf(en_m_standby_str, "%d", configManager.enable_master_standby);
  WiFiManagerParameter custom_en_m_standby("en_m_standby", "Enable Master Standby (0/1)", en_m_standby_str, 2);

  // --- Boost Section ---
  WiFiManagerParameter p_boost_head("<h2>Boost Feature</h2>");
  char en_boost_str[2]; sprintf(en_boost_str, "%d", configManager.enable_boost);
  WiFiManagerParameter custom_en_boost("en_boost", "Enable Boost (1=Yes, 0=No)", en_boost_str, 2);
  char boost_str[8]; sprintf(boost_str, "%.1f", configManager.boost_temp);
  WiFiManagerParameter custom_boost("boost", "Boost Target Temp (C)", boost_str, 6);
  WiFiManagerParameter custom_boost_rooms("boost_rooms", "Boost Rooms (e.g. 2-16)", configManager.boost_rooms_str, 64);
  WiFiManagerParameter p_boost_help("<div class='help'>Boost raises the temperature for 1 hour. Select which rooms should be boosted.</div>");

  // --- Vacation Section ---
  WiFiManagerParameter p_vac_head("<h2>Vacation Feature</h2>");
  char en_vac_str[2]; sprintf(en_vac_str, "%d", configManager.enable_vacation);
  WiFiManagerParameter custom_en_vac("en_vac", "Enable Vacation (1=Yes, 0=No)", en_vac_str, 2);
  WiFiManagerParameter custom_vac_rooms("vac_rooms", "Vacation Rooms (e.g. 1-16)", configManager.vacation_rooms_str, 64);
  WiFiManagerParameter p_vac_help("<div class='help'>Vacation mode lowers the temperature for a set period. Select which rooms are affected.</div>");

  // --- Maintenance Section ---
  WiFiManagerParameter p_maint_head("<h2>Valve Maintenance</h2>");
  char en_maint_str[2]; sprintf(en_maint_str, "%d", configManager.enable_maintenance);
  WiFiManagerParameter custom_en_maint("en_maint", "Enable Maintenance (1=Yes, 0=No)", en_maint_str, 2);
  WiFiManagerParameter custom_maint_rooms("maint_rooms", "Maintenance Rooms (e.g. 1-16)", configManager.maint_rooms_str, 64);
  WiFiManagerParameter p_maint_help("<div class='help'>Automatically exercises valves once a week to prevent sticking.</div>");

  // Add parameters in order
  wm.addParameter(&p_mqtt_head);
  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_mqtt_port);
  wm.addParameter(&custom_mqtt_user);
  wm.addParameter(&custom_mqtt_pass);
  wm.addParameter(&custom_mqtt_disc);
  
  wm.addParameter(&p_sys_head);
  wm.addParameter(&custom_timezone);
  wm.addParameter(&p_tz_help);
  wm.addParameter(&custom_en_floor);
  
  wm.addParameter(&p_master_head);
  wm.addParameter(&custom_master_rooms);
  wm.addParameter(&p_master_help);
  
  wm.addParameter(&p_mfeat_head);
  wm.addParameter(&p_mfeat_help);
  wm.addParameter(&custom_en_m_alarm);
  wm.addParameter(&custom_en_m_hyst);
  wm.addParameter(&custom_en_m_lock);
  wm.addParameter(&custom_lock_rooms);
  wm.addParameter(&custom_eco_rooms);
  wm.addParameter(&custom_comfort_rooms);
  wm.addParameter(&custom_en_m_minmax);
  wm.addParameter(&custom_en_m_standby);

  wm.addParameter(&p_boost_head);
  wm.addParameter(&custom_en_boost);
  wm.addParameter(&custom_boost);
  wm.addParameter(&custom_boost_rooms);
  wm.addParameter(&p_boost_help);

  wm.addParameter(&p_vac_head);
  wm.addParameter(&custom_en_vac);
  wm.addParameter(&custom_vac_rooms);
  wm.addParameter(&p_vac_help);

  wm.addParameter(&p_maint_head);
  wm.addParameter(&custom_en_maint);
  wm.addParameter(&custom_maint_rooms);
  wm.addParameter(&p_maint_help);

  // Room Name Parameters
  WiFiManagerParameter p_rooms_head("<h2>Room Names</h2>");
  WiFiManagerParameter p_rooms_help("<div class='help'>Name your rooms (e.g., Living Room, Kitchen). Leave empty to keep current.</div>");
  wm.addParameter(&p_rooms_head);
  wm.addParameter(&p_rooms_help);

  WiFiManagerParameter* room_params[WAVIN_CHANNELS];
  char room_ids[WAVIN_CHANNELS][10];
  char room_labels[WAVIN_CHANNELS][20];

  for(int i=0; i<WAVIN_CHANNELS; i++) {
    sprintf(room_ids[i], "room_%d", i);
    sprintf(room_labels[i], "Room %d", i + 1);
    room_params[i] = new WiFiManagerParameter(room_ids[i], room_labels[i], configManager.room_names[i], 31);
    wm.addParameter(room_params[i]);
  }

  #ifdef STATUS_LED_PIN
  ledTicker.attach(0.5, toggleLed); // Medium blink (2Hz) while trying to connect
  #endif

  if (!wm.autoConnect("WavinGateway_Setup")) {
    // Serial.println("Failed to connect and hit timeout");
    ESP.restart();
  }

  #ifdef STATUS_LED_PIN
  ledTicker.detach(); // Stop blinking, loop() will take over
  #endif

  // Save settings if changed
  if (shouldSaveConfig) {
    strcpy(configManager.mqtt_server, custom_mqtt_server.getValue());
    strcpy(configManager.mqtt_port, custom_mqtt_port.getValue());
    strcpy(configManager.mqtt_user, custom_mqtt_user.getValue());
    strcpy(configManager.mqtt_pass, custom_mqtt_pass.getValue());
    strcpy(configManager.mqtt_discovery_prefix, custom_mqtt_disc.getValue());
    strcpy(configManager.timezone, custom_timezone.getValue());
    configManager.enable_floor_sensors = atoi(custom_en_floor.getValue());

    strcpy(configManager.master_rooms_str, custom_master_rooms.getValue());
    strcpy(configManager.boost_rooms_str, custom_boost_rooms.getValue());
    strcpy(configManager.vacation_rooms_str, custom_vac_rooms.getValue());
    strcpy(configManager.maint_rooms_str, custom_maint_rooms.getValue());
    strcpy(configManager.lock_rooms_str, custom_lock_rooms.getValue());
    strcpy(configManager.eco_rooms_str, custom_eco_rooms.getValue());
    strcpy(configManager.comfort_rooms_str, custom_comfort_rooms.getValue());
    
    configManager.boost_temp = atof(custom_boost.getValue());
    
    configManager.enable_maintenance = atoi(custom_en_maint.getValue());
    configManager.enable_boost = atoi(custom_en_boost.getValue());
    configManager.enable_vacation = atoi(custom_en_vac.getValue());
    
    configManager.enable_master_alarm = atoi(custom_en_m_alarm.getValue());
    configManager.enable_master_hysteresis = atoi(custom_en_m_hyst.getValue());
    configManager.enable_master_lock = atoi(custom_en_m_lock.getValue());
    configManager.enable_master_minmax = atoi(custom_en_m_minmax.getValue());
    configManager.enable_master_standby = atoi(custom_en_m_standby.getValue());

    for(int i=0; i<WAVIN_CHANNELS; i++) {
      if (strlen(room_params[i]->getValue()) > 0) {
        strcpy(configManager.room_names[i], room_params[i]->getValue());
      }
    }
    configManager.save();
  }

  // Serial.println("Connected to WiFi");

  // Cleanup heap
  for(int i=0; i<WAVIN_CHANNELS; i++) delete room_params[i];
}

// --- MQTT Logic ---
void reconnect_mqtt() {
  static unsigned long lastReconnectAttempt = 0;
  if (millis() - lastReconnectAttempt < MQTT_RECONNECT_DELAY) return;
  lastReconnectAttempt = millis();
  
  if (strlen(configManager.mqtt_server) == 0) return;

  String mac = WiFi.macAddress();
  String lwt_topic = "wavin/" + mac + "/LWT";
  String clientId = "WavinGateway-" + WiFi.macAddress();
  if (mqtt.connect(clientId.c_str(), configManager.mqtt_user, configManager.mqtt_pass, lwt_topic.c_str(), 0, true, "Offline")) {
    mqtt_reconnect_count++;
    TelnetStream.println("MQTT Connected");
    mqtt.publish(lwt_topic.c_str(), "Online", true);
    
    // Subscribe to all commands
    String baseTopic = "wavin/" + WiFi.macAddress() + "/+/";
    mqtt.subscribe((baseTopic + "set_temp").c_str());
    mqtt.subscribe((baseTopic + "set_mode").c_str());
    mqtt.subscribe((baseTopic + "set_preset").c_str());
    mqtt.subscribe((baseTopic + "set_master_comfort_temp").c_str());
    mqtt.subscribe((baseTopic + "set_master_eco_temp").c_str());
    mqtt.subscribe((baseTopic + "set_holiday_temp").c_str());
    mqtt.subscribe((baseTopic + "set_lock").c_str());
    mqtt.subscribe((baseTopic + "set_min_temp").c_str());
    mqtt.subscribe((baseTopic + "set_max_temp").c_str());
    mqtt.subscribe((baseTopic + "set_alarm_low").c_str());
    mqtt.subscribe((baseTopic + "set_alarm_high").c_str());
    mqtt.subscribe((baseTopic + "set_standby").c_str());
    mqtt.subscribe((baseTopic + "set_hysteresis").c_str());
    mqtt.subscribe((baseTopic + "set_defaults").c_str());
    
    if (configManager.enable_maintenance) {
        mqtt.subscribe((baseTopic + "set_maint_day").c_str());
        mqtt.subscribe((baseTopic + "set_maint_hour").c_str());
        mqtt.subscribe((baseTopic + "set_maint_minute").c_str());
        mqtt.subscribe((baseTopic + "set_maint_dur").c_str());
        mqtt.subscribe((baseTopic + "run_maint").c_str());
    }
    if (configManager.enable_boost) {
        mqtt.subscribe((baseTopic + "run_boost").c_str());
        mqtt.subscribe((baseTopic + "set_boost_temp").c_str());
    }
    
    publish_discovery();
    publish_status(); // Force immediate update
    publish_telemetry(); // Send initial telemetry
  }
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  const size_t MAX_MSG_LEN = 256;
  size_t safeLen = (length < MAX_MSG_LEN) ? length : MAX_MSG_LEN - 1;
  char messageBuf[MAX_MSG_LEN];
  memcpy(messageBuf, payload, safeLen);
  messageBuf[safeLen] = '\0';
  String message = String(messageBuf);
  String strTopic = String(topic);
  
  // Parse: wavin/<MAC>/<CHANNEL>/<COMMAND>
  int firstSlash = strTopic.indexOf('/');
  int secondSlash = strTopic.indexOf('/', firstSlash + 1);
  int thirdSlash = strTopic.indexOf('/', secondSlash + 1);
  
  if (thirdSlash == -1) return;
  
  String channelStr = strTopic.substring(secondSlash + 1, thirdSlash);
  String command = strTopic.substring(thirdSlash + 1);

  // Handle Master Entity
  if (channelStr == "master") {
      for (int i = 0; i < WAVIN_CHANNELS; i++) {
          if (!((configManager.master_mask >> i) & 1)) continue;
          // Only control active thermostats
          WavinChannel data = wavin.getChannelData(i);
          if (!data.hasThermostat) continue;

          bool cmdSent = false;

          if (command == "set_temp") {
              if (data.mode == "off") wavin.setStandbyTemp(i, message.toFloat());
              else wavin.setTargetTemp(i, message.toFloat());
              cmdSent = true;
          }
          else if (command == "set_mode") { wavin.setMode(i, message); cmdSent = true; }
          else if (command == "set_preset") {
              if (message == "eco") {
                  if ((configManager.eco_mask >> i) & 1) { wavin.setMode(i, "eco"); cmdSent = true; }
              } else if (message == "comfort") {
                  if ((configManager.comfort_mask >> i) & 1) { wavin.setMode(i, "comfort"); cmdSent = true; }
              } else if (message == "away") {
                  // This is a global action, but we can trigger it from any master room
                  start_vacation();
                  cmdSent = false; // Don't delay in the loop for this
              } else if (message == "none") {
                  // If we are in vacation mode, stop it. Otherwise, set to heat.
                  if (vacation_active) stop_vacation();
                  else wavin.setMode(i, "heat");
                  cmdSent = true;
              }
          }
          else if (configManager.enable_master_minmax && command == "set_min_temp") { wavin.setMinTemp(i, message.toFloat()); cmdSent = true; }
          else if (configManager.enable_master_minmax && command == "set_max_temp") { wavin.setMaxTemp(i, message.toFloat()); cmdSent = true; }
          else if (configManager.enable_master_alarm && command == "set_alarm_low") { wavin.setAlarmLowTemp(i, message.toFloat()); cmdSent = true; }
          else if (configManager.enable_master_alarm && command == "set_alarm_high") { wavin.setAlarmHighTemp(i, message.toFloat()); cmdSent = true; }
          else if (configManager.enable_master_standby && command == "set_standby") { wavin.setStandbyTemp(i, message.toFloat()); cmdSent = true; }
          else if (configManager.enable_master_hysteresis && command == "set_hysteresis") { wavin.setHysteresis(i, message.toFloat()); cmdSent = true; }
          
          if (cmdSent) delay(50); // Only delay if we actually sent a command
      }

      // Handle global commands that apply to all channels (run exactly once, not per master thermostat)
      if (command == "set_master_eco_temp") {
          configManager.master_eco_temp = message.toFloat();
          configManager.save();
          for (int j = 0; j < WAVIN_CHANNELS; j++) {
              if (!((configManager.eco_mask >> j) & 1)) continue;
              WavinChannel data = wavin.getChannelData(j);
              if (data.hasThermostat) {
                  wavin.setEcoTemp(j, configManager.master_eco_temp);
                  delay(50);
              }
          }
      }
      else if (command == "set_master_comfort_temp") {
          configManager.master_comfort_temp = message.toFloat();
          configManager.save();
          for (int j = 0; j < WAVIN_CHANNELS; j++) {
              if (!((configManager.comfort_mask >> j) & 1)) continue;
              WavinChannel data = wavin.getChannelData(j);
              if (data.hasThermostat) {
                  wavin.setComfortTemp(j, configManager.master_comfort_temp);
                  delay(50);
              }
          }
      }
      else if (command == "set_holiday_temp") {
          float temp = message.toFloat();
          for (int j = 0; j < WAVIN_CHANNELS; j++) {
              WavinChannel data = wavin.getChannelData(j);
              if (data.hasThermostat) {
                  wavin.setHolidayTemp(j, temp);
                  delay(50);
              }
          }
      }

      // Handle Lock separately due to custom mask
      if (configManager.enable_master_lock && command == "set_lock") {
          for (int i = 0; i < WAVIN_CHANNELS; i++) {
              if (!((configManager.lock_mask >> i) & 1)) continue;
              WavinChannel data = wavin.getChannelData(i);
              if (data.hasThermostat) {
                  wavin.setIntLock(i, (message == "ON"));
                  delay(50);
              }
          }
      }
      
      // Maintenance Commands
      if (configManager.enable_maintenance && command == "set_maint_day") {
          for (int i=0; i<9; i++) {
              if (message == days_of_week[i]) {
                  configManager.maint_day = i;
                  preferences.putInt("maint_day", configManager.maint_day);
                  publish_status(); // Update attributes/state
                  break;
              }
          }
      }
      else if (configManager.enable_maintenance && command == "set_maint_hour") {
          configManager.maint_hour = message.toInt();
          if (configManager.maint_hour < 0) configManager.maint_hour = 0;
          if (configManager.maint_hour > 23) configManager.maint_hour = 23;
          preferences.putInt("maint_hour", configManager.maint_hour);
          publish_status();
      }
      else if (configManager.enable_maintenance && command == "set_maint_minute") {
          configManager.maint_minute = message.toInt();
          if (configManager.maint_minute < 0) configManager.maint_minute = 0;
          if (configManager.maint_minute > 59) configManager.maint_minute = 59;
          preferences.putInt("maint_minute", configManager.maint_minute);
          publish_status();
      }
      else if (configManager.enable_maintenance && command == "set_maint_dur") {
          configManager.maint_duration = message.toInt();
          if (configManager.maint_duration < 1) configManager.maint_duration = 1;
          if (configManager.maint_duration > 60) configManager.maint_duration = 60;
          preferences.putInt("maint_dur", configManager.maint_duration);
          publish_status();
      }
      else if (configManager.enable_maintenance && command == "run_maint") {
          if (message == "PRESS") start_maintenance();
      }
      else if (configManager.enable_boost && command == "run_boost") {
          if (message == "PRESS") start_boost();
      }
      else if (configManager.enable_boost && command == "set_boost_temp") {
          configManager.boost_temp = message.toFloat();
          if (configManager.boost_temp < 6.0) configManager.boost_temp = 6.0;
          if (configManager.boost_temp > 40.0) configManager.boost_temp = 40.0;
          preferences.putFloat("boost_temp", configManager.boost_temp);
          if (boost_active) {
              for (int i = 0; i < WAVIN_CHANNELS; i++) {
                   if (!((configManager.master_mask >> i) & 1)) continue;
                   WavinChannel data = wavin.getChannelData(i);
                   if (data.hasThermostat) wavin.setTargetTemp(i, configManager.boost_temp);
              }
          }
          publish_status();
      }
      else if (command == "set_defaults") {
          if (message == "PRESS") {
              TelnetStream.println("Resetting Master channels to defaults...");
              for (int i = 0; i < WAVIN_CHANNELS; i++) {
                   if (!((configManager.master_mask >> i) & 1)) continue;
                   WavinChannel data = wavin.getChannelData(i);
                   if (!data.hasThermostat) continue;
                   
                   wavin.setHysteresis(i, 0.2); delay(50);
                   wavin.setStandbyTemp(i, 6.0); delay(50);
                   wavin.setMinTemp(i, 6.0); delay(50);
                   wavin.setMaxTemp(i, 40.0); delay(50);
                   wavin.setAlarmLowTemp(i, 3.0); delay(50);
                   wavin.setAlarmHighTemp(i, 50.0); delay(50);
              }
              publish_status();
          }
      }
      TelnetStream.println("Master command received: " + command);
      publish_status();
      return;
  }

  int channel = channelStr.toInt() - 1; // Convert 1-based topic to 0-based index
  
  if (channel < 0 || channel >= WAVIN_CHANNELS) return;

  if (command == "set_temp") {
      WavinChannel chData = wavin.getChannelData(channel);
      if (chData.mode == "off") wavin.setStandbyTemp(channel, message.toFloat());
      else wavin.setTargetTemp(channel, message.toFloat());
  }
  else if (command == "set_mode") wavin.setMode(channel, message);
  else if (command == "set_preset") {
      if (message == "eco") wavin.setMode(channel, "eco");
      else if (message == "comfort") wavin.setMode(channel, "comfort");
      else if (message == "none") wavin.setMode(channel, "heat");
  }
  else if (command == "set_lock") wavin.setIntLock(channel, (message == "ON"));
  else if (command == "set_min_temp") wavin.setMinTemp(channel, message.toFloat());
  else if (command == "set_max_temp") wavin.setMaxTemp(channel, message.toFloat());
  else if (command == "set_alarm_low") wavin.setAlarmLowTemp(channel, message.toFloat());
  else if (command == "set_alarm_high") wavin.setAlarmHighTemp(channel, message.toFloat());
  else if (command == "set_standby") wavin.setStandbyTemp(channel, message.toFloat());
  else if (command == "set_hysteresis") wavin.setHysteresis(channel, message.toFloat());
  
  TelnetStream.println("Command received: " + command + " for channel " + String(channel + 1));
  // Force immediate update
  publish_status();
}

void publish_status() {
  String mac = WiFi.macAddress();
  WavinDeviceInfo info = wavin.getDeviceInfo();

  for (int i = 0; i < WAVIN_CHANNELS; i++) {
    WavinChannel data = wavin.getChannelData(i);
    // Only publish if channel is active/assigned
    if (!data.hasThermostat) continue;

    String base = "wavin/" + mac + "/" + String(i + 1);
    
    mqtt.publish((base + "/current_temp").c_str(), String(data.currentTemp, 1).c_str());
    mqtt.publish((base + "/target_temp").c_str(), String(data.targetTemp, 1).c_str());
    mqtt.publish((base + "/battery").c_str(), String(data.batteryLevel).c_str());
    mqtt.publish((base + "/rssi").c_str(), String(data.rssi, 0).c_str());
    mqtt.publish((base + "/current_draw").c_str(), String(g_wavinChannelCurrent[i], 0).c_str());
    mqtt.publish((base + "/valve").c_str(), data.outputActive ? "ON" : "OFF");
    mqtt.publish((base + "/lock").c_str(), data.intLock ? "ON" : "OFF");
    
    DynamicJsonDocument doc(2048);
    doc["room"] = configManager.room_names[i];
    
    // Map Wavin modes to HA Mode + Preset, with vacation taking precedence
    if (vacation_active && ((configManager.vacation_mask >> i) & 1)) {
        doc["mode"] = "heat"; // Vacation is a low-heat mode
        doc["preset_mode"] = "away";
    } else if (data.mode == "off") {
        doc["mode"] = "off";
        doc["preset_mode"] = "none";
    } else {
        doc["mode"] = "heat";
        if (data.mode == "eco") doc["preset_mode"] = "eco";
        else if (data.mode == "comfort") doc["preset_mode"] = "comfort";
        else doc["preset_mode"] = "none";
    }

    doc["min_temp"] = data.minTemp;
    doc["max_temp"] = data.maxTemp;
    doc["temp_low"] = data.minTemp;
    doc["temp_high"] = data.maxTemp;
    doc["alarm_low"] = data.alarmLowTemp;
    doc["alarm_high"] = data.alarmHighTemp;
    doc["standby"] = data.standbyTemp;
    doc["hysteresis"] = round(data.hysteresis * 10.0) / 10.0;
    doc["lost"] = data.isLost;
    doc["battery_level"] = data.batteryLevel;
    doc["rssi"] = data.rssi;
    doc["valve_active"] = data.outputActive;
    doc["alarm_low_triggered"] = data.alarmLowTriggered;
    doc["alarm_high_triggered"] = data.alarmHighTriggered;
    doc["current_temp"] = data.currentTemp;
    doc["target_temp"] = data.targetTemp;
    doc["comfort_temp"] = data.comfortTemp;
    doc["eco_temp"] = data.ecoTemp;
    doc["valve_current_ma"] = (int)g_wavinChannelCurrent[i];

    if (configManager.enable_floor_sensors) {
        doc["floor_temp"] = data.floorTemp;
    }

    if (info.address != 0) {
        doc["device_name"] = info.deviceName;
        doc["hw_version"] = info.hwVersion;
        doc["sw_version"] = info.swVersion;
        doc["device_address"] = info.address;
        doc["ip_address"] = WiFi.localIP().toString();
    }
    
    String jsonOutput;
    serializeJson(doc, jsonOutput);
    mqtt.publish((base + "/attributes").c_str(), jsonOutput.c_str());
  }

  // Publish Master Status
  float sumCurrent = 0;
  float sumTarget = 0;
  float sumMin = 0;
  float sumMax = 0;
  float sumAlarmLow = 0;
  float sumAlarmHigh = 0;
  float sumStandby = 0;
  float sumHysteresis = 0;
  int count = 0;
  int countEco = 0;
  int countComfort = 0;
  bool anyHeat = false;
  bool anyValve = false;
  bool anyLock = false;
  int activeValves = 0;
  int assignedChannels = 0;

  for (int i = 0; i < WAVIN_CHANNELS; i++) {
      if (!((configManager.master_mask >> i) & 1)) continue;
      WavinChannel data = wavin.getChannelData(i);
      if (!data.hasThermostat) continue;
      
      sumCurrent += data.currentTemp;
      sumTarget += data.targetTemp;
      sumMin += data.minTemp;
      sumMax += data.maxTemp;
      sumAlarmLow += data.alarmLowTemp;
      sumAlarmHigh += data.alarmHighTemp;
      sumStandby += data.standbyTemp;
      sumHysteresis += data.hysteresis;
      if (data.mode == "eco") countEco++;
      if (data.mode == "comfort") countComfort++;
      if (data.mode == "heat" || data.mode == "eco" || data.mode == "comfort") anyHeat = true;
      if (data.outputActive) anyValve = true;
      if (data.intLock) anyLock = true;
      
      assignedChannels++;
      if (data.outputActive) activeValves++;
      count++;
  }

  if (count > 0) {
      String base = "wavin/" + mac + "/master";
      
      // Calculate averages and round Target to nearest 0.5 for cleaner UI
      float avgCurrent = sumCurrent / count;
      float avgTarget = round((sumTarget / count) * 2.0) / 2.0;

      // --- Daily Statistics ---
      time_t now = time(nullptr);
      struct tm timeinfo;
      localtime_r(&now, &timeinfo);

      if (timeinfo.tm_mday != statsDay) {
          dailyMinAvgTemp = avgCurrent;
          dailyMaxAvgTemp = avgCurrent;
          statsDay = timeinfo.tm_mday;
      } else {
          if (avgCurrent < dailyMinAvgTemp) dailyMinAvgTemp = avgCurrent;
          if (avgCurrent > dailyMaxAvgTemp) dailyMaxAvgTemp = avgCurrent;
      }

      // Calculate Heat Demand % (Active Valves / Total Assigned Valves)
      float heatDemand = (assignedChannels > 0) ? ((float)activeValves / assignedChannels) * 100.0 : 0.0;
      mqtt.publish((base + "/heat_demand").c_str(), String(heatDemand, 0).c_str());

      mqtt.publish((base + "/current_temp").c_str(), String(avgCurrent, 1).c_str());
      mqtt.publish((base + "/target_temp").c_str(), String(avgTarget, 1).c_str());
      mqtt.publish((base + "/total_current").c_str(), String(g_wavinTotalCurrent, 0).c_str());
      mqtt.publish((base + "/lock").c_str(), anyLock ? "ON" : "OFF");
      
      DynamicJsonDocument doc(2048);
      doc["mode"] = anyHeat ? "heat" : "off";
      
      String masterPreset = "none";
      if (vacation_active) {
          masterPreset = "away";
      } else if (count > 0 && countEco == count) {
          masterPreset = "eco";
      } else if (count > 0 && countComfort == count) {
          masterPreset = "comfort";
      }
      doc["preset_mode"] = masterPreset;
      
      doc["min_temp"] = round((sumMin / count) * 2.0) / 2.0;
      doc["max_temp"] = round((sumMax / count) * 2.0) / 2.0;
      doc["temp_low"] = round((sumMin / count) * 2.0) / 2.0;
      doc["temp_high"] = round((sumMax / count) * 2.0) / 2.0;
      doc["alarm_low"] = round((sumAlarmLow / count) * 2.0) / 2.0;
      doc["alarm_high"] = round((sumAlarmHigh / count) * 2.0) / 2.0;
      doc["standby"] = round((sumStandby / count) * 2.0) / 2.0;
      doc["hysteresis"] = round((sumHysteresis / count) * 10.0) / 10.0;
      doc["current_temp"] = avgCurrent;
      doc["target_temp"] = avgTarget;
      doc["master_eco_temp"] = configManager.master_eco_temp;
      doc["master_comfort_temp"] = configManager.master_comfort_temp;
      WavinChannel data_ch0 = wavin.getChannelData(0); // Holiday temp is system-wide, get from ch0
      doc["holiday_temp"] = data_ch0.holidayTemp;
      doc["daily_min_avg"] = round(dailyMinAvgTemp * 10.0) / 10.0;
      doc["daily_max_avg"] = round(dailyMaxAvgTemp * 10.0) / 10.0;
      doc["valve_active"] = anyValve;
      doc["heat_demand"] = round(heatDemand);
      doc["total_current_ma"] = (int)g_wavinTotalCurrent;
      
      if (configManager.enable_maintenance) {
          doc["maint_day"] = days_of_week[configManager.maint_day];
          doc["maint_hour"] = configManager.maint_hour;
          doc["maint_minute"] = configManager.maint_minute;
          doc["maint_duration"] = configManager.maint_duration;
          doc["maint_active"] = maint_active;
      }
      if (configManager.enable_boost) {
          doc["boost_active"] = boost_active;
          doc["boost_temp"] = configManager.boost_temp;
      }
      if (configManager.enable_vacation) {
          doc["vacation_active"] = vacation_active;
      }
      
      char time_buf[32];
      strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
      doc["esp_time"] = time_buf;

      if (info.address != 0) {
          doc["device_name"] = info.deviceName;
          doc["hw_version"] = info.hwVersion;
          doc["sw_version"] = info.swVersion;
          doc["device_address"] = info.address;
          doc["ip_address"] = WiFi.localIP().toString();
      }

      String jsonOutput;
      serializeJson(doc, jsonOutput);
      mqtt.publish((base + "/attributes").c_str(), jsonOutput.c_str());
  }

  // Publish Last Maintenance Sensor State
  if (configManager.enable_maintenance) {
      String maint_base = "wavin/" + mac + "/master/maintenance";
      mqtt.publish((maint_base + "/last_run").c_str(), configManager.last_maint_str.c_str(), true);
  }

  TelnetStream.println("Status published");
}

String createObjectId(String name) {
    String id = "wavin_" + name;
    // Transliterate Danish characters (UTF-8)
    id.replace("\xC3\xA6", "ae"); // æ
    id.replace("\xC3\x86", "ae"); // Æ
    id.replace("\xC3\xB8", "o");  // ø
    id.replace("\xC3\x98", "o");  // Ø
    id.replace("\xC3\xA5", "a");  // å
    id.replace("\xC3\x85", "a");  // Å
    
    id.toLowerCase();
    id.replace(" ", "_");
    String out = "";
    for(unsigned int i=0; i<id.length(); i++) {
        char c = id.charAt(i);
        if(isAlphaNumeric(c) || c == '_') out += c;
    }
    return out;
}

void publish_discovery() {
  String mac = WiFi.macAddress();
  String cleanMac = mac;
  cleanMac.replace(":", "");
  String lwt_topic = "wavin/" + mac + "/LWT";

  for (int i = 0; i < WAVIN_CHANNELS; i++) {
    // Check if channel is valid (simple check: has target temp)
    WavinChannel data = wavin.getChannelData(i);
    if (!data.hasThermostat) continue;

    String channel = String(i + 1);
    String device_id = "wavin_" + cleanMac + "_ch" + channel;
    String main_device_id = "wavin_" + cleanMac; // Single Device ID for grouping
    String base_topic = "wavin/" + mac + "/" + channel;
    
    // Device Info
    DynamicJsonDocument doc(8192);
    JsonObject device;

    // 1. Climate
    device = doc.createNestedObject("dev");
    device["ids"] = main_device_id;
    device["name"] = "Wavin AHC 9000 Gateway";
    device["mdl"] = "AHC 9000";
    device["mf"] = "Wavin";

    doc["name"] = String(configManager.room_names[i]); // Name entity as the Room Name
    doc["default_entity_id"] = "climate." + createObjectId(String(configManager.room_names[i]));
    doc["uniq_id"] = device_id + "_climate";
    doc["avty_t"] = lwt_topic;
    doc["pl_avail"] = "Online";
    doc["pl_not_avail"] = "Offline";
    doc["dev_cla"] = "climate";
    doc["mode_cmd_t"] = base_topic + "/set_mode";
    doc["mode_stat_t"] = base_topic + "/attributes";
    doc["mode_stat_tpl"] = "{{ value_json.mode }}";
    doc["action_topic"] = base_topic + "/attributes";
    doc["action_template"] = "{{ 'heating' if value_json.mode == 'heat' and value_json.target_temp > value_json.current_temp else 'idle' if value_json.mode == 'heat' else 'off' }}";
    
    doc["pr_mode_cmd_t"] = base_topic + "/set_preset";
    doc["pr_mode_stat_t"] = base_topic + "/attributes";
    doc["pr_mode_val_tpl"] = "{{ value_json.preset_mode }}";
    JsonArray presets = doc.createNestedArray("pr_modes");
    presets.add("eco");
    presets.add("comfort");
    presets.add("away");

    doc["json_attr_t"] = base_topic + "/attributes"; // Tie attributes to Climate Entity
    doc["temp_cmd_t"] = base_topic + "/set_temp";
    doc["temp_stat_t"] = base_topic + "/target_temp";
    doc["curr_temp_t"] = base_topic + "/current_temp";
    doc["min_temp"] = 6;
    doc["max_temp"] = 40;
    doc["temp_step"] = 0.5;
    JsonArray modes = doc.createNestedArray("modes");
    modes.add("heat");
    modes.add("off");
    
    String jsonOutput;
    if (doc.overflowed()) {
        TelnetStream.println("ERROR: JSON Discovery payload overflowed! Increase buffer size.");
    }
    serializeJson(doc, jsonOutput);
    
    String topic = String(configManager.mqtt_discovery_prefix) + "/climate/" + device_id + "/config";
    if (mqtt.publish(topic.c_str(), jsonOutput.c_str(), true)) {
        TelnetStream.println("Climate Discovery OK: " + device_id + " (" + String(jsonOutput.length()) + " bytes)");
    } else {
        TelnetStream.println("Climate Discovery FAILED: " + device_id + " (" + String(jsonOutput.length()) + " bytes)");
        TelnetStream.println("Payload: " + jsonOutput); // Print payload to debug
    }
    delay(50); // Give broker a moment

    // 2. Battery
    doc.clear();
    device = doc.createNestedObject("dev");
    device["ids"] = main_device_id;
    device["name"] = "Wavin AHC 9000 Gateway";
    device["mdl"] = "AHC 9000";
    device["mf"] = "Wavin";

    doc["name"] = String(configManager.room_names[i]) + " Battery";
    doc["default_entity_id"] = "sensor." + createObjectId(String(configManager.room_names[i]) + " Battery");
    doc["uniq_id"] = device_id + "_battery";
    doc["dev_cla"] = "battery";
    doc["unit_of_meas"] = "%";
    doc["stat_t"] = base_topic + "/battery";
    doc["ent_cat"] = "diagnostic";
    jsonOutput = "";
    serializeJson(doc, jsonOutput);
    mqtt.publish((String(configManager.mqtt_discovery_prefix) + "/sensor/" + device_id + "_bat/config").c_str(), jsonOutput.c_str(), true);

    // 3. Valve
    doc.clear();
    device = doc.createNestedObject("dev");
    device["ids"] = main_device_id;
    device["name"] = "Wavin AHC 9000 Gateway";
    device["mdl"] = "AHC 9000";
    device["mf"] = "Wavin";

    doc["name"] = String(configManager.room_names[i]) + " Valve";
    doc["default_entity_id"] = "binary_sensor." + createObjectId(String(configManager.room_names[i]) + " Valve");
    doc["uniq_id"] = device_id + "_valve";
    doc["dev_cla"] = "opening";
    doc["stat_t"] = base_topic + "/valve";
    jsonOutput = "";
    serializeJson(doc, jsonOutput);
    mqtt.publish((String(configManager.mqtt_discovery_prefix) + "/binary_sensor/" + device_id + "_valve/config").c_str(), jsonOutput.c_str(), true);

    // 4. Lock Switch
    doc.clear();
    device = doc.createNestedObject("dev");
    device["ids"] = main_device_id;
    device["name"] = "Wavin AHC 9000 Gateway";
    device["mdl"] = "AHC 9000";
    device["mf"] = "Wavin";

    doc["name"] = String(configManager.room_names[i]) + " Lock";
    doc["default_entity_id"] = "switch." + createObjectId(String(configManager.room_names[i]) + " Lock");
    doc["uniq_id"] = device_id + "_lock";
    doc["ic"] = "mdi:lock";
    doc["cmd_t"] = base_topic + "/set_lock";
    doc["stat_t"] = base_topic + "/lock";
    doc["ent_cat"] = "config";
    jsonOutput = "";
    serializeJson(doc, jsonOutput);
    mqtt.publish((String(configManager.mqtt_discovery_prefix) + "/switch/" + device_id + "_lock/config").c_str(), jsonOutput.c_str(), true);

    // 5. Floor Temp Sensor (Optional)
    if (configManager.enable_floor_sensors) {
        doc.clear();
        device = doc.createNestedObject("dev");
        device["ids"] = main_device_id;
        device["name"] = "Wavin AHC 9000 Gateway";
        device["mdl"] = "AHC 9000";
        device["mf"] = "Wavin";

        doc["name"] = String(configManager.room_names[i]) + " Floor Temp";
        doc["default_entity_id"] = "sensor." + createObjectId(String(configManager.room_names[i]) + " Floor Temp");
        doc["uniq_id"] = device_id + "_floor_temp";
        doc["dev_cla"] = "temperature";
        doc["unit_of_meas"] = "°C";
        doc["stat_t"] = base_topic + "/floor_temp";
        jsonOutput = "";
        serializeJson(doc, jsonOutput);
        mqtt.publish((String(configManager.mqtt_discovery_prefix) + "/sensor/" + device_id + "_floor/config").c_str(), jsonOutput.c_str(), true);
    }
  }

  // Master Climate Entity (Rooms 2-11)
  {
      String device_id = "wavin_" + cleanMac + "_master";
      String base_topic = "wavin/" + mac + "/master";
      
      DynamicJsonDocument doc(8192);
      JsonObject device = doc.createNestedObject("dev");
      device["ids"] = "wavin_" + cleanMac;
      device["name"] = "Wavin AHC 9000 Gateway";
      device["mdl"] = "AHC 9000";
      device["mf"] = "Wavin";
      device["sw"] = wavin.getDeviceInfo().swVersion;
      device["hw"] = wavin.getDeviceInfo().hwVersion;

      doc["name"] = "Master Climate";
      doc["default_entity_id"] = "climate." + createObjectId("Master Climate");
      doc["uniq_id"] = device_id + "_climate";
      doc["avty_t"] = lwt_topic;
      doc["pl_avail"] = "Online";
      doc["pl_not_avail"] = "Offline";
      doc["dev_cla"] = "climate";
      doc["mode_cmd_t"] = base_topic + "/set_mode";
      doc["mode_stat_t"] = base_topic + "/attributes";
      doc["mode_stat_tpl"] = "{{ value_json.mode }}";
      doc["action_topic"] = base_topic + "/attributes";
      doc["action_template"] = "{{ 'heating' if value_json.mode == 'heat' and value_json.target_temp > value_json.current_temp else 'idle' if value_json.mode == 'heat' else 'off' }}";
      
      doc["pr_mode_cmd_t"] = base_topic + "/set_preset";
      doc["pr_mode_stat_t"] = base_topic + "/attributes";
      doc["pr_mode_val_tpl"] = "{{ value_json.preset_mode }}";
      JsonArray presets = doc.createNestedArray("pr_modes");
      presets.add("eco");
      presets.add("comfort");
      presets.add("away");

      doc["json_attr_t"] = base_topic + "/attributes";
      doc["temp_cmd_t"] = base_topic + "/set_temp";
      doc["temp_stat_t"] = base_topic + "/target_temp";
      doc["curr_temp_t"] = base_topic + "/current_temp";
      doc["min_temp"] = 6;
      doc["max_temp"] = 40;
      doc["temp_step"] = 0.5;
      JsonArray modes = doc.createNestedArray("modes");
      modes.add("heat");
      modes.add("off");

      String jsonOutput;
      if (doc.overflowed()) {
          TelnetStream.println("ERROR: Master JSON Discovery payload overflowed!");
      }
      serializeJson(doc, jsonOutput);
      
      String topic = String(configManager.mqtt_discovery_prefix) + "/climate/" + device_id + "/config";
      if (mqtt.publish(topic.c_str(), jsonOutput.c_str(), true)) {
          TelnetStream.println("Master Climate Discovery OK (" + String(jsonOutput.length()) + " bytes)");
      } else {
          TelnetStream.println("Master Climate Discovery FAILED (" + String(jsonOutput.length()) + " bytes)");
          TelnetStream.println("Payload: " + jsonOutput);
      }
      delay(50);

      // Master Holiday Temp
      doc.clear();
      device = doc.createNestedObject("dev");
      device["ids"] = "wavin_" + cleanMac;
      device["name"] = "Wavin AHC 9000 Gateway";
      device["mdl"] = "AHC 9000";
      device["mf"] = "Wavin";

      doc["name"] = "Master Holiday Temp";
      doc["default_entity_id"] = "number." + createObjectId("Master Holiday Temp");
      doc["uniq_id"] = device_id + "_holiday_temp";
      doc["ic"] = "mdi:airplane-takeoff";
      doc["cmd_t"] = base_topic + "/set_holiday_temp";
      doc["stat_t"] = base_topic + "/attributes";
      doc["val_tpl"] = "{{ value_json.holiday_temp }}";
      doc["min"] = 6; doc["max"] = 20; doc["step"] = 0.5;
      doc["ent_cat"] = "config";
      jsonOutput = "";
      serializeJson(doc, jsonOutput);
      mqtt.publish((String(configManager.mqtt_discovery_prefix) + "/number/" + device_id + "_holiday_temp/config").c_str(), jsonOutput.c_str(), true);

      // Master Min Temp
      if (configManager.enable_master_minmax) {
      doc.clear();
      device = doc.createNestedObject("dev");
      device["ids"] = "wavin_" + cleanMac;
      device["name"] = "Wavin AHC 9000 Gateway";
      device["mdl"] = "AHC 9000";
      device["mf"] = "Wavin";

      doc["name"] = "Master Min Temp Limit";
      doc["default_entity_id"] = "number." + createObjectId("Master Min Temp Limit");
      doc["uniq_id"] = device_id + "_min_temp";
      doc["ic"] = "mdi:thermometer-chevron-down";
      doc["cmd_t"] = base_topic + "/set_min_temp";
      doc["stat_t"] = base_topic + "/attributes";
      doc["val_tpl"] = "{{ value_json.min_temp }}";
      doc["min"] = 6; doc["max"] = 40; doc["step"] = 0.5;
      doc["ent_cat"] = "config";
      jsonOutput = "";
      serializeJson(doc, jsonOutput);
      mqtt.publish((String(configManager.mqtt_discovery_prefix) + "/number/" + device_id + "_min_temp/config").c_str(), jsonOutput.c_str(), true);

      // Master Max Temp
      doc.clear();
      device = doc.createNestedObject("dev");
      device["ids"] = "wavin_" + cleanMac;
      device["name"] = "Wavin AHC 9000 Gateway";
      device["mdl"] = "AHC 9000";
      device["mf"] = "Wavin";

      doc["name"] = "Master Max Temp Limit";
      doc["default_entity_id"] = "number." + createObjectId("Master Max Temp Limit");
      doc["uniq_id"] = device_id + "_max_temp";
      doc["ic"] = "mdi:thermometer-chevron-up";
      doc["cmd_t"] = base_topic + "/set_max_temp";
      doc["stat_t"] = base_topic + "/attributes";
      doc["val_tpl"] = "{{ value_json.max_temp }}";
      doc["min"] = 6; doc["max"] = 40; doc["step"] = 0.5;
      doc["ent_cat"] = "config";
      jsonOutput = "";
      serializeJson(doc, jsonOutput);
      mqtt.publish((String(configManager.mqtt_discovery_prefix) + "/number/" + device_id + "_max_temp/config").c_str(), jsonOutput.c_str(), true);
      }

      // Master Alarm Low Temp
      if (configManager.enable_master_alarm) {
      doc.clear();
      device = doc.createNestedObject("dev");
      device["ids"] = "wavin_" + cleanMac;
      device["name"] = "Wavin AHC 9000 Gateway";
      device["mdl"] = "AHC 9000";
      device["mf"] = "Wavin";

      doc["name"] = "Master Alarm Low Temp";
      doc["default_entity_id"] = "number." + createObjectId("Master Alarm Low Temp");
      doc["uniq_id"] = device_id + "_alarm_low";
      doc["ic"] = "mdi:thermometer-low";
      doc["cmd_t"] = base_topic + "/set_alarm_low";
      doc["stat_t"] = base_topic + "/attributes";
      doc["val_tpl"] = "{{ value_json.alarm_low }}";
      doc["min"] = -9; doc["max"] = 20; doc["step"] = 0.5;
      doc["ent_cat"] = "config";
      jsonOutput = "";
      serializeJson(doc, jsonOutput);
      mqtt.publish((String(configManager.mqtt_discovery_prefix) + "/number/" + device_id + "_alarm_low/config").c_str(), jsonOutput.c_str(), true);

      // Master Alarm High Temp
      doc.clear();
      device = doc.createNestedObject("dev");
      device["ids"] = "wavin_" + cleanMac;
      device["name"] = "Wavin AHC 9000 Gateway";
      device["mdl"] = "AHC 9000";
      device["mf"] = "Wavin";

      doc["name"] = "Master Alarm High Temp";
      doc["default_entity_id"] = "number." + createObjectId("Master Alarm High Temp");
      doc["uniq_id"] = device_id + "_alarm_high";
      doc["ic"] = "mdi:thermometer-high";
      doc["cmd_t"] = base_topic + "/set_alarm_high";
      doc["stat_t"] = base_topic + "/attributes";
      doc["val_tpl"] = "{{ value_json.alarm_high }}";
      doc["min"] = 30; doc["max"] = 70; doc["step"] = 0.5;
      doc["ent_cat"] = "config";
      jsonOutput = "";
      serializeJson(doc, jsonOutput);
      mqtt.publish((String(configManager.mqtt_discovery_prefix) + "/number/" + device_id + "_alarm_high/config").c_str(), jsonOutput.c_str(), true);
      }

      // Master Standby Temp
      if (configManager.enable_master_standby) {
      doc.clear();
      device = doc.createNestedObject("dev");
      device["ids"] = "wavin_" + cleanMac;
      device["name"] = "Wavin AHC 9000 Gateway";
      device["mdl"] = "AHC 9000";
      device["mf"] = "Wavin";

      doc["name"] = "Master Standby Temp";
      doc["default_entity_id"] = "number." + createObjectId("Master Standby Temp");
      doc["uniq_id"] = device_id + "_standby";
      doc["ic"] = "mdi:thermometer-low";
      doc["cmd_t"] = base_topic + "/set_standby";
      doc["stat_t"] = base_topic + "/attributes";
      doc["val_tpl"] = "{{ value_json.standby }}";
      doc["min"] = 6; doc["max"] = 40; doc["step"] = 0.5;
      doc["ent_cat"] = "config";
      jsonOutput = "";
      serializeJson(doc, jsonOutput);
      mqtt.publish((String(configManager.mqtt_discovery_prefix) + "/number/" + device_id + "_standby/config").c_str(), jsonOutput.c_str(), true);
      }

      // Master Hysteresis
      if (configManager.enable_master_hysteresis) {
      doc.clear();
      device = doc.createNestedObject("dev");
      device["ids"] = "wavin_" + cleanMac;
      device["name"] = "Wavin AHC 9000 Gateway";
      device["mdl"] = "AHC 9000";
      device["mf"] = "Wavin";

      doc["name"] = "Master Hysteresis";
      doc["default_entity_id"] = "number." + createObjectId("Master Hysteresis");
      doc["uniq_id"] = device_id + "_hysteresis";
      doc["ic"] = "mdi:arrow-expand-vertical";
      doc["cmd_t"] = base_topic + "/set_hysteresis";
      doc["stat_t"] = base_topic + "/attributes";
      doc["val_tpl"] = "{{ value_json.hysteresis }}";
      doc["min"] = 0.1; doc["max"] = 1.0; doc["step"] = 0.1;
      doc["ent_cat"] = "config";
      jsonOutput = "";
      serializeJson(doc, jsonOutput);
      mqtt.publish((String(configManager.mqtt_discovery_prefix) + "/number/" + device_id + "_hysteresis/config").c_str(), jsonOutput.c_str(), true);
      }

      // Master Lock Switch
      if (configManager.enable_master_lock) {
      doc.clear();
      device = doc.createNestedObject("dev");
      device["ids"] = "wavin_" + cleanMac;
      device["name"] = "Wavin AHC 9000 Gateway";
      device["mdl"] = "AHC 9000";
      device["mf"] = "Wavin";

      doc["name"] = "Master Lock";
      doc["default_entity_id"] = "switch." + createObjectId("Master Lock");
      doc["uniq_id"] = device_id + "_lock";
      doc["ic"] = "mdi:lock";
      doc["cmd_t"] = base_topic + "/set_lock";
      doc["stat_t"] = base_topic + "/lock";
      doc["ent_cat"] = "config";
      jsonOutput = "";
      serializeJson(doc, jsonOutput);
      mqtt.publish((String(configManager.mqtt_discovery_prefix) + "/switch/" + device_id + "_lock/config").c_str(), jsonOutput.c_str(), true);
      }
      
      // Master Comfort Temp
      doc.clear();
      device = doc.createNestedObject("dev");
      device["ids"] = "wavin_" + cleanMac;
      device["name"] = "Wavin AHC 9000 Gateway";
      device["mdl"] = "AHC 9000";
      device["mf"] = "Wavin";

      doc["name"] = "Master Comfort Temp";
      doc["default_entity_id"] = "number." + createObjectId("Master Comfort Temp");
      doc["uniq_id"] = device_id + "_comfort_temp";
      doc["ic"] = "mdi:sun-thermometer";
      doc["cmd_t"] = base_topic + "/set_master_comfort_temp";
      doc["stat_t"] = base_topic + "/attributes";
      doc["val_tpl"] = "{{ value_json.master_comfort_temp }}";
      doc["min"] = 6; doc["max"] = 40; doc["step"] = 0.5;
      doc["ent_cat"] = "config";
      jsonOutput = "";
      serializeJson(doc, jsonOutput);
      mqtt.publish((String(configManager.mqtt_discovery_prefix) + "/number/" + device_id + "_comfort_temp/config").c_str(), jsonOutput.c_str(), true);

      // Master Eco Temp
      doc.clear();
      device = doc.createNestedObject("dev");
      device["ids"] = "wavin_" + cleanMac;
      device["name"] = "Wavin AHC 9000 Gateway";
      device["mdl"] = "AHC 9000";
      device["mf"] = "Wavin";

      doc["name"] = "Master Eco Temp";
      doc["default_entity_id"] = "number." + createObjectId("Master Eco Temp");
      doc["uniq_id"] = device_id + "_eco_temp";
      doc["ic"] = "mdi:leaf";
      doc["cmd_t"] = base_topic + "/set_master_eco_temp";
      doc["stat_t"] = base_topic + "/attributes";
      doc["val_tpl"] = "{{ value_json.master_eco_temp }}";
      doc["min"] = 6; doc["max"] = 40; doc["step"] = 0.5;
      doc["ent_cat"] = "config";
      jsonOutput = "";
      serializeJson(doc, jsonOutput);
      mqtt.publish((String(configManager.mqtt_discovery_prefix) + "/number/" + device_id + "_eco_temp/config").c_str(), jsonOutput.c_str(), true);

      // --- Maintenance Entities ---
      
      if (configManager.enable_maintenance) {
      // 1. Maintenance Day Select
      doc.clear();
      device = doc.createNestedObject("dev");
      device["ids"] = "wavin_" + cleanMac;
      device["name"] = "Wavin AHC 9000 Gateway";
      device["mdl"] = "AHC 9000";
      device["mf"] = "Wavin";

      doc["name"] = "Valve Maintenance Day";
      doc["default_entity_id"] = "select." + createObjectId("Valve Maintenance Day");
      doc["uniq_id"] = device_id + "_maint_day";
      doc["ic"] = "mdi:calendar-clock";
      doc["cmd_t"] = base_topic + "/set_maint_day";
      doc["stat_t"] = base_topic + "/attributes";
      doc["val_tpl"] = "{{ value_json.maint_day }}";
      JsonArray options = doc.createNestedArray("options");
      for(int k=0; k<9; k++) options.add(days_of_week[k]);
      doc["ent_cat"] = "config";
      jsonOutput = "";
      serializeJson(doc, jsonOutput);
      mqtt.publish((String(configManager.mqtt_discovery_prefix) + "/select/" + device_id + "_maint_day/config").c_str(), jsonOutput.c_str(), true);

      // 2. Maintenance Hour Number
      doc.clear();
      device = doc.createNestedObject("dev");
      device["ids"] = "wavin_" + cleanMac;
      
      doc["name"] = "Valve Maintenance Hour";
      doc["default_entity_id"] = "number." + createObjectId("Valve Maintenance Hour");
      doc["uniq_id"] = device_id + "_maint_hour";
      doc["ic"] = "mdi:clock-outline";
      doc["cmd_t"] = base_topic + "/set_maint_hour";
      doc["stat_t"] = base_topic + "/attributes";
      doc["val_tpl"] = "{{ value_json.maint_hour }}";
      doc["min"] = 0; doc["max"] = 23; doc["step"] = 1;
      doc["ent_cat"] = "config";
      jsonOutput = "";
      serializeJson(doc, jsonOutput);
      mqtt.publish((String(configManager.mqtt_discovery_prefix) + "/number/" + device_id + "_maint_hour/config").c_str(), jsonOutput.c_str(), true);

      // 2b. Maintenance Minute Number
      doc.clear();
      device = doc.createNestedObject("dev");
      device["ids"] = "wavin_" + cleanMac;
      
      doc["name"] = "Valve Maintenance Minute";
      doc["default_entity_id"] = "number." + createObjectId("Valve Maintenance Minute");
      doc["uniq_id"] = device_id + "_maint_minute";
      doc["ic"] = "mdi:clock-outline";
      doc["cmd_t"] = base_topic + "/set_maint_minute";
      doc["stat_t"] = base_topic + "/attributes";
      doc["val_tpl"] = "{{ value_json.maint_minute }}";
      doc["min"] = 0; doc["max"] = 59; doc["step"] = 1;
      doc["ent_cat"] = "config";
      jsonOutput = "";
      serializeJson(doc, jsonOutput);
      mqtt.publish((String(configManager.mqtt_discovery_prefix) + "/number/" + device_id + "_maint_minute/config").c_str(), jsonOutput.c_str(), true);

      // 3. Maintenance Duration Number
      doc.clear();
      device = doc.createNestedObject("dev");
      device["ids"] = "wavin_" + cleanMac;
      
      doc["name"] = "Valve Maintenance Duration";
      doc["default_entity_id"] = "number." + createObjectId("Valve Maintenance Duration");
      doc["uniq_id"] = device_id + "_maint_dur";
      doc["ic"] = "mdi:timer-sand";
      doc["cmd_t"] = base_topic + "/set_maint_dur";
      doc["stat_t"] = base_topic + "/attributes";
      doc["val_tpl"] = "{{ value_json.maint_duration }}";
      doc["unit_of_meas"] = "min";
      doc["min"] = 1; doc["max"] = 60; doc["step"] = 1;
      doc["ent_cat"] = "config";
      jsonOutput = "";
      serializeJson(doc, jsonOutput);
      mqtt.publish((String(configManager.mqtt_discovery_prefix) + "/number/" + device_id + "_maint_dur/config").c_str(), jsonOutput.c_str(), true);

      // 4. Run Maintenance Button
      doc.clear();
      device = doc.createNestedObject("dev");
      device["ids"] = "wavin_" + cleanMac;
      
      doc["name"] = "Run Valve Maintenance";
      doc["default_entity_id"] = "button." + createObjectId("Run Valve Maintenance");
      doc["uniq_id"] = device_id + "_run_maint";
      doc["ic"] = "mdi:play-circle-outline";
      doc["cmd_t"] = base_topic + "/run_maint";
      doc["ent_cat"] = "config";
      jsonOutput = "";
      serializeJson(doc, jsonOutput);
      mqtt.publish((String(configManager.mqtt_discovery_prefix) + "/button/" + device_id + "_run_maint/config").c_str(), jsonOutput.c_str(), true);

      // 5. Last Run Sensor
      doc.clear();
      device = doc.createNestedObject("dev");
      device["ids"] = "wavin_" + cleanMac;
      
      doc["name"] = "Last Valve Maintenance";
      doc["default_entity_id"] = "sensor." + createObjectId("Last Valve Maintenance");
      doc["uniq_id"] = device_id + "_last_maint";
      doc["ic"] = "mdi:history";
      doc["stat_t"] = base_topic + "/maintenance/last_run";
      doc["ent_cat"] = "diagnostic";
      jsonOutput = "";
      serializeJson(doc, jsonOutput);
      mqtt.publish((String(configManager.mqtt_discovery_prefix) + "/sensor/" + device_id + "_last_maint/config").c_str(), jsonOutput.c_str(), true);
      }

      // 6. Boost Button
      if (configManager.enable_boost) {
      doc.clear();
      device = doc.createNestedObject("dev");
      device["ids"] = "wavin_" + cleanMac;
      
      doc["name"] = "Boost Heating (1h)";
      doc["default_entity_id"] = "button." + createObjectId("Boost Heating");
      doc["uniq_id"] = device_id + "_boost";
      doc["ic"] = "mdi:fire";
      doc["cmd_t"] = base_topic + "/run_boost";
      doc["ent_cat"] = "config";
      jsonOutput = "";
      serializeJson(doc, jsonOutput);
      mqtt.publish((String(configManager.mqtt_discovery_prefix) + "/button/" + device_id + "_boost/config").c_str(), jsonOutput.c_str(), true);

      // 6b. Boost Temperature Number
      doc.clear();
      device = doc.createNestedObject("dev");
      device["ids"] = "wavin_" + cleanMac;
      
      doc["name"] = "Boost Temperature";
      doc["default_entity_id"] = "number." + createObjectId("Boost Temperature");
      doc["uniq_id"] = device_id + "_boost_temp";
      doc["ic"] = "mdi:thermometer-plus";
      doc["cmd_t"] = base_topic + "/set_boost_temp";
      doc["stat_t"] = base_topic + "/attributes";
      doc["val_tpl"] = "{{ value_json.boost_temp }}";
      doc["min"] = 6; doc["max"] = 40; doc["step"] = 0.5;
      doc["ent_cat"] = "config";
      jsonOutput = "";
      serializeJson(doc, jsonOutput);
      mqtt.publish((String(configManager.mqtt_discovery_prefix) + "/number/" + device_id + "_boost_temp/config").c_str(), jsonOutput.c_str(), true);
      }

      // 13. Reset Defaults Button
      doc.clear();
      device = doc.createNestedObject("dev");
      device["ids"] = "wavin_" + cleanMac;
      
      doc["name"] = "Master Reset Defaults";
      doc["default_entity_id"] = "button." + createObjectId("Master Reset Defaults");
      doc["uniq_id"] = device_id + "_reset_defaults";
      doc["ic"] = "mdi:restore";
      doc["cmd_t"] = base_topic + "/set_defaults";
      doc["ent_cat"] = "config";
      jsonOutput = "";
      serializeJson(doc, jsonOutput);
      mqtt.publish((String(configManager.mqtt_discovery_prefix) + "/button/" + device_id + "_reset_defaults/config").c_str(), jsonOutput.c_str(), true);

      // 14. Heat Demand Sensor
      doc.clear();
      device = doc.createNestedObject("dev");
      device["ids"] = "wavin_" + cleanMac;
      
      doc["name"] = "System Heat Demand";
      doc["default_entity_id"] = "sensor." + createObjectId("System Heat Demand");
      doc["uniq_id"] = device_id + "_heat_demand";
      doc["ic"] = "mdi:gauge";
      doc["stat_t"] = base_topic + "/heat_demand";
      doc["unit_of_meas"] = "%";
      doc["ent_cat"] = "diagnostic";
      jsonOutput = "";
      serializeJson(doc, jsonOutput);
      mqtt.publish((String(configManager.mqtt_discovery_prefix) + "/sensor/" + device_id + "_heat_demand/config").c_str(), jsonOutput.c_str(), true);

      // 15. Total System Current Sensor
      doc.clear();
      device = doc.createNestedObject("dev");
      device["ids"] = "wavin_" + cleanMac;
      
      doc["name"] = "Total System Current";
      doc["default_entity_id"] = "sensor." + createObjectId("Total System Current");
      doc["uniq_id"] = device_id + "_total_current";
      doc["dev_cla"] = "current";
      doc["unit_of_meas"] = "mA";
      doc["stat_t"] = base_topic + "/total_current";
      doc["ent_cat"] = "diagnostic";
      jsonOutput = "";
      serializeJson(doc, jsonOutput);
      mqtt.publish((String(configManager.mqtt_discovery_prefix) + "/sensor/" + device_id + "_total_current/config").c_str(), jsonOutput.c_str(), true);
  } // End of master entity scope
}

void publish_telemetry() {
    String mac = WiFi.macAddress();
    String topic = "wavin/" + mac + "/tele";
    
    DynamicJsonDocument doc(1024);
    
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char timeStr[32];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%S", &timeinfo);
    doc["Time"] = timeStr;
    
    long uptime = millis() / 1000;
    int d = uptime / 86400;
    int h = (uptime % 86400) / 3600;
    int m = (uptime % 3600) / 60;
    int s = uptime % 60;
    char uptimeStr[32];
    sprintf(uptimeStr, "%dT%02d:%02d:%02d", d, h, m, s);
    doc["Uptime"] = uptimeStr;
    doc["UptimeSec"] = uptime;
    doc["Heap"] = ESP.getFreeHeap() / 1024;
    doc["MqttCount"] = mqtt_reconnect_count;
    doc["POWER"] = "ON";
    
    JsonObject wifi = doc.createNestedObject("Wifi");
    wifi["AP"] = 1;
    wifi["SSID"] = WiFi.SSID();
    wifi["BSSId"] = WiFi.BSSIDstr();
    wifi["Channel"] = WiFi.channel();
    int rssi = WiFi.RSSI();
    wifi["RSSI"] = (rssi <= -100) ? 0 : (rssi >= -50) ? 100 : 2 * (rssi + 100);
    wifi["Signal"] = rssi;
    
    const char* hostname = WiFi.getHostname();
    doc["Hostname"] = (hostname && strlen(hostname) > 0) ? hostname : "WavinGateway";
    doc["IPAddress"] = WiFi.localIP().toString();
    
    String jsonOutput;
    serializeJson(doc, jsonOutput);
    mqtt.publish(topic.c_str(), jsonOutput.c_str());
}

void check_maintenance() {
    if (!clock_synced || !wavin.isInitialized()) return;
    
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);

    // Debug logging every minute to help troubleshoot scheduling
    static int last_debug_min = -1;
    if (t.tm_min != last_debug_min) {
        last_debug_min = t.tm_min;
        TelnetStream.printf("Maint Check: Now=%02d:%02d Day=%d | Cfg: Day=%d Time=%02d:%02d | LastTrig=%d Active=%d\n", 
            t.tm_hour, t.tm_min, t.tm_wday, configManager.maint_day, configManager.maint_hour, configManager.maint_minute, last_maint_day_triggered, maint_active);
    }

    // Check Start Condition (Day match, Hour match, Minute 0, Not already run today)
    if (!maint_active && configManager.maint_day < 8) {
        bool day_match = (configManager.maint_day == 7) || (t.tm_wday == configManager.maint_day);
        if (day_match && t.tm_hour == configManager.maint_hour && t.tm_min == configManager.maint_minute && last_maint_day_triggered != t.tm_yday) {
            start_maintenance();
            last_maint_day_triggered = t.tm_yday;
        }
    }

    // Check Stop Condition
    if (maint_active) {
        if (millis() - maint_start_millis > (configManager.maint_duration * 60000UL)) {
            stop_maintenance();
        }
    }
}

void start_maintenance() {
    if (maint_active) return;
    maint_active = true;
    maint_start_millis = millis();
    TelnetStream.println("Starting Valve Maintenance (Setting all to 40C)...");
    
    for (int i = 0; i < WAVIN_CHANNELS; i++) {
        if (!((configManager.maint_mask >> i) & 1)) continue; // Check Maintenance Mask
        WavinChannel data = wavin.getChannelData(i);
        if (data.hasThermostat) {
            maint_restore_temps[i] = data.targetTemp;
            maint_restore_modes[i] = data.mode;
            
            // Ensure mode is Heat (Manual) so the 40C setpoint actually opens the valve
            if (data.mode != "heat") {
                wavin.setMode(i, "heat");
                delay(100);
            }

            if (wavin.setTargetTemp(i, 40.0)) {
                TelnetStream.printf("  CH %d set to 40.0C\n", i + 1);
            } else {
                TelnetStream.printf("  CH %d FAILED to set 40.0C\n", i + 1);
            }
            delay(100); // Delay to prevent bus congestion
        }
    }
    publish_status();
}

void update_status_led() {
    #ifdef STATUS_LED_PIN
    static unsigned long lastLedUpdate = 0;
    static int ledState = 0; 
    unsigned long now = millis();

    if (!mqtt.connected()) {
        // Slow Blink (0.5Hz) if WiFi connected but MQTT lost
        if (now - lastLedUpdate > 1000) {
            lastLedUpdate = now;
            digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
        }
    } else {
        // Heartbeat Pattern: ON(50ms) - OFF(150ms) - ON(50ms) - OFF(2000ms)
        long interval = 0;
        switch(ledState) {
            case 0: interval = 50; break;   // First Pulse
            case 1: interval = 150; break;  // Gap
            case 2: interval = 50; break;   // Second Pulse
            case 3: interval = 2000; break; // Rest
        }
        
        if (now - lastLedUpdate > interval) {
            lastLedUpdate = now;
            ledState++;
            if (ledState > 3) ledState = 0;
            
            // Set LED based on new state (Active HIGH assumed)
            bool on = (ledState == 0 || ledState == 2);
            digitalWrite(STATUS_LED_PIN, on ? HIGH : LOW);
        }
    }
    #endif
}

void stop_maintenance() {
    if (!maint_active) return;
    maint_active = false;
    TelnetStream.println("Stopping Valve Maintenance (Restoring temps)...");
    
    for (int i = 0; i < WAVIN_CHANNELS; i++) {
        if (!((configManager.maint_mask >> i) & 1)) continue; // Check Maintenance Mask
        WavinChannel data = wavin.getChannelData(i);
        if (data.hasThermostat) {
            // Restore previous temp (ensure it's valid, otherwise default to 21)
            float restore = maint_restore_temps[i];
            if (restore < 6.0 || restore > 40.0) restore = 21.0;
            if (wavin.setTargetTemp(i, restore)) {
                TelnetStream.printf("  CH %d restored to %.1fC\n", i + 1, restore);
            } else {
                TelnetStream.printf("  CH %d FAILED to restore %.1fC\n", i + 1, restore);
            }
            delay(100); // Delay to prevent bus congestion
            
            // Restore Mode
            if (maint_restore_modes[i].length() > 0 && maint_restore_modes[i] != "heat") {
                wavin.setMode(i, maint_restore_modes[i]);
                delay(100);
            }
        }
    }
    
    // Update Last Run String
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &t);
    configManager.last_maint_str = String(buf);
    preferences.putString("last_maint", configManager.last_maint_str);
    
    publish_status();
}

void check_boost() {
    if (boost_active) {
        // 1 Hour Duration (3600000 ms)
        if (millis() - boost_start_millis > 3600000) {
            stop_boost();
        }
    }
}

void start_boost() {
    if (vacation_active) {
        TelnetStream.println("Cannot start Boost: Vacation Mode is active");
        return;
    }
    if (boost_active) return;
    boost_active = true;
    boost_start_millis = millis();
    TelnetStream.printf("Starting Boost (Setting Rooms 2-16 to %.1fC)...\n", configManager.boost_temp);
    
    // Loop based on Master Mask
    for (int i = 0; i < WAVIN_CHANNELS; i++) {
        if (!((configManager.boost_mask >> i) & 1)) continue; // Check Boost Mask
        WavinChannel data = wavin.getChannelData(i);
        if (data.hasThermostat) {
            boost_restore_temps[i] = data.targetTemp;
            boost_restore_modes[i] = data.mode;
            
            if (data.mode != "heat") {
                wavin.setMode(i, "heat");
                delay(100);
            }

            if (wavin.setTargetTemp(i, configManager.boost_temp)) {
                TelnetStream.printf("  CH %d set to %.1fC\n", i + 1, configManager.boost_temp);
            }
            delay(100);
        }
    }
    publish_status();
}

void start_vacation() {
    if (vacation_active) {
        TelnetStream.println("Start Vacation called, but already active. Ignoring.");
        return;
    }
    
    // If Boost is active, stop it first to restore original temps before saving for vacation
    if (boost_active) {
        TelnetStream.println("Boost is active, stopping it before starting vacation.");
        stop_boost();
    }

    vacation_active = true;
    TelnetStream.println("Starting Vacation Mode...");
    int channels_set = 0;

    for (int i = 0; i < WAVIN_CHANNELS; i++) {
        if (!((configManager.vacation_mask >> i) & 1)) continue; // Check Vacation Mask
        WavinChannel data = wavin.getChannelData(i);
        if (data.hasThermostat) {
            vacation_restore_temps[i] = data.targetTemp;
            vacation_restore_modes[i] = data.mode;
            TelnetStream.printf("  CH %d: Storing temp %.1fC and mode '%s'.\n", i + 1, data.targetTemp, data.mode.c_str());

            if (data.mode != "heat") {
                TelnetStream.printf("  CH %d: Setting mode to 'heat' for vacation.\n", i + 1);
                wavin.setMode(i, "heat");
                delay(100);
            }
            // Use the holiday temp set on the controller
            if (wavin.setTargetTemp(i, data.holidayTemp)) {
                TelnetStream.printf("  CH %d set to %.1fC (Vacation)\n", i + 1, data.holidayTemp);
                channels_set++;
            } else {
                TelnetStream.printf("  CH %d: FAILED to set vacation temp.\n", i + 1);
            }
            delay(100);
        }
    }
    if (channels_set > 0) {
        TelnetStream.println("Vacation Mode started successfully for " + String(channels_set) + " channels.");
    } else {
        TelnetStream.println("Warning: Vacation Mode started, but no channels were set.");
    }
    publish_status();
}

void stop_vacation() {
    if (!vacation_active) {
        TelnetStream.println("Stop Vacation called, but not active. Ignoring.");
        return;
    }
    vacation_active = false;
    TelnetStream.println("Stopping Vacation Mode...");
    int channels_restored = 0;

    for (int i = 0; i < WAVIN_CHANNELS; i++) {
        if (!((configManager.vacation_mask >> i) & 1)) continue; // Check Vacation Mask
        WavinChannel data = wavin.getChannelData(i);
        if (data.hasThermostat) {
            float restore_temp = vacation_restore_temps[i];
            String restore_mode = vacation_restore_modes[i];
            
            if (restore_temp < 6.0 || restore_temp > 40.0) {
                TelnetStream.printf("  CH %d: Invalid restore temp (%.1fC), using 21.0C.\n", i + 1, restore_temp);
                restore_temp = 21.0;
            }
            
            TelnetStream.printf("  CH %d: Restoring temp to %.1fC and mode to '%s'.\n", i + 1, restore_temp, restore_mode.c_str());

            if (wavin.setTargetTemp(i, restore_temp)) {
                channels_restored++;
            } else {
                TelnetStream.printf("  CH %d: FAILED to restore temp.\n", i + 1);
            }
            delay(100);
            
            if (restore_mode.length() > 0 && restore_mode != "heat") {
                if(wavin.setMode(i, restore_mode)) {
                    TelnetStream.printf("  CH %d: Mode restored successfully.\n", i + 1);
                } else {
                    TelnetStream.printf("  CH %d: FAILED to restore mode.\n", i + 1);
                }
                delay(100);
            }
        }
    }
    if (channels_restored > 0) {
        TelnetStream.println("Vacation Mode stopped successfully for " + String(channels_restored) + " channels.");
    } else {
        TelnetStream.println("Warning: Vacation Mode stopped, but no channels were restored.");
    }
    publish_status();
}

void stop_boost() {
    if (!boost_active) return;
    boost_active = false;
    TelnetStream.println("Stopping Boost (Restoring temps)...");
    
    for (int i = 0; i < WAVIN_CHANNELS; i++) {
        if (!((configManager.boost_mask >> i) & 1)) continue; // Check Boost Mask
        WavinChannel data = wavin.getChannelData(i);
        if (data.hasThermostat) {
            float restore = boost_restore_temps[i];
            if (restore < 6.0 || restore > 40.0) restore = 21.0;
            
            if (wavin.setTargetTemp(i, restore)) {
                TelnetStream.printf("  CH %d restored to %.1fC\n", i + 1, restore);
            }
            delay(100);
            
            if (boost_restore_modes[i].length() > 0 && boost_restore_modes[i] != "heat") {
                wavin.setMode(i, boost_restore_modes[i]);
                delay(100);
            }
        }
    }
    publish_status();
}