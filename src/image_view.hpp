#pragma once
#include "linear_algebra.hpp"

struct ImageView {
    virtual void update() = 0;
    virtual void set_projection(Mat m) = 0;
};

struct FastModeImageView : public ImageView {
    FastModeImageView() {
        
    };
    
    void update() override {
        // Fast mode update implementation
    }

    void set_projection(Mat m) override {
        // Fast mode set projection implementation
    }
};

struct NormalModeImageView : public ImageView {
    void update() override {
        // Normal mode update implementation
    }

    void set_projection(Mat m) override {
        // Normal mode set projection implementation
    }
};