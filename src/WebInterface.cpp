#include "WebInterface.h"
#include "AppConfig.h"
#include "html_data.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h>
#include <TelnetStream.h>

// --- Extern Globals ---
extern WavinController wavin;
extern PubSubClient mqtt;
extern ConfigManager configManager;
extern void publish_discovery();

String getSystemInfoHTML() {
    String html = "<html><head><title>System Info</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body { font-family: sans-serif; background-color: #f0f2f5; padding: 20px; color: #333; }";
    html += "h2 { color: #005eb8; border-bottom: 1px solid #ccc; padding-bottom: 10px; }";
    html += "table { width: 100%; border-collapse: collapse; background: white; box-shadow: 0 1px 3px rgba(0,0,0,0.1); margin-bottom: 20px; border-radius: 4px; overflow: hidden; }";
    html += "th, td { padding: 12px 15px; text-align: left; border-bottom: 1px solid #ddd; font-size: 0.9rem; }";
    html += "th { background-color: #f8f9fa; font-weight: 600; color: #555; width: 40%; }";
    html += "tr:last-child td { border-bottom: none; }";
    html += "tr:hover { background-color: #f1f1f1; }";
    html += ".btn { display: inline-block; background: #005eb8; color: white; padding: 10px 20px; text-decoration: none; border-radius: 4px; font-size: 0.9rem; transition: background 0.2s; }";
    html += ".btn:hover { background: #004494; }";
    html += "</style></head><body>";
    
    html += "<h2>System Information</h2>";
    html += "<table>";
    
    // Firmware
    html += "<tr><th>Program Version</th><td>" + String(FIRMWARE_VERSION) + "</td></tr>";
    html += "<tr><th>Build Date & Time</th><td>" + String(__DATE__) + " " + String(__TIME__) + "</td></tr>";
    html += "<tr><th>SDK Version</th><td>" + String(ESP.getSdkVersion()) + "</td></tr>";
    
    // Uptime
    long uptime = millis() / 1000;
    int d = uptime / 86400;
    int h = (uptime % 86400) / 3600;
    int m = (uptime % 3600) / 60;
    int s = uptime % 60;
    char uptimeStr[32];
    sprintf(uptimeStr, "%dT%02d:%02d:%02d", d, h, m, s);
    html += "<tr><th>Uptime</th><td>" + String(uptimeStr) + "</td></tr>";
    
    // Network
    html += "<tr><th>Hostname</th><td>" + String(WiFi.getHostname()) + "</td></tr>";
    html += "<tr><th>IP Address</th><td>" + WiFi.localIP().toString() + "</td></tr>";
    html += "<tr><th>Gateway</th><td>" + WiFi.gatewayIP().toString() + "</td></tr>";
    html += "<tr><th>Subnet Mask</th><td>" + WiFi.subnetMask().toString() + "</td></tr>";
    html += "<tr><th>DNS Server</th><td>" + WiFi.dnsIP().toString() + "</td></tr>";
    html += "<tr><th>MAC Address</th><td>" + WiFi.macAddress() + "</td></tr>";
    
    // WiFi
    html += "<tr><th>SSID</th><td>" + WiFi.SSID() + "</td></tr>";
    html += "<tr><th>BSSID</th><td>" + WiFi.BSSIDstr() + "</td></tr>";
    html += "<tr><th>Channel</th><td>" + String(WiFi.channel()) + "</td></tr>";
    html += "<tr><th>RSSI</th><td>" + String(WiFi.RSSI()) + " dBm</td></tr>";
    
    // MQTT
    html += "<tr><th>MQTT Host</th><td>" + String(configManager.mqtt_server) + ":" + String(configManager.mqtt_port) + "</td></tr>";
    html += "<tr><th>MQTT User</th><td>" + String(configManager.mqtt_user) + "</td></tr>";
    html += "<tr><th>MQTT Prefix</th><td>" + String(configManager.mqtt_discovery_prefix) + "</td></tr>";
    html += "<tr><th>MQTT Status</th><td>" + String(mqtt.connected() ? "Connected" : "Disconnected") + "</td></tr>";

    // Hardware
    html += "<tr><th>ESP Chip ID</th><td>" + String((uint32_t)ESP.getEfuseMac(), HEX) + "</td></tr>";
    html += "<tr><th>Chip Model</th><td>" + String(ESP.getChipModel()) + " (Rev " + String(ESP.getChipRevision()) + ")</td></tr>";
    html += "<tr><th>CPU Freq</th><td>" + String(ESP.getCpuFreqMHz()) + " MHz</td></tr>";
    html += "<tr><th>Flash Size</th><td>" + String(ESP.getFlashChipSize() / 1024) + " KB</td></tr>";
    html += "<tr><th>Free Heap</th><td>" + String(ESP.getFreeHeap() / 1024.0, 1) + " KB</td></tr>";
    
    html += "</table>";

    // Wavin Controller Info
    WavinDeviceInfo info = wavin.getDeviceInfo();
    if (info.address != 0) {
        html += "<h2>Wavin Controller Information</h2>";
        html += "<table>";
        html += "<tr><th>Device Name</th><td>" + info.deviceName + "</td></tr>";
        html += "<tr><th>HW Version</th><td>" + info.hwVersion + "</td></tr>";
        html += "<tr><th>SW Version</th><td>" + info.swVersion + "</td></tr>";
        html += "</table>";
    }

    html += "<a href='/' class='btn'>Back</a>";
    html += "</body></html>";
    return html;
}

WebInterface::WebInterface(int port) : server(port) {}

void WebInterface::begin() {
    server.on("/", HTTP_GET, std::bind(&WebInterface::handleRoot, this));
    server.on("/info", HTTP_GET, [this]() {
        this->server.send(200, "text/html", getSystemInfoHTML());
    });
    server.on("/reset", HTTP_POST, std::bind(&WebInterface::handleReset, this));
    server.on("/reboot", HTTP_POST, std::bind(&WebInterface::handleReboot, this));
    server.on("/update", HTTP_POST, std::bind(&WebInterface::handleUpdate, this), std::bind(&WebInterface::handleUpdateUpload, this));
    server.on("/github_check", HTTP_POST, std::bind(&WebInterface::handleGitHubCheck, this));
    server.on("/github_update", HTTP_POST, std::bind(&WebInterface::handleGitHubUpdate, this));
    server.on("/toggle_telnet", HTTP_POST, std::bind(&WebInterface::handleToggleTelnet, this));
    server.on("/toggle_dark_mode", HTTP_POST, std::bind(&WebInterface::handleToggleDarkMode, this));
    server.on("/discovery", HTTP_POST, std::bind(&WebInterface::handleDiscovery, this));
    server.on("/backup", HTTP_GET, std::bind(&WebInterface::handleBackup, this));
    server.on("/restore", HTTP_POST, std::bind(&WebInterface::handleRestore, this), std::bind(&WebInterface::handleRestoreUpload, this));
    
    server.begin();
}

void WebInterface::handleClient() {
    server.handleClient();
}

void WebInterface::handleRoot() {
  String html = html_template;
  
  html.replace("%VERSION%", FIRMWARE_VERSION);
  
  long uptime = millis() / 1000;
  int d = uptime / 86400;
  int h = (uptime % 86400) / 3600;
  int m = (uptime % 3600) / 60;
  char uptimeStr[32];
  sprintf(uptimeStr, "%dd %02dh %02dm", d, h, m);
  html.replace("%UPTIME%", String(uptimeStr));
  
  html.replace("%RSSI%", String(WiFi.RSSI()));
  html.replace("%IP%", WiFi.localIP().toString());
  
  String mqttStatus = mqtt.connected() ? "<span class='status-ok'>Connected</span>" : "<span class='status-err'>Disconnected</span>";
  html.replace("%MQTT_STATUS%", mqttStatus);

  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  char timeStr[32];
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
  html.replace("%TIME%", String(timeStr));

  html.replace("%TELNET_STATUS%", configManager.enable_telnet ? "<span class='status-ok'>Enabled</span>" : "<span class='status-err'>Disabled</span>");
  html.replace("%TELNET_ACTION%", configManager.enable_telnet ? "Disable" : "Enable");

  String roomListHtml = "";
  for (int i = 0; i < WAVIN_CHANNELS; i++) {
      WavinChannel data = wavin.getChannelData(i);
      if (data.hasThermostat) {
          String signalIcon;
          if (data.rssi > -60) signalIcon = "<span style='color:#28a745'>&#9679;&#9679;&#9679;</span>";
          else if (data.rssi > -75) signalIcon = "<span style='color:#ffc107'>&#9679;&#9679;&#9675;</span>";
          else signalIcon = "<span style='color:#dc3545'>&#9679;&#9675;&#9675;</span>";
          
          String batColor = (data.batteryLevel > 20) ? "var(--text-color)" : "#dc3545";
          
          roomListHtml += "<div class='info-row'>";
          roomListHtml += "<div style='flex:2; font-weight:500; overflow:hidden; white-space:nowrap; text-overflow:ellipsis;'>" + String(configManager.room_names[i]) + "</div>";
          roomListHtml += "<div style='flex:1; text-align:center;'>" + String(data.currentTemp, 1) + "&deg;</div>";
          roomListHtml += "<div style='flex:1; text-align:center; color:var(--label-color);'>" + String(data.targetTemp, 1) + "&deg;</div>";
          roomListHtml += "<div style='flex:1; text-align:right; font-size:0.9em;'><span style='color:" + batColor + "'>" + String(data.batteryLevel) + "%</span> " + signalIcon + "</div>";
          roomListHtml += "</div>";
      }
  }
  if (roomListHtml == "") roomListHtml = "<div class='help' style='text-align:center; padding:20px;'>No thermostats detected yet.<br>Wait for next scan...</div>";
  roomListHtml += "<div style='margin-top:20px; text-align:center;'><a href='/info' style='color:var(--primary-color); text-decoration:none; font-weight:500;'>System Information &raquo;</a></div>";
  html.replace("%ROOM_LIST%", roomListHtml);

  html.replace("%DARK_MODE_CLASS%", configManager.enable_dark_mode ? "dark" : "");
  html.replace("%DARK_MODE_CHECKED%", configManager.enable_dark_mode ? "checked" : "");

  server.send(200, "text/html", html);
}

void WebInterface::handleBackup() {
  DynamicJsonDocument doc(8192);

  doc["mqtt_server"] = configManager.mqtt_server;
  doc["mqtt_port"] = configManager.mqtt_port;
  doc["mqtt_user"] = configManager.mqtt_user;
  doc["mqtt_pass"] = configManager.mqtt_pass;
  doc["mqtt_prefix"] = configManager.mqtt_discovery_prefix;
  doc["timezone"] = configManager.timezone;
  doc["enable_telnet"] = configManager.enable_telnet;
  doc["enable_dark_mode"] = configManager.enable_dark_mode;
  doc["enable_floor_sensors"] = configManager.enable_floor_sensors;

  doc["master_rooms_str"] = configManager.master_rooms_str;
  doc["boost_rooms_str"] = configManager.boost_rooms_str;
  doc["vacation_rooms_str"] = configManager.vacation_rooms_str;
  doc["maint_rooms_str"] = configManager.maint_rooms_str;
  doc["lock_rooms_str"] = configManager.lock_rooms_str;
  doc["eco_rooms_str"] = configManager.eco_rooms_str;
  doc["comfort_rooms_str"] = configManager.comfort_rooms_str;

  doc["enable_master_alarm"] = configManager.enable_master_alarm;
  doc["enable_master_hysteresis"] = configManager.enable_master_hysteresis;
  doc["enable_master_lock"] = configManager.enable_master_lock;
  doc["enable_master_minmax"] = configManager.enable_master_minmax;
  doc["enable_master_standby"] = configManager.enable_master_standby;

  doc["enable_maintenance"] = configManager.enable_maintenance;
  doc["maint_day"] = configManager.maint_day;
  doc["maint_hour"] = configManager.maint_hour;
  doc["maint_minute"] = configManager.maint_minute;
  doc["maint_duration"] = configManager.maint_duration;
  doc["enable_boost"] = configManager.enable_boost;
  doc["boost_temp"] = configManager.boost_temp;
  doc["enable_vacation"] = configManager.enable_vacation;
  doc["master_eco_temp"] = configManager.master_eco_temp;
  doc["master_comfort_temp"] = configManager.master_comfort_temp;

  JsonArray rooms = doc.createNestedArray("room_names");
  for (int i = 0; i < WAVIN_CHANNELS; i++) {
    rooms.add(configManager.room_names[i]);
  }

  String json_output;
  serializeJson(doc, json_output);

  server.sendHeader("Content-Disposition", "attachment; filename=\"wavin_config_backup.json\"");
  server.send(200, "application/json", json_output);
}

void WebInterface::handleRestoreUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    restore_json_buffer = "";
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    restore_json_buffer.concat((const char*)upload.buf, upload.currentSize);
  }
}

void WebInterface::handleRestore() {
    if (restore_json_buffer.length() == 0) {
        server.send(400, "text/plain", "Empty file or upload failed");
        return;
    }
    
    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, restore_json_buffer);
    
    if (error) {
        server.send(400, "text/plain", "Invalid JSON");
        return;
    }
    
    if (doc.containsKey("mqtt_server")) strlcpy(configManager.mqtt_server, doc["mqtt_server"], sizeof(configManager.mqtt_server));
    if (doc.containsKey("mqtt_port")) strlcpy(configManager.mqtt_port, doc["mqtt_port"], sizeof(configManager.mqtt_port));
    if (doc.containsKey("mqtt_user")) strlcpy(configManager.mqtt_user, doc["mqtt_user"], sizeof(configManager.mqtt_user));
    if (doc.containsKey("mqtt_pass")) strlcpy(configManager.mqtt_pass, doc["mqtt_pass"], sizeof(configManager.mqtt_pass));
    if (doc.containsKey("mqtt_prefix")) strlcpy(configManager.mqtt_discovery_prefix, doc["mqtt_prefix"], sizeof(configManager.mqtt_discovery_prefix));
    if (doc.containsKey("timezone")) strlcpy(configManager.timezone, doc["timezone"], sizeof(configManager.timezone));
    
    if (doc.containsKey("enable_telnet")) configManager.enable_telnet = doc["enable_telnet"];
    if (doc.containsKey("enable_dark_mode")) configManager.enable_dark_mode = doc["enable_dark_mode"];
    if (doc.containsKey("enable_floor_sensors")) configManager.enable_floor_sensors = doc["enable_floor_sensors"];
    
    if (doc.containsKey("master_rooms_str")) strlcpy(configManager.master_rooms_str, doc["master_rooms_str"], sizeof(configManager.master_rooms_str));
    if (doc.containsKey("boost_rooms_str")) strlcpy(configManager.boost_rooms_str, doc["boost_rooms_str"], sizeof(configManager.boost_rooms_str));
    if (doc.containsKey("vacation_rooms_str")) strlcpy(configManager.vacation_rooms_str, doc["vacation_rooms_str"], sizeof(configManager.vacation_rooms_str));
    if (doc.containsKey("maint_rooms_str")) strlcpy(configManager.maint_rooms_str, doc["maint_rooms_str"], sizeof(configManager.maint_rooms_str));
    if (doc.containsKey("lock_rooms_str")) strlcpy(configManager.lock_rooms_str, doc["lock_rooms_str"], sizeof(configManager.lock_rooms_str));
    if (doc.containsKey("eco_rooms_str")) strlcpy(configManager.eco_rooms_str, doc["eco_rooms_str"], sizeof(configManager.eco_rooms_str));
    if (doc.containsKey("comfort_rooms_str")) strlcpy(configManager.comfort_rooms_str, doc["comfort_rooms_str"], sizeof(configManager.comfort_rooms_str));
    
    if (doc.containsKey("enable_master_alarm")) configManager.enable_master_alarm = doc["enable_master_alarm"];
    if (doc.containsKey("enable_master_hysteresis")) configManager.enable_master_hysteresis = doc["enable_master_hysteresis"];
    if (doc.containsKey("enable_master_lock")) configManager.enable_master_lock = doc["enable_master_lock"];
    if (doc.containsKey("enable_master_minmax")) configManager.enable_master_minmax = doc["enable_master_minmax"];
    if (doc.containsKey("enable_master_standby")) configManager.enable_master_standby = doc["enable_master_standby"];
    
    if (doc.containsKey("enable_maintenance")) configManager.enable_maintenance = doc["enable_maintenance"];
    if (doc.containsKey("maint_day")) configManager.maint_day = doc["maint_day"];
    if (doc.containsKey("maint_hour")) configManager.maint_hour = doc["maint_hour"];
    if (doc.containsKey("maint_minute")) configManager.maint_minute = doc["maint_minute"];
    if (doc.containsKey("maint_duration")) configManager.maint_duration = doc["maint_duration"];
    
    if (doc.containsKey("enable_boost")) configManager.enable_boost = doc["enable_boost"];
    if (doc.containsKey("boost_temp")) configManager.boost_temp = doc["boost_temp"];
    
    if (doc.containsKey("enable_vacation")) configManager.enable_vacation = doc["enable_vacation"];
    if (doc.containsKey("master_eco_temp")) configManager.master_eco_temp = doc["master_eco_temp"];
    if (doc.containsKey("master_comfort_temp")) configManager.master_comfort_temp = doc["master_comfort_temp"];
    
    if (doc.containsKey("room_names")) {
        JsonArray rooms = doc["room_names"];
        for (int i = 0; i < WAVIN_CHANNELS && i < rooms.size(); i++) {
            strlcpy(configManager.room_names[i], rooms[i], sizeof(configManager.room_names[i]));
        }
    }
    
    configManager.save();
    restore_json_buffer = ""; 
    
    server.send(200, "text/html", "<html><head><meta http-equiv='refresh' content='10;url=/'><style>body{font-family:sans-serif;padding:20px;text-align:center;}</style></head><body><h1>Configuration Restored</h1><p>Settings have been updated. Device is rebooting...</p></body></html>");
    delay(1000);
    ESP.restart();
}

void WebInterface::handleReset() {
  server.send(200, "text/plain", "Resetting... Device will reboot into AP mode.");
  delay(1000);
  configManager.reset();
  WiFi.disconnect(true, true); 
  ESP.restart();
}

void WebInterface::handleReboot() {
  server.send(200, "text/plain", "Rebooting...");
  ESP.restart();
}

void WebInterface::handleUpdate() {
  server.send(200, "text/plain", (Update.hasError()) ? "Update Failed" : "Update Success! Rebooting...");
  ESP.restart();
}

void WebInterface::handleUpdateUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
    } else {
      Update.printError(Serial);
    }
  }
}

void WebInterface::handleGitHubCheck() {
    WiFiClientSecure client;
    client.setInsecure(); // Allow self-signed/unknown certs (GitHub changes CAs sometimes)
    HTTPClient https;
    
    // GitHub API requires a User-Agent header
    https.setUserAgent("ESP32-Wavin-Gateway");
    
    String url = "https://api.github.com/repos/" GITHUB_REPO "/releases/latest";
    
    if (https.begin(client, url)) {
        int httpCode = https.GET();
        if (httpCode == HTTP_CODE_OK) {
            // Use a filter to only parse what we need (saves significant memory)
            StaticJsonDocument<200> filter;
            filter["tag_name"] = true;
            filter["assets"][0]["name"] = true;
            filter["assets"][0]["browser_download_url"] = true;

            // Increase buffer size and parse from stream with filter
            DynamicJsonDocument doc(16384); 
            DeserializationError error = deserializeJson(doc, https.getStream(), DeserializationOption::Filter(filter));
            
            if (error) {
                String errStr = String(error.c_str());
                TelnetStream.println("GitHub JSON Error: " + errStr);
                server.send(500, "text/plain", "JSON Parse Error: " + errStr);
                https.end();
                return;
            }
            
            String tag_name = doc["tag_name"].as<String>();
            String clean_tag = tag_name;
            if (clean_tag.startsWith("v")) clean_tag = clean_tag.substring(1);
            
            String html = "<html><head><title>Update Check</title><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{font-family:sans-serif;padding:20px;background:#f0f2f5;} .card{background:white;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1);max-width:500px;margin:0 auto;} .btn{background:#005eb8;color:white;padding:10px 20px;text-decoration:none;border-radius:4px;display:inline-block;border:none;cursor:pointer;font-size:16px;} .btn:hover{background:#004494;}</style></head><body>";
            html += "<div class='card'><h2>Firmware Update</h2>";
            html += "<p><strong>Current Version:</strong> " + String(FIRMWARE_VERSION) + "</p>";
            html += "<p><strong>Latest Version:</strong> " + clean_tag + "</p>";
            
            if (clean_tag != String(FIRMWARE_VERSION)) {
                html += "<p style='color:#28a745;'><strong>A new version is available!</strong></p>";
                
                String assetUrl = "";
                JsonArray assets = doc["assets"];
                
                // Determine board identifier to find correct binary
                #if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(ESP32C3)
                    const char* target_match = "c3";
                #else
                    const char* target_match = "pico"; // Default/Rev1
                #endif
                
                for (JsonObject asset : assets) {
                    String name = asset["name"].as<String>();
                    if (name.endsWith(".bin")) {
                        // Prioritize exact match for board type
                        if (name.indexOf(target_match) >= 0) {
                            assetUrl = asset["browser_download_url"].as<String>();
                            break;
                        }
                        // Fallback for Pico (Rev1) if binary doesn't explicitly say "pico" but isn't "c3"
                        #if !defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(ESP32C3)
                        if (name.indexOf("c3") == -1 && assetUrl == "") { 
                             assetUrl = asset["browser_download_url"].as<String>();
                        }
                        #endif
                    }
                }
                
                if (assetUrl != "") {
                    html += "<p>Found compatible binary.</p>";
                    html += "<form method='POST' action='/github_update'>";
                    html += "<input type='hidden' name='url' value='" + assetUrl + "'>";
                    html += "<button type='submit' class='btn'>Install Update</button>";
                    html += "</form>";
                } else {
                    html += "<p style='color:#dc3545;'>No compatible binary found for this board (" + String(target_match) + ").</p>";
                }
            } else {
                html += "<p>Your firmware is up to date.</p>";
            }
            html += "<br><a href='/' style='color:#666;text-decoration:none;'>&laquo; Back to Dashboard</a></div></body></html>";
            server.send(200, "text/html", html);
        } else {
            server.send(500, "text/plain", "GitHub API Error: " + String(httpCode));
        }
        https.end();
    } else {
        server.send(500, "text/plain", "Connection Failed");
    }
}

void WebInterface::handleGitHubUpdate() {
    String url = server.arg("url");
    if (url == "") {
        server.send(400, "text/plain", "Missing URL");
        return;
    }
    
    server.send(200, "text/html", "<html><head><meta http-equiv='refresh' content='20;url=/'><style>body{font-family:sans-serif;padding:50px;text-align:center;}</style></head><body><h1>Updating...</h1><p>Downloading and installing firmware.</p><p>The device will reboot automatically.</p></body></html>");
    server.client().flush(); // Send response immediately
    
    WiFiClientSecure client;
    client.setInsecure();
    
    // Increase buffer size for faster download
    #ifdef STATUS_LED_PIN
    httpUpdate.setLedPin(STATUS_LED_PIN, LOW);
    #endif
    t_httpUpdate_return ret = httpUpdate.update(client, url);
    
    if (ret == HTTP_UPDATE_FAILED) {
        TelnetStream.printf("OTA Failed: %s\n", httpUpdate.getLastErrorString().c_str());
    }
}

void WebInterface::handleDiscovery() {
    publish_discovery();
    server.send(200, "text/html", "<html><head><meta http-equiv='refresh' content='3;url=/'></head><body><h1>Discovery Sent</h1><p>Home Assistant discovery messages have been resent.</p></body></html>");
}

void WebInterface::handleToggleTelnet() {
    configManager.enable_telnet = !configManager.enable_telnet;
    // We need to save this setting specifically
    // configManager.save() saves everything, which is fine
    configManager.save();
    server.send(200, "text/html", "<html><head><meta http-equiv='refresh' content='5;url=/'></head><body><h1>Saved</h1><p>Telnet setting updated. Rebooting...</p></body></html>");
    delay(1000);
    ESP.restart();
}

void WebInterface::handleToggleDarkMode() {
    configManager.enable_dark_mode = !configManager.enable_dark_mode;
    configManager.save();
    server.sendHeader("Location", "/", true);
    server.send(303, "text/plain", "");
}