#pragma once

#include <cstddef>
#include <expected>

#include <errors.hpp>
#include <render/gl/framebuffer.hpp>
#include <render/gl/texture.hpp>

namespace enim {

/// @brief Fills a texture's mip chain progressively, a bounded amount of work per step, so the main loop stays responsive.
/// Each level is a GL_LINEAR blit of the previous one at half size (a 2x2 box filter), scissored to a band of rows.
/// Progress lives in Texture::mip_levels_ready: a builder can be dropped and a new one resumes from there.
/// The texture's levels must already be allocated (Texture::allocate_mip_levels), and init() should run while the texture
/// is still empty: making a texture that holds a large image a render target for the first time stalls the driver.
struct MipmapBuilder {
    /// @brief Creates the read and draw framebuffers.
    /// @return An error if the texture format can't be blitted (not color-renderable): use Texture::generate_mip_chain().
    std::expected<void, enim::Error> init(const Texture& texture);

    /// @brief Blits about texel_budget destination texels (at least one row).
    /// @return True when the whole chain is ready.
    bool step(Texture& texture, std::size_t texel_budget);

  private:
    Framebuffer read_fbo, draw_fbo;
    int next_row = 0; // in level texture.mip_levels_ready, the one being built; kept across pauses
};

} // namespace enim
