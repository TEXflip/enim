# Runs enim on one image with --exit-after-load and checks its exit code.
#   cmake -DENIM=<exe> -DIMAGE=<file> -DEXPECT=ok|<ErrorCode name> [-DARGS=<arg>|<arg>...] -P run_image.cmake
# The exit code is an enim::ErrorCode (src/errors.hpp): EXPECT=ErrorLogged passes only if an error was logged during
# the load, a crash or a window/GL failure (its own code) doesn't count as the expected error.

# error_codes: "<code>;<name>;..." parsed from the enim::ErrorCode lines ("Name = value,") of src/errors.hpp
set(errors_hpp "${CMAKE_CURRENT_LIST_DIR}/../src/errors.hpp")
file(STRINGS "${errors_hpp}" enum_lines REGEX "^[ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]*=[ \t]*[0-9]+[ \t]*,")
set(error_codes "")
foreach (line IN LISTS enum_lines)
    string(REGEX MATCH "([A-Za-z_][A-Za-z0-9_]*)[ \t]*=[ \t]*([0-9]+)" _ "${line}")
    list(APPEND error_codes "${CMAKE_MATCH_2}" "${CMAKE_MATCH_1}")
endforeach()
if (NOT error_codes)
    message(FATAL_ERROR "no error codes found in ${errors_hpp}")
endif()

# "<code> (<name>)" for a known code, else the raw result (a crash, e.g. "Subprocess aborted" or 3 for std::abort on
# Windows)
function(describe_result result out)
    list(FIND error_codes "${result}" index)
    if (index GREATER_EQUAL 0 AND result MATCHES "^[0-9]+$")
        math(EXPR name_index "${index} + 1")
        list(GET error_codes ${name_index} name)
        set(${out} "${result} (${name})" PARENT_SCOPE)
    elseif (result STREQUAL "0")
        set(${out} "0 (success)" PARENT_SCOPE)
    else()
        set(${out} "${result}" PARENT_SCOPE)
    endif()
endfunction()

if (NOT EXISTS "${IMAGE}")
    message(FATAL_ERROR "${IMAGE} doesn't exist: generate the test images with `uv run tests/images/generate.py`")
endif()

if (EXPECT STREQUAL "ok")
    set(expected_code 0)
else()
    list(FIND error_codes "${EXPECT}" index)
    if (index LESS 1)
        message(FATAL_ERROR "unknown EXPECT=${EXPECT}: ok or an enim::ErrorCode name")
    endif()
    math(EXPR code_index "${index} - 1")
    list(GET error_codes ${code_index} expected_code)
endif()

string(REPLACE "|" ";" args "${ARGS}")
execute_process(COMMAND "${ENIM}" --exit-after-load ${args} "${IMAGE}" RESULT_VARIABLE result)

if (NOT result STREQUAL expected_code)
    describe_result("${result}" got)
    describe_result("${expected_code}" want)
    message(FATAL_ERROR "expected exit code ${want}, enim exited with: ${got}")
endif()
