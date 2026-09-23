cmake_minimum_required(VERSION 3.20)
project(enim LANGUAGES C CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Shader compilation settings for sokol-shdc

set(SOKOL_SHDC_BIN "${CMAKE_CURRENT_SOURCE_DIR}/tools/sokol-tools-bin/bin")
set(SHADER_IN "${CMAKE_CURRENT_SOURCE_DIR}/src/vertextexture-sapp.glsl")
set(SHADER_OUT "${CMAKE_CURRENT_SOURCE_DIR}/src/vertextexture-sapp.glsl.h")

if (WIN32)
    set(SOKOL_SHDC "${SOKOL_SHDC_BIN}/win32/sokol-shdc.exe")
elseif(APPLE)
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(SOKOL_SHDC "${SOKOL_SHDC_BIN}/osx_arm64/sokol-shdc")
    else()
        set(SOKOL_SHDC "${SOKOL_SHDC_BIN}/osx/sokol-shdc")
    endif()
elseif(UNIX)
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(SOKOL_SHDC "${SOKOL_SHDC_BIN}/linux_arm64/sokol-shdc")
    else()
        set(SOKOL_SHDC "${SOKOL_SHDC_BIN}/linux/sokol-shdc")
    endif()
endif()

add_custom_command(
    OUTPUT "${SHADER_OUT}"
    COMMAND "${SOKOL_SHDC}"
            -i "${SHADER_IN}"
            -o "${SHADER_OUT}"
            -l hlsl5:glsl430
    DEPENDS "${SHADER_IN}"
    COMMENT "Generating shader header from vertextexture-sapp.glsl"
    VERBATIM
)

# end of sokol-shdc shader compilation settings

add_subdirectory(third_party/plog)

add_executable(enim
    src/main.cpp
    src/sokol_impl.c
)
    
target_compile_definitions(enim PRIVATE $<$<CONFIG:Debug>:DEBUG>) #ifdef DEBUG
    
target_include_directories(enim PRIVATE
    third_party/sokol
    third_party/mio
    third_party/vecmath
)

set(LINK_LIBRARIES plog::plog)

if (WIN32)
    set(COMMON_LIBS ${LINK_LIBRARIES} d3d11 dxgi)
endif()

target_link_libraries(enim PRIVATE ${COMMON_LIBS})