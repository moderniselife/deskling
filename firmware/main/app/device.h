// Small device-level actions shared by the UI, the web page and the network code.
#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Save and apply the daytime brightness (10..100). */
void device_set_brightness(uint8_t percent);
/** Turn the picture around (saved). */
void device_set_rotate_180(bool on);
/** Save and apply volume (0..100) and quiet hours. */
void device_set_sound(uint8_t volume, bool quiet_night);
