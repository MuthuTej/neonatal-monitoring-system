/**
 * @file    temperature_manager.h
 * @brief   DS18B20 body-temperature acquisition (OneWire / DallasTemperature).
 *
 * Conversions are started and collected asynchronously so the main loop is
 * never blocked by the sensor's conversion time. A short moving average
 * smooths the 0.125 C quantisation of the 11-bit resolution mode.
 */

#ifndef TEMPERATURE_MANAGER_H
#define TEMPERATURE_MANAGER_H

#include <Arduino.h>
#include "config.h"

class TemperatureManager {
public:
    TemperatureManager();

    /// Enumerate the OneWire bus and configure the first DS18B20 found.
    bool begin();

    /// Drive the request/read state machine. Non-blocking.
    void update();

    float getTemperature() const;   ///< Degrees C, or TEMP_INVALID
    bool  isValid() const;          ///< True when the last reading was usable
    bool  isSensorAvailable() const;///< True while the probe answers on the bus

    /// Most recent unaveraged sample, useful for serial diagnostics.
    float getRawTemperature() const;

private:
    void acceptSample(float celsius);
    void rejectSample();

    bool     m_available;
    bool     m_valid;
    bool     m_conversionPending;
    uint32_t m_conversionStartedMs;
    uint32_t m_conversionDelayMs;

    float    m_temperature;
    float    m_rawTemperature;
    float    m_window[TEMP_AVERAGE_WINDOW];
    uint8_t  m_count;
    uint8_t  m_index;
    uint8_t  m_failureCount;
};

#endif // TEMPERATURE_MANAGER_H
