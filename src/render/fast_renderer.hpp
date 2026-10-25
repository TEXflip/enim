#pragma once

#include <cstdint>
#include <memory>

#include <glad/glad.h>

#include <formats.hpp>
#include <render/image_renderer.hpp>

namespace enim {

/// @brief Renders the whole image from a single texture, for images that fit in VRAM.
struct FastRenderer : public ImageRenderer {
    std::unique_ptr<enim::FileReader> reader;
    GLuint program = 0, texture = 0;
    std::uint32_t vao = 0;
    std::uint32_t vbo = 0;
    std::uint32_t ebo = 0;

    FastRenderer(std::unique_ptr<enim::FileReader> reader) : reader(std::move(reader)) {}
    ~FastRenderer() override;

    std::expected<void, enim::Error> init() override;
    void update(const ViewTransform& view) override {}
    void draw(const ViewTransform& view) override;
    Vec2f image_size() const override;

  private:
    std::expected<void, enim::Error> create_texture();
};

} // namespace enim
