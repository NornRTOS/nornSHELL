/**
 * @file test_nosh.c
 * nosh on nornCore's host port: a scripted transport feeds the shell task one byte per
 * read() and collects everything it writes; when the script is spent and the shell has
 * gone back to polling, the output is checked against what the lines should have
 * produced. Covers nosh_enable()'s checks, the line editor (CRLF, backspace, Ctrl-U,
 * overflow), the tokenizer (argc, runs of spaces, too many arguments), `help`, an
 * unknown command, that a command sees its arguments, and the `log` command against
 * nornCore's log level (hal/log.h on the host HAL).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rtos.h"
#include "nosh.h"
#include "log.h"

static LOG_MODULE_LEVEL(tst);                                  /* a module with its own level */
static const log_module_t _logModules[] = { { "tst", &tst_log_level } };

#define T_PARTNER   1u
#define T_SHELL     2u
#define T_FREE      3u

/* The host port runs tasks on its own stacks; these areas are only filled and scanned
   for the stack columns of `ps`. */
static uint8_t _stackPartner[128], _stackShell[256];
static const char *const _names[] = { "idle", "partner", "nosh" };
static rtos_sem_t _sem;
static uint8_t _heap[1024];
static unsigned _checks, _fails;
static uint32_t _idleLoops, _idleSinceSpent;

#define CHECK(cond)  check((cond), #cond, __LINE__)
static void check(bool ok, const char *what, int line)
{
    ++ _checks;
    if(!ok)
    {
        ++ _fails;
        printf("CHECK FAILED line %d: %s\n", line, what);
    }
}

/* ---- the scripted transport ------------------------------------------------------- */

#define BS      "\x08"
#define DEL     "\x7f"
#define CTRL_U  "\x15"

static const char _script[] =
    "help\r\n"                              /* CRLF: one line, not two */
    "\r"                                    /* empty line: just a new prompt */
    "   \r"                                 /* only spaces: same */
    "nosuch\r"                              /* unknown command */
    "echo  one   two\r"                     /* runs of spaces are one separator */
    "ecxo" BS BS "ho a" DEL "b\r"           /* backspace and DEL */
    "garbage" CTRL_U "echo cleared\r"       /* Ctrl-U drops the line */
    "echo 1 2 3 4 5 6 7 8\r"                /* 9 tokens > NOSH_CFG_MAX_ARGS 8 */
    "echo 1 2 3 4 5 6 7\r"                  /* exactly 8: fine */
    "0123456789012345678901234567890123456789012345678901234567890123456789\r" /* 70 > 63 */
    "echo after\r"                          /* the editor recovered */
    "echo \x01\x02tab\x09x\r"               /* control characters are ignored */
    "uptime\r"
    "ps\r"
    "mem\r"
    "sched\r"
    "log\r"                                 /* info, the default */
    "log warning\r"
    "log 4\r"                               /* above LOG_CFG_MAX (info here) */
    "log bogus\r"
    "log 5\r"
    "log 1 2\r"                             /* a module named 1: none */
    "log error\r"
    "log tst warning\r"                     /* one module */
    "log nosuch 2\r"
    "log info\r";                           /* the global level and the module's */
static size_t _scriptPos = 0;
static bool _spent = false;

static char _out[8192];
static size_t _outLen = 0;

/** One byte per call, and nothing every fourth call: the shell then sleeps
    NOSH_CFG_POLL_MS, the partner task gets to run and block, and the ticks advance (one
    per idle loop), so that `ps` and `uptime` have a live system to report on. */
static size_t ioRead(void *buf, size_t n)
{
    static unsigned calls = 0;
    (void)n;
    if(_scriptPos >= sizeof(_script) - 1u)
    {
        _spent = true;
        return 0;
    }
    if(++calls % 4u == 0)
        return 0;
    *(char *)buf = _script[_scriptPos++];
    return 1;
}

static void ioWrite(const void *buf, size_t n)
{
    if(_outLen + n < sizeof(_out))
    {
        memcpy(&_out[_outLen], buf, n);
        _outLen += n;
        _out[_outLen] = '\0';
    }
}

static const nosh_io_t _io = { ioRead, ioWrite };

/* ---- a command of the test: echo prints argc and the arguments ---------------------- */

static void cmdEcho(uint8_t argc, const char *const argv[])
{
    uint8_t i;
    nosh_putsP(NOSH_STR("argc="));
    nosh_putU32(argc);
    for(i = 1; i < argc; ++i)
    {
        nosh_putc(' ');
        nosh_putc('[');
        nosh_puts(argv[i]);
        nosh_putc(']');
    }
    nosh_putEol();
}

NOSH_CMD_STATIC(_cmdEcho, "echo", "print the arguments", cmdEcho);
NOSH_CMD_STATIC(_cmdSilent, "silent", "", cmdEcho);

/* ---- the partner task: blocks on a semaphore with a long timeout, occupies slot 1 ---- */

static void partner(void *arg)
{
    (void)arg;
    for(;;)
        (void)rtos_semTake(&_sem, 60000);
}

/* ---- checks on the collected output ------------------------------------------------- */

static unsigned countOf(const char *needle)
{
    unsigned n = 0;
    const char *p = _out;
    while((p = strstr(p, needle)) != NULL)
    {
        ++ n;
        p += strlen(needle);
    }
    return n;
}

static void checkOutput(void)
{
    const char *p;

    /* The run time conversion: 32 bit values straight, and the 64 bit path (forced
       through the two-word division) on values above 32 bits, saturating. */
    CHECK(nosh_runtimeMs(0) == 0);
    CHECK(nosh_runtimeMs(999) == 999);           /* host: RTOS_RUNTIME_HZ 1000 -> div 1 */
    CHECK(nosh_runtimeMs(UINT32_MAX) == UINT32_MAX);
    {
        /* The two-word division against the compiler's 64 bit arithmetic. */
        static const uint32_t hi[] = { 0, 0, 1, 0x1234, 0xFFFFFFFEu, 239999, 240000, 5 };
        static const uint32_t lo[] = { 0, 1000, 0, 0x89ABCDEFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0, 7 };
        static const uint32_t d[]  = { 7, 1, 1000, 240000, 0xFFFFFFFFu, 240000, 240000, 3 };
        unsigned i;
        for(i = 0; i < sizeof hi / sizeof *hi; ++i)
        {
            const uint64_t n = ((uint64_t)hi[i] << 32) | lo[i];
            const uint64_t q = n / d[i];
            CHECK(nosh_div64by32(hi[i], lo[i], d[i]) == (q > UINT32_MAX ? UINT32_MAX : (uint32_t)q));
        }
    }

    /* help: the three commands in registration order, help first, padded to column 10,
       the one without a help text alone on its line. */
    p = strstr(_out, "help      list the commands\r\n"
                     "echo      print the arguments\r\n"
                     "silent\r\n"
                     "uptime    time since start, tick, reset cause\r\n"
                     "ps        list the tasks\r\n"
                     "mem       stacks and heap\r\n"
                     "sched     load, task states, tick, last panic\r\n"
                     "log       the log level (log [module] [level])\r\n");
    CHECK(p != NULL);

    CHECK(strstr(_out, "nosh: nosuch: not found\r\n") != NULL);
    CHECK(strstr(_out, "argc=3 [one] [two]\r\n") != NULL);
    CHECK(strstr(_out, "argc=2 [b]\r\n") != NULL);                      /* "ecxo"BS BS"ho a"DEL"b" */
    CHECK(strstr(_out, "argc=2 [cleared]\r\n") != NULL);
    CHECK(strstr(_out, "nosh: too many arguments\r\n") != NULL);
    CHECK(strstr(_out, "argc=8 [1] [2] [3] [4] [5] [6] [7]\r\n") != NULL);
    CHECK(strstr(_out, "nosh: line too long\r\n") != NULL);
    CHECK(strstr(_out, "argc=2 [after]\r\n") != NULL);
    CHECK(strstr(_out, "argc=2 [tabx]\r\n") != NULL);
    CHECK(strstr(_out, "argc=1") == NULL);                             /* never ran bare */

    /* Echo: the backspace sequence went out, the Ctrl-U erased 7 characters. */
    CHECK(countOf("\b \b") == 2u + 1u + 7u);

    /* mem: two tasks' stacks (the idle task has none), both untouched, the smaller one
       named; the heap with one allocation of 100 bytes plus its header. */
    CHECK(strstr(_out, "stacks  2 tasks 384 B, 384 untouched, least margin 128 B in 1 (partner)\r\n") != NULL);
    CHECK(strstr(_out, "heap    total 1024 free ") != NULL);
    CHECK(strstr(_out, " allocs 1 frees 0 failed 0\r\n") != NULL);

    /* sched: a load figure, the three tasks (the shell running, the partner blocked), the
       tick, no panic. */
    CHECK(strstr(_out, "load    ") != NULL);
    CHECK(strstr(_out, " % since last sched\r\n") != NULL);
    CHECK(strstr(_out, "tasks   2 of 3 slots: 1 running 0 ready 1 blocked 0 suspended 0 terminated\r\n") != NULL);
    CHECK(strstr(_out, "tick    1000 Hz, 32 bit; timers in the tick; switches -\r\n") != NULL);
    CHECK(strstr(_out, "panic   none\r\n") != NULL);

    /* log: shown, set by name, set by number above the compiled cap (said so), three
       refusals with the usage line, set by name again; the variable followed. */
    CHECK(countOf("log level info (3), compiled up to info; tst info\r\n") == 2u);   /* the default, and `log info` */
    CHECK(countOf("log level warning (2), compiled up to info; tst warning\r\n") == 1u);
    CHECK(countOf("log level debug (4), compiled up to info: debug messages are not in this image; tst debug\r\n") == 1u);
    CHECK(countOf("usage: log [module] [panic|error|warning|info|debug|0..4]\r\n") == 2u);
    CHECK(countOf("log: 1: no such module\r\n") == 1u);
    CHECK(countOf("log level error (1), compiled up to info; tst error\r\n") == 1u);
    CHECK(countOf("log level error (1), compiled up to info; tst warning\r\n") == 1u);
    CHECK(countOf("log: nosuch: no such module\r\n") == 1u);
    CHECK(tst_log_level == LOG_INFO  &&  log_level == LOG_INFO);     /* `log info` set both */

    /* One prompt per line (26 lines) plus the first one; CRLF did not make an extra. */
    CHECK(countOf("nosh> ") == 27u);

    /* uptime: a fraction of a second has passed (one tick per idle loop), the tick and
       the rate are printed, and a reset cause word follows. */
    CHECK(strstr(_out, "up 0d 00:00:0") != NULL);
    CHECK(strstr(_out, " at 1000 Hz  reset ") != NULL);

    /* ps: the header, then idle, the partner blocked on the semaphore with a timeout,
       and the shell itself running; the free slot 3 is not listed. */
    CHECK(strstr(_out, " id name      st prio wait      left  stack      cpu ms  ovr\r\n") != NULL);
    CHECK(strstr(_out, "\r\n  0 idle      r  0    -            -          -  ") != NULL);
    CHECK(strstr(_out, "\r\n  1 partner   B  1    sem      59") != NULL);   /* ~59 s left */
    CHECK(strstr(_out, "   128/ 128  ") != NULL);                      /* partner, untouched */
    CHECK(strstr(_out, "\r\n  2 nosh      R  2    -            -   256/ 256       0    0\r\n") != NULL);
    CHECK(strstr(_out, "\r\n  3 ") == NULL);

    /* The 70 character line was echoed only up to the buffer's 63 characters. */
    CHECK(strstr(_out, "012345678901234567890123456789012345678901234567890123456789012\r\n") != NULL);
    CHECK(strstr(_out, "0123456789012345678901234567890123456789012345678901234567890123") == NULL);
}

/* ---- the kernel's hooks ------------------------------------------------------------- */

void test_idleHook(void)
{
    if(_spent  &&  ++_idleSinceSpent > 50)
    {
        checkOutput();
        if(_fails != 0  ||  getenv("NOSH_TEST_SHOW") != NULL)
            printf("---- shell output ----\n%s\n----------------------\n", _out);
        printf("%s: test_nosh (%u checks, %u failures, %lu idle loops)\n"
               , _fails == 0 ? "PASS" : "FAIL", _checks, _fails, (unsigned long)_idleLoops);
        exit(_fails == 0 ? 0 : 1);
    }
    if(++_idleLoops > 100000ul)
    {
        printf("FAIL: runaway, test did not finish (%u checks, %u failures)\n", _checks, _fails);
        exit(1);
    }
    rtos_host_tick();
}

void rtos_assertFailed(const char *file, uint16_t line)
{
    printf("ASSERTION FAILED: %s line %u (current task %u)\n", file, line, rtos_getCurrentTask());
    exit(2);
}

void rtos_panicHook(const rtos_panicInfo_t *pInfo)
{
    printf("PANIC reason %u task %u arg1 %lu arg2 %lu\n", pInfo->reason, pInfo->task
           , (unsigned long)pInfo->arg1, (unsigned long)pInfo->arg2);
    exit(3);
}

int main(void)
{
    nosh_io_t badIo = _io;

    rtos_init();
    rtos_semInit(&_sem, 0);
    rtos_heapInit(_heap, sizeof _heap);
    CHECK(rtos_malloc(100) != NULL);
    rtos_createTask(T_PARTNER, partner, NULL, _stackPartner, sizeof(_stackPartner));

    /* nosh_enable's checks: argument errors first, then the priority. */
    CHECK(nosh_enable(NULL, T_SHELL, _stackShell, sizeof(_stackShell)) == NOSH_ERR_ARG);
    badIo.read = NULL;
    CHECK(nosh_enable(&badIo, T_SHELL, _stackShell, sizeof(_stackShell)) == NOSH_ERR_ARG);
    CHECK(nosh_enable(&_io, T_SHELL, NULL, sizeof(_stackShell)) == NOSH_ERR_ARG);
    CHECK(nosh_enable(&_io, T_SHELL, _stackShell, 0) == NOSH_ERR_ARG);
    CHECK(nosh_enable(&_io, 0, _stackShell, sizeof(_stackShell)) == NOSH_ERR_PRIO);
    CHECK(nosh_enable(&_io, RTOS_CFG_NUM_TASKS + 1u, _stackShell, sizeof(_stackShell)) == NOSH_ERR_PRIO);
    CHECK(nosh_enable(&_io, T_PARTNER, _stackShell, sizeof(_stackShell)) == NOSH_ERR_PRIO);
    CHECK(nosh_enable(&_io, T_SHELL, _stackShell, sizeof(_stackShell)) == NOSH_OK);
    /* Slot 3 stays free: the kernel still has a slot nothing was created in. */
    {
        rtos_taskInfo_t info;
        CHECK(rtos_getTaskInfo(T_FREE, &info)  &&  info.state == RTOS_TASK_NONE);
        CHECK(rtos_getTaskInfo(T_SHELL, &info)  &&  info.state != RTOS_TASK_NONE);
    }

    nosh_add(&_cmdEcho);
    nosh_add(&_cmdSilent);
    nosh_add(&nosh_cmd_uptime);
    nosh_add(&nosh_cmd_ps);
    nosh_add(&nosh_cmd_mem);
    nosh_add(&nosh_cmd_sched);
    nosh_add(&nosh_cmd_log);
    log_setModules(_logModules, 1);
    nosh_setTaskNames(_names, sizeof _names / sizeof *_names);
    rtos_start();
    return 0;
}
