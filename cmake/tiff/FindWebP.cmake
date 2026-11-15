# libtiff's find_package(WebP), answered with libwebp's in-tree static library.
if (NOT TARGET WebP::webp)
    add_library(WebP::webp ALIAS webp)
endif()
set(WebP_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/third_party/libwebp/src)
set(WebP_LIBRARIES WebP::webp)
set(WebP_FOUND TRUE)
