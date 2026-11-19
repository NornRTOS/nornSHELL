/**
 * @file nosh_default.c
 * The default command set: nosh_enableDefault() is nosh_enable() plus `uptime`, `ps`,
 * `mem` and `sched`. In its own file so that an application that picks its commands one
 * by one does not link these four.
 */

#include "nosh.h"

int nosh_enableDefault(const nosh_io_t *pIo, uint8_t prio, void *pStack, size_t stackSize)
{
    const int res = nosh_enable(pIo, prio, pStack, stackSize);
    if(res == NOSH_OK)
    {
        nosh_add(&nosh_cmd_uptime);
        nosh_add(&nosh_cmd_ps);
        nosh_add(&nosh_cmd_mem);
        nosh_add(&nosh_cmd_sched);
    }
    return res;
}
