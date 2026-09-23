#pragma once
#include "formats.hpp"
#include "linear_algebra.hpp"

namespace enim {
struct ImageView {
    virtual void update() = 0;
    virtual void set_projection(Mat m) = 0;
};

struct FastModeImageView : public ImageView {
    enim::FileReader& reader;
    FastModeImageView(enim::FileReader& reader) : reader(reader) {}
    
    void update() override {
        // Fast mode update implementation
    }
    
    void set_projection(Mat m) override {
        // Fast mode set projection implementation
    }
};

struct NormalModeImageView : public ImageView {
    enim::FileReader& reader;
    NormalModeImageView(enim::FileReader& reader) : reader(reader) {}

    void update() override {
        // Normal mode update implementation
    }

    void set_projection(Mat m) override {
        // Normal mode set projection implementation
    }
};

} // namespace enim