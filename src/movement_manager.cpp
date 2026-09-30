/**
 * @file    movement_manager.cpp
 * @brief   Implementation of the MPU6050 movement / orientation manager.
 *
 * Classification rule:
 *
 *   deviation = | |a| - 1 g |            (gravity removed by magnitude)
 *
 *   deviation >= MOVE_SUDDEN_THRESHOLD_G  or  |w| >= MOVE_GYRO_THRESHOLD_DPS
 *                                          -> SUDDEN_MOVEMENT
 *   deviation >= MOVE_THRESHOLD_G          -> MOVEMENT
 *   otherwise, after MOVE_HOLD_MS          -> NORMAL
 *
 * Using the magnitude rather than individual axes makes the classification
 * independent of how the sensor happens to be oriented on the garment.
 */

#include "movement_manager.h"

#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

namespace {
Adafruit_MPU6050 g_mpu;
}

MovementManager::MovementManager()
    : m_available(false),
      m_state(MovementState::NORMAL),
      m_orientation(Orientation::UNKNOWN),
      m_ax(0.0f), m_ay(0.0f), m_az(0.0f),
      m_gx(0.0f), m_gy(0.0f), m_gz(0.0f),
      m_accelMagnitude(1.0f),
      m_gyroMagnitude(0.0f),
      m_tiltAngle(0.0f),
      m_lastStateChangeMs(0),
      m_suddenSinceMs(0),
      m_lastMotionMs(0),
      m_primed(false) {}

bool MovementManager::begin() {
    if (!g_mpu.begin(I2C_ADDR_MPU6050, &Wire)) {
        m_available = false;
        return false;
    }

#if MPU_ACCEL_RANGE_G == 2
    g_mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
#elif MPU_ACCEL_RANGE_G == 4
    g_mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
#elif MPU_ACCEL_RANGE_G == 8
    g_mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
#else
    g_mpu.setAccelerometerRange(MPU6050_RANGE_16_G);
#endif

#if MPU_GYRO_RANGE_DPS == 250
    g_mpu.setGyroRange(MPU6050_RANGE_250_DEG);
#elif MPU_GYRO_RANGE_DPS == 500
    g_mpu.setGyroRange(MPU6050_RANGE_500_DEG);
#elif MPU_GYRO_RANGE_DPS == 1000
    g_mpu.setGyroRange(MPU6050_RANGE_1000_DEG);
#else
    g_mpu.setGyroRange(MPU6050_RANGE_2000_DEG);
#endif

    g_mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

    m_lastMotionMs = millis();
    m_available    = true;
    return true;
}

void MovementManager::update() {
    if (!m_available) {
        return;
    }

    sensors_event_t accel;
    sensors_event_t gyro;
    sensors_event_t temp;

    if (!g_mpu.getEvent(&accel, &gyro, &temp)) {
        m_available = false;   // Device stopped acknowledging on the bus.
        return;
    }

    m_ax = accel.acceleration.x;
    m_ay = accel.acceleration.y;
    m_az = accel.acceleration.z;

    // The Adafruit driver reports rad/s; the thresholds are expressed in
    // degrees per second, which is easier to reason about.
    m_gx = gyro.gyro.x * RAD_TO_DEG;
    m_gy = gyro.gyro.y * RAD_TO_DEG;
    m_gz = gyro.gyro.z * RAD_TO_DEG;

    const float rawMagnitudeG =
        sqrtf(m_ax * m_ax + m_ay * m_ay + m_az * m_az) / GRAVITY_MS2;

    if (!m_primed) {
        m_accelMagnitude = rawMagnitudeG;
        m_primed = true;
    } else {
        m_accelMagnitude += MOVE_SMOOTHING_ALPHA * (rawMagnitudeG - m_accelMagnitude);
    }

    m_gyroMagnitude = sqrtf(m_gx * m_gx + m_gy * m_gy + m_gz * m_gz);

    // Compare the instantaneous magnitude, not the smoothed one, so a short
    // startle is not averaged away before it is ever classified.
    classify(fabsf(rawMagnitudeG - 1.0f), m_gyroMagnitude);
    updateOrientation();
}

void MovementManager::classify(float deviationG, float gyroMagnitude) {
    const uint32_t now = millis();
    MovementState  candidate = MovementState::NORMAL;

    if (deviationG >= MOVE_SUDDEN_THRESHOLD_G ||
        gyroMagnitude >= MOVE_GYRO_THRESHOLD_DPS) {
        candidate = MovementState::SUDDEN_MOVEMENT;
    } else if (deviationG >= MOVE_THRESHOLD_G) {
        candidate = MovementState::MOVEMENT;
    }

    if (candidate != MovementState::NORMAL) {
        m_lastMotionMs = now;
    }

    if (candidate == MovementState::SUDDEN_MOVEMENT) {
        if (m_suddenSinceMs == 0) {
            m_suddenSinceMs = now;
        }
    } else {
        m_suddenSinceMs = 0;
    }

    // Escalate immediately, but hold a classification for MOVE_HOLD_MS before
    // decaying so the display does not flicker between states.
    if (candidate > m_state) {
        m_state = candidate;
        m_lastStateChangeMs = now;
    } else if (candidate < m_state && (now - m_lastStateChangeMs) >= MOVE_HOLD_MS) {
        m_state = candidate;
        m_lastStateChangeMs = now;
    }
}

void MovementManager::updateOrientation() {
    const float magnitude = sqrtf(m_ax * m_ax + m_ay * m_ay + m_az * m_az);
    if (magnitude < 0.1f) {
        m_orientation = Orientation::UNKNOWN;
        return;
    }

    // Angle between the measured gravity vector and the sensor's +Z axis.
    float cosTheta = m_az / magnitude;
    if (cosTheta >  1.0f) cosTheta =  1.0f;
    if (cosTheta < -1.0f) cosTheta = -1.0f;

    m_tiltAngle = acosf(cosTheta) * RAD_TO_DEG;

    if (m_tiltAngle >= ORIENT_PRONE_ANGLE_DEG) {
        m_orientation = Orientation::PRONE;
    } else if (m_tiltAngle >= ORIENT_SIDE_ANGLE_DEG) {
        m_orientation = Orientation::SIDE;
    } else {
        m_orientation = Orientation::NORMAL;
    }
}

bool MovementManager::isSustainedSuddenMovement() const {
    return (m_suddenSinceMs != 0) &&
           ((millis() - m_suddenSinceMs) >= MOVE_SUSTAINED_ALERT_MS);
}

bool MovementManager::isProlongedStillness() const {
    return m_available && ((millis() - m_lastMotionMs) >= MOVE_STILLNESS_MS);
}

MovementState MovementManager::getState() const       { return m_state; }
Orientation   MovementManager::getOrientation() const { return m_orientation; }
float MovementManager::getAccelerationMagnitude() const { return m_accelMagnitude; }
float MovementManager::getGyroMagnitude() const         { return m_gyroMagnitude; }
float MovementManager::getTiltAngle() const             { return m_tiltAngle; }
float MovementManager::getAx() const { return m_ax; }
float MovementManager::getAy() const { return m_ay; }
float MovementManager::getAz() const { return m_az; }
float MovementManager::getGx() const { return m_gx; }
float MovementManager::getGy() const { return m_gy; }
float MovementManager::getGz() const { return m_gz; }
bool  MovementManager::isSensorAvailable() const { return m_available; }

const char* MovementManager::getStateText() const {
    switch (m_state) {
        case MovementState::SUDDEN_MOVEMENT: return "SUDDEN";
        case MovementState::MOVEMENT:        return "ACTIVE";
        case MovementState::NORMAL:
        default:                             return "NORMAL";
    }
}

const char* MovementManager::getOrientationText() const {
    switch (m_orientation) {
        case Orientation::NORMAL: return "NORMAL";
        case Orientation::SIDE:   return "SIDE";
        case Orientation::PRONE:  return "PRONE";
        case Orientation::UNKNOWN:
        default:                  return "UNKNOWN";
    }
}
