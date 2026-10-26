// Public entry points for the Deskling UI. Call everything with the LVGL lock held.
#pragma once

#include <stdint.h>

#include "model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DK_CONN_SETUP,  // no Wi-Fi configured
    DK_CONN_WIFI,   // joining Wi-Fi
    DK_CONN_RELAY,  // Wi-Fi up, connecting to the relay
    DK_CONN_ONLINE, // live
} dk_conn_t;

typedef void (*ui_backlight_fn)(uint8_t percent);

void ui_init(void);
/** Hand the UI a freshly parsed snapshot (copied). */
void ui_apply_model(const dk_model_t *m);
void ui_set_connection(dk_conn_t c, const char *detail);
void ui_set_backlight_cb(ui_backlight_fn fn);

#ifdef __cplusplus
}
#endif
