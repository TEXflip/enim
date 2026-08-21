cmake_minimum_required(VERSION 3.20)
project(MySokolApp LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(enim
    src/main.c
    src/sokol_impl.c
)

target_include_directories(enim PRIVATE
    third_party/sokol
)

if (WIN32)
    target_link_libraries(enim PRIVATE d3d11 dxgi)
endif()