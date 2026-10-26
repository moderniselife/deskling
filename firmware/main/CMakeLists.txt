file(GLOB UI_SRCS "ui/*.c" "ui/fonts/*.c")

idf_component_register(
    SRCS "main.c" "board/board.c" "board/audio.c" "app/sound_synth.c" "board/gsl3680.c"
         "net/net.c" "net/portal.c" "net/ota.c" "net/images.c"
         "app/model.c" "app/settings.c" "app/crashlog.c" ${UI_SRCS}
    INCLUDE_DIRS "." "app" "ui" "board" "net"
    EMBED_TXTFILES "net/portal.html"
    PRIV_REQUIRES esp_lcd esp_driver_ppa esp_driver_jpeg esp_driver_gpio esp_driver_ledc esp_driver_i2c esp_driver_i2s esp_hw_support esp_timer
                  nvs_flash esp_wifi esp_netif esp_event mbedtls esp_http_server esp_http_client
                  esp_https_ota app_update esp_app_format
)

# The GSL3680 needs its RAM firmware uploaded on every boot. That table comes
# from the guition-jc component (fetched at build time) rather than this repo.
idf_component_get_property(gsl_dir guition-jc__esp_lcd_touch_gsl3680 COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE ${gsl_dir}/private_include)
# UI strings are deliberately clipped with snprintf.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-deprecated-declarations -Wno-format-truncation)
# Remember why the last crash happened (app/crashlog.c), for the board's page.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_panic_handler")
