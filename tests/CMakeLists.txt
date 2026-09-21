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
        kdash_core   # WI #2244: registry.c calls kdash_apttemps_band()
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

# T003: layout.h unit arithmetic tests
add_executable(test_layout ${CMAKE_CURRENT_SOURCE_DIR}/test_layout.c)
target_include_directories(test_layout PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(test_layout PRIVATE -Wall -Wextra -Wno-unused-parameter)
add_test(NAME test_layout COMMAND test_layout)

# Sprint 006 / T002: stub test executables for upcoming foundational work.
# Full assertions land in T011 (layout_pool), T018 (icon_registry),
# T020 (service_card), T033 (graph_router).
add_executable(test_layout_pool
    ${CMAKE_CURRENT_SOURCE_DIR}/test_layout_pool.c
    ${CMAKE_SOURCE_DIR}/src/registry.c
)
target_include_directories(test_layout_pool PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_layout_pool kdash_core pthread)  # WI #2244
target_compile_definitions(test_layout_pool PRIVATE KPIDASH_TEST_STUBS=1)
target_compile_options(test_layout_pool PRIVATE -Wall -Wextra -Wno-unused-parameter)
add_test(NAME test_layout_pool COMMAND test_layout_pool)

add_executable(test_graph_router
    ${CMAKE_CURRENT_SOURCE_DIR}/test_graph_router.c
    ${CMAKE_SOURCE_DIR}/src/registry.c
)
target_include_directories(test_graph_router PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_graph_router kdash_core pthread)  # WI #2244
target_compile_definitions(test_graph_router PRIVATE KPIDASH_TEST_STUBS=1)
target_compile_options(test_graph_router PRIVATE -Wall -Wextra -Wno-unused-parameter)
add_test(NAME test_graph_router COMMAND test_graph_router)

add_executable(test_icon_registry
    ${CMAKE_CURRENT_SOURCE_DIR}/test_icon_registry.c
    ${CMAKE_SOURCE_DIR}/src/icons.c
)
target_include_directories(test_icon_registry PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(test_icon_registry PRIVATE -Wall -Wextra -Wno-unused-parameter)
add_test(NAME test_icon_registry COMMAND test_icon_registry)

# WI #2244: the apartment-temperature band boundaries, pinned on both sides so
# the repoint onto libkdash's classifier is provably a no-op in rendering.
add_kpidash_test(test_apttemps_band
    ${CMAKE_CURRENT_SOURCE_DIR}/test_apttemps_band.c
    ${CMAKE_SOURCE_DIR}/src/registry.c
)
target_compile_definitions(test_apttemps_band PRIVATE KPIDASH_TEST_STUBS=1)

add_kpidash_test(test_service_card
    ${CMAKE_CURRENT_SOURCE_DIR}/test_service_card.c
    ${CMAKE_SOURCE_DIR}/src/registry.c
    ${CMAKE_SOURCE_DIR}/src/redis.c
)
target_compile_definitions(test_service_card PRIVATE KPIDASH_TEST_STUBS=1)

# WI #1903: host staleness feed reader (kdash:stale:<host>:<deployer>).
add_kpidash_test(test_stale_card
    ${CMAKE_CURRENT_SOURCE_DIR}/test_stale_card.c
    ${CMAKE_SOURCE_DIR}/src/registry.c
    ${CMAKE_SOURCE_DIR}/src/redis.c
)
target_compile_definitions(test_stale_card PRIVATE KPIDASH_TEST_STUBS=1)

# WI #2524: host-card admission. registry.c only — the rule is expressed as
# registry_admit() precisely so it is reachable without a live Redis.
add_executable(test_client_registry
    ${CMAKE_CURRENT_SOURCE_DIR}/test_client_registry.c
    ${CMAKE_SOURCE_DIR}/src/registry.c
)
target_include_directories(test_client_registry PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_client_registry kdash_core pthread)  # WI #2244
target_compile_definitions(test_client_registry PRIVATE KPIDASH_TEST_STUBS=1)
target_compile_options(test_client_registry PRIVATE -Wall -Wextra -Wno-unused-parameter)
add_test(NAME test_client_registry COMMAND test_client_registry)

# WI #2646: per-codepoint dedupe of LVGL's missing-glyph warning. Pure string
# work with no LVGL dependency, which is the point of splitting it out of the
# print callback that uses it — the gate can hold the flood shut.
add_executable(test_logfilter
    ${CMAKE_CURRENT_SOURCE_DIR}/test_logfilter.c
    ${CMAKE_SOURCE_DIR}/src/logfilter.c
)
target_include_directories(test_logfilter PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(test_logfilter PRIVATE -Wall -Wextra -Wno-unused-parameter)
add_test(NAME test_logfilter COMMAND test_logfilter)

# Spec 005 / T006: widget leak regression suite.
# Requires LVGL, which is only built in the full (non-TESTS_ONLY) configuration.
if(TARGET lvgl)
    pkg_check_modules(PNG_TEST REQUIRED libpng)
    add_executable(test_widget_leak
        ${CMAKE_CURRENT_SOURCE_DIR}/test_widget_leak.c
        ${CMAKE_SOURCE_DIR}/src/memstat.c
    )
    target_include_directories(test_widget_leak PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${PNG_TEST_INCLUDE_DIRS}
    )
    target_link_libraries(test_widget_leak lvgl ${PNG_TEST_LIBRARIES} pthread)
    target_compile_options(test_widget_leak PRIVATE -Wall -Wextra -Wno-unused-parameter)
    add_test(NAME test_widget_leak COMMAND test_widget_leak)
endif()
