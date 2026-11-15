#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <io/pixel_layout.hpp>

namespace enim::tiff {

enum class InFormat : std::uint8_t { UInt, Int, Float };

/// @brief What turns the decoded samples into the output channels.
enum class ColorOp : std::uint8_t {
    Copy,       // gray, RGB, ...: the color samples are copied (and rescaled)
    MinIsWhite, // gray, inverted
    Palette,    // one index sample → RGB from the colormap
    Cmyk,       // 4 inks → RGB (naive, no color management)
    XyzToRgb,   // LogLuv decoded as float CIE XYZ → linear sRGB
};

/// @brief How a decoded row (libtiff's output: host byte order, chunky or one buffer per plane, samples of any bit
/// depth packed MSB-first) becomes a row of the source's native output layout.
struct ConvertSpec {
    std::size_t width = 0;
    std::uint16_t bits = 8;   // per input sample
    InFormat format = InFormat::UInt;
    std::uint16_t samples = 1; // input samples per pixel
    bool planar = false;       // one row buffer per sample
    ColorOp op = ColorOp::Copy;
    std::uint8_t color_in = 1; // input color samples (1 gray/index, 3 RGB/XYZ, 4 CMYK); the others are extra samples
    int alpha = -1;            // input sample index of the alpha channel, -1 if none
    bool unpremultiply = false; // the alpha is associated: the colors are divided by it
    bool mirror = false;        // pixels are written right to left (orientations 2 and 3)

    SampleType out_sample = SampleType::U8;
    std::uint8_t out_channels = 1;
    std::array<float, 2> float_range{0.0f, 1.0f}; // value range of float output (MinIsWhite inverts within it)
    std::array<std::vector<std::uint16_t>, 3> colormap; // Palette: 16-bit red, green and blue

    std::uint8_t color_out() const { return this->op == ColorOp::Palette || this->op == ColorOp::Cmyk || this->op == ColorOp::XyzToRgb ? 3 : this->color_in; }
};

/// @brief Bytes of one decoded row of one plane (all samples if chunky).
std::size_t decoded_row_bytes(const ConvertSpec& spec);

/// @brief Converts one row. planes[s] is the row of sample s (planar) or planes[0] holds every sample (chunky).
/// Input rows must be readable for 8 bytes past their end (bit unpacking reads whole words).
void convert_row(const ConvertSpec& spec, const std::uint8_t* const* planes, std::uint8_t* out);

/// @brief RGBA8 rows from TIFFRGBAImage (premultiplied, packed ABGR words) → straight R, G, B, A bytes, in place.
/// unpremultiply: the image has an alpha channel. mirror: reverse the pixels.
void finish_rgba_row(std::uint8_t* row, std::size_t width, bool unpremultiply, bool mirror);

/// @brief Reverses the order of the pixels of a row in place.
void mirror_row(std::uint8_t* row, std::size_t width, std::size_t bytes_per_pixel);

} // namespace enim::tiff
