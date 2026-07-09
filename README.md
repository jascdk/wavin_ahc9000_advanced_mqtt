# Wavin AHC 9000 MQTT Gateway

**Developed by Jacob J. Scherrebeck**

This project connects your **Wavin AHC 9000** underfloor heating controller to **Home Assistant** via MQTT. It runs on an ESP32 and provides full control over your heating system, including advanced features like Boost, Vacation Mode, and Valve Maintenance.

## Features

### 🏠 Smart Home Integration
*   **Home Assistant Auto Discovery**: No YAML configuration needed. Devices appear automatically.
*   **Full Room Control**: View current temperature, set target temperature, and monitor battery levels for every room.
*   **Danish Language Support**: Correctly handles room names with Æ, Ø, Å.
*   **Telemetry**: Reports system health (Uptime, RSSI, IP, Heap) via MQTT in Tasmota-compatible format.

### 🚀 Advanced Features
*   **Boost Mode**: Quickly heat up the house (Rooms 2-16) to a configurable temperature (default 24°C) for 1 hour. Great for chilly mornings.
 *   **Vacation Mode**: Set the entire house to a low temperature (e.g., 15°C) using the "Away" preset.
*   **Valve Maintenance**: Automatically exercises the valves (opens them fully) once a week to prevent them from seizing up during summer.
*   **Granular Control**: Define exactly which rooms participate in Boost, Vacation, Maintenance, and Master Lock using flexible masks (e.g., "1, 2, 5-10").
*   **Power Monitoring**: Real-time monitoring of current draw (mA) for individual valves and the total system to detect faulty actuators.
*   **Master Limits**: Set global Minimum/Maximum temperature limits and Alarm thresholds for all rooms.
*   **Status LED**: Visual feedback on the device for WiFi/MQTT connection status.

### ⚙️ System Management
*   **Web Interface**: Built-in dashboard to view system status, signal strength, and perform OTA firmware updates.
*   **System Info**: Detailed view of network, MQTT, and hardware status.
*   **Easy Setup**: Connect to the "WavinGateway_Setup" WiFi network to configure MQTT and Room Names from your phone.
*   **Master Control**: A virtual "Master" thermostat to control global settings for the whole house.

## Hardware Required

1.  **ESP32 Development Board** (e.g., ESP32 Pico Kit, NodeMCU, Wemos D1 Mini ESP32).
2.  **RS485 to TTL Module** (Must be 3.3V compatible).

> **Tip:** A custom ESP32 Modbus Module designed specifically for this purpose is available from **Mogens Groth Nicolaisen**. It integrates the ESP32 and RS485 on a single board. Check it out here: [ESP32_Modbus_Module](https://github.com/nic6911/ESP32_Modbus_Module).

### Wiring

The project supports two hardware revisions. Please use the wiring diagram that matches your board.

#### Rev. 1 Board (ESP32-Pico)

| ESP32 Pin | RS485 Module | Description |
|-----------|--------------|-------------|
| **GPIO 13** | **RO** (RX) | Receive Data from Wavin |
| **GPIO 14** | **DI** (TX) | Transmit Data to Wavin |
| **GPIO 26** | **DE** & **RE** | Flow Control (Connect both pins to GPIO 26) |
| **3.3V**    | **VCC**     | Power (Do not use 5V) |
| **GND**     | **GND**     | Ground |

#### Rev. 2 Board (ESP32-C3)

This board does not have a status LED.

| ESP32 Pin | RS485 Module | Description |
|-----------|--------------|-------------|
| **GPIO 20** | **RO** (RX) | Receive Data from Wavin |
| **GPIO 21** | **DI** (TX) | Transmit Data to Wavin |
| **GPIO 10** | **DE** & **RE** | Flow Control |
| **3.3V**    | **VCC**     | Power |
| **GND**     | **GND**     | Ground |

*Note: Connect the RS485 module's A and B terminals to the Wavin AHC 9000 bus terminals.*

## Installation

1.  Open the project in **PlatformIO** (VS Code).
2.  Connect your ESP32 via USB.
3.  Upload the firmware.

## Setup & Configuration

### 1. First Boot
When powered on for the first time, the ESP32 will create a WiFi Hotspot named **`WavinGateway_Setup`**.

1.  Connect to this network with your phone or laptop.
2.  A configuration page should open automatically (if not, visit `192.168.4.1`).
3.  **Configure:**
    *   **WiFi**: Select your home network and enter the password.
    *   **MQTT**: Enter your Broker IP, Username, and Password.
    *   **Master Features**: Select which global controls to expose (Alarms, Hysteresis, Lock, Min/Max Limits).
    *   **Room Masks**: Define which rooms belong to specific features (e.g., "1-16" for Vacation, "2-16" for Boost).
    *   **Room Names**: Rename the channels (e.g., "Stue", "Køkken"). Default Danish names are pre-filled.
4.  Click **Save**. The device will reboot and connect.

> **Note on Masks:** You can enter ranges (`2-16`) or comma-separated lists (`1, 5, 7`) to define exactly which rooms are affected by specific features.

### 2. Home Assistant
Go to **Settings > Devices & Services > MQTT**. You will see a new device: **Wavin AHC 9000 Gateway**.

Inside, you will find:
*   **Climate Entities**: One for each active room.
*   **Sensors**: Battery levels, Valve status (Open/Closed), and Total System Current.
*   **Master Control**: A dedicated "Master Climate" entity and configuration controls for global settings.

## Web Interface

You can access the gateway's dashboard by visiting its IP address in a browser.

*   **Dashboard**: View Uptime, WiFi Signal (RSSI), MQTT Status, and System Time.
*   **Firmware Update**: Upload a `.bin` file to update the device over-the-air (OTA).
*   **Check for Updates**: One-click check against GitHub releases to download and install the latest firmware.
*   **Debug**: Toggle Telnet logging on/off to save resources when not debugging.
*   **System Control**: Reboot or Factory Reset the device.

## Status LED
If your board has a status LED (defaulting to GPIO 33 on the Rev. 1 board), it will indicate the following:
*   **Fast Blink (10Hz)**: AP Mode (Configuration Portal active).
*   **Medium Blink (2Hz)**: Connecting to WiFi.
*   **Slow Blink (0.5Hz)**: WiFi Connected, but MQTT disconnected.
*   **Heartbeat (Double Pulse)**: Normal Operation (WiFi & MQTT Connected).

*Note: This feature is automatically disabled when compiling for boards without an LED, like the Rev. 2 hardware.*

## Advanced Features

### 🎛️ Master Control
The **Master Climate** entity allows you to control multiple rooms at once.
*   **Target Temp**: Sets the setpoint for all "Master Rooms".
*   **Min/Max Limits**: Restricts the temperature range users can set on physical thermostats.
*   **Alarms**: Sets High/Low temperature alarm thresholds.
*   **Lock**: Locks the physical interface of the thermostats (configurable via "Lock Rooms Mask").

### 🌡️ Boost Mode
*   **What it does**: Sets selected rooms to a high temperature (configurable, default 24°C) for 1 hour.
*   **How to use**: Press the "Boost Heating" button in Home Assistant.
*   **Configuration**: Use the "Boost Rooms" mask in setup to exclude specific rooms (e.g., bedroom or workshop).

### ✈️ Vacation Mode
*   **How to use**: Select the **"Away"** preset on the **Master Climate** entity in Home Assistant.
*   **Effect**: Sets all configured rooms to the "Holiday Temperature" (default 15°C).
*   **Configuration**: Use the "Vacation Rooms" mask in setup to define which rooms are affected.

### 🔧 Valve Maintenance
*   **What it does**: Prevents valves from getting stuck by opening them fully for a short period.
*   **Default**: Runs every Sunday at 03:00 AM for 15 minutes.
*   **Config**: You can change the Day, Time, and Duration in Home Assistant.

## Troubleshooting

**Q: I don't see all my rooms?**
A: The gateway only creates entities for channels that have a thermostat connected. Check your wiring or perform a new scan by rebooting the ESP32.

**Q: The Web UI is slow.**
A: The gateway prioritizes Modbus communication to ensure heating control is reliable. A slight delay in the Web UI is normal while it scans the Wavin controller.

**Q: How do I reset the WiFi settings?**
A: You can either:
1.  Use the "Factory Reset" button in the Web UI.
2.  Hold the **BOOT** button on the ESP32 for 3 seconds during startup.

**Q: Debugging?**
A: Connect via USB and use a Serial Monitor (115200 baud) or connect via Telnet to the device's IP to see live logs.

## License

MIT License