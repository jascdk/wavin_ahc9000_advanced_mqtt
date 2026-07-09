#ifndef WAVINCONTROLLER_H
#define WAVINCONTROLLER_H

#include <Arduino.h>

#define WAVIN_CHANNELS 16
#define WAVIN_MODBUS_ADDRESS 1

struct WavinChannel {
    bool isLost = false;
    bool outputActive = false;
    bool hasThermostat = false;
    float targetTemp = 0.0;
    float manualTemp = 0.0;
    float comfortTemp = 0.0;
    float ecoTemp = 0.0;
    float holidayTemp = 0.0;
    float minTemp = 0.0;
    float maxTemp = 0.0;
    float floorMinTemp = 0.0;
    float floorMaxTemp = 0.0;
    bool alarmLowTriggered = false;
    bool alarmHighTriggered = false;
    float alarmLowTemp = 0.0;
    float alarmHighTemp = 0.0;
    float standbyTemp = 0.0;
    String mode = "heat";
    bool intLock = false;
    float hysteresis = 0.0;
    float currentTemp = 0.0;
    float floorTemp = 0.0;
    int batteryLevel = 0;
    float rssi = 0.0;
};

struct WavinDeviceInfo {
    uint32_t address = 0;
    String hwVersion = "";
    String swVersion = "";
    String deviceName = "";
};

class WavinController {
public:
    WavinController(HardwareSerial& serial, int dePin, int rePin);
    void begin(int rxPin, int txPin);
    bool loop();
    bool isInitialized() { return _initialized; }
    WavinChannel getChannelData(int channelIndex);
    WavinDeviceInfo getDeviceInfo() { return _deviceInfo; }
    
    bool setTargetTemp(int channelIndex, float temp);
    bool setComfortTemp(int channelIndex, float temp);
    bool setEcoTemp(int channelIndex, float temp);
    bool setHolidayTemp(int channelIndex, float temp);
    bool setMinTemp(int channelIndex, float temp);
    bool setMaxTemp(int channelIndex, float temp);
    bool setFloorMinTemp(int channelIndex, float temp);
    bool setFloorMaxTemp(int channelIndex, float temp);
    bool setAlarmLowTemp(int channelIndex, float temp);
    bool setAlarmHighTemp(int channelIndex, float temp);
    bool setStandbyTemp(int channelIndex, float temp);
    bool setHysteresis(int channelIndex, float temp);
    bool setMode(int channelIndex, String mode);
    bool setIntLock(int channelIndex, bool locked);
    bool syncClock(uint16_t year, uint16_t month, uint16_t day, uint16_t weekday, uint16_t hour, uint16_t minute, uint16_t second);

private:
    HardwareSerial& _serial;
    int _dePin;
    int _rePin;
    unsigned long _lastReadTime;
    WavinChannel _channels[WAVIN_CHANNELS];
    WavinDeviceInfo _deviceInfo;
    bool _initialized = false;

    void updateChannels();
    void updateDeviceInfo();
    uint16_t calculateCRC(const uint8_t* data, uint8_t len);
    void sendPacket(uint8_t* buffer, uint8_t len);
    bool readResponse(uint8_t fnCode, uint8_t expectedBytes, uint16_t* dataBuffer);
    bool readRegisters(uint8_t category, uint8_t page, uint8_t index, uint8_t qty, uint16_t* dest);
    bool writeRegister(uint8_t category, uint8_t page, uint8_t index, uint16_t value);
    bool writeRegisters(uint8_t category, uint8_t page, uint8_t index, uint8_t qty, uint16_t* data);
    bool writeMaskedRegister(uint8_t category, uint8_t page, uint8_t index, uint16_t value, uint16_t mask);
};

#endif