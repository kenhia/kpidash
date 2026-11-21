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
        m            # WI #4523: hosthist.c uses floor()
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
    ${CMAKE_SOURCE_DIR}/src/hosthist.c
    ${CMAKE_SOURCE_DIR}/src/registry.c
)
# Provide stub implementations of ui.h / status.h symbols
# so redis.c links without the full UI tree.
target_compile_definitions(test_redis_json PRIVATE KPIDASH_TEST_STUBS=1)

# T003: layout.h unit arithmetic tests
add_executable(test_layout ${CMAKE_CURRENT_SOURCE_DIR}/test_layout.c)
target_include_directories(test_layout PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(test_layout PRIVATE -Wall -Wextra -Wno-unused-parameter)
add_test(NAME test_layout COMMAND test_layout)

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

# WI #4848: the apt-temps widget's model -- three fixed zones in a fixed
# order, and the green-border signal for a fresh unknown sensor. Links
# registry.c for apttemps_color(), the band and freshness rule it reuses.
add_kpidash_test(test_atstate
    ${CMAKE_CURRENT_SOURCE_DIR}/test_atstate.c
    ${CMAKE_SOURCE_DIR}/src/atstate.c
    ${CMAKE_SOURCE_DIR}/src/registry.c
)
target_compile_definitions(test_atstate PRIVATE KPIDASH_TEST_STUBS=1)

add_kpidash_test(test_service_card
    ${CMAKE_CURRENT_SOURCE_DIR}/test_service_card.c
    ${CMAKE_SOURCE_DIR}/src/registry.c
    ${CMAKE_SOURCE_DIR}/src/redis.c
    ${CMAKE_SOURCE_DIR}/src/hosthist.c
)
target_compile_definitions(test_service_card PRIVATE KPIDASH_TEST_STUBS=1)

# WI #4847: the service card's width per span and its line fitting. Pure.
add_kpidash_test(test_svclayout
    ${CMAKE_CURRENT_SOURCE_DIR}/test_svclayout.c
    ${CMAKE_SOURCE_DIR}/src/svclayout.c
)

# WI #1903: host staleness feed reader (kdash:stale:<host>:<deployer>).
add_kpidash_test(test_stale_card
    ${CMAKE_CURRENT_SOURCE_DIR}/test_stale_card.c
    ${CMAKE_SOURCE_DIR}/src/registry.c
    ${CMAKE_SOURCE_DIR}/src/redis.c
    ${CMAKE_SOURCE_DIR}/src/hosthist.c
)
target_compile_definitions(test_stale_card PRIVATE KPIDASH_TEST_STUBS=1)

# WI #3642: the update-issue feed reader (kdash:update:<host>:<kind>).
add_kpidash_test(test_update_card
    ${CMAKE_CURRENT_SOURCE_DIR}/test_update_card.c
    ${CMAKE_SOURCE_DIR}/src/registry.c
    ${CMAKE_SOURCE_DIR}/src/redis.c
    ${CMAKE_SOURCE_DIR}/src/hosthist.c
)
target_compile_definitions(test_update_card PRIVATE KPIDASH_TEST_STUBS=1)

# WI #3012: the admitted-hosts file. Pure file I/O with no LVGL and no Redis,
# so the load side -- which must never fail, because it runs at startup on a
# panel with no keyboard -- is reachable from the gate.
add_executable(test_admitted
    ${CMAKE_CURRENT_SOURCE_DIR}/test_admitted.c
    ${CMAKE_SOURCE_DIR}/src/admitted.c
)
target_include_directories(test_admitted PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(test_admitted PRIVATE -Wall -Wextra -Wno-unused-parameter)
add_test(NAME test_admitted COMMAND test_admitted)

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
    # WI #4524: the host columns widget and everything it draws with.
    set(HOST_COLUMNS_SRCS
        ${CMAKE_SOURCE_DIR}/src/widgets/host_columns.c
        ${CMAKE_SOURCE_DIR}/src/hostcols.c
        ${CMAKE_SOURCE_DIR}/src/hosthist.c
        ${CMAKE_SOURCE_DIR}/src/registry.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_20.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_28.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_36.c
    )
    add_executable(test_widget_leak
        ${CMAKE_CURRENT_SOURCE_DIR}/test_widget_leak.c
        ${CMAKE_SOURCE_DIR}/src/memstat.c
        ${HOST_COLUMNS_SRCS}
    )
    target_include_directories(test_widget_leak PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/fonts
        ${PNG_TEST_INCLUDE_DIRS}
    )
    target_link_libraries(test_widget_leak lvgl kdash_core ${PNG_TEST_LIBRARIES} pthread m)
    target_compile_options(test_widget_leak PRIVATE -Wall -Wextra -Wno-unused-parameter)
    add_test(NAME test_widget_leak COMMAND test_widget_leak)

    # WI #4524/#4525: offline render of the widget to BMP, for review without
    # a Pi. A tool, not a test — see the file's header for usage.
    add_executable(render_host_columns
        ${CMAKE_CURRENT_SOURCE_DIR}/render_host_columns.c
        ${CMAKE_SOURCE_DIR}/src/bmp_write.c
        ${HOST_COLUMNS_SRCS}
    )
    target_include_directories(render_host_columns PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/fonts
    )
    target_link_libraries(render_host_columns lvgl kdash_core ${PNG_TEST_LIBRARIES} pthread m)
    target_compile_options(render_host_columns PRIVATE -Wall -Wextra -Wno-unused-parameter)

    # Sprint 033: offline render of the NAS card in its four states, for
    # review against sprints/planning/nas-card-mockups.html (vA).
    add_executable(render_nas_card
        ${CMAKE_CURRENT_SOURCE_DIR}/render_nas_card.c
        ${CMAKE_SOURCE_DIR}/src/widgets/nas_card.c
        ${CMAKE_SOURCE_DIR}/src/nasstate.c
        ${CMAKE_SOURCE_DIR}/src/registry.c
        ${CMAKE_SOURCE_DIR}/src/bmp_write.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_20.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_24.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_28.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_48.c
    )
    target_include_directories(render_nas_card PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/fonts
        ${CJSON_INCLUDE_DIRS}
    )
    target_link_libraries(render_nas_card lvgl kdash_core ${CJSON_LIBRARIES} ${PNG_TEST_LIBRARIES} pthread m)
    target_compile_options(render_nas_card PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_compile_definitions(render_nas_card PRIVATE
        NAS_LIVE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/nas-gratch-2026-11-12.json")

    # Sprint 034: offline render of the weather filler in its four states at
    # any width, for review against sprints/planning/weather-mockups.html (vC).
    add_executable(render_weather
        ${CMAKE_CURRENT_SOURCE_DIR}/render_weather.c
        ${CMAKE_SOURCE_DIR}/src/widgets/weather_card.c
        ${CMAKE_SOURCE_DIR}/src/wxstate.c
        ${CMAKE_SOURCE_DIR}/src/bmp_write.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_20.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_24.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_28.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_36.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_96_digits.c
    )
    target_include_directories(render_weather PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/fonts
        ${CJSON_INCLUDE_DIRS}
    )
    target_link_libraries(render_weather lvgl ${CJSON_LIBRARIES} ${PNG_TEST_LIBRARIES} pthread m)
    target_compile_options(render_weather PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_compile_definitions(render_weather PRIVATE
        LV_FONT_MONTSERRAT_BOLD_96_DIGITS=1
        KDASH_WEATHER_SCHEMA="${CMAKE_SOURCE_DIR}/lib/kdashdata/contracts/schemas/kdash-weather.schema.json"
        WX_LIVE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/weather-home-2026-11-12.json")

    # Sprint 037: offline render of the Claude limits card in the mock-up's
    # ok / warn / stale states, for review against
    # sprints/planning/row2-cards-mockups.html (`limits()`).
    add_executable(render_limits
        ${CMAKE_CURRENT_SOURCE_DIR}/render_limits.c
        ${CMAKE_SOURCE_DIR}/src/widgets/limits_card.c
        ${CMAKE_SOURCE_DIR}/src/limstate.c
        ${CMAKE_SOURCE_DIR}/src/bmp_write.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_20.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_24.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_28.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_36.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_96_digits.c
    )
    target_include_directories(render_limits PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/fonts
    )
    target_link_libraries(render_limits lvgl kdash_core ${PNG_TEST_LIBRARIES} pthread m)
    target_compile_options(render_limits PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_compile_definitions(render_limits PRIVATE LV_FONT_MONTSERRAT_BOLD_96_DIGITS=1)

    # Sprint 038: offline render of the korg + klams card in the mock-up's
    # ok / warn / stale states (and down, nofeed, live), for review against
    # sprints/planning/row2-cards-mockups.html (`combo()`).
    add_executable(render_korg_klams
        ${CMAKE_CURRENT_SOURCE_DIR}/render_korg_klams.c
        ${CMAKE_SOURCE_DIR}/src/widgets/korg_klams_card.c
        ${CMAKE_SOURCE_DIR}/src/kkstate.c
        ${CMAKE_SOURCE_DIR}/src/bmp_write.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_24.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_28.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_36.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_96_digits.c
    )
    target_include_directories(render_korg_klams PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/fonts
    )
    target_link_libraries(render_korg_klams lvgl kdash_core ${PNG_TEST_LIBRARIES} pthread m)
    target_compile_options(render_korg_klams PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_compile_definitions(render_korg_klams PRIVATE LV_FONT_MONTSERRAT_BOLD_96_DIGITS=1)

    # Sprint 040: offline render of the time and calendar card in the
    # mock-up's now / d27 / six states (and nofeed, stale, outdated, live),
    # for review against sprints/planning/calendar-mockups.html (`cardA`).
    add_executable(render_calendar
        ${CMAKE_CURRENT_SOURCE_DIR}/render_calendar.c
        ${CMAKE_SOURCE_DIR}/src/widgets/calendar_card.c
        ${CMAKE_SOURCE_DIR}/src/calstate.c
        ${CMAKE_SOURCE_DIR}/src/bmp_write.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_20.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_24.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_28.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_36.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_96_digits.c
    )
    target_include_directories(render_calendar PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/fonts
    )
    target_link_libraries(render_calendar lvgl kdash_core ${PNG_TEST_LIBRARIES} pthread m)
    target_compile_options(render_calendar PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_compile_definitions(render_calendar PRIVATE LV_FONT_MONTSERRAT_BOLD_96_DIGITS=1)
    # The one render that is also a test: names at the contract's 31-character
    # bound must be cut before the grid. render_calendar exits non-zero when a
    # pixel lands in the 24 px gap. Runs under check-warnings-full's ctest.
    add_test(NAME render_calendar_long_names
        COMMAND render_calendar ${CMAKE_CURRENT_BINARY_DIR}/render_calendar_long.bmp long)

    # Sprint 041: offline render of the Agents card in the mock-up's day and
    # the states it does not draw (empty, nofeed, stale, crowded, legs, long,
    # live). Sprint 042 added the v2 target day, for review against
    # sprints/planning/agents-mockups-v2.html (`focus(3)`), and a quiet one.
    add_executable(render_agents
        ${CMAKE_CURRENT_SOURCE_DIR}/render_agents.c
        ${CMAKE_SOURCE_DIR}/src/widgets/agents_card.c
        ${CMAKE_SOURCE_DIR}/src/agstate.c
        ${CMAKE_SOURCE_DIR}/src/bmp_write.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_20.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_24.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_28.c
    )
    target_include_directories(render_agents PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/fonts
    )
    target_link_libraries(render_agents lvgl kdash_core ${PNG_TEST_LIBRARIES} pthread m)
    target_compile_options(render_agents PRIVATE -Wall -Wextra -Wno-unused-parameter)
    # Renders that are also tests: the v2 target day; a quiet one (strip, no
    # detail); the crowded day (rows at their floor, the oldest left out); and
    # names at the 31-character bound with glyphs the face lacks, plus names
    # written left of their bar. Each fails if anything is drawn right of the
    # now line, in the detail rows or the strip's lanes.
    add_test(NAME render_agents_target
        COMMAND render_agents ${CMAKE_CURRENT_BINARY_DIR}/render_agents_target.bmp target)
    add_test(NAME render_agents_quiet
        COMMAND render_agents ${CMAKE_CURRENT_BINARY_DIR}/render_agents_quiet.bmp quiet)
    add_test(NAME render_agents_crowded
        COMMAND render_agents ${CMAKE_CURRENT_BINARY_DIR}/render_agents_crowded.bmp crowded)
    add_test(NAME render_agents_long_names
        COMMAND render_agents ${CMAKE_CURRENT_BINARY_DIR}/render_agents_long.bmp long)

    # Sprint 047 (WI #4848): offline render of the apt-temps widget in each
    # state it draws, for review. Also a test: the widest temperature, each
    # zone's name and the widest humidity must fit (each centred, WI #4870).
    add_executable(render_apt_temps
        ${CMAKE_CURRENT_SOURCE_DIR}/render_apt_temps.c
        ${CMAKE_SOURCE_DIR}/src/widgets/apt_temps_card.c
        ${CMAKE_SOURCE_DIR}/src/atstate.c
        ${CMAKE_SOURCE_DIR}/src/registry.c
        ${CMAKE_SOURCE_DIR}/src/bmp_write.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_28.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_48.c
    )
    target_include_directories(render_apt_temps PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/fonts
    )
    target_link_libraries(render_apt_temps lvgl kdash_core ${PNG_TEST_LIBRARIES} pthread m)
    target_compile_options(render_apt_temps PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_compile_definitions(render_apt_temps PRIVATE
        LV_FONT_MONTSERRAT_BOLD_28=1 LV_FONT_MONTSERRAT_BOLD_48=1)
    foreach(st normal bands stale missing unknown extremes)
        add_test(NAME render_apt_temps_${st}
            COMMAND render_apt_temps ${CMAKE_CURRENT_BINARY_DIR}/render_apt_temps_${st}.bmp ${st})
    endforeach()

    # Sprint 048 (WI #4847): offline render of the footer strip's service
    # cards with the contract's title, lines and span, for review. Also a
    # test: every drawn line sits wholly inside its card and is one line high.
    add_executable(render_service_card
        ${CMAKE_CURRENT_SOURCE_DIR}/render_service_card.c
        ${CMAKE_SOURCE_DIR}/src/widgets/service_card.c
        ${CMAKE_SOURCE_DIR}/src/svclayout.c
        ${CMAKE_SOURCE_DIR}/src/registry.c
        ${CMAKE_SOURCE_DIR}/src/icons.c
        ${CMAKE_SOURCE_DIR}/src/bmp_write.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_16.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_20.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_24.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_icons_56.c
    )
    target_include_directories(render_service_card PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/fonts
    )
    target_link_libraries(render_service_card lvgl kdash_core ${PNG_TEST_LIBRARIES} pthread m)
    target_compile_options(render_service_card PRIVATE -Wall -Wextra -Wno-unused-parameter)
    foreach(st argus fail plain long full)
        add_test(NAME render_service_card_${st}
            COMMAND render_service_card ${CMAKE_CURRENT_BINARY_DIR}/render_service_card_${st}.bmp ${st})
    endforeach()

    # Sprint 044: offline render of the lamp panel after N status cards, for
    # review against sprints/planning/altair-widths.png (and, since 045,
    # pdp8-widths.png). Also a test at the mock-ups' widths: nothing may be
    # drawn in the gap before the panel or past the strip's right edge.
    add_executable(render_lamp_panel
        ${CMAKE_CURRENT_SOURCE_DIR}/render_lamp_panel.c
        ${CMAKE_SOURCE_DIR}/src/widgets/lamp_panel.c
        ${CMAKE_SOURCE_DIR}/src/lampmach.c
        ${CMAKE_SOURCE_DIR}/src/altair.c
        ${CMAKE_SOURCE_DIR}/src/pdp8.c
        ${CMAKE_SOURCE_DIR}/src/bmp_write.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_20.c
        ${CMAKE_SOURCE_DIR}/fonts/lv_font_montserrat_bold_24.c
    )
    target_include_directories(render_lamp_panel PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/fonts
    )
    target_link_libraries(render_lamp_panel lvgl ${PNG_TEST_LIBRARIES} pthread m)
    target_compile_options(render_lamp_panel PRIVATE -Wall -Wextra -Wno-unused-parameter)
    foreach(ncards 8 12 14 15 16)
        add_test(NAME render_lamp_panel_${ncards}
            COMMAND render_lamp_panel ${CMAKE_CURRENT_BINARY_DIR}/render_lamp_panel_${ncards}.bmp ${ncards})
    endforeach()
    foreach(ncards 8 12 13 14 15 16)
        add_test(NAME render_lamp_panel_pdp8_${ncards}
            COMMAND render_lamp_panel ${CMAKE_CURRENT_BINARY_DIR}/render_lamp_panel_pdp8_${ncards}.bmp ${ncards} 10 pdp8)
    endforeach()
endif()

# WI #4062: the network card's model. Pure — no LVGL, no Redis — and held to
# the contract: it reads kdashdata's kdash-net.schema.json at run time and
# requires every example to parse and every counterexample to be distrusted.
add_executable(test_netstate
    ${CMAKE_CURRENT_SOURCE_DIR}/test_netstate.c
    ${CMAKE_SOURCE_DIR}/src/netstate.c
)
target_include_directories(test_netstate PRIVATE ${CMAKE_SOURCE_DIR}/src ${CJSON_INCLUDE_DIRS})
target_link_libraries(test_netstate ${CJSON_LIBRARIES})
target_compile_options(test_netstate PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_compile_definitions(test_netstate PRIVATE
    KDASH_NET_SCHEMA="${CMAKE_SOURCE_DIR}/lib/kdashdata/contracts/schemas/kdash-net.schema.json"
    KDASH_NET_ACK_SCHEMA="${CMAKE_SOURCE_DIR}/lib/kdashdata/contracts/schemas/kdash-net-ack.schema.json")
add_test(NAME test_netstate COMMAND test_netstate)

# WI #3956: the NAS big panel's model, fed the kdash-nas schema's own examples.
add_executable(test_nasstate
    ${CMAKE_CURRENT_SOURCE_DIR}/test_nasstate.c
    ${CMAKE_SOURCE_DIR}/src/nasstate.c
)
target_include_directories(test_nasstate PRIVATE ${CMAKE_SOURCE_DIR}/src ${CJSON_INCLUDE_DIRS})
target_link_libraries(test_nasstate ${CJSON_LIBRARIES})
target_compile_options(test_nasstate PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_compile_definitions(test_nasstate PRIVATE
    KDASH_NAS_SCHEMA="${CMAKE_SOURCE_DIR}/lib/kdashdata/contracts/schemas/kdash-nas.schema.json"
    NAS_LIVE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/nas-gratch-2026-11-12.json")
add_test(NAME test_nasstate COMMAND test_nasstate)

# WI #4523: the per-host history ring behind the host columns sparklines.
add_executable(test_hosthist
    ${CMAKE_CURRENT_SOURCE_DIR}/test_hosthist.c
    ${CMAKE_SOURCE_DIR}/src/hosthist.c
)
target_include_directories(test_hosthist PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_hosthist m pthread)
target_compile_options(test_hosthist PRIVATE -Wall -Wextra -Wno-unused-parameter)
add_test(NAME test_hosthist COMMAND test_hosthist)

# WI #4524/#4525: the host columns widget's pure half -- fill rule, order,
# attention. Links registry.c for registry_priority_index / client_presence.
add_kpidash_test(test_hostcols
    ${CMAKE_CURRENT_SOURCE_DIR}/test_hostcols.c
    ${CMAKE_SOURCE_DIR}/src/hostcols.c
    ${CMAKE_SOURCE_DIR}/src/registry.c
)
target_compile_definitions(test_hostcols PRIVATE KPIDASH_TEST_STUBS=1)

# WI #4576: the weather filler's model, held to the kdash-weather contract:
# every schema example must parse and every x-counterexample be distrusted.
# The live record kweather published on 2026-11-12 is the other fixture.
add_executable(test_wxstate
    ${CMAKE_CURRENT_SOURCE_DIR}/test_wxstate.c
    ${CMAKE_SOURCE_DIR}/src/wxstate.c
)
target_include_directories(test_wxstate PRIVATE ${CMAKE_SOURCE_DIR}/src ${CJSON_INCLUDE_DIRS})
target_link_libraries(test_wxstate ${CJSON_LIBRARIES} m)
target_compile_options(test_wxstate PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_compile_definitions(test_wxstate PRIVATE
    KDASH_WEATHER_SCHEMA="${CMAKE_SOURCE_DIR}/lib/kdashdata/contracts/schemas/kdash-weather.schema.json"
    WX_LIVE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/weather-home-2026-11-12.json")
add_test(NAME test_wxstate COMMAND test_wxstate)

# WI #4609: the Claude limits card's model. Parsing and per-gauge staleness are
# libkdash's (kdash_core); this holds the card's own rules: colour bands, the
# pace tick, the reset and age strings, and the anti-flicker carry. The live
# record kai published on 2026-11-14 is the fixture.
add_executable(test_limstate
    ${CMAKE_CURRENT_SOURCE_DIR}/test_limstate.c
    ${CMAKE_SOURCE_DIR}/src/limstate.c
)
target_include_directories(test_limstate PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_limstate kdash_core m)
target_compile_options(test_limstate PRIVATE -Wall -Wextra -Wno-unused-parameter)
add_test(NAME test_limstate COMMAND test_limstate)

# WI #4613: the korg + klams card's model. Parsing and cadence staleness are
# libkdash's (kdash_core); this holds the card's own rules — the debounce, the
# scanner verdict, the 30-day line's points, the strings — against both
# schemas' examples and counterexamples and the two records live on 2026-11-14.
add_executable(test_kkstate
    ${CMAKE_CURRENT_SOURCE_DIR}/test_kkstate.c
    ${CMAKE_SOURCE_DIR}/src/kkstate.c
)
target_include_directories(test_kkstate PRIVATE ${CMAKE_SOURCE_DIR}/src ${CJSON_INCLUDE_DIRS})
target_link_libraries(test_kkstate kdash_core ${CJSON_LIBRARIES} m)
target_compile_options(test_kkstate PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_compile_definitions(test_kkstate PRIVATE
    KDASH_KORG_SCHEMA="${CMAKE_SOURCE_DIR}/lib/kdashdata/contracts/schemas/kdash-korg-pulse.schema.json"
    KDASH_KLAMS_SCHEMA="${CMAKE_SOURCE_DIR}/lib/kdashdata/contracts/schemas/kdash-klams-status.schema.json"
    KORG_LIVE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/korg-pulse-2026-11-14.json"
    KLAMS_LIVE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/klams-status-2026-11-14.json")
add_test(NAME test_kkstate COMMAND test_kkstate)

# WI #4665: the time and calendar card's model. Parsing and the window are
# libkdash's (kdash_core); this holds the card's own rules — the month grid,
# the cell flags, the coming-up list, the old-ts hint — against the calendar
# schema's examples and counterexamples, a full-cap record, and an injected
# clock across midnight, a month end and both DST changes.
add_executable(test_calstate
    ${CMAKE_CURRENT_SOURCE_DIR}/test_calstate.c
    ${CMAKE_SOURCE_DIR}/src/calstate.c
)
target_include_directories(test_calstate PRIVATE ${CMAKE_SOURCE_DIR}/src ${CJSON_INCLUDE_DIRS})
target_link_libraries(test_calstate kdash_core ${CJSON_LIBRARIES} m)
target_compile_options(test_calstate PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_compile_definitions(test_calstate PRIVATE
    KDASH_CALENDAR_SCHEMA="${CMAKE_SOURCE_DIR}/lib/kdashdata/contracts/schemas/kdash-calendar.schema.json")
add_test(NAME test_calstate COMMAND test_calstate)

# WI #4695 (v2 #4733): the Agents card's model. Parsing and the bar-end rule
# are libkdash's (kdash_core); this holds the card's own rules — the 3-hour
# detail axis, where each name goes, the packing with legs hung under their
# overseer, the row-shrink and leave-out rule, the label fit, the grids, the
# 24-hour day strip — against the agents schema's examples and
# counterexamples and an injected clock.
add_executable(test_agstate
    ${CMAKE_CURRENT_SOURCE_DIR}/test_agstate.c
    ${CMAKE_SOURCE_DIR}/src/agstate.c
)
target_include_directories(test_agstate PRIVATE ${CMAKE_SOURCE_DIR}/src ${CJSON_INCLUDE_DIRS})
target_link_libraries(test_agstate kdash_core ${CJSON_LIBRARIES} m)
target_compile_options(test_agstate PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_compile_definitions(test_agstate PRIVATE
    KDASH_AGENTS_SCHEMA="${CMAKE_SOURCE_DIR}/lib/kdashdata/contracts/schemas/kdash-agents.schema.json")
add_test(NAME test_agstate COMMAND test_agstate)

# WI #4777: the Altair lamp panel's model -- the 8080 subset, the lamp
# accumulators, the simulated player and the layout. Pure, no LVGL; the
# expected layouts and palette come from the mock-up's own script.
add_executable(test_altair
    ${CMAKE_CURRENT_SOURCE_DIR}/test_altair.c
    ${CMAKE_SOURCE_DIR}/src/altair.c
)
target_include_directories(test_altair PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_altair m)
target_compile_options(test_altair PRIVATE -Wall -Wextra -Wno-unused-parameter)
add_test(NAME test_altair COMMAND test_altair)

# WI #4783: the PDP-8/I lamp panel's model -- the emulator, the two
# programs, the lamp sampling and the layout -- and the machine interface
# and rotation both machines sit behind. Pure, no LVGL; the expected layouts,
# lamp counts and palettes come from the mock-up's own script
# (tests/fixtures/pdp8/gen.js).
add_executable(test_pdp8
    ${CMAKE_CURRENT_SOURCE_DIR}/test_pdp8.c
    ${CMAKE_SOURCE_DIR}/src/pdp8.c
)
target_include_directories(test_pdp8 PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/pdp8)
target_link_libraries(test_pdp8 m)
target_compile_options(test_pdp8 PRIVATE -Wall -Wextra -Wno-unused-parameter)
add_test(NAME test_pdp8 COMMAND test_pdp8)

add_executable(test_lampmach
    ${CMAKE_CURRENT_SOURCE_DIR}/test_lampmach.c
    ${CMAKE_SOURCE_DIR}/src/lampmach.c
    ${CMAKE_SOURCE_DIR}/src/altair.c
    ${CMAKE_SOURCE_DIR}/src/pdp8.c
)
target_include_directories(test_lampmach PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/pdp8)
target_link_libraries(test_lampmach m)
target_compile_options(test_lampmach PRIVATE -Wall -Wextra -Wno-unused-parameter)
add_test(NAME test_lampmach COMMAND test_lampmach)
