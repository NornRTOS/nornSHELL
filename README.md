# nornSHELL (nosh)

nosh is the shell of nornRTOS: a command line on a UART (or any byte transport the
application provides) for looking into a running system. It is an optional module that
depends on nornCore's public API only, and an application that does not use it gets a
binary without a byte of it.

    include/nosh.h      the public API: nosh_io_t, nosh_cmd_t, NOSH_ENABLE, nosh_add, output
    src/nosh.c          the shell task, line editor, tokenizer, dispatcher and `help`
    src/nosh_out.c      output and string helpers (no libc formatting)
    src/nosh_cmd_*.c    one command per file, linked in only when registered
    nosh.mk             makefile fragment for an application's app.mk
    tests/              host test on nornCore's host port (make host-test)

## Enabling

The shortest form, the console UART with the default set of commands:

```c
#include "nosh.h"

int main(void)
{
    rtos_init();
    ...
    nosh_enableUart(1);                 /* the console UART, help uptime ps mem sched */
    rtos_start();
}
```

`nosh_enableUart` is nosh_uartIo (src/nosh_uart.c) plus a stack of NOSH_CFG_UART_STACK
bytes and `nosh_enableDefault`. With `UART_CFG_RX_STREAM 1` (and `RTOS_CFG_STREAMS 1`) in
rtos_config.h the UART driver's receive interrupt writes into a stream buffer the shell
sleeps on, on the AVR as on the ESP32; without it the ESP32 gets a receive callback into a
queue and the AVR polls uart_getc() every NOSH_CFG_POLL_MS. Another transport, or a choice
of commands:

```c
#include "nosh.h"

static const nosh_io_t uartIo = { myRead, myWrite };   /* the application's transport */

int main(void)
{
    rtos_init();
    ...
    NOSH_ENABLE(&uartIo, /*prio*/ 1, RTOS_STACK_DEFAULT);   /* help only */
    nosh_add(&nosh_cmd_uptime);                             /* each command on request */
    nosh_add(&nosh_cmd_ps);
    nosh_setTaskNames(taskNames, count);                    /* optional, for ps and mem */
    rtos_start();
}
```

or, for the whole default set in one line, `NOSH_ENABLE_DEFAULT(&uartIo, 1, RTOS_STACK_DEFAULT);`
(help, uptime, ps, mem, sched).

Commands so far:

    help      the registered commands with their help line (built in)
    uptime    time since start, the tick, the cause of the last reset
    ps        one line per task: state, priority (and ceiling), what it waits for and
              for how long, stack size and untouched bytes, run time, period overruns;
              an RTOS_CFG_SMP build adds the core a running task is on
    mem       the stacks (total, untouched, the task with the least margin) and the heap
    sched     CPU load since the previous sched (all cores, and each core in an
              RTOS_CFG_SMP build), tasks per state, tick, the switch count
              (RTOS_CFG_RUNTIME_STATS 2, else `-`), last panic
    log       the run time level of nornCore's log classes (hal/log.h): `log` shows it and
              the levels of the modules the application listed with log_setModules(),
              `log warning` or `log 2` sets them all, `log wifi warning` one module, until
              the next reset; not in the default set, so a shell without it links none of
              the log code
    run       `run <program>` starts a program: a task the application lists in a
              nosh_program_t table (name, slot, function, stack) instead of creating it
              in main(); `ps` names the task after the program
    stop      `stop <program|task>` asks it to end; the program reads
              nosh_stopRequested() and ends itself (return or rtos_exit) where it holds
              nothing: nothing is killed from outside
    wait      `wait <program|task> [ms]` waits for the end (10 s default), frees the slot
              and the stack for the next run; `run` on an ended program frees it too
    restart   `restart <program> [ms]`: stop, wait and run in one; `stop` and a timed out
              `wait` note when the task still owns a mutex, since it cannot end before it
              has released it
              run, stop, wait and restart need RTOS_CFG_DYNAMIC_TASKS in rtos_config.h; without
              it, registering one is a link error naming the option, since a command
              that can do nothing should not be there

A column or line whose option is off in rtos_config.h (stack check, run time statistics,
heap) shows `-` and says which option. The cost of each command on each board is in
nornInternal/nornSHELL/measurements.md.

and in app.mk: `MODULES += $(NORN_RTOS)/modules/nornSHELL/nosh.mk`. The priority is the
application's choice (one task per priority in nornCore); `nosh_enable()` returns
`NOSH_ERR_PRIO` if the slot is taken. `examples/shell` in the nornRTOS tree runs on the
console UART's receive stream on both boards, so the shell costs nothing while idle.
`tools/qemu_talk.py IMAGE.bin --send help` talks to it in QEMU.

Strings for the AVR: names and texts of a command go through `NOSH_CMD()`, `NOSH_CMD_STATIC()`
or `NOSH_PROGMEM`, messages inside a function through `NOSH_STR()`; both are the identity
on the ESP32. `nosh_config.h` next to the application overrides `NOSH_CFG_LINE_LEN`,
`NOSH_CFG_MAX_ARGS`, `NOSH_CFG_POLL_MS`, `NOSH_CFG_ECHO` and `NOSH_CFG_PROMPT`.

## Tests

    make host-test     scripted transport against the shell task on nornCore's host port:
                       test_nosh (the editor and the diagnostics commands) and test_nosh_run
                       (run, stop and wait with two programs, RTOS_CFG_DYNAMIC_TASKS)
    make analyze       gcc -fanalyzer over src/
    make size          object sizes for the UNO and the ESP32

## Status

v0.1.0: phase 1 of nornInternal/nornSHELL/nosh-implementationsplan.md. The shell, the five commands, task
names, the default set, the example with a polling (AVR) and an interrupt driven (ESP32)
transport, the size contract with the cost per command (tools/size_check.sh in nornRTOS)
and its CI; verified on the host, in QEMU and simavr, and on an ESP32 board. The context
switch counter (step 1.6) was struck from phase 1. Since then: `nosh_strEq`, the `log`
command for nornCore's log classes (2026-11-13), and `tools/qemu_talk.py` with `--nic`
and `--line-delay` for dialogues with a networked image. Phase 2B (nornStore and
`config`) is done in its own module. Phase 2A (2026-11-15): `run`, `stop` and `wait` on
nornCore's dynamic task API (RTOS_CFG_DYNAMIC_TASKS), the nosh_program_t table and
nosh_stopRequested(); the shell example has the `work` program and the CI dialogue runs,
stops and waits for it in QEMU. 2026-11-16: the console transport on
nornCore's receive stream (UART_CFG_RX_STREAM), interrupt driven on the AVR too.
2026-11-18: `restart`, the mutex note in `stop`, the switch count in `sched`
(RTOS_CFG_RUNTIME_STATS 2) and module levels in `log`.
Measurements in nornInternal/nornSHELL/measurements.md; the user guide chapter is in nornDocs.

## Licence

Business Source License 1.1 with the Additional Use Grant in LICENSE.md (Tenforward AB);
no third-party code, THIRD_PARTY_LICENSES.md.
