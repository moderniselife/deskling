// Remembers why Deskling last crashed, so the board's own page can say (no USB cable needed).
// The panic handler is wrapped (-Wl,--wrap=esp_panic_handler): before the usual crash report we
// note the reason, the address and the task in memory that survives the restart.
#include "crashlog.h"

#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_private/panic_internal.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "riscv/rvruntime-frames.h"

#define MAGIC 0xDE5C1A55u

typedef struct {
    uint32_t magic;
    uint32_t pc, ra, sp;
    char reason[96];
    char task[16];
} crash_t;

static RTC_NOINIT_ATTR crash_t s_crash;

#define FRESH_MAGIC 0xF2E5A171u
typedef struct {
    uint32_t magic;
    int32_t screen;
    char why[80];
} fresh_t;
static RTC_NOINIT_ATTR fresh_t s_fresh;
static RTC_NOINIT_ATTR uint32_t s_fresh_n, s_fresh_n_check; // survives restarts, not power cuts
static int s_quiet = -1;
static char s_summary[200];

extern char *g_panic_abort_details;
void __real_esp_panic_handler(panic_info_t *info);

static void copy(char *dst, size_t n, const char *src)
{
    size_t i = 0;
    for (; src && src[i] && i + 1 < n; i++) dst[i] = src[i] == '\n' ? ' ' : src[i];
    dst[i] = '\0';
}

void __wrap_esp_panic_handler(panic_info_t *info)
{
    s_crash.magic = MAGIC;
    const RvExcFrame *f = (const RvExcFrame *)info->frame;
    s_crash.pc = f ? (uint32_t)f->mepc : (uint32_t)(uintptr_t)info->addr;
    s_crash.ra = f ? (uint32_t)f->ra : 0;
    s_crash.sp = f ? (uint32_t)f->sp : 0;
    copy(s_crash.reason, sizeof(s_crash.reason), g_panic_abort_details ? g_panic_abort_details : info->reason);
    const char *t = xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED ? "boot" : pcTaskGetName(NULL);
    copy(s_crash.task, sizeof(s_crash.task), t);
    __real_esp_panic_handler(info);
}

void crashlog_init(void)
{
    esp_reset_reason_t r = esp_reset_reason();
    const char *why = NULL;
    switch (r) {
    case ESP_RST_PANIC: why = "crashed"; break;
    case ESP_RST_INT_WDT: why = "froze (interrupt watchdog)"; break;
    case ESP_RST_TASK_WDT: why = "froze (task watchdog)"; break;
    case ESP_RST_WDT: why = "froze (watchdog)"; break;
    case ESP_RST_BROWNOUT: why = "lost power (brownout)"; break;
    default: break;
    }
    if (why && s_crash.magic == MAGIC && r == ESP_RST_PANIC) {
        snprintf(s_summary, sizeof(s_summary), "Last restart: %s in %s: %s (pc 0x%08lx, ra 0x%08lx)", why, s_crash.task, s_crash.reason,
                 (unsigned long)s_crash.pc, (unsigned long)s_crash.ra);
    } else if (why) {
        snprintf(s_summary, sizeof(s_summary), "Last restart: %s.", why);
    }
    s_crash.magic = 0; // report each crash once

    if (s_fresh_n_check != ~s_fresh_n) s_fresh_n = 0, s_fresh_n_check = ~0u; // powered on: noise
    if (r == ESP_RST_SW && s_fresh.magic == FRESH_MAGIC) {
        s_quiet = s_fresh.screen;
        s_fresh.why[sizeof(s_fresh.why) - 1] = '\0';
        snprintf(s_summary, sizeof(s_summary), "Last restart: a fresh start by itself (%s).", s_fresh.why);
    }
    s_fresh.magic = 0;
}

void crashlog_fresh_start(const char *why, int screen)
{
    s_fresh.screen = screen;
    copy(s_fresh.why, sizeof(s_fresh.why), why);
    s_fresh.magic = FRESH_MAGIC;
    s_fresh_n++;
    s_fresh_n_check = ~s_fresh_n;
    esp_restart();
}

int crashlog_quiet_screen(void) { return s_quiet; }

unsigned crashlog_fresh_starts(void) { return s_fresh_n; }

const char *crashlog_summary(void) { return s_summary; }
