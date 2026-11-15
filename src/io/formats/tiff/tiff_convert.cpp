#include "tiff_convert.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

#include <pipeline/sample_io.hpp>

namespace {

using enim::SampleType;
using enim::tiff::ConvertSpec;
using enim::tiff::InFormat;

bool is_float(SampleType t) { return t == SampleType::F16 || t == SampleType::F32; }

double int_max(SampleType t) {
    switch (t) {
    case SampleType::U8: return 255.0;
    case SampleType::U16: return 65535.0;
    case SampleType::U32: return 4294967295.0;
    case SampleType::I8: return 127.0;
    case SampleType::I16: return 32767.0;
    case SampleType::I32: return 2147483647.0;
    default: return 1.0;
    }
}

double int_min(SampleType t) {
    switch (t) {
    case SampleType::I8: return -128.0;
    case SampleType::I16: return -32768.0;
    case SampleType::I32: return -2147483648.0;
    default: return 0.0;
    }
}

/// @brief Photoshop/GDAL 24-bit float: 1 sign bit, 7 exponent bits (bias 63), 16 mantissa bits.
float fp24_to_float(std::uint32_t v) {
    const std::uint32_t sign = (v >> 23) & 1;
    const std::uint32_t exponent = (v >> 16) & 0x7F;
    const std::uint32_t mantissa = v & 0xFFFF;
    float out;
    if (exponent == 0)
        out = std::ldexp(static_cast<float>(mantissa), -62 - 16); // zero or subnormal
    else if (exponent == 0x7F)
        out = mantissa ? std::bit_cast<float>(0x7FC00000u) : std::bit_cast<float>(0x7F800000u);
    else
        return std::bit_cast<float>((sign << 31) | ((exponent - 63 + 127) << 23) | (mantissa << 7));
    return sign ? -out : out;
}

/// @brief Raw bits of one sample. bit_offset counts from the start of the row; samples of 8, 16, 24, 32 or 64 bits
/// are byte-aligned and in host byte order (libtiff swaps them), other sizes are packed MSB-first.
std::uint64_t read_bits(const std::uint8_t* row, std::size_t bit_offset, unsigned bits) {
    const std::uint8_t* p = row + (bit_offset >> 3);
    switch (bits) {
    case 8: return *p;
    case 16: return enim::sample_io::load<std::uint16_t>(p, false);
    case 32: return enim::sample_io::load<std::uint32_t>(p, false);
    case 64: return enim::sample_io::load<std::uint64_t>(p, false);
    case 24:
        if constexpr (std::endian::native == std::endian::little)
            return std::uint64_t{p[0]} | (std::uint64_t{p[1]} << 8) | (std::uint64_t{p[2]} << 16);
        else
            return std::uint64_t{p[2]} | (std::uint64_t{p[1]} << 8) | (std::uint64_t{p[0]} << 16);
    default: {
        // at most 7 + 32 bits: 8 bytes read big-endian always cover them (rows are padded)
        std::uint64_t word = 0;
        for (int i = 0; i < 8; ++i)
            word = (word << 8) | p[i];
        const unsigned shift = static_cast<unsigned>(bit_offset & 7);
        return (word >> (64 - shift - bits)) & ((std::uint64_t{1} << bits) - 1);
    }
    }
}

double raw_to_value(std::uint64_t raw, InFormat format, unsigned bits) {
    switch (format) {
    case InFormat::UInt: return static_cast<double>(raw);
    case InFormat::Int: {
        if (bits == 64)
            return static_cast<double>(std::bit_cast<std::int64_t>(raw));
        const std::uint64_t sign_bit = std::uint64_t{1} << (bits - 1);
        return static_cast<double>(static_cast<std::int64_t>((raw ^ sign_bit) - sign_bit));
    }
    case InFormat::Float:
        switch (bits) {
        case 16: return enim::sample_io::half_to_float(static_cast<std::uint16_t>(raw));
        case 24: return fp24_to_float(static_cast<std::uint32_t>(raw));
        case 32: return std::bit_cast<float>(static_cast<std::uint32_t>(raw));
        default: return std::bit_cast<double>(raw);
        }
    }
    return 0.0;
}

/// @brief Factor from input integer values to the output domain: the full range of the input bit depth maps to the
/// full range of the output type (or to [0, 1] / [-1, 1] for float output).
double input_scale(const ConvertSpec& spec) {
    if (spec.format == InFormat::Float)
        return 1.0;
    const bool same_size = spec.bits == enim::sample_size(spec.out_sample) * 8 && !is_float(spec.out_sample);
    if (same_size)
        return 1.0;
    const double in_max = spec.format == InFormat::UInt ? std::ldexp(1.0, spec.bits) - 1.0 : std::ldexp(1.0, spec.bits - 1) - 1.0;
    return (is_float(spec.out_sample) ? 1.0 : int_max(spec.out_sample)) / in_max;
}

} // namespace

namespace enim::tiff {

std::size_t decoded_row_bytes(const ConvertSpec& spec) {
    const std::size_t samples = spec.planar ? 1 : spec.samples;
    return (spec.width * samples * spec.bits + 7) / 8;
}

void convert_row(const ConvertSpec& spec, const std::uint8_t* const* planes, std::uint8_t* out) {
    const double scale = input_scale(spec);
    const bool float_out = is_float(spec.out_sample);
    const double full = float_out ? 1.0 : int_max(spec.out_sample); // the value of "100%" in the output domain
    const double flip = float_out ? double{spec.float_range[0]} + spec.float_range[1] : int_min(spec.out_sample) + full;
    const std::uint8_t color_out = spec.color_out();
    const std::size_t out_size = sample_size(spec.out_sample);
    const std::size_t out_bpp = out_size * spec.out_channels;
    const std::size_t palette_size = spec.colormap[0].size();

    std::array<double, kMaxChannels> in{};
    std::array<double, kMaxChannels> px{};
    for (std::size_t x = 0; x < spec.width; ++x) {
        for (std::size_t s = 0; s < spec.samples; ++s) {
            const std::uint8_t* row = spec.planar ? planes[s] : planes[0];
            const std::size_t bit = spec.planar ? x * spec.bits : (x * spec.samples + s) * spec.bits;
            const std::uint64_t raw = read_bits(row, bit, spec.bits);
            in[s] = spec.op == ColorOp::Palette && s == 0 ? static_cast<double>(raw) : raw_to_value(raw, spec.format, spec.bits) * scale;
        }

        switch (spec.op) {
        case ColorOp::Copy:
            for (std::size_t c = 0; c < spec.color_in; ++c)
                px[c] = in[c];
            break;
        case ColorOp::MinIsWhite: px[0] = flip - in[0]; break;
        case ColorOp::Palette: {
            const std::size_t index = static_cast<std::size_t>(in[0]);
            for (std::size_t c = 0; c < 3; ++c) {
                const double v = index < palette_size ? spec.colormap[c][index] : 0.0;
                px[c] = spec.out_sample == SampleType::U8 ? v / 257.0 : v;
            }
            break;
        }
        case ColorOp::Cmyk: {
            const double k = full - in[3];
            for (std::size_t c = 0; c < 3; ++c)
                px[c] = (full - in[c]) * k / full;
            break;
        }
        case ColorOp::XyzToRgb: {
            const double X = in[0], Y = in[1], Z = in[2];
            px[0] = 3.2404542 * X - 1.5371385 * Y - 0.4985314 * Z;
            px[1] = -0.9692660 * X + 1.8760108 * Y + 0.0415560 * Z;
            px[2] = 0.0556434 * X - 0.2040259 * Y + 1.0572252 * Z;
            break;
        }
        }
        // extra samples (alpha, others) follow the color channels
        for (std::size_t s = spec.color_in; s < spec.samples; ++s)
            px[color_out + (s - spec.color_in)] = in[s];

        if (spec.unpremultiply && spec.alpha >= 0) {
            const double a = in[static_cast<std::size_t>(spec.alpha)];
            for (std::size_t c = 0; c < color_out; ++c)
                px[c] = a > 0.0 ? px[c] * full / a : 0.0;
        }

        std::uint8_t* dst = out + (spec.mirror ? spec.width - 1 - x : x) * out_bpp;
        for (std::size_t c = 0; c < spec.out_channels; ++c)
            sample_io::write_native_sample(dst + c * out_size, spec.out_sample, px[c], false);
    }
}

void finish_rgba_row(std::uint8_t* row, std::size_t width, bool unpremultiply, bool mirror) {
    for (std::size_t x = 0; x < width; ++x) {
        std::uint8_t* p = row + x * 4;
        std::uint32_t word;
        std::memcpy(&word, p, 4);
        std::uint8_t r = word & 0xFF, g = (word >> 8) & 0xFF, b = (word >> 16) & 0xFF, a = word >> 24;
        if (unpremultiply) {
            auto straight = [a](std::uint8_t c) -> std::uint8_t {
                return a ? static_cast<std::uint8_t>(std::min(255u, (c * 255u + a / 2u) / a)) : 0;
            };
            r = straight(r), g = straight(g), b = straight(b);
        }
        p[0] = r, p[1] = g, p[2] = b, p[3] = a;
    }
    if (mirror)
        mirror_row(row, width, 4);
}

void mirror_row(std::uint8_t* row, std::size_t width, std::size_t bytes_per_pixel) {
    std::array<std::uint8_t, kMaxChannels * 4> tmp;
    for (std::size_t l = 0, r = width ? width - 1 : 0; l < r; ++l, --r) {
        std::uint8_t* a = row + l * bytes_per_pixel;
        std::uint8_t* b = row + r * bytes_per_pixel;
        std::memcpy(tmp.data(), a, bytes_per_pixel);
        std::memcpy(a, b, bytes_per_pixel);
        std::memcpy(b, tmp.data(), bytes_per_pixel);
    }
}

} // namespace enim::tiff
