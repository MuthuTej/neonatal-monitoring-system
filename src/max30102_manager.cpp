/**
 * @file    max30102_manager.cpp
 * @brief   Implementation of the MAX30102 pulse-oximetry manager.
 *
 * Two estimators run side by side on the same sample stream:
 *
 *   1. Beat detection (SparkFun `heartRate.h`) gives a responsive BPM. Each
 *      accepted beat-to-beat interval is converted to BPM, range-checked and
 *      folded into a rolling mean of HR_AVERAGE_WINDOW beats.
 *
 *   2. The Maxim reference routine (`spo2_algorithm.h`) consumes a one second
 *      window of red/IR samples and returns an SpO2 estimate plus its own
 *      validity flag. That estimate is likewise averaged.
 *
 * Both estimators are invalidated whenever skin contact is lost, so a
 * detached probe reports "--" rather than a dangerous-looking number.
 */

#include "max30102_manager.h"

#include <Wire.h>
#include <MAX30105.h>
#include <heartRate.h>
#include <spo2_algorithm.h>

namespace {
/// Underlying SparkFun driver. The MAX30105 class also drives the MAX30102.
MAX30105 g_sensor;
}

MAX30102Manager::MAX30102Manager()
    : m_available(false),
      m_contact(false),
      m_heartRate(HR_INVALID),
      m_heartRateValid(false),
      m_lastBeatMs(0),
      m_lastValidBeatMs(0),
      m_beatCount(0),
      m_beatIndex(0),
      m_spo2(SPO2_INVALID),
      m_spo2Valid(false),
      m_spo2Count(0),
      m_spo2Index(0),
      m_bufferFill(0),
      m_lastIR(0),
      m_lastRed(0) {
    for (uint8_t i = 0; i < HR_AVERAGE_WINDOW; ++i) {
        m_beatWindow[i] = 0.0f;
    }
    for (uint8_t i = 0; i < SPO2_AVERAGE_WINDOW; ++i) {
        m_spo2Window[i] = 0.0f;
    }
}

bool MAX30102Manager::begin() {
    // The MAX30102 answers at I2C_ADDR_MAX30102 on the shared bus.
    if (!g_sensor.begin(Wire, I2C_SPEED_FAST, I2C_ADDR_MAX30102)) {
        m_available = false;
        return false;
    }

    g_sensor.setup(MAX30102_LED_BRIGHTNESS,
                   MAX30102_SAMPLE_AVERAGE,
                   MAX30102_LED_MODE,
                   MAX30102_SAMPLE_RATE,
                   MAX30102_PULSE_WIDTH,
                   MAX30102_ADC_RANGE);

    // A small amount of red current keeps the SpO2 channel alive; the IR LED
    // carries the plethysmogram used for beat detection.
    g_sensor.setPulseAmplitudeRed(MAX30102_LED_BRIGHTNESS);
    g_sensor.setPulseAmplitudeIR(MAX30102_LED_BRIGHTNESS);
    g_sensor.setPulseAmplitudeGreen(0);   // Not populated on the MAX30102.

    m_available = true;
    return true;
}

void MAX30102Manager::update() {
    if (!m_available) {
        return;
    }

    const uint32_t now = millis();

    // ---- Drain whatever the FIFO has accumulated since the last call -----
    g_sensor.check();
    while (g_sensor.available()) {
        const uint32_t ir  = g_sensor.getFIFOIR();
        const uint32_t red = g_sensor.getFIFORed();
        g_sensor.nextSample();

        m_lastIR  = ir;
        m_lastRed = red;

        const bool contactNow = (ir > PULSEOX_CONTACT_IR_THRESHOLD);
        if (contactNow != m_contact) {
            m_contact = contactNow;
            if (!m_contact) {
                // Probe lifted: everything we knew is stale.
                resetHeartRate();
                resetSpO2();
                m_bufferFill = 0;
            }
        }

        if (!m_contact) {
            continue;
        }

        // ---- Beat detection on the IR channel ----------------------------
        if (checkForBeat(static_cast<int32_t>(ir))) {
            const uint32_t interval = now - m_lastBeatMs;
            m_lastBeatMs = now;
            if (interval > 0) {
                pushBeatInterval(interval);
            }
        }

        // ---- Accumulate a window for the SpO2 estimator -------------------
        if (m_bufferFill < PULSEOX_BUFFER_LENGTH) {
            m_irBuffer[m_bufferFill]  = ir;
            m_redBuffer[m_bufferFill] = red;
            ++m_bufferFill;
        }
        if (m_bufferFill >= PULSEOX_BUFFER_LENGTH) {
            runSpO2Estimator();
            m_bufferFill = 0;
        }
    }

    // ---- Age out a heart rate that has stopped being refreshed -----------
    if (m_heartRateValid && (now - m_lastValidBeatMs) > HR_SIGNAL_TIMEOUT_MS) {
        resetHeartRate();
    }
}

void MAX30102Manager::pushBeatInterval(uint32_t intervalMs) {
    const float bpm = 60000.0f / static_cast<float>(intervalMs);

    // Reject physiologically impossible intervals rather than averaging noise.
    if (bpm < HR_PLAUSIBLE_MIN || bpm > HR_PLAUSIBLE_MAX) {
        return;
    }

    m_beatWindow[m_beatIndex] = bpm;
    m_beatIndex = (m_beatIndex + 1) % HR_AVERAGE_WINDOW;
    if (m_beatCount < HR_AVERAGE_WINDOW) {
        ++m_beatCount;
    }

    float sum = 0.0f;
    for (uint8_t i = 0; i < m_beatCount; ++i) {
        sum += m_beatWindow[i];
    }

    m_heartRate       = sum / static_cast<float>(m_beatCount);
    // Only publish once the window holds a few beats, to suppress the wild
    // first-beat estimate that every optical HR sensor produces.
    m_heartRateValid  = (m_beatCount >= 3);
    m_lastValidBeatMs = millis();
}

void MAX30102Manager::runSpO2Estimator() {
    int32_t spo2Value     = 0;
    int8_t  spo2Valid     = 0;
    int32_t heartRateValue = 0;
    int8_t  heartRateValid = 0;

    maxim_heart_rate_and_oxygen_saturation(m_irBuffer,
                                           PULSEOX_BUFFER_LENGTH,
                                           m_redBuffer,
                                           &spo2Value,
                                           &spo2Valid,
                                           &heartRateValue,
                                           &heartRateValid);

    if (!spo2Valid) {
        return;
    }

    const float value = static_cast<float>(spo2Value);
    if (value < SPO2_PLAUSIBLE_MIN || value > SPO2_PLAUSIBLE_MAX) {
        return;
    }

    m_spo2Window[m_spo2Index] = value;
    m_spo2Index = (m_spo2Index + 1) % SPO2_AVERAGE_WINDOW;
    if (m_spo2Count < SPO2_AVERAGE_WINDOW) {
        ++m_spo2Count;
    }

    float sum = 0.0f;
    for (uint8_t i = 0; i < m_spo2Count; ++i) {
        sum += m_spo2Window[i];
    }

    m_spo2      = sum / static_cast<float>(m_spo2Count);
    m_spo2Valid = true;
}

void MAX30102Manager::resetHeartRate() {
    m_heartRate      = HR_INVALID;
    m_heartRateValid = false;
    m_beatCount      = 0;
    m_beatIndex      = 0;
}

void MAX30102Manager::resetSpO2() {
    m_spo2      = SPO2_INVALID;
    m_spo2Valid = false;
    m_spo2Count = 0;
    m_spo2Index = 0;
}

float MAX30102Manager::getHeartRate() const {
    return m_heartRateValid ? m_heartRate : HR_INVALID;
}

float MAX30102Manager::getSpO2() const {
    return m_spo2Valid ? m_spo2 : SPO2_INVALID;
}

bool MAX30102Manager::isHeartRateValid() const { return m_heartRateValid; }
bool MAX30102Manager::isSpO2Valid() const      { return m_spo2Valid; }
bool MAX30102Manager::isContactDetected() const { return m_contact; }
bool MAX30102Manager::isSensorAvailable() const { return m_available; }
uint32_t MAX30102Manager::getLastIR() const     { return m_lastIR; }
uint32_t MAX30102Manager::getLastRed() const    { return m_lastRed; }
