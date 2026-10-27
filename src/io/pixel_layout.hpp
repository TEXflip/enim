#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace enim {

enum class SampleType : std::uint8_t { U8, U16, U32, I8, I16, I32, F16, F32 };
enum class RowOrder : std::uint8_t { TopDown, BottomUp };
enum class ChannelRole : std::uint8_t { Gray, R, G, B, A, Unused, Other };

constexpr std::size_t kMaxChannels = 16;

constexpr std::size_t sample_size(SampleType t) {
    switch (t) {
    case SampleType::U8:
    case SampleType::I8: return 1;
    case SampleType::U16:
    case SampleType::I16:
    case SampleType::F16: return 2;
    case SampleType::U32:
    case SampleType::I32:
    case SampleType::F32: return 4;
    }
    return 0;
}

/// @brief Describes how pixels are stored, exactly as they are in the source.
struct PixelLayout {
    SampleType sample = SampleType::U8;
    std::uint8_t channels = 0;                   // as stored, may be > 4
    std::array<ChannelRole, kMaxChannels> roles{}; // header-declared
    std::size_t stride = 0;                      // bytes per row, including padding
    RowOrder rows = RowOrder::TopDown;
    bool big_endian = false;

    std::size_t bytes_per_pixel() const { return sample_size(sample) * channels; }
};

struct ImageInfo {
    std::size_t width = 0;
    std::size_t height = 0;
    PixelLayout layout;
};

} // namespace enim
