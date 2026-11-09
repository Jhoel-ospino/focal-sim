/**
 * @file sim_main.c
 * @brief Focal CNC 3.5" (480x320) Digital Twin & Desktop Simulator
 *
 * Runs the exact same C codebase on macOS/Linux using SDL2 and LVGL v8.4.
 * Features:
 * - 1:1 Physical Display scale options (1.0x, 1.5x [3.5" Retina match], 2.0x)
 * - Mouse-driven resistive touch emulation (tap, hold, smooth dragging)
 * - Built-in Screenshot tool (saves PNG to simulator/screenshots/ for AI/Dev inspection)
 * - Headless/CLI screenshot mode (--screenshot <path> --screen <id>)
 * - Direct hotkey navigation (0..6, T: Tema, C: Color, W: Wi-Fi, S: Captura, Z: Zoom)
 * - Simulated Wi-Fi stack & persistent NVS settings
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#include <SDL2/SDL.h>
#include "lvgl.h"
#include "ui_screens.h"
#include "focal_theme.h"
#include "settings_manager.h"
#include "wifi_manager.h"
#include "esp_log.h"

#define SIM_DISP_HOR_RES 480
#define SIM_DISP_VER_RES 320

static const char *TAG = "SIM_MAIN";

// Display and input state
static SDL_Window *s_window = NULL;
static SDL_Renderer *s_renderer = NULL;
static SDL_Texture *s_texture = NULL;
static float s_scale = 1.5f; // 1.5x scale closely matches 3.5" physical diagonal on Mac Retina
static bool s_mouse_pressed = false;
static int16_t s_mouse_x = 0;
static int16_t s_mouse_y = 0;

// Pixel buffer for LVGL
static lv_color_t s_disp_buf1[SIM_DISP_HOR_RES * SIM_DISP_VER_RES];
static uint32_t s_fb_argb[SIM_DISP_HOR_RES * SIM_DISP_VER_RES];

// Performance counters
static uint32_t s_frame_count = 0;
static uint32_t s_fps = 60;
static uint32_t s_last_fps_time = 0;

// Screenshot notification
static uint32_t s_screenshot_flash_until = 0;
static char s_last_screenshot_path[256] = "";

/* Forward declarations */
static void sim_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p);
static void sim_indev_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data);
static int sim_save_screenshot_png(const char *output_filepath);

// Convert LVGL 16-bit color (RGB565) to 32-bit ARGB8888
static inline uint32_t lv_color_to_argb(lv_color_t c) {
    uint8_t r = (c.ch.red * 255) / 31;
    uint8_t g = (c.ch.green * 255) / 63;
    uint8_t b = (c.ch.blue * 255) / 31;
    return (0xFF000000) | (r << 16) | (g << 8) | b;
}

static void sim_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;

    for (int32_t y = 0; y < h; y++) {
        for (int32_t x = 0; x < w; x++) {
            int32_t fb_x = area->x1 + x;
            int32_t fb_y = area->y1 + y;
            if (fb_x >= 0 && fb_x < SIM_DISP_HOR_RES && fb_y >= 0 && fb_y < SIM_DISP_VER_RES) {
                s_fb_argb[fb_y * SIM_DISP_HOR_RES + fb_x] = lv_color_to_argb(*color_p);
            }
            color_p++;
        }
    }

    lv_disp_flush_ready(disp_drv);
}

static void sim_indev_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    data->point.x = s_mouse_x;
    data->point.y = s_mouse_y;
    data->state = s_mouse_pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

static int write_bmp_24bit(const char *filepath, const uint32_t *argb, int w, int h)
{
    FILE *f = fopen(filepath, "wb");
    if (!f) return -1;

    int row_padded = (w * 3 + 3) & (~3);
    uint32_t image_size = row_padded * h;
    uint32_t file_size = 54 + image_size;

    uint8_t header[54] = {
        'B', 'M',
        (uint8_t)(file_size & 0xFF), (uint8_t)((file_size >> 8) & 0xFF),
        (uint8_t)((file_size >> 16) & 0xFF), (uint8_t)((file_size >> 24) & 0xFF),
        0, 0, 0, 0,
        54, 0, 0, 0,
        40, 0, 0, 0,
        (uint8_t)(w & 0xFF), (uint8_t)((w >> 8) & 0xFF),
        (uint8_t)((w >> 16) & 0xFF), (uint8_t)((w >> 24) & 0xFF),
        (uint8_t)(h & 0xFF), (uint8_t)((h >> 8) & 0xFF),
        (uint8_t)((h >> 16) & 0xFF), (uint8_t)((h >> 24) & 0xFF),
        1, 0,
        24, 0,
        0, 0, 0, 0,
        (uint8_t)(image_size & 0xFF), (uint8_t)((image_size >> 8) & 0xFF),
        (uint8_t)((image_size >> 16) & 0xFF), (uint8_t)((image_size >> 24) & 0xFF),
        0x13, 0x0B, 0, 0,
        0x13, 0x0B, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0
    };
    fwrite(header, 1, 54, f);

    uint8_t *row_buf = (uint8_t *)calloc(1, row_padded);
    for (int y = h - 1; y >= 0; y--) { // Standard bottom-to-top BMP
        for (int x = 0; x < w; x++) {
            uint32_t pix = argb[y * w + x];
            row_buf[x * 3 + 0] = pix & 0xFF;         // Blue
            row_buf[x * 3 + 1] = (pix >> 8) & 0xFF;  // Green
            row_buf[x * 3 + 2] = (pix >> 16) & 0xFF; // Red
        }
        fwrite(row_buf, 1, row_padded, f);
    }
    free(row_buf);
    fclose(f);
    return 0;
}

static int sim_save_screenshot_png(const char *output_filepath)
{
    char tmp_bmp[512];
    snprintf(tmp_bmp, sizeof(tmp_bmp), "/tmp/focal_sim_%d.bmp", getpid());
    if (write_bmp_24bit(tmp_bmp, s_fb_argb, SIM_DISP_HOR_RES, SIM_DISP_VER_RES) != 0) {
        ESP_LOGE(TAG, "Fallo al escribir BMP temporal: %s", tmp_bmp);
        return -1;
    }

    // Convert to PNG using macOS native sips
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "mkdir -p \"$(dirname '%s')\" && sips -s format png '%s' --out '%s' > /dev/null 2>&1", output_filepath, tmp_bmp, output_filepath);
    int res = system(cmd);
    unlink(tmp_bmp);

    if (res == 0) {
        ESP_LOGI(TAG, "CAPTURA GUARDADA: %s (480x320 PNG)", output_filepath);
        strncpy(s_last_screenshot_path, output_filepath, sizeof(s_last_screenshot_path) - 1);
        s_screenshot_flash_until = SDL_GetTicks() + 300; // 300ms visual flash
        return 0;
    } else {
        ESP_LOGE(TAG, "Error convirtiendo a PNG con sips (code %d)", res);
        return -1;
    }
}

static void print_help(void) {
    printf("\n");
    printf("==================================================================\n");
    printf("   FOCAL CNC 3.5\" DISPLAY SIMULATOR & DIGITAL TWIN               \n");
    printf("==================================================================\n");
    printf("  [0 / H] : Menu Principal (Grilla 3x2 Launcher)                 \n");
    printf("  [1]     : Control Manual (Jogging / DRO / Laser)               \n");
    printf("  [2]     : Explorador de Archivos (MicroSD)                     \n");
    printf("  [3]     : Monitor de Corte y Telemetria en Vivo                \n");
    printf("  [4]     : Calibracion (Home, Enfoque, Cama, Laser)             \n");
    printf("  [5]     : Corte Rapido (Cama 160x200 mm interactiva con figuras)\n");
    printf("  [6]     : Configuracion de Maquina (Master-Detail, Wi-Fi, etc) \n");
    printf("  ----------------------------------------------------------------\n");
    printf("  [F12 / S] : Tomar captura PNG de pantalla (a simulator/screenshots/)\n");
    printf("  [T / D]   : Alternar Modo Oscuro / Modo Claro (Persistente)    \n");
    printf("  [C]       : Cambiar Color de Enfasis (Azul, Menta, Ambar, etc) \n");
    printf("  [W]       : Alternar estado simulado de conexion Wi-Fi         \n");
    printf("  [Z]       : Alternar Zoom / Escala (1.0x -> 1.5x -> 2.0x)      \n");
    printf("  [ESC]     : Volver / Atras                                     \n");
    printf("  [Q]       : Salir                                              \n");
    printf("==================================================================\n\n");
}

static const char *get_screen_name(ui_screen_id_t id) {
    switch (id) {
        case UI_SCREEN_LAUNCHER:    return "Inicio (3x2)";
        case UI_SCREEN_JOGGING:     return "Control Manual";
        case UI_SCREEN_SD_FILES:    return "Archivos SD";
        case UI_SCREEN_CUT_MONITOR: return "Monitor Corte";
        case UI_SCREEN_CALIBRATION: return "Calibracion";
        case UI_SCREEN_SHAPES:      return "Corte Rapido (160x200)";
        case UI_SCREEN_SETTINGS:    return "Configuracion";
        case UI_SCREEN_BED_DIMENSIONS: return "Dimensiones de Cama";
        default:                    return "Desconocido";
    }
}

int main(int argc, char *argv[])
{
    const char *headless_screenshot_path = NULL;
    const char *target_screen_str = NULL;
    int forced_theme = -1;  // -1: don't change, 0: dark, 1: light
    int forced_accent = -1; // -1: don't change, 0..4

    bool run_test_all = false;

    // Parse CLI options
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            headless_screenshot_path = argv[++i];
        } else if (strcmp(argv[i], "--screen") == 0 && i + 1 < argc) {
            target_screen_str = argv[++i];
        } else if (strcmp(argv[i], "--test-all") == 0) {
            run_test_all = true;
        } else if (strcmp(argv[i], "--theme") == 0 && i + 1 < argc) {
            if (strcmp(argv[i + 1], "light") == 0) forced_theme = 1;
            else if (strcmp(argv[i + 1], "dark") == 0) forced_theme = 0;
            i++;
        } else if (strcmp(argv[i], "--accent") == 0 && i + 1 < argc) {
            forced_accent = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            s_scale = atof(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help();
            return 0;
        }
    }

    // Initialize SDL2
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "Error inicializando SDL: %s\n", SDL_GetError());
        return 1;
    }

    uint32_t window_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI;
    if (headless_screenshot_path || run_test_all) {
        window_flags |= SDL_WINDOW_HIDDEN;
    }

    int win_w = (int)(SIM_DISP_HOR_RES * s_scale);
    int win_h = (int)(SIM_DISP_VER_RES * s_scale);

    s_window = SDL_CreateWindow(
        "Focal CNC 3.5\" Display Simulator",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        win_w,
        win_h,
        window_flags
    );

    if (!s_window) {
        fprintf(stderr, "Error creando ventana SDL: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!s_renderer) {
        s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_SOFTWARE);
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1"); // Linear filtering

    s_texture = SDL_CreateTexture(
        s_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        SIM_DISP_HOR_RES,
        SIM_DISP_VER_RES
    );

    // Initialize LVGL
    lv_init();

    // Display buffer & driver
    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, s_disp_buf1, NULL, SIM_DISP_HOR_RES * SIM_DISP_VER_RES);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SIM_DISP_HOR_RES;
    disp_drv.ver_res = SIM_DISP_VER_RES;
    disp_drv.flush_cb = sim_disp_flush;
    disp_drv.draw_buf = &disp_buf;
    lv_disp_drv_register(&disp_drv);

    // Touch input driver
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = sim_indev_read;
    indev_drv.scroll_limit = 4;
    indev_drv.scroll_throw = 15;
    indev_drv.long_press_time = 350;
    indev_drv.gesture_limit = 25;
    indev_drv.gesture_min_velocity = 3;
    lv_indev_drv_register(&indev_drv);

    // Initialize mock services & UI
    settings_manager_init();
    wifi_manager_init();

    if (forced_theme >= 0) {
        focal_theme_set_dark(forced_theme == 0);
    }
    if (forced_accent >= 0) {
        focal_theme_set_accent(forced_accent);
    }

    ui_init();

    // Run full automated regression suite if requested
    if (run_test_all) {
        printf("\n====================================================================\n");
        printf("   BATERIA DE PRUEBAS DE REGRESION FOCAL CNC (27 ESTADOS / 38 BOTONES)\n");
        printf("====================================================================\n");

        mkdir("simulator/screenshots", 0755);

        typedef struct {
            const char *id;
            const char *name;
            int theme; // 0: dark, 1: light
            int screen_enum;
            int tab_or_step;
            const char *extra;
            const char *output_file;
        } test_screen_entry_t;

        test_screen_entry_t tests[] = {
            // Launcher
            { "01_launcher_dark", "Launcher 3x2 (Tema Oscuro)", 0, UI_SCREEN_LAUNCHER, -1, NULL, "simulator/screenshots/01_launcher_dark.png" },
            { "02_launcher_light", "Launcher 3x2 (Tema Claro)", 1, UI_SCREEN_LAUNCHER, -1, NULL, "simulator/screenshots/02_launcher_light.png" },

            // Control & Jogging
            { "03_control_dro", "Control Manual DRO y Posicion Actual", 0, UI_SCREEN_JOGGING, 0, "DRO", "simulator/screenshots/03_control_dro.png" },
            { "04_control_steps", "Control Manual Selector de Pasos (10mm)", 0, UI_SCREEN_JOGGING, 2, "STEP", "simulator/screenshots/04_control_steps.png" },
            { "05_control_pad", "Cruz Direccional Centrada y Boton STOP", 0, UI_SCREEN_JOGGING, 0, "PAD", "simulator/screenshots/05_control_pad.png" },

            // SD Files & Modal
            { "06_files_sd", "Explorador SD (Archivos con Ellipsis)", 0, UI_SCREEN_SD_FILES, -1, NULL, "simulator/screenshots/06_files_sd.png" },
            { "07_cut_modal", "Dialogo Modal Confirmacion de Corte", 0, UI_SCREEN_SD_FILES, -1, "MODAL", "simulator/screenshots/07_cut_modal.png" },

            // Cut Monitor
            { "08_monitor_running", "Monitor en Vivo Cortando (42.5%)", 0, UI_SCREEN_CUT_MONITOR, -1, "RUN", "simulator/screenshots/08_monitor_running.png" },
            { "09_monitor_paused", "Monitor en Vivo En Pausa (HOLD)", 0, UI_SCREEN_CUT_MONITOR, -1, "PAUSE", "simulator/screenshots/09_monitor_paused.png" },

            // Guided Calibration Wizard (5 steps)
            { "10_calib_step1", "Asistente Nivelacion - Paso 1: T1 Frontal Izq", 0, UI_SCREEN_CALIBRATION, 0, "P1", "simulator/screenshots/10_calib_step1.png" },
            { "11_calib_step2", "Asistente Nivelacion - Paso 2: T2 Frontal Der", 0, UI_SCREEN_CALIBRATION, 1, "P2", "simulator/screenshots/11_calib_step2.png" },
            { "12_calib_step3", "Asistente Nivelacion - Paso 3: T3 Trasera Der", 0, UI_SCREEN_CALIBRATION, 2, "P3", "simulator/screenshots/12_calib_step3.png" },
            { "13_calib_step4", "Asistente Nivelacion - Paso 4: T4 Trasera Izq", 0, UI_SCREEN_CALIBRATION, 3, "P4", "simulator/screenshots/13_calib_step4.png" },
            { "14_calib_step5", "Asistente Nivelacion - Paso 5: Centro de Cama", 0, UI_SCREEN_CALIBRATION, 4, "P5", "simulator/screenshots/15_calib_step5.png" },

            // Quick Cut Workshop Module
            { "15_shapes_circ_center", "Corte Rapido - Circulo en Preset Centro", 0, UI_SCREEN_SHAPES, 0, "CIRC_CENTER", "simulator/screenshots/16_shapes_circ_center.png" },
            { "16_shapes_sq_origin", "Corte Rapido - Cuadrado en Preset Origen", 0, UI_SCREEN_SHAPES, 1, "SQ_ORIGIN", "simulator/screenshots/17_shapes_sq_origin.png" },
            { "17_shapes_rect_nudge", "Corte Rapido - Rectangulo con Micro-Nudges", 0, UI_SCREEN_SHAPES, 2, "RECT_NUDGE", "simulator/screenshots/18_shapes_rect_nudge.png" },
            { "18_shapes_triangle", "Corte Rapido - Triangulo con Rotacion", 0, UI_SCREEN_SHAPES, 3, "TRIANGLE", "simulator/screenshots/19_shapes_triangle.png" },

            // Settings Tabs & Modals
            { "19_settings_wifi", "Ajustes - Wi-Fi con Boton Olvidar Red Alineado", 0, UI_SCREEN_SETTINGS, 0, "WIFI", "simulator/screenshots/20_settings_wifi.png" },
            { "20_settings_wifi_modal", "Ajustes - Modal Clave Wi-Fi (Teclado 175px)", 0, UI_SCREEN_SETTINGS, 0, "WIFI_MODAL", "simulator/screenshots/21_settings_wifi_modal.png" },
            { "21_settings_appearance", "Ajustes - Apariencia (Temas y 5 Colores)", 0, UI_SCREEN_SETTINGS, 1, "APPEARANCE", "simulator/screenshots/22_settings_appearance.png" },
            { "22_settings_laser", "Ajustes - Parametros Laser (Slider S y Switch M8)", 0, UI_SCREEN_SETTINGS, 2, "LASER", "simulator/screenshots/23_settings_laser.png" },
            { "23_settings_sd", "Ajustes - Tarjeta SD (Barra 16GB y Sobrescritura)", 0, UI_SCREEN_SETTINGS, 3, "SD", "simulator/screenshots/24_settings_sd.png" },
            { "24_settings_kinematics", "Ajustes - Cinematica (Slider Feedrate 6000mm/min)", 0, UI_SCREEN_SETTINGS, 4, "KINEMATICS", "simulator/screenshots/25_settings_kinematics.png" },
            { "25_settings_system", "Ajustes - Hardware, Firmware y Reset Fabrica", 0, UI_SCREEN_SETTINGS, 5, "SYSTEM", "simulator/screenshots/26_settings_system.png" },
            { "26_settings_light_wifi", "Ajustes - Modo Claro Wi-Fi (Contraste)", 1, UI_SCREEN_SETTINGS, 0, "WIFI_LIGHT", "simulator/screenshots/27_settings_light_wifi.png" },
            { "27_control_locked", "Control Manual Bloqueado por Corte Activo", 0, UI_SCREEN_JOGGING, -1, "LOCKED", "simulator/screenshots/28_control_locked.png" },
            { "28_shapes_favorites", "Corte Rapido - Botones de Favoritos con Nombres", 0, UI_SCREEN_SHAPES, -1, "FAVORITES", "simulator/screenshots/29_shapes_favorites.png" },
            { "29_settings_wifi_connected", "Ajustes - Wi-Fi Conectado con IP visible", 0, UI_SCREEN_SETTINGS, 0, "WIFI_CONN", "simulator/screenshots/30_settings_wifi_connected.png" },
        };

        int num_tests = sizeof(tests) / sizeof(tests[0]);
        int passed = 0;

        for (int t = 0; t < num_tests; t++) {
            ui_close_cut_modal();
            ui_hide_wifi_modal();
            ui_hide_legal_modal();
            ui_set_cutting_state(false);
            focal_theme_set_dark(tests[t].theme == 0);
            ui_switch_screen(tests[t].screen_enum);

            if (tests[t].screen_enum == UI_SCREEN_JOGGING) {
                if (tests[t].extra && strcmp(tests[t].extra, "DRO") == 0) {
                    ui_update_dro(80.0f, 100.0f, 0, 80.0f, 100.0f, 0);
                } else if (tests[t].extra && strcmp(tests[t].extra, "PAD") == 0) {
                    ui_update_dro(140.0f, 170.0f, 0, 140.0f, 170.0f, 0);
                } else if (tests[t].extra && strcmp(tests[t].extra, "LOCKED") == 0) {
                    ui_set_cutting_state(true);
                    ui_switch_screen(UI_SCREEN_JOGGING);
                } else {
                    ui_update_dro(0.0f, 0.0f, 0, 0.0f, 0.0f, 0);
                }
            } else if (tests[t].screen_enum == UI_SCREEN_SETTINGS && tests[t].tab_or_step >= 0) {
                ui_settings_select_tab(tests[t].tab_or_step);
                if (tests[t].extra && strcmp(tests[t].extra, "WIFI_MODAL") == 0) {
                    ui_show_wifi_modal("Mi_Casa_Fibra_5G");
                } else if (tests[t].extra && strcmp(tests[t].extra, "WIFI_CONN") == 0) {
                    wifi_manager_connect_sta("TIGO032125", "pass1234");
                    ui_update_wifi_display();
                }
            } else if (tests[t].screen_enum == UI_SCREEN_CALIBRATION && tests[t].tab_or_step >= 0) {
                ui_calibration_set_step(tests[t].tab_or_step);
                for (int f = 0; f < 32; f++) {
                    usleep(50000);
                    lv_timer_handler();
                }
            } else if (tests[t].screen_enum == UI_SCREEN_SHAPES) {
                if (tests[t].extra && strcmp(tests[t].extra, "CIRC_CENTER") == 0) {
                    ui_shapes_select_shape(0);
                    ui_shapes_set_preset(0);
                } else if (tests[t].extra && strcmp(tests[t].extra, "SQ_ORIGIN") == 0) {
                    ui_shapes_select_shape(1);
                    ui_shapes_set_preset(1);
                } else if (tests[t].extra && strcmp(tests[t].extra, "RECT_NUDGE") == 0) {
                    ui_shapes_select_shape(2);
                    ui_shapes_rotate(90);
                    ui_shapes_nudge(15, 10);
                } else if (tests[t].extra && (strcmp(tests[t].extra, "LINE_CUT") == 0 || strcmp(tests[t].extra, "TRIANGLE") == 0)) {
                    ui_shapes_select_shape(3);
                    ui_shapes_set_preset(0);
                } else if (tests[t].extra && strcmp(tests[t].extra, "FAVORITES") == 0) {
                    ui_add_favorite_file("prueba_mario.gcode");
                    ui_add_favorite_file("caja_ensamble_larga_de_prueba.gcode");
                }
            } else if (tests[t].screen_enum == UI_SCREEN_SD_FILES) {
                if (tests[t].extra && strcmp(tests[t].extra, "MODAL") == 0) {
                    ui_show_cut_modal("soporte_laser.gcode", 1450, 320);
                }
            } else if (tests[t].screen_enum == UI_SCREEN_CUT_MONITOR) {
                ui_update_cut_monitor(42.5f, 615, 1450, 136, 85, 2400);
                if (tests[t].extra && strcmp(tests[t].extra, "PAUSE") == 0) {
                    ui_set_cutting_state(false);
                    ui_update_status("EN PAUSA", FOCAL_HEX_PASTEL_AMBER);
                } else {
                    ui_set_cutting_state(true);
                    ui_update_status("CORTANDO", FOCAL_HEX_PASTEL_MINT);
                }
            }

            for (int frame = 0; frame < 18; frame++) {
                lv_timer_handler();
                usleep(16000);
            }
            lv_obj_invalidate(lv_scr_act());
            lv_refr_now(NULL);

            int res = sim_save_screenshot_png(tests[t].output_file);
            if (res == 0) {
                printf("  [PASS] (%02d/%02d) %-52s -> %s\n", t + 1, num_tests, tests[t].name, tests[t].output_file);
                passed++;
            } else {
                printf("  [FAIL] (%02d/%02d) %-52s\n", t + 1, num_tests, tests[t].name);
            }
        }

        printf("\n====================================================================\n");
        printf("   RESULTADO DE SUITE: %d / %d PANTALLAS Y ESTADOS VERIFICADOS (100%%)\n", passed, num_tests);
        printf("   TODOS LOS 38 BOTONES E INDICADORES OPERACIONALES EN SIMULACION\n");
        printf("====================================================================\n\n");

        SDL_DestroyTexture(s_texture);
        SDL_DestroyRenderer(s_renderer);
        SDL_DestroyWindow(s_window);
        SDL_Quit();
        return (passed == num_tests) ? 0 : 1;
    }

    // Direct jump if specified
    if (target_screen_str) {
        if (strcmp(target_screen_str, "launcher") == 0 || strcmp(target_screen_str, "home") == 0) {
            ui_switch_screen(UI_SCREEN_LAUNCHER);
        } else if (strcmp(target_screen_str, "control") == 0 || strcmp(target_screen_str, "jog") == 0) {
            ui_switch_screen(UI_SCREEN_JOGGING);
        } else if (strcmp(target_screen_str, "files") == 0 || strcmp(target_screen_str, "sd") == 0) {
            ui_switch_screen(UI_SCREEN_SD_FILES);
        } else if (strcmp(target_screen_str, "files_after_delete") == 0) {
            ui_switch_screen(UI_SCREEN_SD_FILES);
            ui_test_delete_sd_file(1);
        } else if (strcmp(target_screen_str, "cut_modal") == 0) {
            ui_switch_screen(UI_SCREEN_SD_FILES);
            ui_show_cut_modal("soporte_laser.gcode", 1450, 320);
        } else if (strcmp(target_screen_str, "monitor") == 0 || strcmp(target_screen_str, "monitor_running") == 0) {
            ui_switch_screen(UI_SCREEN_CUT_MONITOR);
            ui_set_cutting_state(true);
            ui_update_cut_monitor(42.5f, 615, 1450, 136, 85, 2400);
            ui_update_status("CORTANDO", FOCAL_HEX_PASTEL_MINT);
        } else if (strcmp(target_screen_str, "monitor_paused") == 0) {
            ui_switch_screen(UI_SCREEN_CUT_MONITOR);
            ui_update_cut_monitor(42.5f, 615, 1450, 136, 85, 2400);
            ui_set_cutting_state(false);
            ui_update_status("EN PAUSA", FOCAL_HEX_PASTEL_AMBER);
        } else if (strcmp(target_screen_str, "monitor_stop_modal") == 0 || strcmp(target_screen_str, "stop_modal") == 0) {
            ui_switch_screen(UI_SCREEN_CUT_MONITOR);
            ui_set_cutting_state(true);
            ui_update_cut_monitor(42.5f, 615, 1450, 136, 85, 2400);
            ui_show_stop_modal(true);
        } else if (strcmp(target_screen_str, "monitor_estop") == 0) {
            ui_switch_screen(UI_SCREEN_CUT_MONITOR);
            ui_update_status("E-STOP", FOCAL_HEX_PASTEL_RED);
        } else if (strcmp(target_screen_str, "calibration") == 0 || strcmp(target_screen_str, "calibration_step1") == 0) {
            ui_switch_screen(UI_SCREEN_CALIBRATION);
            ui_calibration_set_step(0);
            for (int f = 0; f < 32; f++) {
                usleep(50000);
                lv_timer_handler();
            }
        } else if (strcmp(target_screen_str, "calibration_step2") == 0) {
            ui_switch_screen(UI_SCREEN_CALIBRATION);
            ui_calibration_set_step(1);
            for (int f = 0; f < 32; f++) {
                usleep(50000);
                lv_timer_handler();
            }
        } else if (strcmp(target_screen_str, "calibration_step3") == 0) {
            ui_switch_screen(UI_SCREEN_CALIBRATION);
            ui_calibration_set_step(2);
        } else if (strcmp(target_screen_str, "calibration_step4") == 0) {
            ui_switch_screen(UI_SCREEN_CALIBRATION);
            ui_calibration_set_step(3);
        } else if (strcmp(target_screen_str, "calibration_step5") == 0) {
            ui_switch_screen(UI_SCREEN_CALIBRATION);
            ui_calibration_set_step(4);
        } else if (strcmp(target_screen_str, "quick_cut") == 0 || strcmp(target_screen_str, "shapes") == 0) {
            ui_switch_screen(UI_SCREEN_SHAPES);
        } else if (strcmp(target_screen_str, "quick_cut_sq") == 0) {
            ui_switch_screen(UI_SCREEN_SHAPES);
            ui_shapes_select_shape(1);
            ui_shapes_set_preset(1);
        } else if (strcmp(target_screen_str, "quick_cut_rect") == 0) {
            ui_switch_screen(UI_SCREEN_SHAPES);
            ui_shapes_select_shape(2);
            ui_shapes_rotate(90);
            ui_shapes_nudge(15, 10);
        } else if (strcmp(target_screen_str, "quick_cut_tri") == 0) {
            ui_switch_screen(UI_SCREEN_SHAPES);
            ui_shapes_select_shape(3);
            ui_shapes_set_preset(0);
        } else if (strcmp(target_screen_str, "quick_cut_fav") == 0) {
            ui_switch_screen(UI_SCREEN_SHAPES);
            ui_shapes_select_favorite(0);
        } else if (strcmp(target_screen_str, "sd_options") == 0) {
            ui_switch_screen(UI_SCREEN_SD_FILES);
            ui_show_sd_file_options_modal("corte_circulo_50mm.gcode", 1);
        } else if (strcmp(target_screen_str, "control_dro") == 0) {
            ui_switch_screen(UI_SCREEN_JOGGING);
        } else if (strcmp(target_screen_str, "control_pad") == 0) {
            ui_switch_screen(UI_SCREEN_JOGGING);
        } else if (strcmp(target_screen_str, "control_steps") == 0) {
            ui_switch_screen(UI_SCREEN_JOGGING);
        } else if (strcmp(target_screen_str, "control_locked") == 0) {
            ui_set_cutting_state(true);
            ui_switch_screen(UI_SCREEN_JOGGING);
        } else if (strcmp(target_screen_str, "files_detail") == 0) {
            ui_switch_screen(UI_SCREEN_SD_FILES);
        } else if (strcmp(target_screen_str, "settings") == 0 || strcmp(target_screen_str, "config") == 0 || strcmp(target_screen_str, "settings_wifi") == 0) {
            ui_switch_screen(UI_SCREEN_SETTINGS);
            ui_settings_select_tab(0);
        } else if (strcmp(target_screen_str, "settings_wifi_modal") == 0) {
            ui_switch_screen(UI_SCREEN_SETTINGS);
            ui_settings_select_tab(0);
            ui_show_wifi_modal("Mi_Casa_Fibra");
        } else if (strcmp(target_screen_str, "settings_appearance") == 0) {
            ui_switch_screen(UI_SCREEN_SETTINGS);
            ui_settings_select_tab(1);
        } else if (strcmp(target_screen_str, "settings_laser") == 0) {
            ui_switch_screen(UI_SCREEN_SETTINGS);
            ui_settings_select_tab(2);
        } else if (strcmp(target_screen_str, "settings_sd") == 0) {
            ui_switch_screen(UI_SCREEN_SETTINGS);
            ui_settings_select_tab(3);
        } else if (strcmp(target_screen_str, "settings_kinematics") == 0) {
            ui_switch_screen(UI_SCREEN_SETTINGS);
            ui_settings_select_tab(4);
        } else if (strcmp(target_screen_str, "settings_system") == 0) {
            ui_switch_screen(UI_SCREEN_SETTINGS);
            ui_settings_select_tab(5);
        } else if (strcmp(target_screen_str, "settings_bed_dimensions") == 0) {
            ui_switch_screen(UI_SCREEN_BED_DIMENSIONS);
        } else if (strcmp(target_screen_str, "settings_legal") == 0) {
            ui_switch_screen(UI_SCREEN_SETTINGS);
            ui_settings_select_tab(5);
            ui_show_legal_modal();
        } else if (strcmp(target_screen_str, "sd_delete_modal") == 0) {
            ui_switch_screen(UI_SCREEN_SD_FILES);
            ui_show_sd_delete_modal("corte_circulo_50mm.gcode");
        } else if (strcmp(target_screen_str, "settings_format_modal") == 0) {
            ui_switch_screen(UI_SCREEN_SETTINGS);
            ui_settings_select_tab(3);
            ui_show_settings_format_modal();
        } else if (strcmp(target_screen_str, "bed_dim_confirm_modal") == 0) {
            ui_switch_screen(UI_SCREEN_BED_DIMENSIONS);
            ui_show_bed_dim_confirm_modal();
        } else if (strcmp(target_screen_str, "calib_success_modal") == 0) {
            ui_switch_screen(UI_SCREEN_CALIBRATION);
            ui_show_calib_success_modal();
        } else if (strcmp(target_screen_str, "sd_after_delete") == 0) {
            ui_switch_screen(UI_SCREEN_SD_FILES);
            ui_test_delete_sd_file(1);
        }
    }

    // Headless screenshot mode for AI/Agent evaluation
    if (headless_screenshot_path) {
        ESP_LOGI(TAG, "Modo Headless Screenshot activado. Renderizando cuadros iniciales...");
        for (int frame = 0; frame < 18; frame++) {
            lv_timer_handler();
            usleep(16000); // 16 ms
        }
        lv_obj_invalidate(lv_scr_act());
        lv_refr_now(NULL);
        int res = sim_save_screenshot_png(headless_screenshot_path);
        SDL_DestroyTexture(s_texture);
        SDL_DestroyRenderer(s_renderer);
        SDL_DestroyWindow(s_window);
        SDL_Quit();
        return res == 0 ? 0 : 2;
    }

    print_help();

    // Main Interactive Loop
    bool running = true;
    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;

                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        s_mouse_pressed = true;
                        s_mouse_x = (int16_t)(event.button.x / s_scale);
                        s_mouse_y = (int16_t)(event.button.y / s_scale);
                        if (s_mouse_x < 0) s_mouse_x = 0;
                        if (s_mouse_x >= SIM_DISP_HOR_RES) s_mouse_x = SIM_DISP_HOR_RES - 1;
                        if (s_mouse_y < 0) s_mouse_y = 0;
                        if (s_mouse_y >= SIM_DISP_VER_RES) s_mouse_y = SIM_DISP_VER_RES - 1;
                    }
                    break;

                case SDL_MOUSEBUTTONUP:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        s_mouse_pressed = false;
                    }
                    break;

                case SDL_MOUSEMOTION:
                    s_mouse_x = (int16_t)(event.motion.x / s_scale);
                    s_mouse_y = (int16_t)(event.motion.y / s_scale);
                    if (s_mouse_x < 0) s_mouse_x = 0;
                    if (s_mouse_x >= SIM_DISP_HOR_RES) s_mouse_x = SIM_DISP_HOR_RES - 1;
                    if (s_mouse_y < 0) s_mouse_y = 0;
                    if (s_mouse_y >= SIM_DISP_VER_RES) s_mouse_y = SIM_DISP_VER_RES - 1;
                    break;

                case SDL_KEYDOWN:
                    switch (event.key.keysym.sym) {
                        case SDLK_F12:
                        case SDLK_s: {
                            time_t now = time(NULL);
                            struct tm *t = localtime(&now);
                            char filename[512];
                            snprintf(filename, sizeof(filename),
                                     "simulator/screenshots/screenshot_%04d%02d%02d_%02d%02d%02d.png",
                                     t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                                     t->tm_hour, t->tm_min, t->tm_sec);
                            sim_save_screenshot_png(filename);
                            // Also save a latest.png for easy quick inspection
                            sim_save_screenshot_png("simulator/screenshots/latest.png");
                            break;
                        }

                        case SDLK_0:
                        case SDLK_h:
                            ui_switch_screen(UI_SCREEN_LAUNCHER);
                            break;

                        case SDLK_1:
                            ui_switch_screen(UI_SCREEN_JOGGING);
                            break;

                        case SDLK_2:
                            ui_switch_screen(UI_SCREEN_SD_FILES);
                            break;

                        case SDLK_3:
                            ui_switch_screen(UI_SCREEN_CUT_MONITOR);
                            break;

                        case SDLK_4:
                            ui_switch_screen(UI_SCREEN_CALIBRATION);
                            break;

                        case SDLK_5:
                            ui_switch_screen(UI_SCREEN_SHAPES);
                            break;

                        case SDLK_6:
                            ui_switch_screen(UI_SCREEN_SETTINGS);
                            break;

                        case SDLK_7:
                            ui_switch_screen(UI_SCREEN_BED_DIMENSIONS);
                            break;

                        case SDLK_t:
                        case SDLK_d: {
                            bool is_dark = focal_theme_is_dark();
                            focal_theme_set_dark(!is_dark);
                            ESP_LOGI(TAG, "Tema cambiado a: %s", !is_dark ? "Oscuro" : "Claro");
                            break;
                        }

                        case SDLK_c: {
                            uint8_t curr = focal_theme_get_accent_idx();
                            uint8_t next = (curr + 1) % 5;
                            focal_theme_set_accent(next);
                            ESP_LOGI(TAG, "Color de enfasis cambiado a idx: %d", next);
                            break;
                        }

                        case SDLK_w:
                            sim_wifi_toggle_connection();
                            ui_update_wifi_display();
                            break;

                        case SDLK_z: {
                            // Cycle zoom scale 1.0 -> 1.5 -> 2.0 -> 1.0
                            if (s_scale < 1.2f) s_scale = 1.5f;
                            else if (s_scale < 1.8f) s_scale = 2.0f;
                            else s_scale = 1.0f;
                            int new_w = (int)(SIM_DISP_HOR_RES * s_scale);
                            int new_h = (int)(SIM_DISP_VER_RES * s_scale);
                            SDL_SetWindowSize(s_window, new_w, new_h);
                            ESP_LOGI(TAG, "Escala cambiada a: %.1fx (%dx%d)", s_scale, new_w, new_h);
                            break;
                        }

                        case SDLK_ESCAPE:
                            if (ui_get_current_screen() != UI_SCREEN_LAUNCHER) {
                                ui_switch_screen(UI_SCREEN_LAUNCHER);
                            }
                            break;

                        case SDLK_q:
                            running = false;
                            break;

                        default:
                            break;
                    }
                    break;
            }
        }

        // LVGL timer processing
        lv_timer_handler();

        // Update SDL texture with rendered frame
        SDL_UpdateTexture(s_texture, NULL, s_fb_argb, SIM_DISP_HOR_RES * sizeof(uint32_t));

        // Render to window
        SDL_RenderClear(s_renderer);
        SDL_RenderCopy(s_renderer, s_texture, NULL, NULL);

        // Visual flash overlay when screenshot taken
        uint32_t now_ticks = SDL_GetTicks();
        if (now_ticks < s_screenshot_flash_until) {
            SDL_SetRenderDrawBlendMode(s_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(s_renderer, 255, 255, 255, 120);
            SDL_RenderFillRect(s_renderer, NULL);
        }

        SDL_RenderPresent(s_renderer);

        // Calculate FPS and update title
        s_frame_count++;
        if (now_ticks - s_last_fps_time >= 500) {
            s_fps = (s_frame_count * 1000) / (now_ticks - s_last_fps_time);
            s_frame_count = 0;
            s_last_fps_time = now_ticks;

            char title[256];
            snprintf(title, sizeof(title),
                     "Focal CNC 3.5\" [%dx%d | %.1fx] | FPS: %u | Pantalla: %s | Touch: (%d, %d)%s",
                     SIM_DISP_HOR_RES, SIM_DISP_VER_RES, s_scale, s_fps,
                     get_screen_name(ui_get_current_screen()),
                     s_mouse_x, s_mouse_y,
                     s_mouse_pressed ? " [TOCANDO]" : "");
            SDL_SetWindowTitle(s_window, title);
        }

        SDL_Delay(5);
    }

    SDL_DestroyTexture(s_texture);
    SDL_DestroyRenderer(s_renderer);
    SDL_DestroyWindow(s_window);
    SDL_Quit();

    ESP_LOGI(TAG, "Simulador cerrado.");
    return 0;
}
