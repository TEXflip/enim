#include "display_params.hpp"

#include <limits>

namespace {

template <typename T>
Vec2f limits_range() {
    return {static_cast<float>(std::numeric_limits<T>::min()), static_cast<float>(std::numeric_limits<T>::max())};
}

} // namespace

namespace enim {

Vec2f default_range(SampleType sample) {
    switch (sample) {
    case SampleType::U8: return limits_range<std::uint8_t>();
    case SampleType::U16: return limits_range<std::uint16_t>();
    case SampleType::U32: return limits_range<std::uint32_t>();
    case SampleType::I8: return limits_range<std::int8_t>();
    case SampleType::I16: return limits_range<std::int16_t>();
    case SampleType::I32: return limits_range<std::int32_t>();
    case SampleType::F16:
    case SampleType::F32: return {0.0f, 1.0f};
    }
    return {0.0f, 1.0f};
}

DisplayParams default_display(const PixelLayout& layout) {
    auto find = [&](ChannelRole role) -> std::int8_t {
        for (std::uint8_t c = 0; c < layout.channels; ++c)
            if (layout.roles[c] == role)
                return static_cast<std::int8_t>(c);
        return -1;
    };

    DisplayParams display;
    display.range = default_range(layout.sample);

    std::int8_t r = find(ChannelRole::R), g = find(ChannelRole::G), b = find(ChannelRole::B);
    std::int8_t gray = find(ChannelRole::Gray);
    if (r >= 0 && g >= 0 && b >= 0)
        display.channel_map = {r, g, b, -1};
    else if (gray >= 0)
        display.channel_map = {gray, gray, gray, -1};
    else
        display.channel_map = {0, 0, 0, -1};

    display.channel_map[3] = find(ChannelRole::A);
    return display;
}

} // namespace enim
