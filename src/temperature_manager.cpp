/**
 * @file    temperature_manager.cpp
 * @brief   Implementation of the DS18B20 temperature manager.
 *
 * The DallasTemperature library is used in "async" mode: setWaitForConversion
 * (false) makes requestTemperatures() return immediately, and the result is
 * collected once the resolution-dependent conversion time has elapsed.
 */

#include "temperature_manager.h"

#include <OneWire.h>
#include <DallasTemperature.h>

namespace {
OneWire           g_oneWire(PIN_DS18B20_DATA);
DallasTemperature g_dallas(&g_oneWire);
DeviceAddress     g_probeAddress;
bool              g_probeFound = false;
}

TemperatureManager::TemperatureManager()
    : m_available(false),
      m_valid(false),
      m_conversionPending(false),
      m_conversionStartedMs(0),
      m_conversionDelayMs(750),
      m_temperature(TEMP_INVALID),
      m_rawTemperature(TEMP_INVALID),
      m_count(0),
      m_index(0),
      m_failureCount(0) {
    for (uint8_t i = 0; i < TEMP_AVERAGE_WINDOW; ++i) {
        m_window[i] = 0.0f;
    }
}

bool TemperatureManager::begin() {
    g_dallas.begin();

    if (g_dallas.getDeviceCount() == 0) {
        m_available = false;
        return false;
    }

    g_probeFound = g_dallas.getAddress(g_probeAddress, 0);
    if (!g_probeFound) {
        m_available = false;
        return false;
    }

    g_dallas.setResolution(g_probeAddress, TEMP_RESOLUTION_BITS);
    g_dallas.setWaitForConversion(false);   // Never block the main loop.

    // Conversion time roughly halves per dropped resolution bit from 750 ms.
    m_conversionDelayMs = 750 >> (12 - TEMP_RESOLUTION_BITS);

    m_available = true;
    return true;
}

void TemperatureManager::update() {
    if (!m_available) {
        return;
    }

    const uint32_t now = millis();

    if (!m_conversionPending) {
        g_dallas.requestTemperatures();
        m_conversionStartedMs = now;
        m_conversionPending   = true;
        return;
    }

    if ((now - m_conversionStartedMs) < m_conversionDelayMs) {
        return;   // Still converting.
    }

    m_conversionPending = false;
    const float celsius = g_dallas.getTempC(g_probeAddress);
    m_rawTemperature    = celsius;

    const bool usable = (celsius != DEVICE_DISCONNECTED_C) &&
                        (celsius >= TEMP_PLAUSIBLE_MIN) &&
                        (celsius <= TEMP_PLAUSIBLE_MAX);

    if (usable) {
        acceptSample(celsius);
    } else {
        rejectSample();
    }
}

void TemperatureManager::acceptSample(float celsius) {
    m_failureCount = 0;

    m_window[m_index] = celsius;
    m_index = (m_index + 1) % TEMP_AVERAGE_WINDOW;
    if (m_count < TEMP_AVERAGE_WINDOW) {
        ++m_count;
    }

    float sum = 0.0f;
    for (uint8_t i = 0; i < m_count; ++i) {
        sum += m_window[i];
    }

    m_temperature = sum / static_cast<float>(m_count);
    m_valid       = true;
}

void TemperatureManager::rejectSample() {
    m_valid       = false;
    m_temperature = TEMP_INVALID;
    m_count       = 0;
    m_index       = 0;

    if (m_failureCount < 255) {
        ++m_failureCount;
    }

    // Repeated failures mean the probe has most likely been unplugged.
    if (m_failureCount >= TEMP_FAILURE_LIMIT) {
        m_available = false;
    }
}

float TemperatureManager::getTemperature() const {
    return m_valid ? m_temperature : TEMP_INVALID;
}

float TemperatureManager::getRawTemperature() const { return m_rawTemperature; }
bool  TemperatureManager::isValid() const           { return m_valid; }
bool  TemperatureManager::isSensorAvailable() const { return m_available; }
