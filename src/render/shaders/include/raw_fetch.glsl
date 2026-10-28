// Reads raw source bytes from a buffer texture of R32UI words (bytes in little-endian order inside a word).

uniform usamplerBuffer raw;

// matches enim::SampleType
const int SAMPLE_U8 = 0;
const int SAMPLE_U16 = 1;
const int SAMPLE_U32 = 2;
const int SAMPLE_I8 = 3;
const int SAMPLE_I16 = 4;
const int SAMPLE_I32 = 5;
const int SAMPLE_F16 = 6;
const int SAMPLE_F32 = 7;

uint sample_bytes(int type) {
    if (type == SAMPLE_U8 || type == SAMPLE_I8)
        return 1u;
    if (type == SAMPLE_U16 || type == SAMPLE_I16 || type == SAMPLE_F16)
        return 2u;
    return 4u;
}

// n bytes (1..4) starting at byte address addr, which needs no alignment: reads one or two words and shifts.
uint fetch_bytes(uint addr, uint n) {
    uint word = addr >> 2u;
    uint shift = (addr & 3u) * 8u;
    uint value = texelFetch(raw, int(word)).r >> shift;
    if (shift + n * 8u > 32u)
        value |= texelFetch(raw, int(word + 1u)).r << (32u - shift);
    if (n < 4u)
        value &= (1u << (n * 8u)) - 1u;
    return value;
}

uint swap_bytes(uint v, uint n) {
    if (n == 2u)
        return ((v & 0xFFu) << 8u) | (v >> 8u);
    if (n == 4u)
        return (v >> 24u) | ((v >> 8u) & 0xFF00u) | ((v << 8u) & 0xFF0000u) | (v << 24u);
    return v;
}

// GLSL 3.30 has no unpackHalf2x16
float half_to_float(uint h) {
    uint exponent = (h >> 10u) & 0x1Fu;
    uint mantissa = h & 0x3FFu;
    float v;
    if (exponent == 0u)
        v = float(mantissa) * exp2(-24.0); // subnormal
    else if (exponent == 31u)
        v = uintBitsToFloat(mantissa == 0u ? 0x7F800000u : 0x7FC00000u); // inf, nan
    else
        v = (1.0 + float(mantissa) / 1024.0) * exp2(float(int(exponent) - 15));
    return (h & 0x8000u) != 0u ? -v : v;
}

// The stored value of one sample, not normalized.
float read_sample(uint addr, int type, bool big_endian) {
    uint n = sample_bytes(type);
    uint v = fetch_bytes(addr, n);
    if (big_endian)
        v = swap_bytes(v, n);

    switch (type) {
    case SAMPLE_I8: return float(int(v << 24u) >> 24);
    case SAMPLE_I16: return float(int(v << 16u) >> 16);
    case SAMPLE_I32: return float(int(v));
    case SAMPLE_F16: return half_to_float(v);
    case SAMPLE_F32: return uintBitsToFloat(v);
    default: return float(v); // U8, U16, U32 (above 2^24 loses precision)
    }
}
