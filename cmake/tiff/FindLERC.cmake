# libtiff's find_package(LERC), answered with Esri's in-tree static Lerc library (C++, C API).
set(LERC_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/third_party/lerc/src/LercLib/include)
if (NOT TARGET LERC::LERC)
    add_library(LERC::LERC INTERFACE IMPORTED)
    set_target_properties(LERC::LERC PROPERTIES
        INTERFACE_LINK_LIBRARIES Lerc
        INTERFACE_INCLUDE_DIRECTORIES ${LERC_INCLUDE_DIRS} # libtiff's tif_config.h defines LERC_STATIC
    )
endif()
file(STRINGS "${LERC_INCLUDE_DIRS}/Lerc_c_api.h" _lerc_h REGEX "^#define[\t ]+LERC_VERSION_[A-Z]+[\t ]+[0-9]+")
string(REGEX REPLACE "^.*LERC_VERSION_MAJOR[\t ]+([0-9]+).*$" "\\1" LERC_VERSION_MAJOR "${_lerc_h}")
string(REGEX REPLACE "^.*LERC_VERSION_MINOR[\t ]+([0-9]+).*$" "\\1" LERC_VERSION_MINOR "${_lerc_h}")
string(REGEX REPLACE "^.*LERC_VERSION_PATCH[\t ]+([0-9]+).*$" "\\1" LERC_VERSION_PATCH "${_lerc_h}")
set(LERC_VERSION_STRING "${LERC_VERSION_MAJOR}.${LERC_VERSION_MINOR}.${LERC_VERSION_PATCH}")
unset(_lerc_h)
set(LERC_LIBRARIES LERC::LERC)
set(LERC_FOUND TRUE)
