# Image tests: enim loads each image written by tests/images/generate.py with --exit-after-load and must exit cleanly,
# or with a load error for the broken/unsupported ones. They need a display and OpenGL 3.3 (CI: Mesa llvmpipe).
#   ctest --test-dir bin -L bmp            # one format; -L normal for one mode

set(ENIM_TEST_IMAGES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/images/out" CACHE PATH "Images written by tests/images/generate.py")
set(ENIM_TEST_CACHE_DIR "${CMAKE_CURRENT_BINARY_DIR}/pyramids") # normal mode pyramids, kept out of the user's cache

# the images generate.py writes by default (not the --large ones); keep in sync with it
set(bmp_images
    bmp_bgr24_bottomup_pillow.bmp
    bmp_bgr24_topdown.bmp
    bmp_bgrx32_bottomup.bmp
    bmp_rgba32_bitfields_topdown.bmp
    bmp_bgra32_bitfields_bottomup.bmp
    bmp_argb32_bitfields_bottomup.bmp
    bmp_argb32_bitfields_large.bmp
    bmp_bgr24_large.bmp
    bmp_bgra32_bitfields_large.bmp
)
set(compressed_images
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
)
# must report an error (and still exit normally)
set(error_images
    qoi_rgb_truncated.qoi
    jpeg_rgb_truncated.jpg
    jpeg_rgb_progressive_truncated.jpg
    jpeg_cmyk.jpg
)

# mode: label of the test, args: enim options separated by |
function(enim_image_test image mode args)
    if (image IN_LIST error_images)
        set(expect error)
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

foreach (image IN LISTS bmp_images compressed_images)
    enim_image_test(${image} default "")
    enim_image_test(${image} normal "--force-normal-mode|--cache-dir|${ENIM_TEST_CACHE_DIR}")
endforeach()
foreach (image IN LISTS bmp_images)
    enim_image_test(${image} gpu-convert "--force-fast-mode|--force-gpu-convert")
    enim_image_test(${image} stream-upload "--force-fast-mode|--force-stream-upload")
endforeach()
