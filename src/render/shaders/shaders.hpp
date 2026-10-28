#pragma once

static const char glsl_version[] = "#version 330 core\n";

static const char image_frag_shader[] = {
#embed "image.frag"
    , 0
};

static const char image_vert_shader[] = {
#embed "image.vert"
    , 0
};

static const char fullscreen_vert_shader[] = {
#embed "fullscreen.vert"
    , 0
};

// include files and pass bodies have no #version: they are compiled as glsl_version + includes + body

static const char raw_fetch_glsl[] = {
#embed "include/raw_fetch.glsl"
    , 0
};

static const char convert_glsl[] = {
#embed "include/convert.glsl"
    , 0
};

static const char convert_raw_frag_shader[] = {
#embed "convert_raw.frag"
    , 0
};
