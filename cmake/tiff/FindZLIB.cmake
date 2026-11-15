# libtiff's find_package(ZLIB), answered with zlib-ng (in-tree, zlib-compatible mode). See add_tiff() in CMakeLists.txt.
if (NOT TARGET ZLIB::ZLIB)
    # zlib-ng-static is itself an alias of zlib-ng when only the static library is built
    get_target_property(_zlib_target zlib-ng-static ALIASED_TARGET)
    if (NOT _zlib_target)
        set(_zlib_target zlib-ng-static)
    endif()
    add_library(ZLIB::ZLIB ALIAS ${_zlib_target})
    unset(_zlib_target)
endif()
get_target_property(ZLIB_INCLUDE_DIRS ZLIB::ZLIB INTERFACE_INCLUDE_DIRECTORIES)
set(ZLIB_LIBRARIES ZLIB::ZLIB)
set(ZLIB_FOUND TRUE)
