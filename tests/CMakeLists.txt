# Image tests: enim loads each image written by tests/images/generate.py with --exit-after-load and must exit cleanly,
# or with ErrorLogged (exit code 1) for the broken/unsupported ones: the log says which error. They need a display and
# OpenGL 3.3 (CI: Mesa llvmpipe).
#   ctest --test-dir bin -L bmp            # one format; -L normal for one mode

set(ENIM_TEST_IMAGES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/images/out" CACHE PATH "Images written by tests/images/generate.py")
set(ENIM_TEST_CACHE_DIR "${CMAKE_CURRENT_BINARY_DIR}/pyramids") # normal mode pyramids, kept out of the user's cache

# the images generate.py writes by default (not the --large ones); keep in sync with it
# uncompressed, read straight from the mmap: also tested through GpuConvert and StreamUpload
set(raw_images
    bmp_bgr24_bottomup_pillow.bmp
    bmp_bgr24_topdown.bmp
    bmp_bgrx32_bottomup.bmp
    bmp_rgba32_bitfields_topdown.bmp
    bmp_bgra32_bitfields_bottomup.bmp
    bmp_argb32_bitfields_bottomup.bmp
    bmp_argb32_bitfields_large.bmp
    bmp_bgr24_large.bmp
    bmp_bgra32_bitfields_large.bmp
    tiff_rgb8.tif
    tiff_rgba8.tif
    tiff_gray16_be.tif
    tiff_rgb16_be.tif
    tiff_i8_gray.tif
    tiff_i16_gray.tif
    tiff_u32_gray.tif
    tiff_f16_rgb.tif
    tiff_f32_gray.tif
    tiff_f32_gray_range.tif
    tiff_multispectral6.tif
    tiff_rgb8_bottomup.tif
    tiff_bigtiff_rgb8.tif
    tiff_multipage.tif
)
# decoded on the CPU
set(decoded_images
    qoi_rgb_odd.qoi
    qoi_rgba.qoi
    qoi_rgb_large.qoi
    qoi_rgb_truncated.qoi
    jpeg_rgb_420_odd.jpg
    jpeg_rgb_444.jpg
    jpeg_gray.jpg
    jpeg_rgb_progressive.jpg
    jpeg_rgb_large.jpg
    jpeg_rgb_truncated.jpg
    jpeg_rgb_progressive_truncated.jpg
    jpeg_cmyk.jpg
    png_rgb_odd.png
    png_rgba.png
    png_gray.png
    png_gray_alpha.png
    png_gray1.png
    png_palette.png
    png_palette_trns.png
    png_gray_trns.png
    png_rgb_trns.png
    png_gray16.png
    png_rgb16.png
    png_rgba16.png
    png_rgba_interlaced.png
    png_rgb16_interlaced.png
    png_rgb_large.png
    png_rgb_truncated.png
    png_rgb_interlaced_truncated.png
    tiff_rgb8_mirrored.tif
    tiff_tiled_rgb8_odd.tif
    tiff_planar_rgb16.tif
    tiff_tiled_planar_rgb8.tif
    tiff_f64_gray.tif
    tiff_lzw_rgb8_pred.tif
    tiff_deflate_rgba16.tif
    tiff_deflate_f32_pred3.tif
    tiff_zstd_rgb8.tif
    tiff_lzma_gray8.tif
    tiff_webp_rgb8.tif
    tiff_lerc_f32.tif
    tiff_packbits_gray8.tif
    tiff_jpeg_rgb.tif
    tiff_jpeg_ycbcr.tif
    tiff_ccitt_g4.tif
    tiff_bilevel1.tif
    tiff_gray4.tif
    tiff_miniswhite_gray8.tif
    tiff_palette8.tif
    tiff_palette16bit_map.tif
    tiff_cmyk8.tif
    tiff_assoc_alpha.tif
    tiff_lab8.tif
    tiff_ycbcr.tif
    tiff_lzw_rgb8_large.tif
    tiff_tiled_jpeg_large.tif
    tiff_lzw_rgb8_truncated.tif
    tiff_complex.tif
)
# must report an error (and still exit normally)
set(error_images
    qoi_rgb_truncated.qoi
    jpeg_rgb_truncated.jpg
    jpeg_rgb_progressive_truncated.jpg
    jpeg_cmyk.jpg
    png_rgb_truncated.png
    png_rgb_interlaced_truncated.png
    tiff_lzw_rgb8_truncated.tif
    tiff_complex.tif
)

# mode: label of the test, args: enim options separated by |
function(enim_image_test image mode args)
    if (image IN_LIST error_images)
        set(expect ErrorLogged)
    else()
        set(expect ok)
    endif()
    string(REGEX MATCH "^[a-z]+" format "${image}")
    set(name "${mode}/${image}")
    add_test(NAME "${name}"
        COMMAND ${CMAKE_COMMAND} -DENIM=$<TARGET_FILE:enim> "-DIMAGE=${ENIM_TEST_IMAGES_DIR}/${image}"
                -DEXPECT=${expect} "-DARGS=${args}" -P "${CMAKE_CURRENT_SOURCE_DIR}/run_image.cmake")
    set_tests_properties("${name}" PROPERTIES LABELS "${format};${mode}" TIMEOUT 300)
endfunction()

foreach (image IN LISTS raw_images decoded_images)
    enim_image_test(${image} default "")
    enim_image_test(${image} normal "--force-normal-mode|--cache-dir|${ENIM_TEST_CACHE_DIR}")
endforeach()
foreach (image IN LISTS raw_images)
    enim_image_test(${image} gpu-convert "--force-fast-mode|--force-gpu-convert")
    enim_image_test(${image} stream-upload "--force-fast-mode|--force-stream-upload")
endforeach()

# Profile run (tests/profile.py): loads every image in every mode with --profile and prints the time of each load
# section, aggregated over all runs. Off by default: it is slow, and ctest's parallel tests would skew its timings.
#   cmake -B bin -DENIM_PROFILE_TEST=ON && ctest --test-dir bin -L profile -V
# or directly: uv run tests/profile.py [images or dirs...] [--modes ...] [--repeat N] [--csv out.csv]
option(ENIM_PROFILE_TEST "Register tests/profile.py as the ctest test 'profile'" OFF)
if (ENIM_PROFILE_TEST)
    find_program(UV_EXECUTABLE uv REQUIRED)
    add_test(NAME profile
        COMMAND "${UV_EXECUTABLE}" run "${CMAKE_CURRENT_SOURCE_DIR}/profile.py" --enim $<TARGET_FILE:enim>
                "${ENIM_TEST_IMAGES_DIR}")
    set_tests_properties(profile PROPERTIES LABELS profile RUN_SERIAL TRUE TIMEOUT 3600)
endif()
