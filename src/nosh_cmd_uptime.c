/**
 * @file nosh_cmd_uptime.c
 * `uptime`: time since rtos_start() from the tick counter, the tick itself, and how the
 * hardware says the last reset came about.
 *
 *     up 0d 00:01:23.456  tick 83456 at 1000 Hz  reset power-on
 *
 * With 16 bit ticks (RTOS_CFG_TICK_BITS 16) the counter wraps after 65 s at 1 kHz, and
 * so does this display; it says so.
 */

#include "nosh.h"

static void put2(uint32_t n)
{
    nosh_putc((char)('0' + n / 10u));
    nosh_putc((char)('0' + n % 10u));
}


static void cmdUptime(uint8_t argc, const char *const argv[])
{
    const rtos_tick_t tick = rtos_getTick();
    const uint32_t seconds = (uint32_t)tick / (uint32_t)RTOS_CFG_TICK_HZ;
    const uint32_t ms = ((uint32_t)tick % (uint32_t)RTOS_CFG_TICK_HZ) * (uint32_t)1000u
                        / (uint32_t)RTOS_CFG_TICK_HZ;
    (void)argc; (void)argv;

    nosh_putsP(NOSH_STR("up "));
    nosh_putU32(seconds / 86400ul);
    nosh_putsP(NOSH_STR("d "));
    put2(seconds / 3600ul % 24ul);
    nosh_putc(':');
    put2(seconds / 60ul % 60ul);
    nosh_putc(':');
    put2(seconds % 60ul);
    nosh_putc('.');
    nosh_putc((char)('0' + ms / 100u));
    put2(ms % 100u);

    nosh_putsP(NOSH_STR("  tick "));
    nosh_putU32((uint32_t)tick);
    nosh_putsP(NOSH_STR(" at "));
    nosh_putU32((uint32_t)RTOS_CFG_TICK_HZ);
    nosh_putsP(NOSH_STR(" Hz"));
#if RTOS_CFG_TICK_BITS == 16
    nosh_putsP(NOSH_STR(" (16 bit, wraps)"));
#endif

    nosh_putsP(NOSH_STR("  reset "));
    switch(rtos_getResetCause())
    {
    case RTOS_RESET_POWER_ON:   nosh_putsP(NOSH_STR("power-on")); break;
    case RTOS_RESET_EXTERNAL:   nosh_putsP(NOSH_STR("external")); break;
    case RTOS_RESET_BROWN_OUT:  nosh_putsP(NOSH_STR("brown-out")); break;
    case RTOS_RESET_WATCHDOG:   nosh_putsP(NOSH_STR("watchdog")); break;
    case RTOS_RESET_SOFTWARE:   nosh_putsP(NOSH_STR("software")); break;
    default:                    nosh_putsP(NOSH_STR("unknown")); break;
    }
    nosh_putEol();
}

NOSH_CMD(nosh_cmd_uptime, "uptime", "time since start, tick, reset cause", cmdUptime);
