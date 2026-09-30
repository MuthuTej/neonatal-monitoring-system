/**
 * @file    display_manager.cpp
 * @brief   Implementation of the SH1106 OLED user interface.
 *
 * All layout constants are expressed in pixels against the 128x64 panel using
 * the default 6x8 Adafruit GFX font, which yields 21 columns by 8 rows.
 */

#include "display_manager.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

namespace {
Adafruit_SH1106G g_oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN);

constexpr int16_t CHAR_W       = 6;
constexpr int16_t CHAR_H       = 8;
constexpr int16_t HEADER_LINE_Y = 11;
constexpr int16_t FOOTER_LINE_Y = 51;
constexpr int16_t VALUE_COLUMN  = 54;   ///< x offset where values are aligned

/// Render a float into a fixed buffer, or "--" when the reading is invalid.
void formatValue(char* out, size_t len, float value, bool valid, uint8_t decimals) {
    if (!valid) {
        snprintf(out, len, "--");
        return;
    }
    dtostrf(value, 0, decimals, out);
}
}   // namespace

DisplayManager::DisplayManager()
    : m_available(false),
      m_page(0),
      m_bootLine(0),
      m_lastPageSwitchMs(0) {}

bool DisplayManager::begin() {
    // `true` performs a soft reset over I2C; the panel has no reset pin.
    if (!g_oled.begin(I2C_ADDR_OLED, true)) {
        m_available = false;
        return false;
    }

    g_oled.setTextSize(1);
    g_oled.setTextColor(SH110X_WHITE);
    g_oled.clearDisplay();
    g_oled.display();

    m_available        = true;
    m_lastPageSwitchMs = millis();
    return true;
}

// ---------------------------------------------------------------------------
//  Boot sequence
// ---------------------------------------------------------------------------

void DisplayManager::showSplash() {
    if (!m_available) return;

    g_oled.clearDisplay();
    g_oled.drawRect(0, 0, OLED_WIDTH, OLED_HEIGHT, SH110X_WHITE);

    drawCentered(14, "NEONATAL");
    drawCentered(26, "MONITOR");
    drawCentered(42, "v" FW_VERSION);
    drawCentered(52, FW_BUILD_TYPE);

    g_oled.display();
    m_bootLine = 0;
}

void DisplayManager::showBootStep(const char* label, bool ok) {
    if (!m_available) return;

    if (m_bootLine == 0) {
        g_oled.clearDisplay();
        drawCentered(0, "INITIALIZING");
        g_oled.drawLine(0, HEADER_LINE_Y, OLED_WIDTH - 1, HEADER_LINE_Y, SH110X_WHITE);
    }

    const int16_t y = 14 + static_cast<int16_t>(m_bootLine) * (CHAR_H + 2);
    if (y > (OLED_HEIGHT - CHAR_H)) {
        return;   // Boot list is full; remaining results go to serial only.
    }

    g_oled.setCursor(2, y);
    g_oled.print(label);

    const char* result = ok ? "OK" : "FAIL";
    const int16_t x = OLED_WIDTH - 2 - static_cast<int16_t>(strlen(result)) * CHAR_W;
    g_oled.setCursor(x, y);
    g_oled.print(result);

    g_oled.display();
    ++m_bootLine;
}

void DisplayManager::showBootResult(bool allOk) {
    if (!m_available) return;

    g_oled.clearDisplay();
    g_oled.drawRect(0, 0, OLED_WIDTH, OLED_HEIGHT, SH110X_WHITE);

    if (allOk) {
        drawCentered(22, "SYSTEM READY");
        drawCentered(38, "MONITORING...");
    } else {
        drawCentered(16, "SYSTEM READY");
        drawCentered(30, "WITH ERRORS");
        drawCentered(44, "CHECK WIRING");
    }

    g_oled.display();
    m_bootLine = 0;
}

// ---------------------------------------------------------------------------
//  Runtime rendering
// ---------------------------------------------------------------------------

void DisplayManager::render(const DisplayData& data) {
    if (!m_available) return;

    g_oled.clearDisplay();

    switch (m_page) {
        case 1:  drawMovementPage(data); break;
        case 2:  drawSystemPage(data);   break;
        case 0:
        default: drawVitalsPage(data);   break;
    }

    g_oled.display();
}

void DisplayManager::drawHeader(const char* title) {
    drawCentered(2, title);
    g_oled.drawLine(0, HEADER_LINE_Y, OLED_WIDTH - 1, HEADER_LINE_Y, SH110X_WHITE);
}

void DisplayManager::drawVitalsPage(const DisplayData& data) {
    drawHeader("NEONATAL MONITOR");

    char buffer[12];

    formatValue(buffer, sizeof(buffer), data.heartRate, data.heartRateValid, 0);
    drawLabelledValue(15, "HR",   buffer, "BPM");

    formatValue(buffer, sizeof(buffer), data.spo2, data.spo2Valid, 0);
    drawLabelledValue(25, "SpO2", buffer, "%");

    formatValue(buffer, sizeof(buffer), data.temperature, data.temperatureValid, 1);
    drawLabelledValue(35, "TEMP", buffer, "C");

    // Cry and movement share one compact row.
    g_oled.setCursor(0, 43);
    g_oled.print("CRY ");
    g_oled.print(data.cryState == CryState::CRY_DETECTED ? "YES" : "NO");

    g_oled.setCursor(64, 43);
    g_oled.print("MV ");
    switch (data.movement) {
        case MovementState::SUDDEN_MOVEMENT: g_oled.print("SUDDEN"); break;
        case MovementState::MOVEMENT:        g_oled.print("ACTIVE"); break;
        default:                             g_oled.print("NORMAL"); break;
    }

    g_oled.drawLine(0, FOOTER_LINE_Y, OLED_WIDTH - 1, FOOTER_LINE_Y, SH110X_WHITE);

    // Anything above NORMAL is drawn inverted so it is readable at a glance.
    const bool highlight = (data.alertState != AlertState::NORMAL);
    if (highlight) {
        g_oled.fillRect(0, FOOTER_LINE_Y + 2, OLED_WIDTH, CHAR_H + 3, SH110X_WHITE);
        g_oled.setTextColor(SH110X_BLACK);
    }

    g_oled.setCursor(2, FOOTER_LINE_Y + 4);
    g_oled.print(data.statusText);

    // The reason is right-aligned next to the status when it fits.
    const int16_t reasonWidth = static_cast<int16_t>(strlen(data.reasonText)) * CHAR_W;
    const int16_t reasonX     = OLED_WIDTH - 2 - reasonWidth;
    if (reasonX > (2 + static_cast<int16_t>(strlen(data.statusText)) * CHAR_W + 4)) {
        g_oled.setCursor(reasonX, FOOTER_LINE_Y + 4);
        g_oled.print(data.reasonText);
    }

    if (highlight) {
        g_oled.setTextColor(SH110X_WHITE);
    }
}

void DisplayManager::drawMovementPage(const DisplayData& data) {
    drawHeader("MOVEMENT");

    char buffer[12];

    dtostrf(data.accelG, 0, 2, buffer);
    drawLabelledValue(16, "ACCEL", buffer, "g");

    dtostrf(data.gyroDps, 0, 0, buffer);
    drawLabelledValue(26, "GYRO", buffer, "d/s");

    dtostrf(data.tiltDeg, 0, 0, buffer);
    drawLabelledValue(36, "TILT", buffer, "deg");

    g_oled.setCursor(0, 46);
    g_oled.print("ORIENT ");
    switch (data.orientation) {
        case Orientation::NORMAL: g_oled.print("NORMAL");  break;
        case Orientation::SIDE:   g_oled.print("SIDE");    break;
        case Orientation::PRONE:  g_oled.print("PRONE");   break;
        default:                  g_oled.print("UNKNOWN"); break;
    }

    g_oled.drawLine(0, FOOTER_LINE_Y + 4, OLED_WIDTH - 1, FOOTER_LINE_Y + 4,
                    SH110X_WHITE);

    g_oled.setCursor(0, 57);
    g_oled.print("MOVE ");
    switch (data.movement) {
        case MovementState::SUDDEN_MOVEMENT: g_oled.print("SUDDEN"); break;
        case MovementState::MOVEMENT:        g_oled.print("ACTIVE"); break;
        default:                             g_oled.print("NORMAL"); break;
    }
}

void DisplayManager::drawSystemPage(const DisplayData& data) {
    drawHeader("SYSTEM");

    const uint32_t hours   = data.uptimeSeconds / 3600UL;
    const uint32_t minutes = (data.uptimeSeconds % 3600UL) / 60UL;
    const uint32_t seconds = data.uptimeSeconds % 60UL;

    char buffer[24];
    snprintf(buffer, sizeof(buffer), "UP %02lu:%02lu:%02lu",
             static_cast<unsigned long>(hours),
             static_cast<unsigned long>(minutes),
             static_cast<unsigned long>(seconds));
    g_oled.setCursor(0, 15);
    g_oled.print(buffer);

    g_oled.setCursor(0, 25);
    g_oled.print("MAX30102 ");
    g_oled.print(data.pulseOxAvailable ? "OK" : "--");
    g_oled.setCursor(72, 25);
    g_oled.print("MPU ");
    g_oled.print(data.movementAvailable ? "OK" : "--");

    g_oled.setCursor(0, 35);
    g_oled.print("DS18B20  ");
    g_oled.print(data.tempAvailable ? "OK" : "--");
    g_oled.setCursor(72, 35);
    g_oled.print("MIC ");
    g_oled.print(data.micAvailable ? "OK" : "--");

    g_oled.setCursor(0, 45);
    g_oled.print("PROBE    ");
    g_oled.print(data.contactDetected ? "CONTACT" : "OFF SKIN");

    g_oled.drawLine(0, FOOTER_LINE_Y + 4, OLED_WIDTH - 1, FOOTER_LINE_Y + 4,
                    SH110X_WHITE);
    g_oled.setCursor(0, 57);
    g_oled.print("FW " FW_VERSION " " FW_BUILD_TYPE);
}

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------

void DisplayManager::drawLabelledValue(int16_t y, const char* label,
                                       const char* value, const char* unit) {
    g_oled.setCursor(0, y);
    g_oled.print(label);

    // Values are right-aligned into a fixed column so the numbers do not
    // jitter horizontally as digits are gained or lost.
    const int16_t valueWidth = static_cast<int16_t>(strlen(value)) * CHAR_W;
    g_oled.setCursor(VALUE_COLUMN + (4 * CHAR_W) - valueWidth, y);
    g_oled.print(value);

    g_oled.setCursor(VALUE_COLUMN + (5 * CHAR_W), y);
    g_oled.print(unit);
}

void DisplayManager::drawCentered(int16_t y, const char* text) {
    const int16_t width = static_cast<int16_t>(strlen(text)) * CHAR_W;
    int16_t x = (OLED_WIDTH - width) / 2;
    if (x < 0) x = 0;
    g_oled.setCursor(x, y);
    g_oled.print(text);
}

// ---------------------------------------------------------------------------
//  Page control
// ---------------------------------------------------------------------------

void DisplayManager::nextPage() {
    m_page = (m_page + 1) % OLED_PAGE_COUNT;
    m_lastPageSwitchMs = millis();
}

void DisplayManager::setPage(uint8_t page) {
    m_page = page % OLED_PAGE_COUNT;
    m_lastPageSwitchMs = millis();
}

uint8_t DisplayManager::getPage() const { return m_page; }
bool    DisplayManager::isAvailable() const { return m_available; }

void DisplayManager::tickAutoPage() {
#if OLED_PAGE_DWELL_MS > 0
    if ((millis() - m_lastPageSwitchMs) >= OLED_PAGE_DWELL_MS) {
        nextPage();
    }
#endif
}
