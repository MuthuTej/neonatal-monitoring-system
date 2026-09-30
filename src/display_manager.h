/**
 * @file    display_manager.h
 * @brief   1.3" 128x64 SH1106 OLED user interface.
 *
 * The display module is a pure renderer: it owns no sensor and performs no
 * evaluation. Everything it draws arrives through a DisplayData snapshot,
 * which keeps the UI trivially testable and lets pages be added without
 * touching any sensor code.
 *
 * Pages:
 *   0 - VITALS    heart rate, SpO2, temperature, cry, movement, status
 *   1 - MOVEMENT  acceleration magnitude, orientation, movement class
 *   2 - SYSTEM    firmware identity, uptime and per-sensor health
 */

#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "alert_manager.h"
#include "cry_detector.h"
#include "movement_manager.h"

/// Everything the UI is allowed to render, gathered once per frame.
struct DisplayData {
    float heartRate        = HR_INVALID;
    bool  heartRateValid   = false;

    float spo2             = SPO2_INVALID;
    bool  spo2Valid        = false;

    float temperature      = TEMP_INVALID;
    bool  temperatureValid = false;

    CryState      cryState    = CryState::NO_CRY;
    uint8_t       cryLevel    = 0;      ///< 0-100 % of the cry threshold

    MovementState movement    = MovementState::NORMAL;
    Orientation   orientation = Orientation::UNKNOWN;
    float         accelG      = 1.0f;
    float         gyroDps     = 0.0f;
    float         tiltDeg     = 0.0f;

    AlertState  alertState = AlertState::NORMAL;
    const char* statusText = "NORMAL";
    const char* reasonText = "SYSTEM OK";

    bool pulseOxAvailable  = false;
    bool tempAvailable     = false;
    bool movementAvailable = false;
    bool micAvailable      = false;
    bool contactDetected   = false;

    uint32_t uptimeSeconds = 0;
};

class DisplayManager {
public:
    DisplayManager();

    /// Initialise the SH1106 over the shared I2C bus.
    bool begin();

    // ---- Boot sequence -------------------------------------------------
    void showSplash();
    /// Append one "COMPONENT ....... OK/FAIL" line to the boot screen.
    void showBootStep(const char* label, bool ok);
    /// Draw the final banner once every component has been probed.
    void showBootResult(bool allOk);

    // ---- Runtime -------------------------------------------------------
    /// Redraw the currently selected page.
    void render(const DisplayData& data);

    void    nextPage();
    void    setPage(uint8_t page);
    uint8_t getPage() const;

    /// Advance the page automatically every OLED_PAGE_DWELL_MS.
    void tickAutoPage();

    bool isAvailable() const;

private:
    void drawHeader(const char* title);
    void drawVitalsPage(const DisplayData& data);
    void drawMovementPage(const DisplayData& data);
    void drawSystemPage(const DisplayData& data);

    void drawLabelledValue(int16_t y, const char* label,
                           const char* value, const char* unit);
    void drawCentered(int16_t y, const char* text);

    bool     m_available;
    uint8_t  m_page;
    uint8_t  m_bootLine;
    uint32_t m_lastPageSwitchMs;
};

#endif // DISPLAY_MANAGER_H
