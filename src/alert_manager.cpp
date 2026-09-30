/**
 * @file    alert_manager.cpp
 * @brief   Implementation of the status engine and local alert outputs.
 *
 * Rule precedence (highest first):
 *
 *   1. SENSOR_ERROR  - a required sensor is missing from the bus.
 *   2. ALERT         - SpO2 < SPO2_ALERT_MIN, temperature outside the alert
 *                      band, heart rate outside the warning band, or a
 *                      sustained sudden-movement event.
 *   3. WARNING       - values inside the borderline bands, a detected cry,
 *                      prone orientation, or prolonged stillness.
 *   4. NORMAL        - everything in range.
 *
 * A candidate state must persist for ALERT_ESCALATE_DEBOUNCE_MS before the
 * status is raised, and for ALERT_RECOVER_DEBOUNCE_MS before it is lowered.
 * That asymmetry keeps the alarm from chattering on a single noisy sample
 * while still reacting quickly to a genuine, sustained condition.
 */

#include "alert_manager.h"

AlertManager::AlertManager()
    : m_state(AlertState::NORMAL),
      m_candidate(AlertState::NORMAL),
      m_reason("SYSTEM OK"),
      m_stateSinceMs(0),
      m_candidateSinceMs(0),
      m_blinkReferenceMs(0),
      m_acknowledged(false),
      m_outputsReady(false) {}

bool AlertManager::begin() {
    pinMode(PIN_LED_GREEN, OUTPUT);
    pinMode(PIN_LED_RED,   OUTPUT);
    pinMode(PIN_BUZZER,    OUTPUT);

    setOutputs(false, false, false);

    m_stateSinceMs     = millis();
    m_candidateSinceMs = m_stateSinceMs;
    m_blinkReferenceMs = m_stateSinceMs;
    m_outputsReady     = true;
    return true;
}

// ---------------------------------------------------------------------------
//  Status evaluation
// ---------------------------------------------------------------------------

AlertState AlertManager::evaluate(const AlertInputs& in) {
    // ---- 1. Sensor availability -----------------------------------------
    // A sensor that is not on the bus at all is an equipment fault, not a
    // clinical finding. It must never be scored as an abnormal reading.
    if (!in.pulseOxAvailable) {
        m_reason = "PULSE OX OFFLINE";
        return AlertState::SENSOR_ERROR;
    }
    if (!in.tempAvailable) {
        m_reason = "TEMP PROBE OFFLINE";
        return AlertState::SENSOR_ERROR;
    }
    if (!in.movementAvailable) {
        m_reason = "IMU OFFLINE";
        return AlertState::SENSOR_ERROR;
    }
    if (!in.micAvailable) {
        m_reason = "MIC OFFLINE";
        return AlertState::SENSOR_ERROR;
    }

    bool warning = false;
    const char* warningReason = "CHECK INFANT";

    // ---- 2. SpO2 ---------------------------------------------------------
    // Only judged when the reading is actually valid. An invalid SpO2 with no
    // skin contact simply means the probe is off, which is handled below.
    if (in.spo2Valid) {
        if (in.spo2 < SPO2_ALERT_MIN) {
            m_reason = "LOW SPO2";
            return AlertState::ALERT;
        }
        if (in.spo2 < SPO2_NORMAL_MIN) {
            warning = true;
            warningReason = "SPO2 BORDERLINE";
        }
    }

    // ---- 3. Temperature --------------------------------------------------
    if (in.temperatureValid) {
        if (in.temperature <= TEMP_ALERT_LOW) {
            m_reason = "HYPOTHERMIA";
            return AlertState::ALERT;
        }
        if (in.temperature >= TEMP_ALERT_HIGH) {
            m_reason = "HYPERTHERMIA";
            return AlertState::ALERT;
        }
        if (in.temperature < TEMP_NORMAL_MIN || in.temperature > TEMP_NORMAL_MAX) {
            warning = true;
            warningReason = "TEMP OUT OF RANGE";
        }
    }

    // ---- 4. Heart rate ---------------------------------------------------
    if (in.heartRateValid) {
        if (in.heartRate < HR_WARNING_MIN) {
            m_reason = "BRADYCARDIA";
            return AlertState::ALERT;
        }
        if (in.heartRate > HR_WARNING_MAX) {
            m_reason = "TACHYCARDIA";
            return AlertState::ALERT;
        }
        if (in.heartRate < HR_NORMAL_MIN || in.heartRate > HR_NORMAL_MAX) {
            warning = true;
            warningReason = "HR OUT OF RANGE";
        }
    }

    // ---- 5. Movement -----------------------------------------------------
    if (in.sustainedSuddenMovement) {
        m_reason = "SUSTAINED MOTION";
        return AlertState::ALERT;
    }
    if (in.movement == MovementState::SUDDEN_MOVEMENT) {
        warning = true;
        warningReason = "SUDDEN MOVEMENT";
    }
    if (in.orientation == Orientation::PRONE) {
        warning = true;
        warningReason = "PRONE POSITION";
    }
    if (in.prolongedStillness) {
        warning = true;
        warningReason = "NO MOVEMENT";
    }

    // ---- 6. Cry ----------------------------------------------------------
    if (in.cryState == CryState::CRY_DETECTED) {
        warning = true;
        warningReason = "CRY DETECTED";
    }

    // ---- 7. Probe detached ----------------------------------------------
    // Informational only: the hardware is fine, the probe is simply not on
    // the skin, so this is the weakest warning and is reported last.
    if (!in.contactDetected && !warning) {
        m_reason = "NO PROBE CONTACT";
        return AlertState::WARNING;
    }

    if (warning) {
        m_reason = warningReason;
        return AlertState::WARNING;
    }

    m_reason = "SYSTEM OK";
    return AlertState::NORMAL;
}

void AlertManager::update(const AlertInputs& inputs) {
    const AlertState candidate = evaluate(inputs);
    applyState(candidate);
}

void AlertManager::applyState(AlertState candidate) {
    const uint32_t now = millis();

    if (candidate != m_candidate) {
        m_candidate        = candidate;
        m_candidateSinceMs = now;
    }

    if (candidate == m_state) {
        return;
    }

    // SENSOR_ERROR is not on the severity ladder: treat any transition into
    // or out of it as an escalation so equipment faults surface promptly.
    const bool escalating = (candidate == AlertState::SENSOR_ERROR) ||
                            (m_state == AlertState::SENSOR_ERROR) ||
                            (candidate > m_state);

    const uint32_t required = escalating ? ALERT_ESCALATE_DEBOUNCE_MS
                                         : ALERT_RECOVER_DEBOUNCE_MS;

    if ((now - m_candidateSinceMs) < required) {
        return;
    }

    if (escalating) {
        m_acknowledged = false;   // A new escalation always re-arms the buzzer.
    }

    m_state            = candidate;
    m_stateSinceMs     = now;
    m_blinkReferenceMs = now;
}

// ---------------------------------------------------------------------------
//  Output driving
// ---------------------------------------------------------------------------

void AlertManager::setOutputs(bool green, bool red, bool buzzer) {
    digitalWrite(PIN_LED_GREEN, green ? HIGH : LOW);
    digitalWrite(PIN_LED_RED,   red   ? HIGH : LOW);
    digitalWrite(PIN_BUZZER,
                 buzzer ? BUZZER_ACTIVE_LEVEL
                        : (BUZZER_ACTIVE_LEVEL == HIGH ? LOW : HIGH));
}

void AlertManager::refreshOutputs() {
    if (!m_outputsReady) {
        return;
    }

    const uint32_t elapsed = millis() - m_blinkReferenceMs;

    switch (m_state) {
        case AlertState::NORMAL:
            setOutputs(true, false, false);
            break;

        case AlertState::WARNING: {
            const bool phase = ((elapsed / ALERT_WARNING_BLINK_MS) % 2) == 0;
            const bool buzz  = ALERT_BUZZER_ON_WARNING && phase && !m_acknowledged;
            setOutputs(false, phase, buzz);
            break;
        }

        case AlertState::ALERT: {
            const bool phase = ((elapsed / ALERT_BUZZER_PERIOD_MS) % 2) == 0;
            setOutputs(false, true, phase && !m_acknowledged);
            break;
        }

        case AlertState::SENSOR_ERROR: {
            // Both LEDs flash in anti-phase: visually distinct from a
            // clinical alert, and the buzzer stays silent.
            const bool phase = ((elapsed / ALERT_ERROR_BLINK_MS) % 2) == 0;
            setOutputs(phase, !phase, false);
            break;
        }
    }
}

void AlertManager::acknowledge()      { m_acknowledged = true; }
bool AlertManager::isAcknowledged() const { return m_acknowledged; }
AlertState  AlertManager::getState() const  { return m_state; }
const char* AlertManager::getReasonText() const { return m_reason; }
uint32_t AlertManager::getStateAgeMs() const { return millis() - m_stateSinceMs; }

const char* AlertManager::getStateText() const {
    switch (m_state) {
        case AlertState::NORMAL:       return "NORMAL";
        case AlertState::WARNING:      return "WARNING";
        case AlertState::ALERT:        return "ALERT";
        case AlertState::SENSOR_ERROR: return "SENSOR ERR";
        default:                       return "UNKNOWN";
    }
}
