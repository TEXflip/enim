add_library(glad STATIC src/glad.c)
target_include_directories(glad PUBLIC include)

if (UNIX)
    target_link_libraries(glad PUBLIC ${CMAKE_DL_LIBS})
endif()

# glad is old C code; don't force C23 on it
set_target_properties(glad PROPERTIES C_STANDARD 99)