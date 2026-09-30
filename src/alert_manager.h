/**
 * @file    alert_manager.h
 * @brief   Centralised status engine and local alert output driver.
 *
 * Every rule that turns sensor values into a system status lives here, and
 * nowhere else. The manager consumes a snapshot of all sensor outputs, decides
 * on an AlertState, applies escalate/recover debouncing, and drives the two
 * LEDs and the buzzer accordingly.
 *
 * Missing sensor data is deliberately NOT treated as an abnormal medical
 * reading: absent or invalid inputs produce SENSOR_ERROR, never ALERT.
 */

#ifndef ALERT_MANAGER_H
#define ALERT_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "cry_detector.h"
#include "movement_manager.h"

/// Overall system status, ordered by increasing severity.
enum class AlertState : uint8_t {
    NORMAL = 0,
    WARNING,
    ALERT,
    SENSOR_ERROR
};

/// Snapshot of everything the status engine is allowed to look at.
struct AlertInputs {
    float heartRate        = HR_INVALID;
    bool  heartRateValid   = false;

    float spo2             = SPO2_INVALID;
    bool  spo2Valid        = false;

    float temperature      = TEMP_INVALID;
    bool  temperatureValid = false;

    CryState      cryState    = CryState::NO_CRY;
    MovementState movement    = MovementState::NORMAL;
    Orientation   orientation = Orientation::UNKNOWN;
    bool          sustainedSuddenMovement = false;
    bool          prolongedStillness      = false;

    // Sensor availability (bus-level presence, not reading validity).
    bool pulseOxAvailable  = false;
    bool tempAvailable     = false;
    bool movementAvailable = false;
    bool micAvailable      = false;

    /// True while the MAX30102 sees skin contact.
    bool contactDetected   = false;
};

class AlertManager {
public:
    AlertManager();

    /// Configure the LED and buzzer pins and drive them to a safe idle state.
    bool begin();

    /// Re-evaluate the status from a fresh snapshot of the sensor outputs.
    void update(const AlertInputs& inputs);

    /// Drive the LEDs/buzzer. Call frequently; contains no blocking delays.
    void refreshOutputs();

    AlertState  getState() const;
    const char* getStateText() const;
    /// Short human-readable explanation of the current state, for the OLED.
    const char* getReasonText() const;

    /// Silence the buzzer until the next escalation (manual acknowledge).
    void acknowledge();
    bool isAcknowledged() const;

    /// Milliseconds the current state has been held.
    uint32_t getStateAgeMs() const;

private:
    AlertState evaluate(const AlertInputs& inputs);
    void       applyState(AlertState candidate);
    void       setOutputs(bool green, bool red, bool buzzer);

    AlertState  m_state;
    AlertState  m_candidate;
    const char* m_reason;

    uint32_t m_stateSinceMs;
    uint32_t m_candidateSinceMs;
    uint32_t m_blinkReferenceMs;

    bool m_acknowledged;
    bool m_outputsReady;
};

#endif // ALERT_MANAGER_H
