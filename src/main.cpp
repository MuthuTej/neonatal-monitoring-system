/**
 * @file    main.cpp
 * @brief   AI-Powered Smart Neonatal Monitoring System - application entry.
 *
 * Architecture
 * ------------
 *   Sensors -> Managers -> DisplayData / AlertInputs snapshot
 *           -> AlertManager (status engine) -> OLED + LEDs + buzzer
 *
 * main.cpp owns nothing but wiring and scheduling. Every sensor concern lives
 * in its manager, every status rule lives in AlertManager, and every pixel
 * lives in DisplayManager. Adding a sensor means adding a manager and one
 * scheduler slot here.
 *
 * Scheduling
 * ----------
 * A flat, millis()-based cooperative scheduler runs each subsystem at its own
 * cadence. There are no blocking delay() calls in the main loop, so the fast
 * tasks (pulse oximetry at 50 Hz, microphone at 100 Hz) are never starved by
 * the slow ones (OLED redraw at 4 Hz, serial report every 2 s).
 *
 * @warning Academic proof-of-concept. NOT a certified medical device.
 */

#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "max30102_manager.h"
#include "temperature_manager.h"
#include "cry_detector.h"
#include "movement_manager.h"
#include "alert_manager.h"
#include "display_manager.h"

// ---------------------------------------------------------------------------
//  Subsystems
// ---------------------------------------------------------------------------
static MAX30102Manager    pulseOx;
static TemperatureManager temperature;
static CryDetector        cryDetector;
static MovementManager    movement;
static AlertManager       alerts;
static DisplayManager     display;

// ---------------------------------------------------------------------------
//  Scheduler bookkeeping
// ---------------------------------------------------------------------------
struct TaskTimer {
    uint32_t last;
    uint32_t interval;

    bool due(uint32_t now) {
        if ((now - last) >= interval) {
            last = now;
            return true;
        }
        return false;
    }
};

static TaskTimer t_pulseOx      { 0, INTERVAL_PULSEOX_MS };
static TaskTimer t_temperature  { 0, INTERVAL_TEMPERATURE_MS };
static TaskTimer t_cry          { 0, INTERVAL_CRY_MS };
static TaskTimer t_movement     { 0, INTERVAL_MOVEMENT_MS };
static TaskTimer t_alerts       { 0, INTERVAL_ALERT_MS };
static TaskTimer t_display      { 0, INTERVAL_DISPLAY_MS };
static TaskTimer t_serialReport { 0, INTERVAL_SERIAL_REPORT_MS };

// ---------------------------------------------------------------------------
//  Boot helpers
// ---------------------------------------------------------------------------

/// Print one aligned "COMPONENT ....... OK" line and mirror it to the OLED.
static void reportBootStep(const char* label, bool ok, bool mirrorToOled = true) {
    char line[32];
    snprintf(line, sizeof(line), "%-12s %s", label, ok ? "OK" : "FAIL");
    Serial.println(line);

    if (mirrorToOled) {
        display.showBootStep(label, ok);
    }
}

static void printBanner() {
    Serial.println();
    Serial.println(F("================================"));
    Serial.println(F("      " FW_PROJECT_NAME));
    Serial.println(F("================================"));
    Serial.println(F("Firmware : " FW_VERSION "  (" FW_BUILD_TYPE ")"));
    Serial.println(F("Academic proof-of-concept."));
    Serial.println(F("NOT a certified medical device."));
    Serial.println(F("================================"));
    Serial.println();
    Serial.println(F("Initializing sensors..."));
    Serial.println();
}

// ---------------------------------------------------------------------------
//  Snapshot builders
// ---------------------------------------------------------------------------

static AlertInputs buildAlertInputs() {
    AlertInputs in;

    in.heartRate        = pulseOx.getHeartRate();
    in.heartRateValid   = pulseOx.isHeartRateValid();
    in.spo2             = pulseOx.getSpO2();
    in.spo2Valid        = pulseOx.isSpO2Valid();

    in.temperature      = temperature.getTemperature();
    in.temperatureValid = temperature.isValid();

    in.cryState    = cryDetector.getState();
    in.movement    = movement.getState();
    in.orientation = movement.getOrientation();
    in.sustainedSuddenMovement = movement.isSustainedSuddenMovement();
    in.prolongedStillness      = movement.isProlongedStillness();

    in.pulseOxAvailable  = pulseOx.isSensorAvailable();
    in.tempAvailable     = temperature.isSensorAvailable();
    in.movementAvailable = movement.isSensorAvailable();
    in.micAvailable      = cryDetector.isSensorAvailable();
    in.contactDetected   = pulseOx.isContactDetected();

    return in;
}

static DisplayData buildDisplayData() {
    DisplayData d;

    d.heartRate        = pulseOx.getHeartRate();
    d.heartRateValid   = pulseOx.isHeartRateValid();
    d.spo2             = pulseOx.getSpO2();
    d.spo2Valid        = pulseOx.isSpO2Valid();

    d.temperature      = temperature.getTemperature();
    d.temperatureValid = temperature.isValid();

    d.cryState = cryDetector.getState();
    d.cryLevel = cryDetector.getLevelPercent();

    d.movement    = movement.getState();
    d.orientation = movement.getOrientation();
    d.accelG      = movement.getAccelerationMagnitude();
    d.gyroDps     = movement.getGyroMagnitude();
    d.tiltDeg     = movement.getTiltAngle();

    d.alertState = alerts.getState();
    d.statusText = alerts.getStateText();
    d.reasonText = alerts.getReasonText();

    d.pulseOxAvailable  = pulseOx.isSensorAvailable();
    d.tempAvailable     = temperature.isSensorAvailable();
    d.movementAvailable = movement.isSensorAvailable();
    d.micAvailable      = cryDetector.isSensorAvailable();
    d.contactDetected   = pulseOx.isContactDetected();

    d.uptimeSeconds = millis() / 1000UL;

    return d;
}

// ---------------------------------------------------------------------------
//  Serial report
// ---------------------------------------------------------------------------

static void printSerialReport() {
    char value[12];

    Serial.println();
    Serial.println(F("================================"));
    Serial.println(F(FW_PROJECT_NAME));
    Serial.println(F("================================"));

    if (pulseOx.isHeartRateValid()) {
        dtostrf(pulseOx.getHeartRate(), 0, 0, value);
        Serial.printf("Heart Rate : %s BPM\n", value);
    } else {
        Serial.println(F("Heart Rate : -- (no valid signal)"));
    }

    if (pulseOx.isSpO2Valid()) {
        dtostrf(pulseOx.getSpO2(), 0, 0, value);
        Serial.printf("SpO2       : %s %%\n", value);
    } else {
        Serial.println(F("SpO2       : -- (no valid signal)"));
    }

    if (temperature.isValid()) {
        dtostrf(temperature.getTemperature(), 0, 1, value);
        Serial.printf("Temperature: %s C\n", value);
    } else {
        Serial.println(F("Temperature: -- (no valid reading)"));
    }

    Serial.printf("Cry        : %s\n", cryDetector.getStateText());
    Serial.printf("Movement   : %s\n", movement.getStateText());
    Serial.printf("Orientation: %s\n", movement.getOrientationText());
    Serial.printf("Status     : %s (%s)\n",
                  alerts.getStateText(), alerts.getReasonText());

    Serial.println(F("--------------------------------"));

    dtostrf(movement.getAccelerationMagnitude(), 0, 2, value);
    Serial.printf("Accel mag  : %s g\n", value);
    dtostrf(cryDetector.getAmplitude(), 0, 0, value);
    Serial.printf("Mic level  : %s counts (%u%% of threshold)\n",
                  value, cryDetector.getLevelPercent());
    Serial.printf("Probe      : %s\n",
                  pulseOx.isContactDetected() ? "CONTACT" : "NO CONTACT");
    Serial.printf("Uptime     : %lu s\n",
                  static_cast<unsigned long>(millis() / 1000UL));

    Serial.println(F("================================"));
}

// ---------------------------------------------------------------------------
//  setup()
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    const uint32_t serialStart = millis();
    while (!Serial && (millis() - serialStart) < SERIAL_BOOT_TIMEOUT_MS) {
        ;   // Give the USB CDC a moment, but never hang waiting for a host.
    }

    printBanner();

    // ---- Shared I2C bus: MAX30102 + MPU6050 + OLED -----------------------
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_BUS_FREQUENCY_HZ);

    // The OLED comes up first so the remaining probe results can be shown
    // on the panel as they happen.
    const bool oledOk = display.begin();
    if (oledOk) {
        display.showSplash();
        delay(OLED_SPLASH_HOLD_MS);   // Boot-time only; the main loop never blocks.
    }
    reportBootStep("OLED", oledOk, false);
    display.showBootStep("OLED", oledOk);

    const bool pulseOxOk = pulseOx.begin();
    reportBootStep("MAX30102", pulseOxOk);

    const bool movementOk = movement.begin();
    reportBootStep("MPU6050", movementOk);

    const bool tempOk = temperature.begin();
    reportBootStep("DS18B20", tempOk);

    const bool micOk = cryDetector.begin();
    reportBootStep("MICROPHONE", micOk);

    // Alert outputs are last so the LEDs stay dark until the status engine
    // actually has something to report.
    alerts.begin();
    reportBootStep("ALERTS", true);

    const bool allOk = oledOk && pulseOxOk && movementOk && tempOk && micOk;

    Serial.println();
    Serial.println(allOk ? F("SYSTEM READY")
                         : F("SYSTEM READY WITH ERRORS - check wiring"));
    Serial.println(F("================================"));
    Serial.println();

    display.showBootResult(allOk);
    delay(OLED_SPLASH_HOLD_MS);

    const uint32_t now = millis();
    t_pulseOx.last = t_temperature.last = t_cry.last = now;
    t_movement.last = t_alerts.last = t_display.last = t_serialReport.last = now;
}

// ---------------------------------------------------------------------------
//  loop()
// ---------------------------------------------------------------------------

void loop() {
    const uint32_t now = millis();

    // ---- Acquisition -----------------------------------------------------
    if (t_cry.due(now))         { cryDetector.update(); }
    if (t_pulseOx.due(now))     { pulseOx.update(); }
    if (t_movement.due(now))    { movement.update(); }
    if (t_temperature.due(now)) { temperature.update(); }

    // ---- Status engine ---------------------------------------------------
    if (t_alerts.due(now)) {
        alerts.update(buildAlertInputs());
    }

    // The alert outputs are refreshed every pass so LED blinking and the
    // buzzer cadence stay smooth regardless of the evaluation interval.
    alerts.refreshOutputs();

    // ---- Presentation ----------------------------------------------------
    if (t_display.due(now)) {
        display.tickAutoPage();
        display.render(buildDisplayData());
    }

    if (t_serialReport.due(now)) {
        printSerialReport();
    }
}
