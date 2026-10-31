#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include <io/pixel_layout.hpp>

namespace enim {

/// @brief Reading and writing single samples of any SampleType, shared by the CPU converter and the pyramid builder.
namespace sample_io {

inline float half_to_float(std::uint16_t h) {
    std::uint32_t sign = static_cast<std::uint32_t>(h & 0x8000) << 16;
    std::uint32_t exponent = (h >> 10) & 0x1F;
    std::uint32_t mantissa = h & 0x3FF;
    if (exponent == 0) // zero or subnormal: mantissa * 2^-24
        return (sign ? -1.0f : 1.0f) * std::ldexp(static_cast<float>(mantissa), -24);
    if (exponent == 31) // inf or nan
        return std::bit_cast<float>(sign | 0x7F800000 | (mantissa << 13));
    return std::bit_cast<float>(sign | ((exponent + 112) << 23) | (mantissa << 13));
}

inline std::uint16_t float_to_half(float f) {
    std::uint32_t bits = std::bit_cast<std::uint32_t>(f);
    std::uint16_t sign = static_cast<std::uint16_t>((bits >> 16) & 0x8000);
    std::int32_t exponent = static_cast<std::int32_t>((bits >> 23) & 0xFF) - 127 + 15;
    std::uint32_t mantissa = bits & 0x7FFFFF;
    if (((bits >> 23) & 0xFF) == 0xFF) // inf or nan
        return sign | 0x7C00 | (mantissa ? 0x200 : 0);
    if (exponent >= 31) // overflow: inf
        return sign | 0x7C00;
    if (exponent <= 0) { // subnormal or zero
        if (exponent < -10)
            return sign;
        mantissa |= 0x800000;
        std::uint32_t shift = static_cast<std::uint32_t>(14 - exponent);
        std::uint32_t rounded = (mantissa + (1u << (shift - 1))) >> shift;
        return sign | static_cast<std::uint16_t>(rounded);
    }
    std::uint32_t rounded = mantissa + 0x1000; // round to nearest
    if (rounded & 0x800000) {
        rounded = 0;
        ++exponent;
        if (exponent >= 31)
            return sign | 0x7C00;
    }
    return sign | static_cast<std::uint16_t>(exponent << 10) | static_cast<std::uint16_t>(rounded >> 13);
}

template <typename T>
T load(const std::uint8_t* p, bool swap) {
    T value;
    std::memcpy(&value, p, sizeof(T));
    return swap ? std::byteswap(value) : value;
}

template <typename T>
void store(std::uint8_t* p, T value, bool swap) {
    if (swap)
        value = std::byteswap(value);
    std::memcpy(p, &value, sizeof(T));
}

inline float read_sample(const std::uint8_t* p, SampleType type, bool swap) {
    switch (type) {
    case SampleType::U8: return static_cast<float>(*p);
    case SampleType::I8: return static_cast<float>(static_cast<std::int8_t>(*p));
    case SampleType::U16: return static_cast<float>(load<std::uint16_t>(p, swap));
    case SampleType::I16: return static_cast<float>(static_cast<std::int16_t>(load<std::uint16_t>(p, swap)));
    case SampleType::U32: return static_cast<float>(load<std::uint32_t>(p, swap));
    case SampleType::I32: return static_cast<float>(static_cast<std::int32_t>(load<std::uint32_t>(p, swap)));
    case SampleType::F16: return half_to_float(load<std::uint16_t>(p, swap));
    case SampleType::F32: return std::bit_cast<float>(load<std::uint32_t>(p, swap));
    }
    return 0.0f;
}

template <typename T>
T round_to(double value) {
    constexpr double lo = static_cast<double>(std::numeric_limits<T>::min());
    constexpr double hi = static_cast<double>(std::numeric_limits<T>::max());
    return static_cast<T>(std::clamp(std::round(value), lo, hi));
}

/// @brief Writes a value in the source's own sample type and byte order (pyramid levels): integers are rounded and clamped.
inline void write_native_sample(std::uint8_t* p, SampleType type, double value, bool swap) {
    switch (type) {
    case SampleType::U8: *p = round_to<std::uint8_t>(value); break;
    case SampleType::I8: *p = static_cast<std::uint8_t>(round_to<std::int8_t>(value)); break;
    case SampleType::U16: store(p, round_to<std::uint16_t>(value), swap); break;
    case SampleType::I16: store(p, static_cast<std::uint16_t>(round_to<std::int16_t>(value)), swap); break;
    case SampleType::U32: store(p, round_to<std::uint32_t>(value), swap); break;
    case SampleType::I32: store(p, static_cast<std::uint32_t>(round_to<std::int32_t>(value)), swap); break;
    case SampleType::F16: store(p, float_to_half(static_cast<float>(value)), swap); break;
    case SampleType::F32: store(p, std::bit_cast<std::uint32_t>(static_cast<float>(value)), swap); break;
    }
}

/// @brief True if samples of this layout are stored in the other byte order than the machine's.
inline bool needs_swap(const PixelLayout& layout) {
    return sample_size(layout.sample) > 1 && layout.big_endian != (std::endian::native == std::endian::big);
}

} // namespace sample_io

} // namespace enim
