/**
 * @file test_nosh_run.c
 * `run`, `stop` and `wait` on the host port with RTOS_CFG_DYNAMIC_TASKS (tests/run/
 * rtos_config.h): a scripted console drives the shell, and the programs count what
 * happened to them. Two programs: `work` (slot 3) loops until nosh_stopRequested() and
 * ends by returning on odd runs and with rtos_exit() on even ones; `brief` (slot 4) ends
 * by itself after a tick, so that `run brief` on the terminated slot has to reap it
 * first. Checks the messages of every path (usage, unknown program, slot busy, by id, by
 * program name, by task name, timeout), that the stop flag is clear at every start, that
 * `ps` names the program's task after the program, and the run counts.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rtos.h"
#include "nosh.h"

#define T_PARTNER   1u
#define T_SHELL     2u
#define T_WORK      3u
#define T_BRIEF     4u

static uint8_t _stackPartner[128], _stackShell[256], _stackWork[128], _stackBrief[128];
static const char *const _names[] = { "idle", "partner", "nosh" };   /* 3 and 4: the programs */
static rtos_sem_t _sem;
static rtos_mutex_t _mtx;               /* held by partner for ever: stop notes it */
static unsigned _checks, _fails;
static uint32_t _idleLoops, _idleSinceSpent;
static volatile unsigned _workRuns, _workLoops, _workReturns, _workExits, _briefRuns, _errors;

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

/* ---- the programs ---------------------------------------------------------------------- */

static void work(void *arg)
{
    (void)arg;
    ++ _workRuns;
    if(nosh_stopRequested())
        ++ _errors;                     /* the flag must be clear at a start */
    while(!nosh_stopRequested())
    {
        rtos_delay(2);
        ++ _workLoops;
    }
    if(_workRuns & 1u)
    {
        ++ _workReturns;
        return;
    }
    ++ _workExits;
    rtos_exit();
}

static void brief(void *arg)
{
    (void)arg;
    ++ _briefRuns;
    rtos_delay(1);
}

static const nosh_program_t _programs[] =
{
    { "work",  T_WORK,  work,  NULL, _stackWork,  sizeof _stackWork },
    { "brief", T_BRIEF, brief, NULL, _stackBrief, sizeof _stackBrief },
};

/* ---- the scripted transport ------------------------------------------------------------ */

static const char _script[] =
    "run\r"                             /* usage */
    "run nosuch\r"                      /* unknown program */
    "run work\r"                        /* started, run 1 */
    "run work\r"                        /* its slot is busy: blocked in its delay */
    "ps\r"                              /* the task is named after the program */
    "stop work\r"                       /* requested; work returns within two ticks */
    "wait work\r"                       /* ended, reaped */
    "wait work\r"                       /* nothing there any more */
    "stop work\r"                       /* same */
    "run brief\r"                       /* started, ends by itself */
    "run brief\r"                       /* the slot is terminated: reaped, started again */
    "wait brief\r"                      /* ended */
    "stop 9\r"                          /* no such task */
    "wait nosuch\r"                     /* no such task */
    "stop partner\r"                    /* a task by its name: the request is recorded */
    "stop\r"                            /* usage */
    "wait\r"                            /* usage */
    "wait work 1 2\r"                   /* usage */
    "wait work x\r"                     /* usage */
    "run work\r"                        /* run 2: ends with rtos_exit */
    "wait work 20\r"                    /* still blocked after the timeout */
    "stop 3\r"                          /* by id */
    "wait work\r"                       /* ended */
    "run work\r"                        /* run 3 */
    "restart\r"                         /* usage */
    "restart nosuch\r"                  /* unknown program */
    "restart work\r"                    /* run 3 stopped, waited for, reaped; run 4 started */
    "restart brief\r"                   /* the slot is empty: just started */
    "sched\r";                          /* the switch count (RTOS_CFG_RUNTIME_STATS 2) */

static size_t _scriptPos = 0;
static bool _spent = false;
static char _out[8192];
static size_t _outLen = 0;

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

static void partner(void *arg)
{
    (void)arg;
    rtos_mutexLock(&_mtx);
    for(;;)
        (void)rtos_semTake(&_sem, 60000);
}

/* ---- checks on the collected output ---------------------------------------------------- */

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
    rtos_taskInfo_t info;

    CHECK(countOf("usage: run <program>\r\n") == 1u);
    CHECK(countOf("run: nosuch: no such program\r\n") == 1u);
    CHECK(countOf("task 3 (work) started\r\n") == 4u);
    CHECK(countOf("task 3 (work) is blocked\r\n") == 1u);
    CHECK(strstr(_out, "\r\n  3 work      B  3 ") != NULL);              /* ps: named after the program */
    CHECK(countOf("stop requested for task 3 (work)\r\n") == 2u);
    CHECK(countOf("task 3 (work) ended\r\n") == 3u);
    CHECK(countOf("task 3 (work) is not running\r\n") == 2u);           /* wait and stop on the empty slot */
    CHECK(countOf("task 4 (brief) started\r\n") == 3u);
    CHECK(countOf("task 4 (brief) ended\r\n") == 1u);
    CHECK(countOf("9: no such task\r\n") == 1u);
    CHECK(countOf("nosuch: no such task\r\n") == 1u);
    CHECK(countOf("stop requested for task 1 (partner); holds 1 mutex, ends when released\r\n") == 1u);
    CHECK(countOf("usage: restart <program> [ms]\r\n") == 1u);
    CHECK(countOf("restart: nosuch: no such program\r\n") == 1u);
    CHECK(countOf("; switches ") == 1u  &&  countOf("; switches -") == 0u);   /* a number */
    CHECK(countOf("usage: stop <program|task>\r\n") == 1u);
    CHECK(countOf("usage: wait <program|task> [ms]\r\n") == 3u);
    CHECK(countOf("task 3 (work) still blocked after 20 ms\r\n") == 1u);

    /* The programs' own view. */
    CHECK(_workRuns == 4u);
    CHECK(_workReturns == 2u  &&  _workExits == 1u);                  /* runs 1 and 3 returned, run 2 exited */
    CHECK(_workLoops >= 3u);
    CHECK(_briefRuns == 3u);
    CHECK(_errors == 0u);                                             /* the stop flag was clear at every start */
    CHECK(rtos_getTaskInfo(T_WORK, &info)  &&  info.state == RTOS_TASK_BLOCKED);   /* run 4 is still going */
    CHECK(rtos_getTaskInfo(T_BRIEF, &info)  &&  info.state == RTOS_TASK_TERMINATED); /* run 3 ended, not reaped */

    CHECK(countOf("nosh> ") == 30u);                                  /* 29 lines plus the first prompt */
}

/* ---- kernel hooks and main ------------------------------------------------------------ */

void test_idleHook(void)
{
    if(_spent  &&  ++_idleSinceSpent > 50)
    {
        checkOutput();
        if(_fails != 0  ||  getenv("NOSH_TEST_SHOW") != NULL)
            printf("---- shell output ----\n%s\n----------------------\n", _out);
        printf("%s: test_nosh_run (%u checks, %u failures, %lu idle loops)\n"
               , _fails == 0 ? "PASS" : "FAIL", _checks, _fails, (unsigned long)_idleLoops);
        exit(_fails == 0 ? 0 : 1);
    }
    if(++_idleLoops > 200000ul)
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
    rtos_init();
    rtos_semInit(&_sem, 0);
    rtos_mutexInit(&_mtx, T_PARTNER);
    rtos_createTask(T_PARTNER, partner, NULL, _stackPartner, sizeof(_stackPartner));
    CHECK(nosh_enable(&_io, T_SHELL, _stackShell, sizeof(_stackShell)) == NOSH_OK);
    nosh_add(&nosh_cmd_ps);
    nosh_add(&nosh_cmd_run);
    nosh_add(&nosh_cmd_stop);
    nosh_add(&nosh_cmd_wait);
    nosh_add(&nosh_cmd_restart);
    nosh_add(&nosh_cmd_sched);
    nosh_setTaskNames(_names, sizeof _names / sizeof *_names);
    nosh_setPrograms(_programs, sizeof _programs / sizeof *_programs);
    CHECK(nosh_programByName("work") == &_programs[0]);
    CHECK(nosh_programByName("brief") == &_programs[1]);
    CHECK(nosh_programByName("nosuch") == NULL);
    CHECK(nosh_taskName(T_WORK) != NULL  &&  strcmp(nosh_taskName(T_WORK), "work") == 0);
    CHECK(nosh_taskName(T_PARTNER) != NULL  &&  strcmp(nosh_taskName(T_PARTNER), "partner") == 0);
    rtos_start();
    return 0;
}
