/**
 * @file nosh_cmd_ps.c
 * `ps`: one line per task from rtos_getTaskInfo(), the idle task included.
 *
 *      id name      st prio wait      left  stack      cpu ms  ovr
 *       0 idle      r  0    -            -          -     812    0
 *       1 nosh      R  1    -            -  1024/ 660       9    0
 *       2 blink     B  2    period     312  1024/ 872       3    0
 *
 *   st     R running, r ready, B blocked, S suspended, T terminated (its function returned)
 *   prio   the task id; "1>3" when a mutex ceiling raises it
 *   wait   what a blocked task waits for (RTOS_WAIT_*), "-" otherwise
 *   left   ticks until the timeout of that wait, "-" when it waits for good
 *   stack  size/untouched bytes; "-" without RTOS_CFG_STACK_CHECK
 *   cpu ms run time charged to the task; "-" without RTOS_CFG_RUNTIME_STATS
 *   ovr    rtos_waitPeriodic overruns (saturating)
 *
 * Every line is one snapshot of one task, taken inside rtos_getTaskInfo()'s critical
 * section and printed afterwards; the lines are not a consistent snapshot of the whole
 * system, which keeps interrupts off for a few cycles only.
 */

#include "nosh.h"

static void putWaitKind(uint8_t kind)
{
    switch(kind)
    {
    case RTOS_WAIT_DELAY:       nosh_putPadP(NOSH_STR("delay"), 8);   break;
    case RTOS_WAIT_PERIODIC:    nosh_putPadP(NOSH_STR("period"), 8);  break;
    case RTOS_WAIT_EVENTS:      nosh_putPadP(NOSH_STR("events"), 8);  break;
    case RTOS_WAIT_SEM:         nosh_putPadP(NOSH_STR("sem"), 8);     break;
    case RTOS_WAIT_MUTEX:       nosh_putPadP(NOSH_STR("mutex"), 8);   break;
    case RTOS_WAIT_QUEUE_SEND:  nosh_putPadP(NOSH_STR("qsend"), 8);   break;
    case RTOS_WAIT_QUEUE_RECV:  nosh_putPadP(NOSH_STR("qrecv"), 8);   break;
    case RTOS_WAIT_MBOX:        nosh_putPadP(NOSH_STR("mbox"), 8);    break;
    case RTOS_WAIT_POOL:        nosh_putPadP(NOSH_STR("pool"), 8);    break;
    case RTOS_WAIT_NOTIFY:      nosh_putPadP(NOSH_STR("notify"), 8);  break;
    case RTOS_WAIT_EVGROUP:     nosh_putPadP(NOSH_STR("evgroup"), 8); break;
    case RTOS_WAIT_STREAM_READ: nosh_putPadP(NOSH_STR("sread"), 8);   break;
    case RTOS_WAIT_STREAM_WRITE: nosh_putPadP(NOSH_STR("swrite"), 8); break;
    default:                    nosh_putPadP(NOSH_STR("-"), 8);       break;
    }
}


static char stateLetter(uint8_t state)
{
    switch(state)
    {
    case RTOS_TASK_RUNNING:     return 'R';
    case RTOS_TASK_READY:       return 'r';
    case RTOS_TASK_BLOCKED:     return 'B';
    case RTOS_TASK_SUSPENDED:   return 'S';
    case RTOS_TASK_TERMINATED:  return 'T';
    default:                    return '?';
    }
}


static void cmdPs(uint8_t argc, const char *const argv[])
{
    uint8_t id;
    (void)argc; (void)argv;

    nosh_putsP(NOSH_STR(" id name      st prio wait      left  stack      cpu ms  ovr"));
#if RTOS_CFG_SMP
    nosh_putsP(NOSH_STR(" core"));                /* the core a running task is on */
#endif
    nosh_putEol();
    for(id = 0; id <= RTOS_CFG_NUM_TASKS; ++id)
    {
        rtos_taskInfo_t info;
        const char *name;
        const rtos_tick_t now = rtos_getTick();
        uint8_t n;

        if(!rtos_getTaskInfo(id, &info)  ||  info.state == RTOS_TASK_NONE)
            continue;

        nosh_putU32Pad(id, 3);
        nosh_putc(' ');
        name = nosh_taskName(id);
        if(name != NULL)
            nosh_putPad(name, 10);
        else if(id == 0)
            nosh_putPadP(NOSH_STR("idle"), 10);
        else
            nosh_putPadP(NOSH_STR("-"), 10);

        nosh_putc(stateLetter(info.state));
        nosh_putsP(NOSH_STR("  "));

        /* prio, or prio>effective, in 5 columns */
        nosh_putU32(info.prio);
        n = info.prio >= 10u ? 2u : 1u;
        if(info.effPrio != info.prio)
        {
            nosh_putc('>');
            nosh_putU32(info.effPrio);
            n = (uint8_t)(n + (info.effPrio >= 10u ? 3u : 2u));
        }
        while(n++ < 5u)
            nosh_putc(' ');

        putWaitKind(info.waitKind);
        if(info.hasTimeout)
        {
            /* Signed difference: the counter may wrap, and a wake-up that is due reads
               as 0, not as a huge number. */
            const rtos_tick_t left = (rtos_tick_t)(info.wakeAt - now);
            nosh_putU32Pad(left > RTOS_MAX_TIMEOUT ? 0u : (uint32_t)left, 6);
        }
        else
            nosh_putsP(NOSH_STR("     -"));
        nosh_putsP(NOSH_STR("  "));

        if(info.stackSize != 0)
        {
            nosh_putU32Pad((uint32_t)info.stackSize, 4);
            nosh_putc('/');
            nosh_putU32Pad((uint32_t)info.stackFree, 4);
        }
        else
            nosh_putPadP(NOSH_STR("        -"), 9);
        nosh_putsP(NOSH_STR("  "));

#if RTOS_CFG_RUNTIME_STATS
        nosh_putU32Pad(nosh_runtimeMs(info.runTime), 6);
#else
        nosh_putsP(NOSH_STR("     -"));
#endif
        nosh_putU32Pad(info.overruns, 5);
#if RTOS_CFG_SMP
        if(info.core != RTOS_NO_CORE)
            nosh_putU32Pad(info.core, 5);
        else
            nosh_putsP(NOSH_STR("    -"));
#endif
        nosh_putEol();
    }
}

NOSH_CMD(nosh_cmd_ps, "ps", "list the tasks", cmdPs);
