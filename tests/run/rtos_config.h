#ifndef RTOS_CONFIG_INCLUDED
#define RTOS_CONFIG_INCLUDED
/* Kernel configuration of test_nosh_run: the shell at 2, a partner at 1, slots 3 and 4 for the
   programs; the dynamic task API on. */

#define RTOS_CFG_NUM_TASKS      4
#define RTOS_CFG_STACK_CHECK    1
#define RTOS_CFG_DYNAMIC_TASKS  1
#define RTOS_CFG_RUNTIME_STATS  2       /* sched shows the switch count */
#define RTOS_CFG_IDLE_HOOK      test_idleHook

void test_idleHook(void);

#endif  /* RTOS_CONFIG_INCLUDED */
