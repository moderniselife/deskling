// Over-the-air updates from the published firmware (firmware-dist branch), with rollback.
#pragma once

#include <stdbool.h>

/** Which prebuilt image this firmware is, e.g. "p4r1-lcd1" (chip revision x LCD revision). */
const char *ota_variant(void);
/** Call once at boot: arms the rollback watchdog for a freshly installed update. */
void ota_init(void);
/** The network is up: check for updates shortly, then every 12 hours. */
void ota_check_soon(void);
void ota_request_check(void);
void ota_request_install(void);
/** The new firmware reached the relay: keep it (cancels the rollback). */
void ota_mark_healthy(void);
