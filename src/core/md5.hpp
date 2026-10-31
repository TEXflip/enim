#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace enim {

/// @brief MD5 (RFC 1321). Used for cache keys, not for security.
struct Md5 {
    Md5();

    void update(std::span<const std::uint8_t> data);
    void update(std::string_view text) { this->update({reinterpret_cast<const std::uint8_t*>(text.data()), text.size()}); }

    /// @brief Ends the hash. The object must not be updated afterwards.
    std::array<std::uint8_t, 16> finish();

    /// @brief Lowercase hexadecimal form of a digest.
    static std::string hex(const std::array<std::uint8_t, 16>& digest);

  private:
    std::array<std::uint32_t, 4> state;
    std::array<std::uint8_t, 64> buffer{};
    std::uint64_t length = 0; // bytes hashed so far

    void block(const std::uint8_t* data);
};

} // namespace enim
