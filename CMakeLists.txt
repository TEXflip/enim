cmake_minimum_required(VERSION 3.20)
project(enim LANGUAGES C CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_ok OUTPUT ipo_msg)
if(ipo_ok)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    message(STATUS "IPO supported")
endif()

add_subdirectory(third_party/plog)
add_subdirectory(third_party/glad)

# libjpeg-turbo can't be added with add_subdirectory(): it is built as a separate project and linked statically.
# Its x86 SIMD code is NASM assembly; without NASM it would silently fall back to the much slower C code, so the
# build fails instead (REQUIRE_SIMD). Arm uses NEON intrinsics and needs no assembler.
include(ExternalProject)

if (NOT EXISTS ${CMAKE_SOURCE_DIR}/third_party/libjpeg-turbo/CMakeLists.txt)
    message(FATAL_ERROR "third_party/libjpeg-turbo is empty: run git submodule update --init")
endif()

set(JPEG_ARGS
    -DCMAKE_BUILD_TYPE=Release # optimized in Debug builds of enim too
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
    -DCMAKE_INSTALL_LIBDIR=lib
    -DENABLE_SHARED=OFF
    -DENABLE_STATIC=ON
    -DWITH_TURBOJPEG=OFF # only the libjpeg API is used
    -DWITH_TOOLS=OFF     # also disables the tests
    -DWITH_ARITH_ENC=OFF
    -DREQUIRE_SIMD=ON
)
if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64|x86|i[3-6]86)$")
    # the Windows installers (and winget) don't add NASM to the PATH
    find_program(NASM_EXECUTABLE NAMES nasm yasm HINTS "$ENV{LOCALAPPDATA}/bin/NASM" "$ENV{ProgramFiles}/NASM")
    if (NOT NASM_EXECUTABLE)
        message(FATAL_ERROR "NASM not found: it is needed to build libjpeg-turbo's SIMD code. Install it "
            "(winget install NASM.NASM, apt install nasm, brew install nasm) or set NASM_EXECUTABLE.")
    endif()
    list(APPEND JPEG_ARGS -DCMAKE_ASM_NASM_COMPILER=${NASM_EXECUTABLE})
endif()
if (CMAKE_OSX_ARCHITECTURES)
    list(APPEND JPEG_ARGS -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES})
endif()

set(JPEG_PREFIX ${CMAKE_BINARY_DIR}/libjpeg-turbo)
if (MSVC)
    set(JPEG_LIB ${JPEG_PREFIX}/lib/jpeg-static.lib)
else()
    set(JPEG_LIB ${JPEG_PREFIX}/lib/libjpeg.a)
endif()

ExternalProject_Add(libjpeg-turbo-build
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/third_party/libjpeg-turbo
    PREFIX ${JPEG_PREFIX}
    INSTALL_DIR ${JPEG_PREFIX}
    CMAKE_ARGS ${JPEG_ARGS}
    BUILD_BYPRODUCTS ${JPEG_LIB} # needed by Ninja
)

file(MAKE_DIRECTORY ${JPEG_PREFIX}/include) # must exist at configure time
add_library(jpeg STATIC IMPORTED)
set_target_properties(jpeg PROPERTIES
    IMPORTED_LOCATION ${JPEG_LIB}
    INTERFACE_INCLUDE_DIRECTORIES ${JPEG_PREFIX}/include
)
add_dependencies(jpeg libjpeg-turbo-build)

# PNG: libspng (third_party/libspng/spng/spng.c, compiled here) over zlib-ng in zlib-compatible mode, both static.
# zlib-ng picks its SIMD code (SSE/AVX2/AVX-512, NEON) at runtime, so the binary still runs on any x86-64/arm64 CPU.
foreach (dir libspng zlib-ng)
    if (NOT EXISTS ${CMAKE_SOURCE_DIR}/third_party/${dir}/README.md)
        message(FATAL_ERROR "third_party/${dir} is empty: run git submodule update --init")
    endif()
endforeach()

# a function, so that these settings don't leak into enim (BUILD_TESTING is enim's too: tests/)
function(add_zlib_ng)
    set(ZLIB_COMPAT ON)        # zlib.h API, what libspng includes
    set(BUILD_SHARED_LIBS OFF) # only the static library
    set(BUILD_TESTING OFF)     # zlib-ng's tests would download GoogleTest
    set(WITH_GZFILEOP OFF)     # no gzFile API
    set(SKIP_INSTALL_ALL ON)
    add_subdirectory(third_party/zlib-ng EXCLUDE_FROM_ALL)
endfunction()
add_zlib_ng()

add_library(spng STATIC third_party/libspng/spng/spng.c)
target_include_directories(spng PUBLIC third_party/libspng/spng)
# SPNG_STATIC: no __declspec(dllimport) on Windows. SSE2 filter code on x86 (the default, SPNG_SSE=1), NEON on arm64.
target_compile_definitions(spng PUBLIC SPNG_STATIC)
target_link_libraries(spng PRIVATE zlib-ng-static)

# WebP: libwebp (decoder, and the demuxer for the first frame of animations), static. libtiff uses it too.
if (NOT EXISTS ${CMAKE_SOURCE_DIR}/third_party/libwebp/CMakeLists.txt)
    message(FATAL_ERROR "third_party/libwebp is empty: run git submodule update --init")
endif()
function(add_webp)
    set(BUILD_SHARED_LIBS OFF)
    foreach (tool ANIM_UTILS CWEBP DWEBP GIF2WEBP IMG2WEBP VWEBP WEBPINFO LIBWEBPMUX WEBPMUX EXTRAS)
        set(WEBP_BUILD_${tool} OFF)
    endforeach()
    add_subdirectory(third_party/libwebp EXCLUDE_FROM_ALL)
endfunction()
add_webp()

# TIFF: libtiff over the codec libraries, all static and built in-tree: zlib-ng (Deflate, PixarLog), libjpeg-turbo
# (JPEG 8/12-bit, old JPEG), zstd, liblzma (xz), libwebp and LERC. JBIG is left out: JBIG-KIT is GPL.
# libtiff looks for each codec with find_package(); the modules in cmake/tiff/ come first in CMAKE_MODULE_PATH and
# answer with the in-tree targets, so nothing is taken from the system and the build order follows the links.
foreach (dir libtiff zstd xz lerc)
    if (NOT EXISTS ${CMAKE_SOURCE_DIR}/third_party/${dir}/CMakeLists.txt AND NOT EXISTS ${CMAKE_SOURCE_DIR}/third_party/${dir}/build/cmake/CMakeLists.txt)
        message(FATAL_ERROR "third_party/${dir} is empty: run git submodule update --init")
    endif()
endforeach()

function(add_tiff)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW) # option() keeps the variables set here (libtiff and lerc predate 3.13)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_TESTING OFF)

    set(ZSTD_BUILD_SHARED OFF)
    set(ZSTD_BUILD_STATIC ON)
    set(ZSTD_BUILD_PROGRAMS OFF)
    set(ZSTD_BUILD_TESTS OFF)
    set(ZSTD_BUILD_CONTRIB OFF)
    set(ZSTD_LEGACY_SUPPORT OFF)      # pre-1.0 frames: never in TIFF files
    set(ZSTD_MULTITHREAD_SUPPORT OFF) # only used by the compressor
    add_subdirectory(third_party/zstd/build/cmake ${CMAKE_BINARY_DIR}/zstd EXCLUDE_FROM_ALL)

    set(XZ_NLS OFF)
    set(XZ_DOC OFF)
    foreach (tool XZ XZDEC LZMADEC LZMAINFO SCRIPTS SYMLINKS SYMLINKS_LZMA)
        set(XZ_TOOL_${tool} OFF)
    endforeach()
    add_subdirectory(third_party/xz EXCLUDE_FROM_ALL)

    add_subdirectory(third_party/lerc EXCLUDE_FROM_ALL)

    set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/tiff)
    set(jpeg-prefer-standard ON) # no find_package(libjpeg-turbo CONFIG): it would find a system copy
    # libjpeg-turbo 3 has the 8/12-bit dual API; libtiff's check links against jpeglib, which isn't built yet
    set(HAVE_JPEGTURBO_DUAL_MODE_8 1 CACHE INTERNAL "")
    set(HAVE_JPEGTURBO_DUAL_MODE_12 1 CACHE INTERNAL "")
    set(tiff-tools OFF)
    set(tiff-tests OFF)
    set(tiff-contrib OFF)
    set(tiff-docs OFF)
    set(tiff-deprecated OFF)
    set(tiff-install OFF)
    set(tiff-opengl OFF)
    set(tiff-cxx OFF)
    set(jbig OFF)
    set(libdeflate OFF)
    add_subdirectory(third_party/libtiff EXCLUDE_FROM_ALL)

    foreach (codec ZIP_SUPPORT PIXARLOG_SUPPORT JPEG_SUPPORT OJPEG_SUPPORT HAVE_JPEGTURBO_DUAL_MODE_8_12
                   ZSTD_SUPPORT LZMA_SUPPORT WEBP_SUPPORT LERC_SUPPORT)
        get_directory_property(enabled DIRECTORY third_party/libtiff DEFINITION ${codec})
        if (NOT enabled)
            message(FATAL_ERROR "libtiff: ${codec} is not enabled")
        endif()
    endforeach()
endfunction()
add_tiff()

# decoding is the hot path: the decoders are optimized in Debug builds of enim too, like libjpeg-turbo
if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    foreach (target zlib-ng spng tiff libzstd_static liblzma webp webpdemux sharpyuv Lerc)
        target_compile_options(${target} PRIVATE -O2)
    endforeach()
endif()

add_executable(enim
    src/main.cpp
    src/linalgebra.cpp
    src/core/cache_dir.cpp
    src/core/md5.cpp
    src/core/thread_pool.cpp
    src/core/wakeup.cpp
    src/io/detect.cpp
    src/io/formats/bmp/bmp_source.cpp
    src/io/formats/jpeg/jpeg_source.cpp
    src/io/formats/png/png_source.cpp
    src/io/formats/qoi/qoi_source.cpp
    src/io/formats/tiff/tiff_convert.cpp
    src/io/formats/tiff/tiff_source.cpp
    src/io/formats/webp/webp_source.cpp
    src/pipeline/pixel_converter.cpp
    src/pipeline/texture_upload_sink.cpp
    src/plan/load_plan.cpp
    src/plan/loader.cpp
    src/pyramid/pyramid_builder.cpp
    src/pyramid/pyramid_store.cpp
    src/pyramid/tile_selection.cpp
    src/pyramid/view_sampler.cpp
    src/view/display_params.cpp
    src/window/window.cpp
    src/window/events.cpp
    src/window/gl_debug_helpers.cpp
    src/window/rgfw.cpp
    src/render/gl/framebuffer.cpp
    src/render/gl/mipmap_builder.cpp
    src/render/gl/program.cpp
    src/render/gl/raw_buffer.cpp
    src/render/gl/texture.cpp
    src/render/gl/texture_format.cpp
    src/render/gpu_passes/convert_pass.cpp
    src/render/image_pass.cpp
    src/render/fast_renderer.cpp
    src/render/tiled_renderer.cpp
    src/settings/settings.cpp
)

# version.hpp (ENIM_VERSION) is regenerated from git at every build, in the build directory (never committed)
set(VERSION_HEADER ${CMAKE_BINARY_DIR}/generated/version.hpp)
add_custom_target(enim_version ALL
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUTPUT=${VERSION_HEADER}
        -P ${CMAKE_SOURCE_DIR}/cmake/version.cmake
    BYPRODUCTS ${VERSION_HEADER}
    COMMENT "Generating version.hpp"
)
add_dependencies(enim enim_version)

target_compile_definitions(enim PRIVATE
    RGFWDEF=
    $<$<CONFIG:Debug>:DEBUG>
) #ifdef DEBUG

target_include_directories(enim PRIVATE
    third_party/glad/include
    third_party/RGFW
    third_party/mio
    third_party/vecmath
    src
    ${CMAKE_BINARY_DIR}/generated
)


set(LIBS plog::plog glad jpeg spng tiff webpdemux webp)

if (WIN32)
    set(LIBS ${LIBS} opengl32 gdi32 shell32 user32 advapi32 comctl32)
    target_compile_definitions(enim PRIVATE ENIM_OS_WIN)
    # GUI program: no console window when opened from Explorer (main.cpp attaches to the terminal's console when
    # there is one). Debug builds keep their own console for the logs.
    if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        set_target_properties(enim PROPERTIES WIN32_EXECUTABLE ON)
    endif()
elseif(APPLE)
    set(LIBS ${LIBS} "-framework OpenGL -framework Cocoa")
    target_compile_definitions(enim PRIVATE ENIM_OS_MAC)
else()
    # sudo apt install libglx-dev libx11-dev libxrandr-dev libxi-dev libxinerama-dev libxcursor-dev libxext-dev libgl1-mesa-dev
    set(LIBS ${LIBS} X11 GL m Xrandr)
    target_compile_definitions(enim PRIVATE ENIM_OS_LINUX)
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    if(MINGW)
        target_link_options(enim PRIVATE -static)
    elseif(NOT APPLE)
        target_link_options(enim PRIVATE -static-libstdc++ -static-libgcc)
    endif()
    if (CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-fno-exceptions -fno-rtti)
    endif()
endif()


target_link_libraries(enim PRIVATE ${LIBS})

if (CMAKE_BUILD_TYPE STREQUAL "Release")
    add_custom_command(TARGET enim POST_BUILD
        COMMAND $<$<CONFIG:Release>:${CMAKE_STRIP}> $<$<CONFIG:Release>:$<TARGET_FILE:enim>>
        COMMENT "Stripping enim (Release)"
    )
endif()

# image tests (tests/CMakeLists.txt): ctest --test-dir bin, after uv run tests/images/generate.py
set_property(GLOBAL PROPERTY CTEST_TARGETS_ADDED 1) # no Experimental/Nightly/... dashboard targets
include(CTest)
if (BUILD_TESTING)
    add_subdirectory(tests)
endif()
