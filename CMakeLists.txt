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
    find_program(NASM_EXECUTABLE NAMES nasm yasm)
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

add_executable(enim
    src/main.cpp
    src/linalgebra.cpp
    src/core/wakeup.cpp
    src/io/detect.cpp
    src/io/formats/bmp/bmp_source.cpp
    src/io/formats/qoi/qoi_source.cpp
    src/pipeline/pixel_converter.cpp
    src/pipeline/texture_upload_sink.cpp
    src/plan/load_plan.cpp
    src/plan/loader.cpp
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
    src/settings/settings.cpp
)
    
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
)


set(LIBS plog::plog glad jpeg)

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
    target_link_options(enim PRIVATE -static-libstdc++ -static-libgcc)
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