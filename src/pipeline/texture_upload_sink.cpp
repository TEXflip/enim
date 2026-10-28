#include "texture_upload_sink.hpp"

namespace enim {

void TextureUploadSink::on_rows(std::size_t y0, std::size_t count, const std::uint8_t* data, std::size_t stride) {
    const std::size_t row_bytes = this->converter.dst_row_bytes();

    UploadBand band;
    band.rows = count;
    band.pixels.resize(count * row_bytes);

    if (this->info.layout.rows == RowOrder::BottomUp) {
        // storage rows y0..y0+count-1 are texture rows height-1-y0 down to height-y0-count: write them reversed
        band.first_row = this->info.height - y0 - count;
        std::uint8_t* last_row = band.pixels.data() + (count - 1) * row_bytes;
        this->converter.convert_rows(data, stride, count, last_row, -static_cast<std::ptrdiff_t>(row_bytes));
    } else {
        band.first_row = y0;
        this->converter.convert_rows(data, stride, count, band.pixels.data(), static_cast<std::ptrdiff_t>(row_bytes));
    }

    if (this->queue.push(std::move(band), this->stop))
        this->wakeup();
}

void TextureUploadSink::on_done(std::expected<void, enim::Error> result) {
    this->queue.finish(std::move(result));
    this->wakeup();
}

} // namespace enim
