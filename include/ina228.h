/**
 * @file ina228.h
 * @brief Przetestowany laboratoryjnie sterownik Texas Instruments INA228 (20-bit I2C)
 * Zawiera poprawki metrologiczne:
 * - VBUS traktowane poprawnie jako BEZZNAKOWE 20-bit (zakres 0 do 85V bez błędów znaku)
 * - VSHUNT traktowane jako DWUKIERUNKOWE ze znakiem (two's complement)
 * - Sprzętowe uśrednianie do 1024x oraz czuły zakres +/-40.96mV
 */

#ifndef INA228_H
#define INA228_H

#include <Arduino.h>
#include <Wire.h>

#define INA228_I2C_ADDR_DEFAULT 0x40

#define INA228_REG_CONFIG       0x00
#define INA228_REG_ADC_CONFIG   0x01
#define INA228_REG_SHUNT_CAL    0x02
#define INA228_REG_SHUNT_TEMPCO 0x03
#define INA228_REG_VSHUNT       0x04 // 24-bit (20-bit ze znakiem)
#define INA228_REG_VBUS         0x05 // 24-bit (20-bit BEZ ZNAKU!)
#define INA228_REG_DIETEMP      0x06 // 16-bit
#define INA228_REG_CURRENT      0x07 // 24-bit (20-bit ze znakiem)
#define INA228_REG_POWER        0x08 // 24-bit
#define INA228_REG_ENERGY       0x09 // 40-bit
#define INA228_REG_CHARGE       0x0A // 40-bit
#define INA228_REG_DIAG_ALERT   0x0B
#define INA228_REG_MANUF_ID     0x3E
#define INA228_REG_DEVICE_ID    0x3F

enum INA228_Averaging {
    INA228_AVG_1    = 0b000,
    INA228_AVG_4    = 0b001,
    INA228_AVG_16   = 0b010,
    INA228_AVG_64   = 0b011,
    INA228_AVG_128  = 0b100,
    INA228_AVG_256  = 0b101,
    INA228_AVG_512  = 0b110,
    INA228_AVG_1024 = 0b111
};

enum INA228_ConvTime {
    INA228_TIME_50_US   = 0b000,
    INA228_TIME_84_US   = 0b001,
    INA228_TIME_150_US  = 0b010,
    INA228_TIME_280_US  = 0b011,
    INA228_TIME_540_US  = 0b100,
    INA228_TIME_1052_US = 0b101,
    INA228_TIME_2074_US = 0b110,
    INA228_TIME_4120_US = 0b111
};

class INA228_Sensor {
private:
    uint8_t _addr;
    TwoWire* _wire;
    float _r_shunt;
    double _current_lsb;
    double _power_lsb;
    bool _high_range;

    uint16_t readRegister16(uint8_t reg) {
        _wire->beginTransmission(_addr);
        _wire->write(reg);
        if (_wire->endTransmission() != 0) return 0;
        _wire->requestFrom(_addr, (uint8_t)2);
        if (_wire->available() < 2) return 0;
        uint16_t val = (_wire->read() << 8) | _wire->read();
        return val;
    }

    // Odczyt 24-bit rejestru ZE ZNAKIEM (VSHUNT, CURRENT)
    int32_t readRegister24Signed(uint8_t reg) {
        _wire->beginTransmission(_addr);
        _wire->write(reg);
        if (_wire->endTransmission() != 0) return 0;
        _wire->requestFrom(_addr, (uint8_t)3);
        if (_wire->available() < 3) return 0;
        uint32_t raw = ((uint32_t)_wire->read() << 16) | ((uint32_t)_wire->read() << 8) | _wire->read();
        raw >>= 4; // Bity 23:4 to dane pomiarowe
        int32_t val = (int32_t)raw;
        if (val & (1 << 19)) {
            val |= 0xFFF00000; // Rozszerzenie znaku do 32-bit
        }
        return val;
    }

    // Odczyt 24-bit rejestru BEZ ZNAKU (VBUS: 0 do 85V)
    uint32_t readRegister24Unsigned(uint8_t reg) {
        _wire->beginTransmission(_addr);
        _wire->write(reg);
        if (_wire->endTransmission() != 0) return 0;
        _wire->requestFrom(_addr, (uint8_t)3);
        if (_wire->available() < 3) return 0;
        uint32_t raw = ((uint32_t)_wire->read() << 16) | ((uint32_t)_wire->read() << 8) | _wire->read();
        return (raw >> 4);
    }

    void writeRegister16(uint8_t reg, uint16_t value) {
        _wire->beginTransmission(_addr);
        _wire->write(reg);
        _wire->write((value >> 8) & 0xFF);
        _wire->write(value & 0xFF);
        _wire->endTransmission();
    }

public:
    INA228_Sensor(uint8_t addr = INA228_I2C_ADDR_DEFAULT)
        : _addr(addr), _wire(&Wire), _r_shunt(0.015f), _current_lsb(9.5367e-6), _power_lsb(3.0517e-5), _high_range(true) {}

    bool begin(TwoWire* wire = &Wire, float r_shunt = 0.015f, float max_expected_current = 5.0f, bool highPrecisionRange = false) {
        _wire = wire;
        _r_shunt = r_shunt;
        _high_range = !highPrecisionRange;

        uint16_t mfg = readRegister16(INA228_REG_MANUF_ID);
        if (mfg != 0x5449) { // 0x5449 = "TI" w ASCII
            return false;
        }

        // Reset
        writeRegister16(INA228_REG_CONFIG, 0x8000);
        delay(5);

        // Ustawienie ADCRANGE (bit 4): 0 = +/-163.84mV, 1 = +/-40.96mV
        uint16_t cfg = 0x0000;
        if (!_high_range) {
            cfg |= (1 << 4);
        }
        writeRegister16(INA228_REG_CONFIG, cfg);

        // CURRENT_LSB = MaxCurrent / 2^19 (524288)
        _current_lsb = (double)max_expected_current / 524288.0;
        _power_lsb = 3.2 * _current_lsb;

        // SHUNT_CAL
        double scale = _high_range ? 13107200000.0 : 52428800000.0;
        uint16_t shunt_cal = (uint16_t)(scale * _current_lsb * (double)_r_shunt);
        if (shunt_cal > 32767) shunt_cal = 32767;
        writeRegister16(INA228_REG_SHUNT_CAL, shunt_cal);

        // Domyślnie AVG 64x, czas konwersji 1052us
        setPrecisionMode(INA228_AVG_64, INA228_TIME_1052_US);

        return true;
    }

    void setPrecisionMode(INA228_Averaging avg, INA228_ConvTime conv) {
        uint16_t val = (0x0F << 12) | ((conv & 0x7) << 9) | ((conv & 0x7) << 6) | ((conv & 0x7) << 3) | (avg & 0x7);
        writeRegister16(INA228_REG_ADC_CONFIG, val);
    }

    void setShuntTempCo(int16_t ppm_per_c) {
        writeRegister16(INA228_REG_SHUNT_TEMPCO, (uint16_t)ppm_per_c);
    }

    // Napięcie VBUS (BEZZNAKOWE 0..85V, krok LSB = 195.3125 uV)
    double getBusVoltage() {
        uint32_t raw = readRegister24Unsigned(INA228_REG_VBUS);
        return (double)raw * 0.0001953125;
    }

    // Spadek na boczniku (ZE ZNAKIEM, krok 312.5 nV lub 78.125 nV)
    double getShuntVoltage_uV() {
        int32_t raw = readRegister24Signed(INA228_REG_VSHUNT);
        double lsb_uV = _high_range ? 0.3125 : 0.078125;
        return (double)raw * lsb_uV;
    }

    // Prąd (ZE ZNAKIEM, kierunek dwukierunkowy)
    double getCurrent() {
        int32_t raw = readRegister24Signed(INA228_REG_CURRENT);
        return (double)raw * _current_lsb;
    }

    // Moc czynna
    double getPower() {
        uint32_t raw = readRegister24Unsigned(INA228_REG_POWER);
        return (double)raw * _power_lsb;
    }

    // Temperatura matrycy (krok 7.8125 m°C)
    float getDieTemperature() {
        int16_t raw = (int16_t)readRegister16(INA228_REG_DIETEMP);
        return raw * 0.0078125f;
    }
};

#endif // INA228_H
