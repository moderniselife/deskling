file(GLOB UI_SRCS "ui/*.c" "ui/fonts/*.c")

idf_component_register(
    SRCS "main.c" "board/board.c" "board/gsl3680.c" "net/net.c" "app/model.c" ${UI_SRCS}
    INCLUDE_DIRS "." "app" "ui" "board" "net"
    PRIV_REQUIRES esp_lcd esp_driver_gpio esp_driver_ledc esp_driver_i2c esp_hw_support esp_timer
                  nvs_flash esp_wifi esp_netif esp_event mbedtls
)

# The GSL3680 needs its RAM firmware uploaded on every boot. That table comes
# from the guition-jc component (fetched at build time) rather than this repo.
idf_component_get_property(gsl_dir guition-jc__esp_lcd_touch_gsl3680 COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE ${gsl_dir}/private_include)
# UI strings are deliberately clipped with snprintf.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-deprecated-declarations -Wno-format-truncation)
