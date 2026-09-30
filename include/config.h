/**
 * @file    config.h
 * @brief   Central configuration for the AI-Powered Smart Neonatal
 *          Monitoring System.
 *
 * Every tunable value used anywhere in the firmware lives here: GPIO
 * assignments, I2C addresses, clinical-style thresholds, timing/scheduling
 * intervals and UI settings. No module is allowed to hard-code a pin number
 * or a threshold locally.
 *
 * @warning Academic prototype only. Not a certified medical device.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ===========================================================================
//  PROJECT IDENTITY
// ===========================================================================
#define FW_PROJECT_NAME     "NEONATAL MONITOR"
#define FW_VERSION          "1.0.0"
#define FW_BUILD_TYPE       "PROTOTYPE"

// ===========================================================================
//  SERIAL
// ===========================================================================
#ifndef SERIAL_BAUD_RATE
#define SERIAL_BAUD_RATE    115200
#endif

/// Wait at most this long for the USB serial port to come up (ms).
#define SERIAL_BOOT_TIMEOUT_MS      1500

// ===========================================================================
//  I2C BUS
//  MAX30102, MPU6050 and the SH1106 OLED all share this single bus.
// ===========================================================================
#define PIN_I2C_SDA                 21      ///< GPIO 21 -> SDA
#define PIN_I2C_SCL                 22      ///< GPIO 22 -> SCL
#define I2C_BUS_FREQUENCY_HZ        400000UL ///< 400 kHz fast mode

#define I2C_ADDR_MAX30102           0x57
#define I2C_ADDR_MPU6050            0x68
#define I2C_ADDR_OLED               0x3C

// ===========================================================================
//  DIGITAL / ANALOG PIN MAP
// ===========================================================================
#define PIN_DS18B20_DATA            4       ///< OneWire data line
#define PIN_MIC_ANALOG              34      ///< MAX9814 OUT (ADC1_CH6, input only)
#define PIN_BUZZER                  25      ///< Active buzzer (HIGH = sound)
#define PIN_LED_RED                 26      ///< Warning / alert indicator
#define PIN_LED_GREEN               27      ///< Normal-status indicator

// ===========================================================================
//  SCHEDULER INTERVALS (millis-based cooperative scheduling)
// ===========================================================================
#define INTERVAL_PULSEOX_MS         20      ///< MAX30102 sample pump
#define INTERVAL_TEMPERATURE_MS     1000    ///< DS18B20 conversion cadence
#define INTERVAL_CRY_MS             10      ///< Microphone sampling tick
#define INTERVAL_MOVEMENT_MS        50      ///< MPU6050 poll (20 Hz)
#define INTERVAL_ALERT_MS           100     ///< Status engine re-evaluation
#define INTERVAL_DISPLAY_MS         250     ///< OLED redraw (4 FPS)
#define INTERVAL_SERIAL_REPORT_MS   2000    ///< Human-readable serial dump

// ===========================================================================
//  HEART RATE  (beats per minute)
//  Reference range for a resting healthy newborn is roughly 100-160 BPM.
// ===========================================================================
#define HR_INVALID                  (-1.0f) ///< Sentinel for "no valid reading"
#define HR_NORMAL_MIN               100.0f
#define HR_NORMAL_MAX               160.0f
#define HR_WARNING_MIN              90.0f   ///< Below NORMAL_MIN but not critical
#define HR_WARNING_MAX              180.0f
#define HR_PLAUSIBLE_MIN            30.0f   ///< Outside this the sample is noise
#define HR_PLAUSIBLE_MAX            250.0f

/// Number of accepted beat intervals averaged into the reported BPM.
#define HR_AVERAGE_WINDOW           8
/// Drop the reading to "invalid" if no beat is seen for this long.
#define HR_SIGNAL_TIMEOUT_MS        5000

// ===========================================================================
//  SpO2  (percent saturation)
// ===========================================================================
#define SPO2_INVALID                (-1.0f)
#define SPO2_NORMAL_MIN             95.0f
#define SPO2_WARNING_MIN            90.0f   ///< 90-95 % -> warning band
#define SPO2_ALERT_MIN              90.0f   ///< below this -> alert
#define SPO2_PLAUSIBLE_MIN          70.0f
#define SPO2_PLAUSIBLE_MAX          100.0f
#define SPO2_AVERAGE_WINDOW         4

/// IR DC level below which we assume there is no skin contact at all.
#define PULSEOX_CONTACT_IR_THRESHOLD 50000UL

// ---- MAX30102 sensor configuration -----------------------------------
#define MAX30102_LED_BRIGHTNESS     0x1F    ///< 0x00-0xFF (~6.4 mA)
#define MAX30102_SAMPLE_AVERAGE     4
#define MAX30102_LED_MODE           2       ///< 2 = Red + IR (required for SpO2)
#define MAX30102_SAMPLE_RATE        100     ///< Hz
#define MAX30102_PULSE_WIDTH        411     ///< us
#define MAX30102_ADC_RANGE          4096

/// Length of the rolling buffer handed to the SpO2 estimator (100 Hz -> 1 s).
#define PULSEOX_BUFFER_LENGTH       100

// ===========================================================================
//  BODY TEMPERATURE  (degrees Celsius, axillary/skin contact)
// ===========================================================================
#define TEMP_INVALID                (-127.0f) ///< Matches DallasTemperature error
#define TEMP_NORMAL_MIN             36.4f
#define TEMP_NORMAL_MAX             37.5f
#define TEMP_WARNING_LOW            36.0f   ///< 36.0-36.4 -> cool, warning
#define TEMP_WARNING_HIGH           37.8f   ///< 37.5-37.8 -> warm, warning
#define TEMP_ALERT_LOW              36.0f   ///< below -> hypothermia alert
#define TEMP_ALERT_HIGH             37.8f   ///< above -> hyperthermia alert
#define TEMP_PLAUSIBLE_MIN          20.0f
#define TEMP_PLAUSIBLE_MAX          45.0f

#define TEMP_RESOLUTION_BITS        11      ///< 11 bit -> 0.125 C, ~375 ms
#define TEMP_AVERAGE_WINDOW         4
/// Consecutive failed conversions before the sensor is declared missing.
#define TEMP_FAILURE_LIMIT          3

// ===========================================================================
//  CRY DETECTION  (MAX9814 envelope on an ADC pin)
// ===========================================================================
/// ADC counts (12-bit, 0-4095). Amplitude above this counts as "loud".
#define CRY_AMPLITUDE_THRESHOLD     420
/// Exponential smoothing factor applied to the amplitude envelope (0..1).
#define CRY_SMOOTHING_ALPHA         0.15f
/// Sustained loudness required before the state flips to CRY_DETECTED (ms).
#define CRY_PERSISTENCE_MS          800
/// Quiet time required before the state flips back to NO_CRY (ms).
#define CRY_RELEASE_MS              2500
/// Samples used to establish the ambient DC bias at boot.
#define CRY_CALIBRATION_SAMPLES     256
/// Slow adaptation rate of the DC bias tracker during normal operation.
#define CRY_BIAS_TRACK_ALPHA        0.0005f
/// Envelope samples folded into one peak-to-peak amplitude estimate.
#define CRY_WINDOW_SAMPLES          32

// ===========================================================================
//  MOVEMENT DETECTION  (MPU6050)
// ===========================================================================
/// Standard gravity used to convert m/s^2 into g.
#define GRAVITY_MS2                 9.80665f
/// |a| deviation from 1 g, in g, above which we call it MOVEMENT.
#define MOVE_THRESHOLD_G            0.18f
/// |a| deviation from 1 g, in g, above which we call it SUDDEN MOVEMENT.
#define MOVE_SUDDEN_THRESHOLD_G     0.65f
/// Gyroscope magnitude (deg/s) that also qualifies as SUDDEN MOVEMENT.
#define MOVE_GYRO_THRESHOLD_DPS     180.0f
/// Low-pass factor for the acceleration magnitude.
#define MOVE_SMOOTHING_ALPHA        0.25f
/// How long a movement classification is held before decaying to NORMAL (ms).
#define MOVE_HOLD_MS                1500
/// Sustained SUDDEN MOVEMENT for this long escalates the alert state (ms).
#define MOVE_SUSTAINED_ALERT_MS     3000
/// No movement at all for this long is reported as prolonged stillness (ms).
#define MOVE_STILLNESS_MS           120000UL
/// Tilt angle (degrees from vertical axis) beyond which orientation is "SIDE".
#define ORIENT_SIDE_ANGLE_DEG       45.0f
/// Tilt angle beyond which orientation is reported as "PRONE" (face-down).
#define ORIENT_PRONE_ANGLE_DEG      135.0f

// ---- MPU6050 ranges ----------------------------------------------------
#define MPU_ACCEL_RANGE_G           4       ///< +/- 4 g
#define MPU_GYRO_RANGE_DPS          500     ///< +/- 500 deg/s
#define MPU_FILTER_BANDWIDTH_HZ     21

// ===========================================================================
//  DISPLAY (1.3" 128x64 SH1106, I2C)
// ===========================================================================
#define OLED_WIDTH                  128
#define OLED_HEIGHT                 64
#define OLED_RESET_PIN              (-1)    ///< No dedicated reset line
#define OLED_PAGE_COUNT             3       ///< Vitals / Movement / System
/// Automatic page rotation period (ms). Set to 0 to disable auto-cycling.
#define OLED_PAGE_DWELL_MS          5000
#define OLED_SPLASH_HOLD_MS         1200

// ===========================================================================
//  ALERT SYSTEM
// ===========================================================================
/// Red LED blink period while in WARNING (ms).
#define ALERT_WARNING_BLINK_MS      500
/// Buzzer on/off period while in ALERT (ms). Equal on and off time.
#define ALERT_BUZZER_PERIOD_MS      400
/// Sound the buzzer briefly during WARNING as well?
#define ALERT_BUZZER_ON_WARNING     false
/// Green LED blink period while in SENSOR_ERROR (ms).
#define ALERT_ERROR_BLINK_MS        150
/// A condition must persist this long before the state is allowed to escalate.
#define ALERT_ESCALATE_DEBOUNCE_MS  1500
/// A condition must clear for this long before the state is allowed to relax.
#define ALERT_RECOVER_DEBOUNCE_MS   3000
/// Buzzer logic level that produces sound (active-high module).
#define BUZZER_ACTIVE_LEVEL         HIGH

#endif // CONFIG_H
