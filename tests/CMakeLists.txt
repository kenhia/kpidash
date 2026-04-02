cmake_minimum_required(VERSION 3.22)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CJSON REQUIRED libcjson)

# hiredis::hiredis is already an IMPORTED target created by the parent
# CMakeLists.txt — no need to find it again here.

# Helper: build a test executable that links the given source files
function(add_kpidash_test target_name)
    add_executable(${target_name} ${ARGN})
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}
        ${CJSON_INCLUDE_DIRS}
    )
    target_link_libraries(${target_name}
        hiredis::hiredis
        ${CJSON_LIBRARIES}
        pthread
    )
    target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

# T010a: config.c unit tests
add_kpidash_test(test_config
    ${CMAKE_CURRENT_SOURCE_DIR}/test_config.c
    ${CMAKE_SOURCE_DIR}/src/config.c
)

# T010b: redis.c JSON parse unit tests
# Only compile the JSON parse helpers — we do NOT link against a live Redis.
# The test uses a thin stub for hiredis-dependent symbols.
add_kpidash_test(test_redis_json
    ${CMAKE_CURRENT_SOURCE_DIR}/test_redis_json.c
    ${CMAKE_SOURCE_DIR}/src/redis.c
    ${CMAKE_SOURCE_DIR}/src/registry.c
)
# Provide stub implementations of ui.h / status.h / fortune.h symbols
# so redis.c links without the full UI tree.
target_compile_definitions(test_redis_json PRIVATE KPIDASH_TEST_STUBS=1)
