# libtiff's find_package(JPEG), answered with the libjpeg-turbo external project (the imported target `jpeg`).
if (NOT TARGET JPEG::JPEG)
    add_library(JPEG::JPEG INTERFACE IMPORTED)
    set_target_properties(JPEG::JPEG PROPERTIES INTERFACE_LINK_LIBRARIES jpeg)
endif()
get_target_property(JPEG_INCLUDE_DIRS jpeg INTERFACE_INCLUDE_DIRECTORIES)
set(JPEG_LIBRARIES JPEG::JPEG)
set(JPEG_FOUND TRUE)
