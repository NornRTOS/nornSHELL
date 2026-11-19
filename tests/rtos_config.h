#ifndef RTOS_CONFIG_INCLUDED
#define RTOS_CONFIG_INCLUDED
/* Kernel configuration of the nosh host tests: three tasks (a partner, the shell, and
   one slot that stays free or is taken to test nosh_enable). Stack check and run time
   statistics on, so that `ps` has every column to show. */

#define RTOS_CFG_NUM_TASKS      3
#define RTOS_CFG_TICK_HZ        1000ul
#define RTOS_CFG_TICK_BITS      32
#define RTOS_CFG_EVT_BITS       16
#define RTOS_CFG_STACK_CHECK    1
#define RTOS_CFG_RUNTIME_STATS  1
#define RTOS_CFG_HEAP           1
#define RTOS_CFG_IDLE_SLEEP     1
#define RTOS_CFG_ASSERT         1
#define RTOS_CFG_IDLE_HOOK      test_idleHook

void test_idleHook(void);

#endif  /* RTOS_CONFIG_INCLUDED */
