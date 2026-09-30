/**
 * @file    cry_detector.cpp
 * @brief   Implementation of the MAX9814 amplitude-based cry detector.
 *
 * Pipeline:
 *
 *   ADC burst  ->  peak-to-peak span  ->  exponential smoothing
 *              ->  threshold compare  ->  persistence / release timers
 *
 * The persistence timer (CRY_PERSISTENCE_MS) suppresses door slams and other
 * impulses; the release timer (CRY_RELEASE_MS) keeps the state latched through
 * the natural pauses a baby takes between cries.
 */

#include "cry_detector.h"

CryDetector::CryDetector()
    : m_available(false),
      m_state(CryState::NO_CRY),
      m_bias(2048.0f),
      m_amplitude(0.0f),
      m_loudSinceMs(0),
      m_quietSinceMs(0) {}

bool CryDetector::begin() {
    pinMode(PIN_MIC_ANALOG, INPUT);

    // GPIO 34 sits on ADC1. 12-bit width with 11 dB attenuation gives the
    // full 0-3.3 V swing the MAX9814 can produce.
    analogReadResolution(12);
    analogSetPinAttenuation(PIN_MIC_ANALOG, ADC_11db);

    // Establish the quiescent DC bias of the module. The MAX9814 idles at
    // roughly half its supply, so a reading stuck at 0 or 4095 means the
    // module is not wired up.
    uint32_t sum = 0;
    for (uint16_t i = 0; i < CRY_CALIBRATION_SAMPLES; ++i) {
        sum += analogRead(PIN_MIC_ANALOG);
        delayMicroseconds(200);
    }
    m_bias = static_cast<float>(sum) / static_cast<float>(CRY_CALIBRATION_SAMPLES);

    m_available = (m_bias > 100.0f) && (m_bias < 3995.0f);

    m_quietSinceMs = millis();
    return m_available;
}

void CryDetector::update() {
    if (!m_available) {
        return;
    }
    applyAmplitude(sampleBurst());
}

uint16_t CryDetector::sampleBurst() {
    // Audio cannot be characterised by one sample per scheduler tick: at the
    // 100 Hz tick rate a 400 Hz cry would alias into nonsense. Instead each
    // tick takes a short burst of back-to-back conversions. An ESP32 ADC read
    // costs roughly 10-20 us, so a 32-sample burst finishes in well under a
    // millisecond and never stalls the scheduler.
    uint16_t minimum = 4095;
    uint16_t maximum = 0;
    uint32_t sum     = 0;

    for (uint16_t i = 0; i < CRY_WINDOW_SAMPLES; ++i) {
        const uint16_t sample = static_cast<uint16_t>(analogRead(PIN_MIC_ANALOG));
        if (sample < minimum) minimum = sample;
        if (sample > maximum) maximum = sample;
        sum += sample;
    }

    // Let the bias drift very slowly so supply variation does not bake in a
    // permanent offset, while a real cry is far too short to move it.
    const float burstMean = static_cast<float>(sum) /
                            static_cast<float>(CRY_WINDOW_SAMPLES);
    m_bias += CRY_BIAS_TRACK_ALPHA * (burstMean - m_bias);

    return static_cast<uint16_t>(maximum - minimum);
}

void CryDetector::applyAmplitude(uint16_t peakToPeak) {
    // Exponential moving average: fast enough to follow a cry onset, slow
    // enough to ignore single-burst ADC noise.
    m_amplitude += CRY_SMOOTHING_ALPHA *
                   (static_cast<float>(peakToPeak) - m_amplitude);

    const uint32_t now  = millis();
    const bool     loud = (m_amplitude >= static_cast<float>(CRY_AMPLITUDE_THRESHOLD));

    if (loud) {
        m_quietSinceMs = 0;
        if (m_loudSinceMs == 0) {
            m_loudSinceMs = now;
        }
        // Sustained loudness -> declare a cry.
        if (m_state == CryState::NO_CRY &&
            (now - m_loudSinceMs) >= CRY_PERSISTENCE_MS) {
            m_state = CryState::CRY_DETECTED;
        }
    } else {
        m_loudSinceMs = 0;
        if (m_quietSinceMs == 0) {
            m_quietSinceMs = now;
        }
        // Sustained quiet -> release the latch.
        if (m_state == CryState::CRY_DETECTED &&
            (now - m_quietSinceMs) >= CRY_RELEASE_MS) {
            m_state = CryState::NO_CRY;
        }
    }
}

CryState CryDetector::getState() const     { return m_state; }
bool     CryDetector::isCrying() const     { return m_state == CryState::CRY_DETECTED; }
float    CryDetector::getAmplitude() const { return m_amplitude; }
float    CryDetector::getBias() const      { return m_bias; }
bool     CryDetector::isSensorAvailable() const { return m_available; }

uint8_t CryDetector::getLevelPercent() const {
    const float ratio = (m_amplitude * 100.0f) /
                        static_cast<float>(CRY_AMPLITUDE_THRESHOLD);
    if (ratio <= 0.0f)   return 0;
    if (ratio >= 100.0f) return 100;
    return static_cast<uint8_t>(ratio);
}

const char* CryDetector::getStateText() const {
    return (m_state == CryState::CRY_DETECTED) ? "YES" : "NO";
}
