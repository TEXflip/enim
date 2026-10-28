// Channel selection, depth conversion and range: one source pixel -> one display texel.
// The input variant defines source_channel(c): the stored value of channel c of the current pixel.

uniform ivec4 channel_map;  // source channel per texel channel, -1 = constant
uniform vec4 constant_value;
uniform vec2 value_range;   // value -> [0, 1]

float source_channel(int c);

vec4 convert_pixel() {
    vec4 texel;
    for (int k = 0; k < 4; ++k) {
        int c = channel_map[k];
        texel[k] = c >= 0 ? (source_channel(c) - value_range.x) / (value_range.y - value_range.x) : constant_value[k];
    }
    return texel;
}
