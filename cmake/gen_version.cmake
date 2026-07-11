# Regenerates ${OUT} with the current git description + build date.
#
# Run at BUILD time (via a custom target) so the embedded version string can
# never go stale relative to the source that produced the binary. Only rewrites
# the header when the content actually changes, to avoid needless recompiles.
#
# Expected -D args: SRC_DIR (repo root), OUT (header path to write).

find_package(Git QUIET)

set(_ver "unknown")
if(GIT_EXECUTABLE)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --always --dirty --tags
        WORKING_DIRECTORY ${SRC_DIR}
        OUTPUT_VARIABLE _ver
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif()
if(NOT _ver)
    set(_ver "unknown")
endif()

string(TIMESTAMP _date "%Y-%m-%d" UTC)

set(_content "#pragma once\n/* Generated at build time by cmake/gen_version.cmake — do not edit. */\n#define KPIDASH_BUILD_VERSION \"${_ver} (${_date})\"\n")

set(_existing "")
if(EXISTS ${OUT})
    file(READ ${OUT} _existing)
endif()

if(NOT _existing STREQUAL _content)
    file(WRITE ${OUT} "${_content}")
endif()
