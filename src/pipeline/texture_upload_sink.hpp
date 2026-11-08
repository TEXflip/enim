#pragma once

#include <functional>
#include <stop_token>

#include <io/pixel_layout.hpp>
#include <io/row_sink.hpp>
#include <pipeline/pixel_converter.hpp>
#include <pipeline/upload_queue.hpp>

namespace enim {

/// @brief Fast mode StreamUpload: converts decoded rows into display texture rows (on the decode thread) and queues
/// them for the main thread, which uploads them with glTexSubImage2D.
struct TextureUploadSink : public RowSink {
    TextureUploadSink(const ImageInfo& info, const PixelConverter& converter, BandQueue& queue, std::function<void()> wakeup,
                      std::stop_token stop)
        : info(info), converter(converter), queue(queue), wakeup(std::move(wakeup)), stop(std::move(stop)) {}

    void on_rows(std::size_t y0, std::size_t count, const std::uint8_t* data, std::size_t stride) override;
    void on_done(std::expected<void, enim::Error> result) override;

  private:
    ImageInfo info;
    const PixelConverter& converter;
    BandQueue& queue;
    std::function<void()> wakeup; // wakes the main thread when a band is queued
    std::stop_token stop;
};

} // namespace enim
