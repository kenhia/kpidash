cmake_minimum_required(VERSION 3.16)
project(kpidash C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# LVGL expects lv_conf.h next to the lvgl folder or via include path
set(LV_CONF_PATH "${CMAKE_SOURCE_DIR}/lv_conf.h" CACHE STRING "Path to lv_conf.h")
add_compile_definitions(LV_CONF_PATH=${LV_CONF_PATH})

find_package(PkgConfig REQUIRED)
pkg_check_modules(DRM REQUIRED libdrm)
pkg_check_modules(CJSON REQUIRED libcjson)
pkg_check_modules(PNG REQUIRED libpng)

# LVGL's DRM driver includes <drm.h> — make libdrm headers available
include_directories(${DRM_INCLUDE_DIRS} ${PNG_INCLUDE_DIRS})

add_subdirectory(lib/lvgl)

add_executable(kpidash
    src/main.c
    src/registry.c
    src/net.c
    src/ui.c
)

target_include_directories(kpidash PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${DRM_INCLUDE_DIRS}
    ${CJSON_INCLUDE_DIRS}
)

target_link_libraries(kpidash
    lvgl
    ${DRM_LIBRARIES}
    ${CJSON_LIBRARIES}
    ${PNG_LIBRARIES}
    pthread
)
