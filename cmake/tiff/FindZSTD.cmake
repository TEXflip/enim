# libtiff's find_package(ZSTD), answered with zstd's in-tree static library.
if (NOT TARGET ZSTD::ZSTD)
    add_library(ZSTD::ZSTD ALIAS libzstd_static)
endif()
set(ZSTD_LIBRARIES ZSTD::ZSTD)
set(ZSTD_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/third_party/zstd/lib)
set(ZSTD_HAVE_DECOMPRESS_STREAM ON) # zstd >= 1.0, no need for libtiff's link check
set(ZSTD_FOUND TRUE)
