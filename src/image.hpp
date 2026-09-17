#pragma once
#include <cstdint>

struct Header {
    
};

struct Image {
    uint32_t type_size; // Size of the data type in bytes
    size_t shape[4]; // B, H, W, C
    size_t strides[4];
};