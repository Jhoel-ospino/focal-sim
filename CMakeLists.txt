cmake_minimum_required(VERSION 3.16)
project(focal_cnc_simulator C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Optimization & warnings
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -Wall -Wno-unused-function -Wno-unused-variable")

# Homebrew SDL2 paths for Apple Silicon / Intel
list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")
find_package(SDL2 REQUIRED)

# Check for local LVGL or fetch via FetchContent
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../managed_components/lvgl__lvgl")
    set(LVGL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../managed_components/lvgl__lvgl")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lvgl")
    set(LVGL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lvgl")
else()
    include(FetchContent)
    FetchContent_Declare(
        lvgl
        GIT_REPOSITORY https://github.com/lvgl/lvgl.git
        GIT_TAG v8.3.11
    )
    FetchContent_MakeAvailable(lvgl)
    set(LVGL_DIR "${lvgl_SOURCE_DIR}")
endif()

set(UI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ui")

# Gather LVGL core sources
file(GLOB_RECURSE LVGL_SRC "${LVGL_DIR}/src/*.c")

# Simulator and Mock sources
set(SIM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/sim_main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mock_services/mock_wifi_manager.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mock_services/mock_settings_manager.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mock_services/mock_laser_driver.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mock_services/mock_gcode_parser.c
    ${UI_DIR}/ui_screens.c
    ${UI_DIR}/focal_icons.c
    ${UI_DIR}/gcode_preview.c
    ${UI_DIR}/liquid_wave_progress.c
    ${LVGL_SRC}
)

add_executable(focal_sim ${SIM_SOURCES})

target_compile_definitions(focal_sim PRIVATE
    LV_CONF_INCLUDE_SIMPLE=1
    LV_LVGL_H_INCLUDE_SIMPLE=1
    SIMULATOR=1
)

target_include_directories(focal_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}                # For lv_conf.h
    ${CMAKE_CURRENT_SOURCE_DIR}/mock_esp       # Mock esp_log.h, esp_err.h, FreeRTOS
    ${CMAKE_CURRENT_SOURCE_DIR}/mock_services  # Mock wifi_manager.h, settings_manager.h, etc.
    ${UI_DIR}/include                          # ui_screens.h, focal_theme.h, focal_icons.h
    ${LVGL_DIR}                                # lvgl.h
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(focal_sim PRIVATE
    ${SDL2_LIBRARIES}
    m
    pthread
)
