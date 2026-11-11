// Convert pass, raw input: the buffer texture holds a band of stored rows, starting at storage row band_first_row.
// Compiled after include/raw_fetch.glsl and include/convert.glsl.

uniform int sample_type;
uniform bool big_endian;
uniform uint stride;       // bytes per stored row
uniform uint pixel_bytes;  // bytes per stored pixel
uniform bool bottom_up;
uniform int image_height;
uniform int band_first_row;

out vec4 FragColor;

uint pixel_addr = 0u;

float source_channel(int c) {
    return read_sample(pixel_addr + uint(c) * sample_bytes(sample_type), sample_type, big_endian);
}

void main(void) {
    ivec2 p = ivec2(gl_FragCoord.xy); // target texel, row 0 = image top
    int row = bottom_up ? image_height - 1 - p.y : p.y;
    pixel_addr = uint(row - band_first_row) * stride + uint(p.x) * pixel_bytes;
    FragColor = convert_pixel();
}
