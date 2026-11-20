/**
 * @file nosh_cmd_run.c
 * `run`, `stop`, `wait` and `restart`: programs started and ended from the console, on nornCore's
 * dynamic task API (RTOS_CFG_DYNAMIC_TASKS). A program is a task the application did not
 * create in main(): a row in its nosh_program_t table with a name, the slot (priority),
 * the function and a stack the application owns.
 *
 *     nosh> run work
 *     task 3 (work) started
 *     nosh> stop work
 *     stop requested for task 3
 *     nosh> wait work
 *     task 3 (work) ended
 *     nosh> restart work
 *     task 3 (work) ended
 *     task 3 (work) started
 *
 * `stop` is cooperative: it raises a flag the program reads with nosh_stopRequested()
 * and ends itself, by returning or with rtos_exit(), at a point where it holds nothing.
 * Nothing is killed from outside (the kernel has no such call, on purpose). A task that
 * owns a mutex when stop is requested gets a note in the reply: it cannot end before it
 * has released it (rtos_exit checks). `wait` polls for the end, with a timeout (ms,
 * default 10 s), and reaps the slot, which makes the stack and the slot free for the
 * next `run`; `run` on a slot whose task has ended reaps it itself. `restart` is stop,
 * wait and run in one, for a program whose slot is busy; on an empty or ended slot it is
 * run. The program's name is what `ps` shows for the task unless the application's task
 * name table has one.
 *
 * Only with RTOS_CFG_DYNAMIC_TASKS; without it nosh_add(&nosh_cmd_run) fails to link with
 * the symbol nosh_cmd_run_needs_RTOS_CFG_DYNAMIC_TASKS (nosh.h), since a command that
 * can do nothing at all should not be registered.
 */

#include "nosh.h"
#include "nosh_internal.h"

#if RTOS_CFG_DYNAMIC_TASKS

#define WAIT_DEFAULT_MS     10000ul
#define WAIT_POLL_MS        10u

/** One bit per task id: a stop was requested. Set under a critical section, read by the
    program itself. */
static rtos_taskmask_t _stopMask;


bool nosh_stopRequested(void)
{
    return (_stopMask & (rtos_taskmask_t)((rtos_taskmask_t)1u << rtos_getCurrentTask())) != 0;
}

static void setStop(uint8_t id, bool on)
{
    const rtos_taskmask_t bit = (rtos_taskmask_t)((rtos_taskmask_t)1u << id);
    const rtos_cs_t cs = rtos_enterCritical();
    if(on)
        _stopMask = (rtos_taskmask_t)(_stopMask | bit);
    else
        _stopMask = (rtos_taskmask_t)(_stopMask & (rtos_taskmask_t)~bit);
    rtos_exitCritical(cs);
}


/* ---- output helpers ---------------------------------------------------------------- */

static void putTask(uint8_t id)
{
    const char *name = nosh_taskName(id);
    nosh_putsP(NOSH_STR("task "));
    nosh_putU32(id);
    if(name != NULL)
    {
        nosh_putsP(NOSH_STR(" ("));
        nosh_puts(name);
        nosh_putc(')');
    }
}

static void putState(uint8_t state)
{
    switch(state)
    {
    case RTOS_TASK_RUNNING:
    case RTOS_TASK_READY:       nosh_putsP(NOSH_STR("running"));    break;
    case RTOS_TASK_BLOCKED:     nosh_putsP(NOSH_STR("blocked"));    break;
    case RTOS_TASK_SUSPENDED:   nosh_putsP(NOSH_STR("suspended"));  break;
    case RTOS_TASK_TERMINATED:  nosh_putsP(NOSH_STR("ended"));      break;
    default:                    nosh_putsP(NOSH_STR("not running")); break;
    }
}

/** A task id from a number, a program name or a task name; RTOS_NO_TASK with a message
    if nothing matches. */
static uint8_t resolve(const char *s)
{
    uint32_t n;
    const nosh_program_t *pProg;
    uint8_t id;

    if(nosh_parseU32(s, &n))
    {
        if(n >= 1u  &&  n <= RTOS_CFG_NUM_TASKS)
            return (uint8_t)n;
    }
    else
    {
        pProg = nosh_programByName(s);
        if(pProg != NULL)
            return pProg->prio;
        for(id = 1; id <= RTOS_CFG_NUM_TASKS; ++id)
        {
            const char *name = nosh_taskName(id);
            if(name != NULL  &&  nosh_strEq(name, s))
                return id;
        }
    }
    nosh_puts(s);
    nosh_putsP(NOSH_STR(": no such task"));
    nosh_putEol();
    return RTOS_NO_TASK;
}

/** "; holds N mutex, ends when released" when the task owns one (RTOS_CFG_DYNAMIC_TASKS
    counts them): a stop cannot take effect before that. */
static void putHeld(uint8_t held)
{
    if(held == 0)
        return;
    nosh_putsP(NOSH_STR("; holds "));
    nosh_putU32(held);
    nosh_putsP(NOSH_STR(" mutex, ends when released"));
}

static void putErr(const char *what, rtos_err_t e)
{
    nosh_putsP(what);
    nosh_putsP(NOSH_STR(": "));
    nosh_puts(rtos_errName(e));
    nosh_putEol();
}


/* ---- run ----------------------------------------------------------------------------- */

/** The program named \a s, or NULL after a message. */
static const nosh_program_t *program(const char *cmd, const char *s)
{
    const nosh_program_t * const pProg = nosh_programByName(s);
    if(pProg == NULL)
    {
        nosh_putsP(cmd);
        nosh_putsP(NOSH_STR(": "));
        nosh_puts(s);
        nosh_putsP(NOSH_STR(": no such program"));
        nosh_putEol();
    }
    return pProg;
}

/** Reap the slot if the previous run ended, then start the program; a message either way.
    The slot must be empty or ended; a running slot is reported and left alone. */
static void startProgram(const char *cmd, const nosh_program_t *pProg)
{
    rtos_taskInfo_t info;
    rtos_err_t e;

    if(rtos_getTaskInfo(pProg->prio, &info)  &&  info.state == RTOS_TASK_TERMINATED)
    {
        e = rtos_reapTask(pProg->prio);         /* the previous run ended: free the slot */
        if(e != RTOS_OK)
        {
            putErr(cmd, e);
            return;
        }
    }
    else if(info.state != RTOS_TASK_NONE)
    {
        putTask(pProg->prio);
        nosh_putsP(NOSH_STR(" is "));
        putState(info.state);
        nosh_putEol();
        return;
    }
    setStop(pProg->prio, false);
    e = rtos_startTask(pProg->prio, pProg->fn, pProg->arg, pProg->stack, pProg->stackSize);
    if(e != RTOS_OK)
    {
        putErr(cmd, e);
        return;
    }
    putTask(pProg->prio);
    nosh_putsP(NOSH_STR(" started"));
    nosh_putEol();
}

static void cmdRun(uint8_t argc, const char *const argv[])
{
    const nosh_program_t *pProg;

    if(argc != 2)
    {
        nosh_putsP(NOSH_STR("usage: run <program>"));
        nosh_putEol();
        return;
    }
    pProg = program(NOSH_STR("run"), argv[1]);
    if(pProg != NULL)
        startProgram(NOSH_STR("run"), pProg);
}


/* ---- stop ---------------------------------------------------------------------------- */

static void cmdStop(uint8_t argc, const char *const argv[])
{
    rtos_taskInfo_t info;
    uint8_t id;

    if(argc != 2)
    {
        nosh_putsP(NOSH_STR("usage: stop <program|task>"));
        nosh_putEol();
        return;
    }
    id = resolve(argv[1]);
    if(id == RTOS_NO_TASK)
        return;
    (void)rtos_getTaskInfo(id, &info);
    if(info.state == RTOS_TASK_NONE  ||  info.state == RTOS_TASK_TERMINATED)
    {
        putTask(id);
        nosh_putsP(NOSH_STR(" is "));
        putState(info.state);
        nosh_putEol();
        return;
    }
    setStop(id, true);
    nosh_putsP(NOSH_STR("stop requested for "));
    putTask(id);
    putHeld(info.mutexHeld);
    nosh_putEol();
}


/* ---- wait ---------------------------------------------------------------------------- */

/** Poll until the task has ended or is gone, at most \a ms; reap an ended task and clear
    its stop flag. Prints one line; true if the slot is free afterwards. */
static bool waitEnd(const char *cmd, uint8_t id, uint32_t ms)
{
    rtos_taskInfo_t info;
    uint32_t waited = 0;

    for(;;)
    {
        (void)rtos_getTaskInfo(id, &info);
        if(info.state == RTOS_TASK_NONE  ||  info.state == RTOS_TASK_TERMINATED  ||  waited >= ms)
            break;
        rtos_delay(RTOS_MS(WAIT_POLL_MS));
        waited += WAIT_POLL_MS;
    }
    if(info.state == RTOS_TASK_TERMINATED)
    {
        const rtos_err_t e = rtos_reapTask(id);
        if(e != RTOS_OK)
        {
            putErr(cmd, e);
            return false;
        }
        setStop(id, false);
        putTask(id);
        nosh_putsP(NOSH_STR(" ended"));
    }
    else if(info.state == RTOS_TASK_NONE)
    {
        putTask(id);
        nosh_putsP(NOSH_STR(" is not running"));
    }
    else
    {
        putTask(id);
        nosh_putsP(NOSH_STR(" still "));
        putState(info.state);
        nosh_putsP(NOSH_STR(" after "));
        nosh_putU32(waited);
        nosh_putsP(NOSH_STR(" ms"));
        putHeld(info.mutexHeld);
    }
    nosh_putEol();
    return info.state != RTOS_TASK_BLOCKED  &&  info.state != RTOS_TASK_READY
           &&  info.state != RTOS_TASK_RUNNING  &&  info.state != RTOS_TASK_SUSPENDED;
}

static void cmdWait(uint8_t argc, const char *const argv[])
{
    uint32_t ms = WAIT_DEFAULT_MS;
    uint8_t id;

    if(argc < 2  ||  argc > 3  ||  (argc == 3  &&  !nosh_parseU32(argv[2], &ms)))
    {
        nosh_putsP(NOSH_STR("usage: wait <program|task> [ms]"));
        nosh_putEol();
        return;
    }
    id = resolve(argv[1]);
    if(id != RTOS_NO_TASK)
        (void)waitEnd(NOSH_STR("wait: reap"), id, ms);
}


/* ---- restart ------------------------------------------------------------------------- */

static void cmdRestart(uint8_t argc, const char *const argv[])
{
    const nosh_program_t *pProg;
    rtos_taskInfo_t info;
    uint32_t ms = WAIT_DEFAULT_MS;

    if(argc < 2  ||  argc > 3  ||  (argc == 3  &&  !nosh_parseU32(argv[2], &ms)))
    {
        nosh_putsP(NOSH_STR("usage: restart <program> [ms]"));
        nosh_putEol();
        return;
    }
    pProg = program(NOSH_STR("restart"), argv[1]);
    if(pProg == NULL)
        return;
    (void)rtos_getTaskInfo(pProg->prio, &info);
    if(info.state != RTOS_TASK_NONE  &&  info.state != RTOS_TASK_TERMINATED)
    {
        setStop(pProg->prio, true);
        if(!waitEnd(NOSH_STR("restart: reap"), pProg->prio, ms))
            return;                             /* still running: it says so; no new run */
    }
    startProgram(NOSH_STR("restart"), pProg);
}


/* ---- the four commands ----------------------------------------------------------------- */

NOSH_CMD(nosh_cmd_run,  "run",  "start a program (run <program>)", cmdRun);
NOSH_CMD(nosh_cmd_stop, "stop", "ask a program to end (stop <program|task>)", cmdStop);
NOSH_CMD(nosh_cmd_wait, "wait", "wait for a program to end, free its slot (wait <program|task> [ms])", cmdWait);
NOSH_CMD(nosh_cmd_restart, "restart", "stop, wait and run a program (restart <program> [ms])", cmdRestart);

#else
typedef int nosh_cmd_run_unused_t;
#endif  /* RTOS_CFG_DYNAMIC_TASKS */
