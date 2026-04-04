cmake_minimum_required(VERSION 3.22)
project(kpidash C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# LVGL expects lv_conf.h next to the lvgl folder or via include path
set(LV_CONF_PATH "${CMAKE_SOURCE_DIR}/lv_conf.h" CACHE STRING "Path to lv_conf.h")
add_compile_definitions(LV_CONF_PATH=${LV_CONF_PATH})

# TESTS_ONLY=ON: build only the unit test targets (no LVGL/DRM required).
# Use this for native (x86_64) CI; use the full build for cross-compiled Pi targets.
option(TESTS_ONLY "Build unit tests only — skip dashboard binary and LVGL/DRM deps" OFF)

find_package(PkgConfig REQUIRED)

if(NOT TESTS_ONLY)
    pkg_check_modules(DRM REQUIRED libdrm)
    pkg_check_modules(PNG REQUIRED libpng)
endif()

pkg_check_modules(CJSON REQUIRED libcjson)

# hiredis: prefer CMake config (Trixie libhiredis-dev 1.x);
# fall back to pkg-config for Ubuntu 22.04 dev hosts (0.14.x, no cmake config).
find_package(hiredis QUIET CONFIG)
if(NOT TARGET hiredis::hiredis)
    pkg_check_modules(HIREDIS REQUIRED hiredis)
    add_library(hiredis::hiredis INTERFACE IMPORTED GLOBAL)
    target_include_directories(hiredis::hiredis INTERFACE ${HIREDIS_INCLUDE_DIRS})
    target_link_libraries(hiredis::hiredis INTERFACE ${HIREDIS_LIBRARIES})
    if(HIREDIS_LIBRARY_DIRS)
        target_link_directories(hiredis::hiredis INTERFACE ${HIREDIS_LIBRARY_DIRS})
    endif()
    message(STATUS "hiredis: using pkg-config (${HIREDIS_VERSION})")
endif()

enable_testing()

if(TESTS_ONLY)
    add_subdirectory(tests)
    return()
endif()

# LVGL's DRM driver includes <drm.h> — make libdrm headers available
include_directories(${DRM_INCLUDE_DIRS} ${PNG_INCLUDE_DIRS})

add_subdirectory(lib/lvgl)

add_executable(kpidash
    src/main.c
    src/registry.c
    src/config.c
    src/redis.c
    src/status.c
    src/ui.c
    src/fortune.c
    src/widgets/client_card.c
    src/widgets/activities.c
    src/widgets/repo_status.c
    src/widgets/fortune.c
    src/widgets/status_bar.c
    src/widgets/dev_grid.c
    src/widgets/dev_textsize.c
)

target_include_directories(kpidash PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${DRM_INCLUDE_DIRS}
    ${CJSON_INCLUDE_DIRS}
)

target_link_libraries(kpidash
    lvgl
    hiredis::hiredis
    ${DRM_LIBRARIES}
    ${CJSON_LIBRARIES}
    ${PNG_LIBRARIES}
    pthread
)

target_compile_options(kpidash PRIVATE -Wall -Wextra -Wno-unused-parameter)

# Cross-linking against a Trixie sysroot (glibc 2.38) from an Ubuntu 22.04
# host (glibc 2.35): shared libs like libdrm reference GLIBC_2.38 symbols
# that the host linker's stub libc doesn't define. Allow these at link time —
# they will resolve correctly at runtime on the Pi.
target_link_options(kpidash PRIVATE -Wl,--allow-shlib-undefined)

add_subdirectory(tests)

