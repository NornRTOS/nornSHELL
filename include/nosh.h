#ifndef NOSH_INCLUDED
#define NOSH_INCLUDED
/**
 * @file nosh.h
 * @brief nosh, the nornRTOS shell: public API.
 *
 * nosh is an optional module on top of nornCore's public API. An application enables it
 * with this include and one call, NOSH_ENABLE(), which creates the shell task on a
 * priority the application chooses; an application that does not call it gets a binary
 * without nosh (every function lives in its own section and the linker drops what is
 * not referenced). Commands are registered one by one with nosh_add(), so a command
 * that is not registered costs nothing either; `help` is the only built-in one.
 *
 * nosh never touches the hardware. The application hands it a transport, a nosh_io_t
 * with a read and a write function (a UART in the examples, but anything that moves
 * bytes will do), and nosh depends on nothing but rtos.h.
 *
 * @par Strings in flash
 * Command names, help texts and everything nosh prints are stored with NOSH_STR(), which
 * puts them in program memory on the AVR (2 KB of RAM on the UNO) and is the identity on
 * the ESP32 and the host. A command reads such a string with nosh_strByte() or prints it
 * with nosh_putsP(); RAM strings (argv, formatted numbers) go through nosh_puts().
 *
 * @par Threads
 * nosh_enable(), nosh_add() and the other setup calls are made from main() before
 * rtos_start(), like rtos_createTask(); they take no lock. The output functions are for
 * the shell task only: a command runs in it.
 *
 * @par What nosh uses from nornCore
 * Only the public API in rtos.h, never rtos_kernel.h or the kernel's tables (the size
 * check in the nornRTOS tree fails on either): rtos_createTask(), rtos_delay(),
 * rtos_getTick(), rtos_getTaskInfo(), rtos_getStackFree() (through the task info),
 * rtos_getCpuLoad(), rtos_heapStats(), rtos_getLastPanic(), rtos_getResetCause(), and
 * the RTOS_CFG_* options of the application's rtos_config.h to know which columns exist.
 * The kernel is not changed for nosh: an application that does not call nosh_enable()
 * links the same kernel objects to the byte (tools/size_check.sh in nornRTOS).
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "rtos.h"

#ifdef NOSH_CONFIG_H
#include "nosh_config.h"
#endif

/** Tasks nosh creates, for the application's RTOS_CFG_NUM_TASKS. */
#define NOSH_TASKS              1

/* ---- configuration, overridable from the application's nosh_config.h ----------------- */

/** Longest command line accepted, including the terminating zero. */
#ifndef NOSH_CFG_LINE_LEN
#define NOSH_CFG_LINE_LEN       64u
#endif

/** Most arguments a line may have, the command name included. */
#ifndef NOSH_CFG_MAX_ARGS
#define NOSH_CFG_MAX_ARGS       8u
#endif

/** Milliseconds the shell task sleeps when read() returns nothing. Irrelevant when the
    application's read() blocks (interrupt driven transport). */
#ifndef NOSH_CFG_POLL_MS
#define NOSH_CFG_POLL_MS        10u
#endif

/** Echo the typed characters back on the transport (1), or not (0), for a terminal
    that echoes locally. */
#ifndef NOSH_CFG_ECHO
#define NOSH_CFG_ECHO           1
#endif

/** The prompt. */
#ifndef NOSH_CFG_PROMPT
#define NOSH_CFG_PROMPT         "nosh> "
#endif
/** Stack of the shell task created by nosh_enableUart(): the default set's commands
    print tables; a module command such as `net` or `config` needs this much. */
#ifndef NOSH_CFG_UART_STACK
#define NOSH_CFG_UART_STACK     RTOS_STACK_LARGE
#endif

/* ---- strings in program memory ----------------------------------------------------- */

/** A string literal inside a function: in flash on the AVR, in the ordinary way
    elsewhere. It is an expression and cannot initialize a static object; for those
    (command names, tables) declare the array with NOSH_PROGMEM, or use NOSH_CMD(). */
#define NOSH_STR(s)             RTOS_PORT_STR(s)

#ifdef __AVR__
#include <avr/pgmspace.h>
/** Attribute of a static array that nosh reads with nosh_strByte(): flash on the AVR. */
#define NOSH_PROGMEM            PROGMEM
/** Read one byte of a NOSH_STR or NOSH_PROGMEM string. */
static inline char nosh_strByte(const char *p) { return (char)pgm_read_byte(p); }
#else
#define NOSH_PROGMEM
static inline char nosh_strByte(const char *p) { return *p; }
#endif

/* ---- types ----------------------------------------------------------------------- */

/**
 * The transport, owned by the application.
 *
 * read() copies at most @p n received bytes into @p buf and returns how many; 0 means
 * nothing is available right now, and nosh then sleeps NOSH_CFG_POLL_MS before asking
 * again. read() may instead block the shell task (on a queue fed by a receive interrupt,
 * say): then the poll never happens. write() sends @p n bytes; it may block the shell
 * task until they are out, but it must never hold up a task of higher priority. Both run
 * in the shell task.
 */
typedef struct nosh_io
{
    size_t (*read)(void *buf, size_t n);
    void (*write)(const void *buf, size_t n);
} nosh_io_t;

/**
 * A command. One static object per command, usually `const` apart from the link, with
 * the name and the help text as NOSH_STR() strings. run() gets the tokens of the line,
 * argv[0] being the command name, and prints its result with the nosh_put* functions.
 * A command object lives in the module that owns the function it exposes (nosh's own
 * ones in nosh, a `config` command in a storage module) and is linked in only when the
 * application registers it.
 */
typedef struct nosh_cmd
{
    const char *name;                   /**< NOSH_PROGMEM string, no spaces. */
    const char *help;                   /**< NOSH_PROGMEM string, one line; "" for none. */
    void (*run)(uint8_t argc, const char *const argv[]);
    struct nosh_cmd *pNext;             /**< Set by nosh_add(); initialize to NULL. */
} nosh_cmd_t;

/**
 * @brief Defines a command object with its strings in program memory.
 *
 * `NOSH_CMD(nosh_cmd_ps, "ps", "list the tasks", cmdPs);` declares the two strings and
 * the object `nosh_cmd_t nosh_cmd_ps` with external linkage, for a header to declare
 * `extern nosh_cmd_t nosh_cmd_ps;`. NOSH_CMD_STATIC() is the same for a command local
 * to its file. @p help may be "".
 */
#define NOSH_CMD(var, name, help, fn)           NOSH_CMD_(, var, name, help, fn)
#define NOSH_CMD_STATIC(var, name, help, fn)    NOSH_CMD_(static, var, name, help, fn)
#define NOSH_CMD_(storage, var, name, help, fn)                                 \
    static const char var##_name_[] NOSH_PROGMEM = name;                        \
    static const char var##_help_[] NOSH_PROGMEM = help;                        \
    storage nosh_cmd_t var = { var##_name_, var##_help_, (fn), NULL }

/** @name Return values of nosh_enable()
 * @{ */
#define NOSH_OK                 0
#define NOSH_ERR_PRIO           1       /**< Priority out of range or its slot taken. */
#define NOSH_ERR_ARG            2       /**< NULL io or function, stack too small. */
/** @} */

/* ---- enabling ---------------------------------------------------------------------- */

/** Alignment the shell task's stack needs, from the port. */
#define NOSH_STACK_ALIGN        __attribute__((aligned(RTOS_PORT_STACK_ALIGN)))

/**
 * @brief Enables nosh with a stack of @p stackSize bytes created on the spot.
 *
 * Expands to a static stack and a call to nosh_enable(); the return value is lost, so
 * the macro is for applications that know the priority is free. Call nosh_enable()
 * directly to check the result or to supply the stack yourself.
 */
#define NOSH_ENABLE(io, prio, stackSize)                                        \
    do {                                                                        \
        static uint8_t nosh_stack_[stackSize] NOSH_STACK_ALIGN;                 \
        (void)nosh_enable((io), (prio), nosh_stack_, sizeof nosh_stack_);       \
    } while(0)

/**
 * @brief Creates the shell task.
 *
 * @param pIo
 *     The transport; the object must live as long as the shell does (static).
 * @param prio
 *     Priority and task id of the shell task, 1..RTOS_CFG_NUM_TASKS, a slot no other
 *     task has taken. With a polling read() the shell wakes every NOSH_CFG_POLL_MS, so
 *     a low priority keeps it out of the way; with a blocking read() any priority works.
 * @param pStack
 *     Stack memory, aligned to RTOS_PORT_STACK_ALIGN.
 * @param stackSize
 *     Its size, at least RTOS_STACK_MIN. Commands print and nest a few calls:
 *     RTOS_STACK_DEFAULT is the size to start from.
 * @return NOSH_OK, or NOSH_ERR_PRIO if the slot is taken or out of range, or
 *     NOSH_ERR_ARG. Nothing is created on an error.
 *
 * @par Context
 *     main(), after rtos_init() and before rtos_start(), once.
 *
 * @see NOSH_ENABLE(), nosh_add()
 */
int nosh_enable(const nosh_io_t *pIo, uint8_t prio, void *pStack, size_t stackSize);

/**
 * @brief nosh_enable() plus the default commands: `uptime`, `ps`, `mem` and `sched`.
 *
 * For the application that does not want to pick. Same arguments and result; nothing is
 * registered on an error. Lives in its own source file, so an application that calls
 * nosh_enable() and nosh_add() instead does not link the four commands.
 */
int nosh_enableDefault(const nosh_io_t *pIo, uint8_t prio, void *pStack, size_t stackSize);

/** NOSH_ENABLE() with the default commands, see nosh_enableDefault(). */
#define NOSH_ENABLE_DEFAULT(io, prio, stackSize)                                \
    do {                                                                        \
        static uint8_t nosh_stack_[stackSize] NOSH_STACK_ALIGN;                 \
        (void)nosh_enableDefault((io), (prio), nosh_stack_, sizeof nosh_stack_); \
    } while(0)

/**
 * @brief Registers a command.
 *
 * The command is appended to the list `help` shows and the dispatcher searches, in
 * registration order. A command registered twice is undefined.
 *
 * @par Context
 *     main(), before rtos_start().
 */
void nosh_add(nosh_cmd_t *pCmd);

/* ---- the console UART as the transport, ready made (src/nosh_uart.c) --------------- */

/** nornCore's console UART (uart.h) as a nosh_io_t: a receive interrupt into a queue on
    the ESP32, polling on the AVR; the polling transmitter for output. */
extern const nosh_io_t nosh_uartIo;

/** Prepares nosh_uartIo (the receive queue and interrupt on the ESP32); nosh_enableUart()
    calls it. For an application that uses nosh_uartIo with NOSH_ENABLE() instead. */
void nosh_uartInit(void);

/** The shell on the console UART with the default set of commands in one call: the
    transport, a stack of NOSH_CFG_UART_STACK bytes and nosh_enableDefault(). @p prio 0
    means 1. Returns what nosh_enableDefault() returns. An application that calls this
    links nosh_uart.c; one with its own transport does not. */
int nosh_enableUart(uint8_t prio);

/* ---- task names ------------------------------------------------------------------- */

/**
 * @brief Gives nosh the names of the tasks.
 *
 * @param ppNames
 *     A table indexed by task id (0 is the idle task), @p count entries, in memory that
 *     lives as long as the shell: `static const char *const names[] = {...}`. An entry
 *     may be NULL. The kernel stores no names; nosh keeps this pointer only.
 * @param count
 *     Entries in the table; ids beyond it have no name.
 *
 * @par Context
 *     main(), before rtos_start().
 */
void nosh_setTaskNames(const char *const *ppNames, uint8_t count);

/** The name of task @p taskId from the table, or NULL. */
const char *nosh_taskName(uint8_t taskId);

/* ---- programs: tasks started and ended from the console (RTOS_CFG_DYNAMIC_TASKS) ------ */

/** A program: a task the application does not create in main() but lists here, so that
    `run <name>` starts it in its slot and `stop`/`wait` end it. The application owns the
    stack (RTOS_STACK). The slot is the program's alone; two programs may share a slot
    only if they are never run at the same time. The table is const and lives in the
    application; nosh keeps the pointer. */
typedef struct nosh_program
{
    const char *name;                   /**< RAM string, what `run` takes; also the task's name in `ps`. */
    uint8_t prio;                       /**< Its slot: task id and priority. */
    rtos_taskFunc_t fn;
    void *arg;
    uint8_t *stack;
    size_t stackSize;
} nosh_program_t;

/** Before rtos_start(). With RTOS_CFG_DYNAMIC_TASKS off the table is kept and ignored. */
void nosh_setPrograms(const nosh_program_t *pTable, uint8_t count);

/** The program named @p name, or NULL. */
const nosh_program_t *nosh_programByName(const char *name);

/** For the running program: true once `stop` was given for it. The program then ends
    itself, by returning or with rtos_exit(), at a point where it holds no mutex. Nothing
    ends a task from outside. */
bool nosh_stopRequested(void);

/* ---- the commands of nosh; register the ones wanted with nosh_add() ----------------- */

extern nosh_cmd_t nosh_cmd_uptime;     /**< time since start, tick, reset cause */
extern nosh_cmd_t nosh_cmd_ps;         /**< one line per task */
extern nosh_cmd_t nosh_cmd_mem;        /**< stacks and heap */
extern nosh_cmd_t nosh_cmd_sched;      /**< load, task states, tick, last panic */
extern nosh_cmd_t nosh_cmd_log;        /**< the run time level of nornCore's log classes (hal/log.h) */
#if RTOS_CFG_DYNAMIC_TASKS
extern nosh_cmd_t nosh_cmd_run;        /**< start a program (nosh_program_t) in its slot */
extern nosh_cmd_t nosh_cmd_stop;       /**< ask a program to end (nosh_stopRequested()) */
extern nosh_cmd_t nosh_cmd_wait;       /**< wait for the end and free the slot */
extern nosh_cmd_t nosh_cmd_restart;    /**< stop, wait and run in one */
#else
/* Without the kernel option these commands cannot do anything, so registering one is a
   link error that names the option, not a command that answers "n/a". */
#define nosh_cmd_run    nosh_cmd_run_needs_RTOS_CFG_DYNAMIC_TASKS
#define nosh_cmd_stop   nosh_cmd_stop_needs_RTOS_CFG_DYNAMIC_TASKS
#define nosh_cmd_wait   nosh_cmd_wait_needs_RTOS_CFG_DYNAMIC_TASKS
#define nosh_cmd_restart nosh_cmd_restart_needs_RTOS_CFG_DYNAMIC_TASKS
extern nosh_cmd_t nosh_cmd_run, nosh_cmd_stop, nosh_cmd_wait, nosh_cmd_restart;
#endif

/* ---- output, for commands (shell task only) --------------------------------------- */

void nosh_putc(char c);
void nosh_puts(const char *s);                  /**< A RAM string. */
void nosh_putsP(const char *s);                 /**< A NOSH_STR string. */
void nosh_putU32(uint32_t n);                   /**< Decimal. */
void nosh_putI32(int32_t n);
void nosh_putHex8(uint8_t b);                   /**< Two hex digits. */
/** A RAM string padded with spaces to @p width columns (not truncated). */
void nosh_putPad(const char *s, uint8_t width);
/** A NOSH_STR string padded with spaces to @p width columns (not truncated). */
void nosh_putPadP(const char *s, uint8_t width);
/** @p n in decimal, right aligned in @p width columns. */
void nosh_putU32Pad(uint32_t n, uint8_t width);
void nosh_putEol(void);                         /**< "\r\n". */

/** Run time counts (RTOS_RUNTIME_HZ) to milliseconds, saturating at UINT32_MAX. Where
    the port's counter is 64 bits (ESP32) this divides without libgcc's 64 bit helpers,
    which would cost more than the whole shell. */
uint32_t nosh_runtimeMs(rtos_runtime_t t);

/** (hi:lo) / d for a 32 bit divisor @p d > 0: restoring long division over the low word,
    the quotient saturating at UINT32_MAX when hi >= d. About 40 instructions where
    libgcc's __udivdi3 is 500 bytes. */
uint32_t nosh_div64by32(uint32_t hi, uint32_t lo, uint32_t d);

/** True if the RAM string @p s equals the NOSH_STR string @p p. */
bool nosh_strEqP(const char *s, const char *p);
/** True if the two RAM strings are equal. */
bool nosh_strEq(const char *a, const char *b);

/** Parses an unsigned decimal argument; false if @p s is empty, not all digits or
    above UINT32_MAX. */
bool nosh_parseU32(const char *s, uint32_t *pValue);

#endif  /* NOSH_INCLUDED */
