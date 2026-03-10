/**
 * @file    sensor_reading.h
 * @brief   SensorReading — generic sensor sample value type.
 */
#pragma once

#include <cstdint>

namespace middleware {

/**
 * @struct SensorReading
 * @brief  A single sensor sample suitable for all HAL sensor types.
 *
 * For 1-axis sensors (temperature, pressure, light, proximity) use `scalar`.
 * For 3-axis sensors (accelerometer, gyroscope, magnetometer) use x/y/z.
 */
struct SensorReading {
    /** @brief Identifies which physical sensor produced this reading. */
    enum class Type : uint8_t {
        Unknown      = 0,
        Accelerometer,
        Gyroscope,
        Magnetometer,
        Temperature,
        Pressure,
        Humidity,
        Light,
        Proximity,
        Barometer,
    };

    Type     sensor_type    = Type::Unknown;  ///< Source device type.
    uint32_t sensor_id      = 0;              ///< Instance id (for multi-sensor boards).
    uint64_t timestamp_us   = 0;              ///< Kernel monotonic clock in microseconds.
    uint32_t sequence       = 0;             ///< Monotonically-increasing sample counter.

    // --- 3-axis float values (SI units unless scale_factor != 1.0f) ---
    float x = 0.f;  ///< Primary / x-axis value.
    float y = 0.f;  ///< y-axis value.
    float z = 0.f;  ///< z-axis value.

    // --- Scalar for single-axis sensors ---
    float scalar = 0.f;  ///< Value for 1D sensors; mirrors x.

    // --- Resolution / scaling metadata ---
    float    scale_factor   = 1.f;  ///< Multiplier to convert raw->SI.
    uint32_t raw_bits       = 0;    ///< Original ADC/raw reading.
    uint8_t  validity_flags = 0;    ///< Bit 0: saturated; Bit 1: out-of-range.
    uint8_t  _pad[3]        = {};
};

} // namespace middleware
