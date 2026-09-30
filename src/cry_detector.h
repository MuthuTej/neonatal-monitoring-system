/**
 * @file    cry_detector.h
 * @brief   Amplitude-based cry detection for the MAX9814 electret module.
 *
 * The MAX9814 presents an AGC-amplified audio signal biased around ~1.25 V on
 * a single analog pin. This detector estimates the peak-to-peak envelope of
 * that signal, smooths it, and reports a sustained-loudness state.
 *
 * @note This is a loudness detector, NOT a clinically accurate infant cry
 *       classifier. It cannot distinguish a cry from any other loud sound.
 *       The interface is deliberately kept narrow so that the amplitude rule
 *       in evaluate() can later be swapped for an MFCC + TinyML classifier
 *       without touching any other module (see docs/architecture.md).
 */

#ifndef CRY_DETECTOR_H
#define CRY_DETECTOR_H

#include <Arduino.h>
#include "config.h"

/// Reported cry state.
enum class CryState : uint8_t {
    NO_CRY = 0,
    CRY_DETECTED
};

class CryDetector {
public:
    CryDetector();

    /// Configure the ADC pin and measure the ambient DC bias of the module.
    bool begin();

    /// Take one sample and advance the envelope / persistence logic.
    void update();

    CryState getState() const;
    bool     isCrying() const;

    /// Smoothed peak-to-peak amplitude in ADC counts.
    float    getAmplitude() const;
    /// Amplitude expressed as a 0-100 % of the configured threshold.
    uint8_t  getLevelPercent() const;
    /// Tracked DC bias of the microphone module, in ADC counts.
    float    getBias() const;

    bool     isSensorAvailable() const;

    /// Human-readable state, e.g. for the OLED and serial report.
    const char* getStateText() const;

private:
    /// Take one burst of CRY_WINDOW_SAMPLES readings and return the
    /// peak-to-peak span in ADC counts.
    uint16_t sampleBurst();
    /// Fold a fresh peak-to-peak measurement into the state machine.
    void     applyAmplitude(uint16_t peakToPeak);

    bool     m_available;
    CryState m_state;

    float    m_bias;            ///< Slowly tracked DC offset
    float    m_amplitude;       ///< Smoothed peak-to-peak envelope

    uint32_t m_loudSinceMs;     ///< When the signal first crossed the threshold
    uint32_t m_quietSinceMs;    ///< When the signal last dropped below it
};

#endif // CRY_DETECTOR_H
