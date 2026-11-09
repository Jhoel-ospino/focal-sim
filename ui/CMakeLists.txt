idf_component_register(
    SRCS "ui_screens.c" "focal_icons.c" "gcode_preview.c" "liquid_wave_progress.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl esp_timer log drivers motion comms
)
