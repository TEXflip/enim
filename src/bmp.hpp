#include "formats.hpp"
namespace enim {
struct BmpHeader : public enim::FileHeader {
    std::size_t get_size() const override {
        return sizeof(BmpHeader);
    }

    std::array<std::size_t, 4> get_shape() const override {
        return {1, 100, 100, 3}; // B, H, W, C
    }

};

} // namespace enim