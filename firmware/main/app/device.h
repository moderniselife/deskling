// Small device-level actions shared by the UI, the web page and the network code.
#pragma once

#include <stdint.h>

/** Save and apply the daytime brightness (10..100). */
void device_set_brightness(uint8_t percent);
