/**
 * @file nosh_cmd_mem.c
 * `mem`: the memory the kernel knows about. The stacks of every created task (sizes,
 * untouched bytes, and the task with the least margin, which is the one to watch) and
 * the heap's statistics when RTOS_CFG_HEAP is on.
 *
 *     stacks  3 tasks 2304 B, 1492 untouched, least margin 596 B in 1 (nosh)
 *     heap    total 4096 free 3000 min 2800 largest 2900 allocs 12 frees 10 failed 0
 *
 * Static data and the idle stack are the application's and the port's: the kernel has no
 * view of them, so neither has nosh.
 */

#include "nosh.h"

static void cmdMem(uint8_t argc, const char *const argv[])
{
    (void)argc; (void)argv;

    nosh_putsP(NOSH_STR("stacks  "));
#if RTOS_CFG_STACK_CHECK
    {
        uint8_t id, tasks = 0, leastId = 0;
        uint32_t total = 0, untouched = 0, least = UINT32_MAX;

        for(id = 1; id <= RTOS_CFG_NUM_TASKS; ++id)
        {
            rtos_taskInfo_t info;
            if(!rtos_getTaskInfo(id, &info)  ||  info.state == RTOS_TASK_NONE)
                continue;
            ++ tasks;
            total += (uint32_t)info.stackSize;
            untouched += (uint32_t)info.stackFree;
            if((uint32_t)info.stackFree < least)
            {
                least = (uint32_t)info.stackFree;
                leastId = id;
            }
        }
        nosh_putU32(tasks);
        nosh_putsP(NOSH_STR(" tasks "));
        nosh_putU32(total);
        nosh_putsP(NOSH_STR(" B, "));
        nosh_putU32(untouched);
        nosh_putsP(NOSH_STR(" untouched"));
        if(tasks > 0)
        {
            const char *name = nosh_taskName(leastId);
            nosh_putsP(NOSH_STR(", least margin "));
            nosh_putU32(least);
            nosh_putsP(NOSH_STR(" B in "));
            nosh_putU32(leastId);
            if(name != NULL)
            {
                nosh_putsP(NOSH_STR(" ("));
                nosh_puts(name);
                nosh_putc(')');
            }
        }
    }
#else
    nosh_putsP(NOSH_STR("- (RTOS_CFG_STACK_CHECK 0)"));
#endif
    nosh_putEol();

    nosh_putsP(NOSH_STR("heap    "));
#if RTOS_CFG_HEAP
    {
        rtos_heapStats_t h;
        rtos_heapStats(&h);
        nosh_putsP(NOSH_STR("total "));   nosh_putU32((uint32_t)h.total);
        nosh_putsP(NOSH_STR(" free "));   nosh_putU32((uint32_t)h.free);
        nosh_putsP(NOSH_STR(" min "));    nosh_putU32((uint32_t)h.minFree);
        nosh_putsP(NOSH_STR(" largest ")); nosh_putU32((uint32_t)h.largestFree);
        nosh_putsP(NOSH_STR(" allocs ")); nosh_putU32(h.allocs);
        nosh_putsP(NOSH_STR(" frees "));  nosh_putU32(h.frees);
        nosh_putsP(NOSH_STR(" failed ")); nosh_putU32(h.failed);
    }
#else
    nosh_putsP(NOSH_STR("- (RTOS_CFG_HEAP 0)"));
#endif
    nosh_putEol();
}

NOSH_CMD(nosh_cmd_mem, "mem", "stacks and heap", cmdMem);
