#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include <glad/glad.h>

namespace enim {

/// @brief A GL buffer holding raw source bytes, read by shaders through a buffer texture view (GL_R32UI, usamplerBuffer).
/// R32UI rather than R8UI: GL_MAX_TEXTURE_BUFFER_SIZE counts texels, so 4-byte texels give 4x the byte capacity. Move-only.
struct RawBuffer {
    GLuint buffer = 0;
    GLuint texture_id = 0;
    std::size_t capacity = 0; // bytes

    RawBuffer() = default;
    RawBuffer(const RawBuffer&) = delete;
    RawBuffer& operator=(const RawBuffer&) = delete;
    RawBuffer(RawBuffer&& other) noexcept { *this = std::move(other); }
    RawBuffer& operator=(RawBuffer&& other) noexcept {
        std::swap(this->buffer, other.buffer);
        std::swap(this->texture_id, other.texture_id);
        std::swap(this->capacity, other.capacity);
        return *this;
    }
    ~RawBuffer();

    /// @brief Creates the buffer and its texture view, with room for at least bytes.
    void allocate(std::size_t bytes);

    /// @brief Copies data at byte offset (glBufferSubData). An upload at offset 0 orphans the buffer first, so it never
    /// waits for draws still reading the previous contents.
    void upload(std::size_t offset, std::span<const std::uint8_t> data);

    GLuint texture() const { return this->texture_id; }
};

} // namespace enim
