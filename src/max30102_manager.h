/**
 * @file    max30102_manager.h
 * @brief   Pulse-oximetry front end for the MAX30102 (heart rate + SpO2).
 *
 * The manager owns the sensor, pumps samples out of its FIFO, runs a beat
 * detector for heart rate and the Maxim reference estimator for SpO2, and
 * exposes smoothed values together with explicit validity flags.
 *
 * Invalid readings are reported as HR_INVALID / SPO2_INVALID (negative
 * sentinels) and never as 0, so that "no contact" can never be mistaken for
 * "bradycardia" by the alert engine.
 */

#ifndef MAX30102_MANAGER_H
#define MAX30102_MANAGER_H

#include <Arduino.h>
#include "config.h"

class MAX30102Manager {
public:
    MAX30102Manager();

    /// Probe and configure the sensor. Returns false if it is not on the bus.
    bool begin();

    /// Drain the FIFO and advance the HR / SpO2 estimators. Call often.
    void update();

    float getHeartRate() const;   ///< BPM, or HR_INVALID
    float getSpO2() const;        ///< %, or SPO2_INVALID

    bool isHeartRateValid() const;
    bool isSpO2Valid() const;

    /// True while the IR DC level suggests the sensor is against skin.
    bool isContactDetected() const;

    /// True when begin() succeeded and the device is still responding.
    bool isSensorAvailable() const;

    /// Raw channels, mainly for serial debugging / signal inspection.
    uint32_t getLastIR() const;
    uint32_t getLastRed() const;

private:
    void  pushBeatInterval(uint32_t intervalMs);
    void  resetHeartRate();
    void  resetSpO2();
    void  runSpO2Estimator();

    bool     m_available;
    bool     m_contact;

    // ---- Heart rate state ------------------------------------------------
    float    m_heartRate;
    bool     m_heartRateValid;
    uint32_t m_lastBeatMs;
    uint32_t m_lastValidBeatMs;
    float    m_beatWindow[HR_AVERAGE_WINDOW];
    uint8_t  m_beatCount;
    uint8_t  m_beatIndex;

    // ---- SpO2 state ------------------------------------------------------
    float    m_spo2;
    bool     m_spo2Valid;
    float    m_spo2Window[SPO2_AVERAGE_WINDOW];
    uint8_t  m_spo2Count;
    uint8_t  m_spo2Index;

    // ---- Sample buffers fed to the Maxim estimator -----------------------
    uint32_t m_irBuffer[PULSEOX_BUFFER_LENGTH];
    uint32_t m_redBuffer[PULSEOX_BUFFER_LENGTH];
    uint16_t m_bufferFill;

    uint32_t m_lastIR;
    uint32_t m_lastRed;
};

#endif // MAX30102_MANAGER_H
