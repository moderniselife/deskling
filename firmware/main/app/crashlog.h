#pragma once
/* Call early at boot: works out why the last restart happened. */
void crashlog_init(void);
/* "" if the last restart was a normal one, otherwise a line for the board's page. */
const char *crashlog_summary(void);
