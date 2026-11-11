# Runs enim on one image with --exit-after-load and checks its exit code.
#   cmake -DENIM=<exe> -DIMAGE=<file> -DEXPECT=ok|error [-DARGS=<arg>|<arg>...] -P run_image.cmake
# EXPECT=error passes only on exit code 1 (an error was logged): a crash, a window/GL failure (exit code 2) or a missing
# image don't count as the expected error.

if (NOT EXISTS "${IMAGE}")
    message(FATAL_ERROR "${IMAGE} doesn't exist: generate the test images with `uv run tests/images/generate.py`")
endif()

string(REPLACE "|" ";" args "${ARGS}")
execute_process(COMMAND "${ENIM}" --exit-after-load ${args} "${IMAGE}" RESULT_VARIABLE result)

if (EXPECT STREQUAL "ok" AND NOT result STREQUAL "0")
    message(FATAL_ERROR "expected a clean load, enim exited with: ${result}")
elseif (EXPECT STREQUAL "error" AND NOT result STREQUAL "1")
    message(FATAL_ERROR "expected a load error (exit code 1), enim exited with: ${result}")
endif()
