cmake_minimum_required(VERSION 3.20)
project(enim LANGUAGES C CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(enim
    src/main.cpp
    src/sokol_impl.c
)

target_include_directories(enim PRIVATE
    third_party/sokol
)

if (WIN32)
    target_link_libraries(enim PRIVATE d3d11 dxgi)
endif()