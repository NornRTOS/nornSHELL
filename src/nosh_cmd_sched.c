/**
 * @file nosh_cmd_sched.c
 * `sched`: the scheduler as a whole. CPU load since the previous `sched` (the first one
 * since start), how many tasks are in each state, the tick, where timer callbacks run,
 * and the last panic if the kernel recorded one (a panic record survives a reset).
 *
 *     load    12 % since last sched
 *     tasks   3 of 4 slots: 1 running 1 ready 1 blocked 0 suspended 0 terminated
 *     tick    1000 Hz, 32 bit; timers in the tick; switches 4711
 *     panic   none
 *
 * switches is the sum of the tasks' switch counts (16 bit each, so it wraps) with
 * RTOS_CFG_RUNTIME_STATS 2, `-` below that. An RTOS_CFG_SMP build adds the load of each
 * core after the total (`load 40 % since last sched; cores 90 70 0 0 %`), and ps a `core`
 * column.
 *
 * rtos_getCpuLoad() starts a new interval at every call, so an application that reads
 * the load itself should not register this command.
 */

#include "nosh.h"

static void putCount(const char *pLabel, uint8_t n)
{
    nosh_putc(' ');
    nosh_putU32(n);
    nosh_putc(' ');
    nosh_putsP(pLabel);
}


static void cmdSched(uint8_t argc, const char *const argv[])
{
    uint8_t id, used = 0, running = 0, ready = 0, blocked = 0, suspended = 0, terminated = 0;
#if RTOS_CFG_RUNTIME_STATS >= 2
    uint32_t switches = 0;
#endif
    rtos_panicInfo_t panic;
    (void)argc; (void)argv;

    nosh_putsP(NOSH_STR("load    "));
#if RTOS_CFG_RUNTIME_STATS
    nosh_putU32(rtos_getCpuLoad());
    nosh_putsP(NOSH_STR(" % since last sched"));
#if RTOS_CFG_SMP
    {
        uint8_t c;                              /* all cores together, then each one */
        nosh_putsP(NOSH_STR("; cores"));
        for(c = 0; c < RTOS_PORT_CORES; ++c)
        {
            nosh_putc(' ');
            nosh_putU32(rtos_getCoreLoad(c));
        }
        nosh_putsP(NOSH_STR(" %"));
    }
#endif
#else
    nosh_putsP(NOSH_STR("- (RTOS_CFG_RUNTIME_STATS 0)"));
#endif
    nosh_putEol();

    for(id = 1; id <= RTOS_CFG_NUM_TASKS; ++id)
    {
        rtos_taskInfo_t info;
        if(!rtos_getTaskInfo(id, &info)  ||  info.state == RTOS_TASK_NONE)
            continue;
        ++ used;
#if RTOS_CFG_RUNTIME_STATS >= 2
        switches += info.switches;
#endif
        switch(info.state)
        {
        case RTOS_TASK_RUNNING:     ++ running; break;
        case RTOS_TASK_READY:       ++ ready; break;
        case RTOS_TASK_BLOCKED:     ++ blocked; break;
        case RTOS_TASK_SUSPENDED:   ++ suspended; break;
        default:                    ++ terminated; break;
        }
    }
    nosh_putsP(NOSH_STR("tasks   "));
    nosh_putU32(used);
    nosh_putsP(NOSH_STR(" of "));
    nosh_putU32(RTOS_CFG_NUM_TASKS);
    nosh_putsP(NOSH_STR(" slots:"));
    putCount(NOSH_STR("running"), running);
    putCount(NOSH_STR("ready"), ready);
    putCount(NOSH_STR("blocked"), blocked);
    putCount(NOSH_STR("suspended"), suspended);
    putCount(NOSH_STR("terminated"), terminated);
    nosh_putEol();

    nosh_putsP(NOSH_STR("tick    "));
    nosh_putU32((uint32_t)RTOS_CFG_TICK_HZ);
    nosh_putsP(NOSH_STR(" Hz, "));
    nosh_putU32(RTOS_CFG_TICK_BITS);
    nosh_putsP(NOSH_STR(" bit; timers in "));
#if RTOS_CFG_TIMER_TASK_PRIO > 0
    nosh_putsP(NOSH_STR("task "));
    nosh_putU32(RTOS_CFG_TIMER_TASK_PRIO);
#else
    nosh_putsP(NOSH_STR("the tick"));
#endif
#if RTOS_CFG_RUNTIME_STATS >= 2
    {
        rtos_taskInfo_t info;
        if(rtos_getTaskInfo(RTOS_IDLE_TASK, &info))
            switches += info.switches;
    }
    nosh_putsP(NOSH_STR("; switches "));
    nosh_putU32(switches);                          /* the sum of the tasks' 16 bit counts */
#else
    nosh_putsP(NOSH_STR("; switches -"));           /* RTOS_CFG_RUNTIME_STATS 2 counts them */
#endif
#if RTOS_CFG_RUNTIME_STATS >= 2
    nosh_putsP(NOSH_STR("; mutex contended "));     /* 0 in a correct ceiling configuration */
    nosh_putU32(rtos_getMutexContended());
#endif
    nosh_putEol();

    nosh_putsP(NOSH_STR("panic   "));
    if(rtos_getLastPanic(&panic)  &&  panic.reason != RTOS_PANIC_NONE)
    {
        nosh_putsP(NOSH_STR("reason "));
        nosh_putU32(panic.reason);
        nosh_putsP(NOSH_STR(" task "));
        nosh_putU32(panic.task);
        nosh_putsP(NOSH_STR(" tick "));
        nosh_putU32((uint32_t)panic.tick);
        nosh_putsP(NOSH_STR(" arg1 "));
        nosh_putU32(panic.arg1);
        nosh_putsP(NOSH_STR(" arg2 "));
        nosh_putU32(panic.arg2);
        if(panic.reason == RTOS_PANIC_ASSERT)
        {
            nosh_putsP(NOSH_STR(" line "));
            nosh_putU32(panic.line);
        }
    }
    else
        nosh_putsP(NOSH_STR("none"));
    nosh_putEol();
}

NOSH_CMD(nosh_cmd_sched, "sched", "load, task states, tick, last panic", cmdSched);
