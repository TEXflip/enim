#include "raw_buffer.hpp"

namespace enim {

RawBuffer::~RawBuffer() {
    glDeleteTextures(1, &this->texture_id);
    glDeleteBuffers(1, &this->buffer);
}

void RawBuffer::allocate(std::size_t bytes) {
    // whole words, plus one so that unaligned reads of the last bytes stay inside the buffer
    this->capacity = (bytes + 3) / 4 * 4 + 4;

    glGenBuffers(1, &this->buffer);
    glBindBuffer(GL_TEXTURE_BUFFER, this->buffer);
    glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(this->capacity), nullptr, GL_STREAM_DRAW);

    glGenTextures(1, &this->texture_id);
    glBindTexture(GL_TEXTURE_BUFFER, this->texture_id);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R32UI, this->buffer);

    glBindTexture(GL_TEXTURE_BUFFER, 0);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
}

void RawBuffer::upload(std::size_t offset, std::span<const std::uint8_t> data) {
    glBindBuffer(GL_TEXTURE_BUFFER, this->buffer);
    if (offset == 0)
        glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(this->capacity), nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_TEXTURE_BUFFER, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(data.size()), data.data());
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
}

} // namespace enim
