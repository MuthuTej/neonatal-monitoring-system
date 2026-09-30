/**
 * @file    movement_manager.h
 * @brief   MPU6050 based movement classification and coarse orientation.
 *
 * Reads the six raw axes (AX, AY, AZ, GX, GY, GZ), derives the acceleration
 * magnitude in g and the gyroscope magnitude in deg/s, and classifies the
 * result into NORMAL / MOVEMENT / SUDDEN_MOVEMENT. Orientation is inferred
 * from the tilt of the gravity vector.
 *
 * @note Basic prototype logic. This is not validated fall detection and must
 *       not be relied upon for safety.
 */

#ifndef MOVEMENT_MANAGER_H
#define MOVEMENT_MANAGER_H

#include <Arduino.h>
#include "config.h"

/// Movement classification.
enum class MovementState : uint8_t {
    NORMAL = 0,         ///< At rest or breathing-level motion only
    MOVEMENT,           ///< Ordinary limb / body motion
    SUDDEN_MOVEMENT     ///< Startle, jerk or impact-like acceleration
};

/// Coarse orientation derived from the gravity vector.
enum class Orientation : uint8_t {
    NORMAL = 0,         ///< Supine, sensor facing up
    SIDE,               ///< Tilted onto one side
    PRONE,              ///< Face-down / sensor inverted
    UNKNOWN
};

class MovementManager {
public:
    MovementManager();

    /// Probe and configure the MPU6050 on the shared I2C bus.
    bool begin();

    /// Sample the six axes and update the classification. Non-blocking.
    void update();

    MovementState getState() const;
    Orientation   getOrientation() const;

    float getAccelerationMagnitude() const;  ///< In g (1.0 at rest)
    float getGyroMagnitude() const;          ///< In deg/s
    float getTiltAngle() const;              ///< Degrees from the +Z axis

    /// Raw-ish axis values, exposed for the serial dump.
    float getAx() const; float getAy() const; float getAz() const;
    float getGx() const; float getGy() const; float getGz() const;

    /// True when SUDDEN_MOVEMENT has persisted long enough to be escalated.
    bool isSustainedSuddenMovement() const;
    /// True when nothing has moved for longer than MOVE_STILLNESS_MS.
    bool isProlongedStillness() const;

    bool isSensorAvailable() const;

    const char* getStateText() const;
    const char* getOrientationText() const;

private:
    void classify(float deviationG, float gyroMagnitude);
    void updateOrientation();

    bool          m_available;
    MovementState m_state;
    Orientation   m_orientation;

    float m_ax, m_ay, m_az;     ///< m/s^2
    float m_gx, m_gy, m_gz;     ///< deg/s

    float m_accelMagnitude;     ///< Smoothed, in g
    float m_gyroMagnitude;      ///< In deg/s
    float m_tiltAngle;          ///< Degrees

    uint32_t m_lastStateChangeMs;
    uint32_t m_suddenSinceMs;
    uint32_t m_lastMotionMs;
    bool     m_primed;          ///< First sample seeds the filter directly
};

#endif // MOVEMENT_MANAGER_H
