#pragma once
/* Call early at boot: works out why the last restart happened. */
void crashlog_init(void);
/* "" if the last restart was a normal one, otherwise a line for the board's page. */
const char *crashlog_summary(void);
/* Restart now as a quiet fresh start (see ui/health.h): remembered so the board comes back on
   `screen` without the boot animation, and its page can say why. Doesn't return. */
void crashlog_fresh_start(const char *why, int screen);
/* After a fresh start: the screen to come back on. -1 after any other start. */
int crashlog_quiet_screen(void);
/* Fresh starts since the board was powered on. */
unsigned crashlog_fresh_starts(void);
