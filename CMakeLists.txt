cmake_minimum_required(VERSION 3.20)
project(enim LANGUAGES C CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(third_party/plog)
add_subdirectory(third_party/glad)

add_executable(enim
    src/main.cpp
    src/dispatcher.cpp
    src/window/window.cpp
    src/window/events.cpp
    src/window/setup.cpp
    src/window/gl_debug_helpers.cpp
    src/window/rgfw.cpp
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
elseif(APPLE)
    set(LIBS ${LIBS} "-framework OpenGL -framework Cocoa")
else()
    set(LIBS ${LIBS} X11 GL m Xrandr)
endif()


target_link_libraries(enim PRIVATE ${LIBS})