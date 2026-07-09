#include "WavinController.h"
#include <TelnetStream.h>
#include "AppConfig.h"

float g_wavinChannelCurrent[WAVIN_CHANNELS];
float g_wavinTotalCurrent = 0.0;

WavinController::WavinController(HardwareSerial& serial, int dePin, int rePin) 
    : _serial(serial), _dePin(dePin), _rePin(rePin) {
}

void WavinController::begin(int rxPin, int txPin) {
    if (_dePin >= 0) pinMode(_dePin, OUTPUT);
    if (_rePin >= 0) pinMode(_rePin, OUTPUT);
    if (_dePin >= 0) digitalWrite(_dePin, LOW);
    if (_rePin >= 0) digitalWrite(_rePin, LOW);

    // Wavin AHC 9000 Spec: 38400 bps, 8N1
    _serial.begin(WAVIN_BAUD_RATE, WAVIN_CONFIG, rxPin, txPin);
    
    _lastReadTime = 0;
}

bool WavinController::loop() {
    // Read data every 10 seconds (Heating systems are slow, 2s is overkill and blocks UI)
    if (millis() - _lastReadTime > 10000 || _lastReadTime == 0) {
        updateChannels();
        _lastReadTime = millis();
        return true;
    }
    return false;
}

void WavinController::updateChannels() {
    uint16_t buffer[16]; // Increase buffer size to be safe
    TelnetStream.println("Starting Modbus Scan...");

    // Only read device info if we haven't successfully read it yet
    if (_deviceInfo.address == 0) updateDeviceInfo();

    // Read Total System Current (Category 0x00, Index 0x10, Qty 2)
    if (readRegisters(0x00, 0x00, 0x10, 2, buffer)) {
        uint32_t rawTotal = (buffer[1] << 16) | buffer[0];
        g_wavinTotalCurrent = rawTotal * 0.54f;
    }

    for (int ch = 0; ch < WAVIN_CHANNELS; ch++) {
        // 1. Get Channel Status & Primary Element
        // Category 0x03 (Channels), Page ch, Index 0 (Timer Event) -> Read 3 registers
        if (readRegisters(0x03, ch, 0x00, 3, buffer)) {
            bool outputOn = (buffer[0] & (1 << 4)); // Bit 4 is Output/Valve Active
            g_wavinChannelCurrent[ch] = buffer[1] * 0.54f; // Index 0x01 is Current
            _channels[ch].isLost = (buffer[0] & (1 << 11)) != 0; // Bit 11 is LOST flag
            uint16_t primaryElementReg = buffer[2];
            int primaryElementIndex = primaryElementReg & 0x3F; // Lower 6 bits
            
            _channels[ch].hasThermostat = (primaryElementIndex > 0);
            _channels[ch].outputActive = outputOn;
            _channels[ch].alarmLowTriggered = (primaryElementReg & (1 << 8)) != 0;
            _channels[ch].alarmHighTriggered = (primaryElementReg & (1 << 9)) != 0;
            
            // DEBUG: Check Channel Unknowns (Category 0x03, Index 0x01)
            if (ch == 0) {
                 TelnetStream.printf("CH1 Channel Unknown: [0x01]=%d\n", buffer[1]);
            }
            
            delay(10); // Give controller time to breathe

            // 2. Get all Packed Data registers in one read (0x00 to 0x0E)
            // Category 0x02 (Packed Data), Page ch
            if (readRegisters(0x02, ch, 0x00, 15, buffer)) {
                // Index 0x01 & 0x02: Comfort & Eco Temp
                _channels[ch].comfortTemp = buffer[1] / 10.0f;
                _channels[ch].ecoTemp = buffer[2] / 10.0f;
                // Index 0x03: Holiday Temp
                _channels[ch].holidayTemp = buffer[3] / 10.0f;
                // Index 0x04: Standby Temp
                _channels[ch].standbyTemp = buffer[4] / 10.0f;

                // Index 0x07: Configuration (Mode & Lock)
                int modeBits = buffer[7] & 0x07; // Bits 0-2
                // 0=Manual, 1=Standby, 2=Eco, 3=Comfort
                if (modeBits == 1) {
                    _channels[ch].mode = "off";
                    _channels[ch].targetTemp = _channels[ch].standbyTemp;
                } else if (modeBits == 2) {
                    _channels[ch].mode = "eco";
                    _channels[ch].targetTemp = _channels[ch].ecoTemp;
                } else if (modeBits == 3) {
                    _channels[ch].mode = "comfort";
                    _channels[ch].targetTemp = _channels[ch].comfortTemp;
                } else {
                    _channels[ch].mode = "heat";
                    _channels[ch].targetTemp = buffer[0] / 10.0f; // In heat mode, Reg 0 is target
                }
                
                // Update manualTemp only if in Heat mode, or if not initialized
                // This prevents overwriting manualTemp if the controller mirrors active setpoint to Reg 0 in other modes
                if (_channels[ch].mode == "heat" || _channels[ch].manualTemp == 0.0) {
                    _channels[ch].manualTemp = buffer[0] / 10.0f;
                }

                _channels[ch].intLock = (buffer[7] & 0x0800) != 0; // Bit 11

                // Index 0x08 & 0x09: Min/Max Temp
                _channels[ch].minTemp = buffer[8] / 10.0f;
                _channels[ch].maxTemp = buffer[9] / 10.0f;

                // Index 0x0A & 0x0B: Floor Min/Max Temp
                _channels[ch].floorMinTemp = buffer[10] / 10.0f;
                _channels[ch].floorMaxTemp = buffer[11] / 10.0f;

                // Index 0x0C, 0x0D, 0x0E: Alarm Limits & Hysteresis
                _channels[ch].alarmLowTemp = (int16_t)buffer[12] / 10.0f;
                _channels[ch].alarmHighTemp = buffer[13] / 10.0f;
                _channels[ch].hysteresis = buffer[14] / 10.0f;
            }

            delay(10); // Give controller time to breathe

            // 4. Get Current Temp & Battery (Only if element assigned)
            if (primaryElementIndex > 0) {
                // Element pages are 0-indexed, so ID 1 is Page 0
                int elementPage = primaryElementIndex - 1;
                
                // Read Elements Category 0x01, Index 0x04 (Air Temp) to 0x0A (Battery)
                if (readRegisters(0x01, elementPage, 0x04, 7, buffer)) {
                    uint16_t rawTemp = buffer[0]; // Index 0x04
                    uint16_t rawBatt = buffer[6]; // Index 0x0A
                    
                    if (rawTemp != 0x7FFF) {
                        _channels[ch].currentTemp = (int16_t)rawTemp / 10.0f;
                    }

                    // Floor Temp (Index 0x05 is at buffer[1])
                    uint16_t rawFloor = buffer[1];
                    if (rawFloor != 0x7FFF) {
                        _channels[ch].floorTemp = (int16_t)rawFloor / 10.0f;
                    }

                    int battVal = rawBatt & 0x0F;
                    // Refine battery level: Map 9 (often seen on new batteries) and 10 to 100%
                    if (battVal >= 9) _channels[ch].batteryLevel = 100;
                    else _channels[ch].batteryLevel = battVal * 10;

                    // RSSI (Index 0x09 is at buffer[5])
                    // Low byte is CU signal strength. 0 = -74dBm, step 0.5dBm
                    int8_t rawRssi = (buffer[5] & 0xFF);
                    _channels[ch].rssi = (rawRssi * 0.5f) - 74.0f;
                }
            } else {
                _channels[ch].currentTemp = 0.0f;
                _channels[ch].batteryLevel = 0;
                _channels[ch].floorTemp = 0.0f;
            }
        }
        delay(20); // Small delay between channels
    }
    _initialized = true;
}

void WavinController::updateDeviceInfo() {
    uint16_t buffer[5];
    // Category 0x07 (Info), Page 0, Index 0, Qty 5
    // [0]: Addr L, [1]: Addr H, [2]: HW Vers, [3]: SW Vers, [4]: Dev Name
    if (readRegisters(0x07, 0x00, 0x00, 5, buffer)) {
        _deviceInfo.address = ((uint32_t)buffer[1] << 16) | buffer[0];
        
        // HW Version: MC110xx (xx = decimal)
        _deviceInfo.hwVersion = "MC110" + String(buffer[2] & 0x7F);
        
        // SW Version: MC610xx (xx = BCD) + Beta
        uint16_t swRaw = buffer[3];
        uint8_t swVers = (swRaw >> 4) & 0xFF; // Bits 4-11
        uint8_t beta = swRaw & 0x0F;          // Bits 0-3
        
        char swBuf[5];
        sprintf(swBuf, "%02X", swVers); // Format as BCD (Hex -> String)
        _deviceInfo.swVersion = "MC610" + String(swBuf);
        if (beta > 0) _deviceInfo.swVersion += "b" + String(beta);
        
        // Device Name: AC-xxx (xxx = decimal)
        _deviceInfo.deviceName = "AC-" + String(buffer[4]);
    }
}

WavinChannel WavinController::getChannelData(int channelIndex) {
    if (channelIndex < 0 || channelIndex >= WAVIN_CHANNELS) return WavinChannel();
    return _channels[channelIndex];
}

bool WavinController::setTargetTemp(int channelIndex, float temp) {
    uint16_t rawTemp = (uint16_t)(temp * 10);
    // Write to Category 0x02 (Packed Data), Page channelIndex, Index 0x00 (Manual Temp)
    if (writeRegister(0x02, channelIndex, 0x00, rawTemp)) {
        _channels[channelIndex].manualTemp = temp;
        // Only update targetTemp if we are in Heat mode
        if (_channels[channelIndex].mode == "heat") {
            _channels[channelIndex].targetTemp = temp;
        }
        return true;
    }
    return false;
}

bool WavinController::setComfortTemp(int channelIndex, float temp) {
    uint16_t rawTemp = (uint16_t)(temp * 10);
    // Write to Category 0x02, Page channelIndex, Index 0x01 (Comfort Temp)
    if (writeRegister(0x02, channelIndex, 0x01, rawTemp)) {
        _channels[channelIndex].comfortTemp = temp;
        return true;
    }
    return false;
}

bool WavinController::setEcoTemp(int channelIndex, float temp) {
    uint16_t rawTemp = (uint16_t)(temp * 10);
    // Write to Category 0x02, Page channelIndex, Index 0x02 (Eco Temp)
    if (writeRegister(0x02, channelIndex, 0x02, rawTemp)) {
        _channels[channelIndex].ecoTemp = temp;
        return true;
    }
    return false;
}

bool WavinController::setHolidayTemp(int channelIndex, float temp) {
    uint16_t rawTemp = (uint16_t)(temp * 10);
    // Write to Category 0x02, Page channelIndex, Index 0x03 (Holiday Temp)
    if (writeRegister(0x02, channelIndex, 0x03, rawTemp)) {
        _channels[channelIndex].holidayTemp = temp;
        return true;
    }
    return false;
}

bool WavinController::setStandbyTemp(int channelIndex, float temp) {
    uint16_t rawTemp = (uint16_t)(temp * 10);
    // Write to Category 0x02, Page channelIndex, Index 0x04 (Standby Temp)
    if (writeRegister(0x02, channelIndex, 0x04, rawTemp)) {
        _channels[channelIndex].standbyTemp = temp;
        // If currently in standby ("off") mode, update targetTemp immediately for UI responsiveness
        if (_channels[channelIndex].mode == "off") {
            _channels[channelIndex].targetTemp = temp;
        }
        return true;
    }
    return false;
}

bool WavinController::setMinTemp(int channelIndex, float temp) {
    uint16_t rawTemp = (uint16_t)(temp * 10);
    // Write to Category 0x02, Page channelIndex, Index 0x08 (Min Temp)
    if (writeRegister(0x02, channelIndex, 0x08, rawTemp)) {
        _channels[channelIndex].minTemp = temp;
        return true;
    }
    return false;
}

bool WavinController::setMaxTemp(int channelIndex, float temp) {
    uint16_t rawTemp = (uint16_t)(temp * 10);
    // Write to Category 0x02, Page channelIndex, Index 0x09 (Max Temp)
    if (writeRegister(0x02, channelIndex, 0x09, rawTemp)) {
        _channels[channelIndex].maxTemp = temp;
        return true;
    }
    return false;
}

bool WavinController::setFloorMinTemp(int channelIndex, float temp) {
    uint16_t rawTemp = (uint16_t)(temp * 10);
    // Write to Category 0x02, Page channelIndex, Index 0x0A (Floor Min Temp)
    if (writeRegister(0x02, channelIndex, 0x0A, rawTemp)) {
        _channels[channelIndex].floorMinTemp = temp;
        return true;
    }
    return false;
}

bool WavinController::setFloorMaxTemp(int channelIndex, float temp) {
    uint16_t rawTemp = (uint16_t)(temp * 10);
    // Write to Category 0x02, Page channelIndex, Index 0x0B (Floor Max Temp)
    if (writeRegister(0x02, channelIndex, 0x0B, rawTemp)) {
        _channels[channelIndex].floorMaxTemp = temp;
        return true;
    }
    return false;
}

bool WavinController::setAlarmLowTemp(int channelIndex, float temp) {
    uint16_t rawTemp = (uint16_t)(int16_t)(temp * 10);
    // Write to Category 0x02, Page channelIndex, Index 0x0C (Alarm Low)
    if (writeRegister(0x02, channelIndex, 0x0C, rawTemp)) {
        _channels[channelIndex].alarmLowTemp = temp;
        return true;
    }
    return false;
}

bool WavinController::setAlarmHighTemp(int channelIndex, float temp) {
    uint16_t rawTemp = (uint16_t)(int16_t)(temp * 10);
    // Write to Category 0x02, Page channelIndex, Index 0x0D (Alarm High)
    if (writeRegister(0x02, channelIndex, 0x0D, rawTemp)) {
        _channels[channelIndex].alarmHighTemp = temp;
        return true;
    }
    return false;
}

bool WavinController::setHysteresis(int channelIndex, float temp) {
    uint16_t rawTemp = (uint16_t)(temp * 10);
    // Write to Category 0x02, Page channelIndex, Index 0x0E (Hysteresis)
    if (writeRegister(0x02, channelIndex, 0x0E, rawTemp)) {
        _channels[channelIndex].hysteresis = temp;
        return true;
    }
    return false;
}

bool WavinController::setMode(int channelIndex, String mode) {
    uint16_t value = 0;
    if (mode == "off") value = 1; // Permanent Standby
    else if (mode == "heat") value = 0; // Manual
    else if (mode == "eco") value = 2;
    else if (mode == "comfort") value = 3;
    
    // Mask 0xFFF0 clears bits 0-3: bits 0-2 are MODE, bit 3 is SCHED_ENA.
    // Clearing SCHED_ENA ensures the device enters a permanent (non-schedule) mode
    // and does not revert to following its weekly schedule after a few seconds.
    if (writeMaskedRegister(0x02, channelIndex, 0x07, value, 0xFFF0)) {
        _channels[channelIndex].mode = mode;
        
        // Update targetTemp immediately for UI responsiveness
        if (mode == "off") _channels[channelIndex].targetTemp = _channels[channelIndex].standbyTemp;
        else if (mode == "eco") _channels[channelIndex].targetTemp = _channels[channelIndex].ecoTemp;
        else if (mode == "comfort") _channels[channelIndex].targetTemp = _channels[channelIndex].comfortTemp;
        else if (mode == "heat") _channels[channelIndex].targetTemp = _channels[channelIndex].manualTemp;
        return true;
    }
    return false;
}

bool WavinController::setIntLock(int channelIndex, bool locked) {
    uint16_t value = locked ? 0x0800 : 0x0000; // Bit 11
    uint16_t mask = 0xF7FF; // 1111 0111 1111 1111 (Bit 11 is 0, others 1)
    
    if (writeMaskedRegister(0x02, channelIndex, 0x07, value, mask)) {
        _channels[channelIndex].intLock = locked;
        return true;
    }
    return false;
}

bool WavinController::syncClock(uint16_t year, uint16_t month, uint16_t day, uint16_t weekday, uint16_t hour, uint16_t minute, uint16_t second) {
    uint16_t data[7];
    data[0] = year;
    data[1] = month;
    data[2] = day;
    data[3] = weekday;
    data[4] = hour;
    data[5] = minute;
    data[6] = second;
    return writeRegisters(0x05, 0x00, 0x00, 7, data);
}

// --- Low Level Modbus Implementation ---

uint16_t WavinController::calculateCRC(const uint8_t* data, uint8_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xA001;
            else crc >>= 1;
        }
    }
    return crc;
}

void WavinController::sendPacket(uint8_t* buffer, uint8_t len) {
    // Clear any garbage in RX buffer before sending
    while (_serial.available()) _serial.read();

    TelnetStream.print("TX: ");
    for(int i=0; i<len; i++) TelnetStream.printf("%02X ", buffer[i]);
    TelnetStream.println();

    if (_dePin >= 0) digitalWrite(_dePin, HIGH);
    if (_rePin >= 0) digitalWrite(_rePin, HIGH);
    delay(1); // Pre-transmission delay (matches dkjonas)
    _serial.write(buffer, len);
    _serial.flush();
    delayMicroseconds(600); // Wait ~2 char times for stop bit/line turnaround
    if (_dePin >= 0) digitalWrite(_dePin, LOW);
    if (_rePin >= 0) digitalWrite(_rePin, LOW);
}

bool WavinController::readResponse(uint8_t fnCode, uint8_t expectedBytes, uint16_t* dataBuffer) {
    unsigned long start = millis();
    int totalLen = 3 + expectedBytes + 2; // Default: Header(3) + Data + CRC(2)
    uint8_t rxBuf[64];
    int idx = 0;

    while (millis() - start < 500) { // Increased to 500ms for write operations
        if (_serial.available()) {
            rxBuf[idx++] = _serial.read();
            
            // Check for Exception Response (Function Code + 0x80)
            // Frame: [Addr] [Fn|0x80] [ErrCode] [CRC] [CRC] = 5 bytes
            if (idx == 2 && (rxBuf[1] & 0x80)) {
                totalLen = 5; 
            }
            
            if (idx >= totalLen) break;
        }
    }

    if (idx < totalLen) {
        TelnetStream.print("Modbus Timeout. RX Bytes: ");
        for(int i=0; i<idx; i++) TelnetStream.printf("%02X ", rxBuf[i]);
        TelnetStream.println();
        return false;
    }
    
    uint16_t receivedCRC = rxBuf[totalLen-2] | (rxBuf[totalLen-1] << 8);
    uint16_t calculatedCRC = calculateCRC(rxBuf, totalLen-2);
    if (receivedCRC != calculatedCRC) {
        TelnetStream.print("Modbus CRC Error. RX: ");
        for(int i=0; i<idx; i++) TelnetStream.printf("%02X ", rxBuf[i]);
        TelnetStream.println();
        return false;
    }

    // Handle Exception
    if (rxBuf[1] & 0x80) {
        TelnetStream.printf("Modbus Exception Error Code: %02X\n", rxBuf[2]);
        return false;
    }

    if (rxBuf[1] != fnCode) return false;

    // Only parse data for Read Function (0x43)
    if (fnCode == 0x43) {
        int dataCount = rxBuf[2] / 2;
        for (int i = 0; i < dataCount; i++) {
            dataBuffer[i] = (rxBuf[3 + i*2] << 8) | rxBuf[4 + i*2];
        }
    }
    return true;
}

bool WavinController::readRegisters(uint8_t category, uint8_t page, uint8_t index, uint8_t qty, uint16_t* dest) {
    uint8_t frame[8];
    frame[0] = WAVIN_MODBUS_ADDRESS;
    frame[1] = 0x43;
    frame[2] = category;
    frame[3] = index;
    frame[4] = page;
    frame[5] = qty;
    uint16_t crc = calculateCRC(frame, 6);
    frame[6] = crc & 0xFF;
    frame[7] = crc >> 8;

    sendPacket(frame, 8);
    return readResponse(0x43, qty * 2, dest);
}

bool WavinController::writeRegister(uint8_t category, uint8_t page, uint8_t index, uint16_t value) {
    uint8_t frame[10];
    frame[0] = WAVIN_MODBUS_ADDRESS;
    frame[1] = 0x44;
    frame[2] = category;
    frame[3] = index;
    frame[4] = page;
    frame[5] = 1;
    frame[6] = value >> 8;
    frame[7] = value & 0xFF;
    uint16_t crc = calculateCRC(frame, 8);
    frame[8] = crc & 0xFF;
    frame[9] = crc >> 8;

    sendPacket(frame, 10);
    uint16_t dummy;
    // Response is Addr(1) + Fn(1) + ByteCount(1) + Data(2) + CRC(2) = 7 bytes
    return readResponse(0x44, 2, &dummy);
}

bool WavinController::writeRegisters(uint8_t category, uint8_t page, uint8_t index, uint8_t qty, uint16_t* data) {
    int len = 6 + qty * 2 + 2; // Header(6) + Data(qty*2) + CRC(2)
    uint8_t frame[32]; // Ensure buffer is large enough
    
    frame[0] = WAVIN_MODBUS_ADDRESS;
    frame[1] = 0x44;
    frame[2] = category;
    frame[3] = index;
    frame[4] = page;
    frame[5] = qty;
    for (int i = 0; i < qty; i++) {
        frame[6 + i*2] = data[i] >> 8;
        frame[7 + i*2] = data[i] & 0xFF;
    }
    uint16_t crc = calculateCRC(frame, len - 2);
    frame[len - 2] = crc & 0xFF;
    frame[len - 1] = crc >> 8;

    sendPacket(frame, len);
    uint16_t dummy;
    // For Write Multiple (0x44) with Qty > 1 (like Clock), the device echoes the data.
    // Response: Addr(1) + Fn(1) + ByteCount(1) + Data(Qty*2) + CRC(2)
    // readResponse adds 3 (Header) + 2 (CRC) to expectedBytes.
    return readResponse(0x44, qty * 2, &dummy);
}

bool WavinController::writeMaskedRegister(uint8_t category, uint8_t page, uint8_t index, uint16_t value, uint16_t mask) {
    uint8_t frame[12];
    frame[0] = WAVIN_MODBUS_ADDRESS;
    frame[1] = 0x45;
    frame[2] = category;
    frame[3] = index;
    frame[4] = page;
    frame[5] = 1;
    frame[6] = value >> 8;
    frame[7] = value & 0xFF;
    frame[8] = mask >> 8;
    frame[9] = mask & 0xFF;
    uint16_t crc = calculateCRC(frame, 10);
    frame[10] = crc & 0xFF;
    frame[11] = crc >> 8;

    sendPacket(frame, 12);
    uint16_t dummy;
    // Response is Addr(1) + Fn(1) + ByteCount(1) + Data(2) + CRC(2) = 7 bytes
    return readResponse(0x45, 2, &dummy);
}