#include "load_plan.hpp"

#include <algorithm>

namespace enim {

const char* to_string(Mode mode) { return mode == Mode::Fast ? "fast" : "normal"; }

const char* to_string(FastStrategy strategy) {
    switch (strategy) {
    case FastStrategy::DirectUpload: return "DirectUpload";
    case FastStrategy::GpuConvert: return "GpuConvert";
    case FastStrategy::GpuDecode: return "GpuDecode";
    case FastStrategy::StreamUpload: return "StreamUpload";
    }
    return "?";
}

std::size_t gpu_convert_band_bytes(const GpuLimits& gpu) {
    return std::min(kGpuConvertStagingBytes, gpu.max_texture_buffer_size * 4); // R32UI texels
}

LoadPlan make_plan(const ImageInfo& info, const FormatCaps& caps, const GpuLimits& gpu, const Settings& settings) {
    LoadPlan plan;
    plan.display = default_display(info.layout);

    TextureFormat tex = choose_texture_format(info.layout, plan.display);

    if (caps.raw_mappable)
        plan.fast = direct_upload_params(info.layout, info.width, tex, plan.display) && !settings.force_gpu_convert
                        ? FastStrategy::DirectUpload
                        : FastStrategy::GpuConvert;
    else if (caps.gpu_decodable)
        plan.fast = FastStrategy::GpuDecode;
    else
        plan.fast = FastStrategy::StreamUpload;

    // FBO targets can't be RGB
    if (plan.fast == FastStrategy::GpuConvert || plan.fast == FastStrategy::GpuDecode)
        tex = renderable(tex);

    std::size_t tex_bytes = info.width * info.height * tex.bytes_per_pixel() * 4 / 3; // + mip chain
    std::size_t transient_bytes = 0;
    plan.band_bytes = gpu_convert_band_bytes(gpu);
    if (plan.fast == FastStrategy::GpuConvert)
        transient_bytes = std::min(plan.band_bytes, info.layout.stride * info.height);
    else if (plan.fast == FastStrategy::GpuDecode)
        transient_bytes = 2 * info.width * info.height * tex.bytes_per_pixel(); // encoded + decoded, upper bound
    plan.fast_bytes = tex_bytes + transient_bytes;

    bool fast = info.width <= gpu.max_texture_size
             && info.height <= gpu.max_texture_size
             && plan.fast_bytes <= settings.fast_mode_max_bytes;
    if (settings.force_fast_mode) fast = true;
    if (settings.force_normal_mode) fast = false;

    plan.mode = fast ? Mode::Fast : Mode::Normal;
    plan.level0 = caps.raw_mappable ? Level0Source::SourceMmap : Level0Source::DecodeToDisk;
    // normal mode frames are produced by the CPU converter, so they can be RGB
    plan.texture = fast ? tex : choose_texture_format(info.layout, plan.display);

    return plan;
}

} // namespace enim
