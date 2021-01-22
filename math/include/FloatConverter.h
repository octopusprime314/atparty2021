#include <inttypes.h>
#include <cmath>
#include <cfloat>
#include <numeric>

/// A static constant for a half float with a value of zero.
static const uint16_t ZERO = 0x0000;

/// A static constant for a half float with a value of not-a-number.
static const uint16_t NOT_A_NUMBER = 0xFFFF;

/// A static constant for a half float with a value of positive infinity.
static const uint16_t POSITIVE_INFINITY = 0x7C00;

/// A static constant for a half float with a value of negative infinity.
static const uint16_t NEGATIVE_INFINITY = 0xFC00;

/// A mask which isolates the sign of a half float number.
static const uint16_t HALF_FLOAT_SIGN_MASK = 0x8000;

/// A mask which isolates the exponent of a half float number.
static const uint16_t HALF_FLOAT_EXPONENT_MASK = 0x7C00;

/// A mask which isolates the significand of a half float number.
static const uint16_t HALF_FLOAT_SIGNIFICAND_MASK = 0x03FF;

/// A mask which isolates the sign of a single precision float number.
static const uint32_t FLOAT_SIGN_MASK = 0x80000000;

/// A mask which isolates the exponent of a single precision float number.
static const uint32_t FLOAT_EXPONENT_MASK = 0x7F800000;

/// A mask which isolates the significand of a single precision float number.
static const uint32_t FLOAT_SIGNIFICAND_MASK = 0x007FFFFF;

/// Epsilon to handle very close to 0.0
static const float FLOAT_EPSILON = 0.001;

/// Convert the specified single precision float number to a half precision float number.
static uint16_t floatToHalfFloat(float floatValue)
{
    // Catch special case floating point values.
    if (std::isnan(floatValue))
    {
        return NOT_A_NUMBER;
    }
    else if (std::isinf(floatValue))
    {
        return POSITIVE_INFINITY;
    }

    uint32_t value = *((uint32_t*)&floatValue);

    // Required otherwise normals get bungled
    if (floatValue <= FLOAT_EPSILON && floatValue >= -FLOAT_EPSILON)
    {
        return uint16_t(0);
        //return uint16_t(value >> 16);
    }
    else
    {
        // Start by computing the significand in half precision format.
        uint16_t output = uint16_t((value & FLOAT_SIGNIFICAND_MASK) >> 13);

        uint32_t exponent = ((value & FLOAT_EXPONENT_MASK) >> 23);

        // Check for subnormal numbers.
        if (exponent != 0)
        {
            // Check for overflow when converting large numbers, returning positive or negative
            // infinity.
            if (exponent > 142)
            {
                return uint16_t((value & FLOAT_SIGN_MASK) >> 16) | uint16_t(0x7C00);
            }

            // Add the exponent of the half float, converting the offset binary formats of the
            // representations.
            output |= (((exponent - 112) << 10) & HALF_FLOAT_EXPONENT_MASK);
        }

        // Add the sign bit.
        output |= uint16_t((value & FLOAT_SIGN_MASK) >> 16);

        return output;
    }
}

/// Convert the specified half float number to a single precision float number.
static float halfFloatToFloat(uint16_t halfFloat)
{
    // Catch special case half floating point values.
    switch (halfFloat)
    {
        case NOT_A_NUMBER:
            return std::numeric_limits<float>::quiet_NaN();
        case POSITIVE_INFINITY:
            return std::numeric_limits<float>::infinity();
        case NEGATIVE_INFINITY:
            return std::numeric_limits<float>::infinity();
    }

    // Start by computing the significand in single precision format.
    uint32_t value = uint32_t(halfFloat & HALF_FLOAT_SIGNIFICAND_MASK) << 13;

    uint32_t exponent = uint32_t(halfFloat & HALF_FLOAT_EXPONENT_MASK) >> 10;

    if (exponent != 0)
    {
        // Add the exponent of the float, converting the offset binary formats of the
        // representations.
        value |= (((exponent - 15 + 127) << 23) & FLOAT_EXPONENT_MASK);
    }

    // Add the sign bit.
    value |= uint32_t(halfFloat & HALF_FLOAT_SIGN_MASK) << 16;

    return *((float*)&value);
}
