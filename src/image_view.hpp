#pragma once

#include "formats.hpp"

namespace enim {
struct ImageView {
    virtual void update() = 0;
    // virtual void set_projection(Mat m) = 0;
    virtual void bind_buffer() = 0;
    virtual FileReader& reader() = 0;
};

struct FastModeImageView : public ImageView {
    std::unique_ptr<enim::FileReader> _reader;
    FastModeImageView(std::unique_ptr<enim::FileReader> reader) : _reader(std::move(reader)) {}
    
    void update() override {
        // Fast mode update implementation
    }

    // void set_projection(Mat m) override {
    //     // Fast mode set projection implementation
    // }
    
    void bind_buffer() override {
        // Fast mode bind buffer implementation
    }

    FileReader& reader() override { 
        return *this->_reader;
    }
};

struct NormalModeImageView : public ImageView {
    std::unique_ptr<enim::FileReader> _reader;
    NormalModeImageView(std::unique_ptr<enim::FileReader> reader) : _reader(std::move(reader)) {}

    void update() override {
        // Normal mode update implementation
    }

    // void set_projection(Mat m) override {
    //     // Normal mode set projection implementation
    // }
    
    void bind_buffer() override {
        // Normal mode bind buffer implementation
    }
    
    FileReader& reader() override { 
        return *this->_reader;
    }
};

} // namespace enim