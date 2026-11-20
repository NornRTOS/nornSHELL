/**
 * @file nosh_cmd_log.c
 * `log`: the run time level of nornCore's log classes (hal/log.h), shown and set from
 * the console. What is set lasts until the next reset; an application that keeps the
 * level in a configuration store applies the stored value at boot and follows changes
 * through the store's change hook (examples/device in nornRTOS, with nornStore).
 *
 *     nosh> log
 *     log level info (3), compiled up to info
 *     nosh> log warning
 *     log level warning (2), compiled up to info
 *     nosh> log 4
 *     log level debug (4), compiled up to info: debug messages are not in this image
 *     nosh> log wifi warning
 *     log level info (3), compiled up to info; wifi warning
 *
 * A module with its own level (LOG_MODULE in hal/log.h) that the application has listed
 * with log_setModules() is shown after the global level and set with `log <module>
 * <level>`; `log <level>` sets the global level and every module's.
 *
 * Registered with nosh_add(&nosh_cmd_log); not among the default commands, so a shell
 * without it links none of the log code.
 */

#include "nosh.h"
#include "log.h"

static void putLevel(void)
{
    uint8_t n, i;
    const log_module_t * const pMod = log_modules(&n);

    nosh_putsP(NOSH_STR("log level "));
    nosh_putsP(log_levelName(log_level));
    nosh_putsP(NOSH_STR(" ("));
    nosh_putU32(log_level);
    nosh_putsP(NOSH_STR("), compiled up to "));
    nosh_putsP(log_levelName(LOG_CFG_MAX));
    if(log_level > LOG_CFG_MAX)
    {
        nosh_putsP(NOSH_STR(": "));
        nosh_putsP(log_levelName(log_level));
        nosh_putsP(NOSH_STR(" messages are not in this image"));
    }
    for(i = 0; i < n; ++i)
    {
        nosh_putsP(i == 0 ? NOSH_STR("; ") : NOSH_STR(", "));
        nosh_puts(pMod[i].name);
        nosh_putc(' ');
        nosh_putsP(log_levelName(*pMod[i].pLevel));
    }
    nosh_putEol();
}

/** A level from a name or a number; LOG_DEBUG + 1 if neither. */
static uint8_t parseLevel(const char *s)
{
    uint32_t n;
    uint8_t level;
    for(level = LOG_PANIC; level <= LOG_DEBUG; ++level)
        if(nosh_strEqP(s, log_levelName(level)))
            return level;
    if(nosh_parseU32(s, &n)  &&  n <= LOG_DEBUG)
        return (uint8_t)n;
    return LOG_DEBUG + 1u;
}

static void cmdLog(uint8_t argc, const char *const argv[])
{
    uint8_t level;

    if(argc == 1)
    {
        putLevel();
        return;
    }
    if(argc == 2  &&  (level = parseLevel(argv[1])) <= LOG_DEBUG)
    {
        log_setLevel(level);                    /* the global level and every module's */
        putLevel();
        return;
    }
    if(argc == 3  &&  (level = parseLevel(argv[2])) <= LOG_DEBUG)
    {
        uint8_t n, i;
        const log_module_t * const pMod = log_modules(&n);
        for(i = 0; i < n; ++i)
            if(nosh_strEq(argv[1], pMod[i].name))
            {
                *pMod[i].pLevel = level;
                putLevel();
                return;
            }
        nosh_putsP(NOSH_STR("log: "));
        nosh_puts(argv[1]);
        nosh_putsP(NOSH_STR(": no such module"));
        nosh_putEol();
        return;
    }
    nosh_putsP(NOSH_STR("usage: log [module] [panic|error|warning|info|debug|0..4]"));
    nosh_putEol();
}

NOSH_CMD(nosh_cmd_log, "log", "the log level (log [module] [level])", cmdLog);
