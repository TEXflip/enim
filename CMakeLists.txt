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

add_executable(enim
    src/main.cpp
    src/linalgebra.cpp
    src/io/detect.cpp
    src/io/formats/bmp/bmp_source.cpp
    src/plan/load_plan.cpp
    src/plan/loader.cpp
    src/view/display_params.cpp
    src/window/window.cpp
    src/window/events.cpp
    src/window/gl_debug_helpers.cpp
    src/window/rgfw.cpp
    src/render/gl/framebuffer.cpp
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


set(LIBS plog::plog glad)

if (WIN32)
    set(LIBS ${LIBS} opengl32 gdi32 shell32 user32 advapi32 comctl32)
    target_compile_definitions(enim PRIVATE ENIM_OS_WIN)
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