#include "md5.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

namespace {

constexpr std::uint32_t kShifts[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

// floor(abs(sin(i + 1)) * 2^32)
constexpr std::uint32_t kConstants[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};

std::uint32_t load_le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 | static_cast<std::uint32_t>(p[2]) << 16 |
           static_cast<std::uint32_t>(p[3]) << 24;
}

} // namespace

namespace enim {

Md5::Md5() : state{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476} {}

void Md5::block(const std::uint8_t* data) {
    std::uint32_t m[16];
    for (int i = 0; i < 16; ++i)
        m[i] = load_le32(data + i * 4);

    std::uint32_t a = this->state[0], b = this->state[1], c = this->state[2], d = this->state[3];
    for (std::uint32_t i = 0; i < 64; ++i) {
        std::uint32_t f, g;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) % 16;
        }
        std::uint32_t rotated = std::rotl(a + f + kConstants[i] + m[g], static_cast<int>(kShifts[i]));
        a = d;
        d = c;
        c = b;
        b = b + rotated;
    }
    this->state[0] += a;
    this->state[1] += b;
    this->state[2] += c;
    this->state[3] += d;
}

void Md5::update(std::span<const std::uint8_t> data) {
    std::size_t used = this->length % 64;
    this->length += data.size();

    const std::uint8_t* p = data.data();
    std::size_t left = data.size();
    if (used > 0) {
        std::size_t take = std::min(left, 64 - used);
        std::memcpy(this->buffer.data() + used, p, take);
        p += take;
        left -= take;
        if (used + take < 64)
            return;
        this->block(this->buffer.data());
    }
    for (; left >= 64; p += 64, left -= 64)
        this->block(p);
    std::memcpy(this->buffer.data(), p, left);
}

std::array<std::uint8_t, 16> Md5::finish() {
    std::uint64_t bits = this->length * 8;
    std::uint8_t padding[64] = {0x80};
    std::size_t used = this->length % 64;
    this->update({padding, used < 56 ? 56 - used : 120 - used});
    std::uint8_t length_bytes[8];
    for (int i = 0; i < 8; ++i)
        length_bytes[i] = static_cast<std::uint8_t>(bits >> (8 * i));
    this->update({length_bytes, 8});

    std::array<std::uint8_t, 16> digest;
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 4; ++k)
            digest[i * 4 + k] = static_cast<std::uint8_t>(this->state[i] >> (8 * k));
    return digest;
}

std::string Md5::hex(const std::array<std::uint8_t, 16>& digest) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(32);
    for (std::uint8_t byte : digest) {
        out.push_back(digits[byte >> 4]);
        out.push_back(digits[byte & 0xF]);
    }
    return out;
}

} // namespace enim
