# libtiff's find_package(liblzma), answered with xz's in-tree static liblzma (its include directory is private).
if (NOT TARGET liblzma::liblzma)
    add_library(liblzma::liblzma INTERFACE IMPORTED)
    set_target_properties(liblzma::liblzma PROPERTIES
        INTERFACE_LINK_LIBRARIES liblzma
        INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/third_party/xz/src/liblzma/api
    )
endif()
set(LIBLZMA_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/third_party/xz/src/liblzma/api)
set(LIBLZMA_LIBRARIES liblzma::liblzma)
set(LIBLZMA_FOUND TRUE)
set(liblzma_FOUND TRUE)
