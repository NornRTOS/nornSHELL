/**
 * @file nosh.c
 * The shell: the task, the line editor, the tokenizer, the dispatcher and the built-in
 * `help`. Everything else is a command in its own file, linked in only when registered.
 */

#include "nosh.h"
#include "nosh_internal.h"

/* Control characters the line editor knows. */
#define CH_BS           0x08
#define CH_DEL          0x7F
#define CH_CTRL_U       0x15
#define CH_CR           '\r'
#define CH_LF           '\n'

nosh_ioState_t nosh_io;

/** Head and tail of the command list, in registration order. */
static nosh_cmd_t *_pCmdHead = NULL;
static nosh_cmd_t *_pCmdTail = NULL;

/** The line being typed, its length, and whether the last character was a CR (so that
    the LF of a CRLF pair does not count as a second, empty line). */
static char _line[NOSH_CFG_LINE_LEN];
static uint8_t _lineLen = 0;
static bool _lastWasCr = false;
/** Set when the line overflowed: the rest of it is discarded until its end. */
static bool _overflow = false;

static const char _prompt[] NOSH_PROGMEM = NOSH_CFG_PROMPT;


static void prompt(void)
{
    nosh_putsP(_prompt);
}


/**
 * Splits the line at spaces into argv; a run of spaces is one separator. Returns the
 * count, or NOSH_CFG_MAX_ARGS + 1 if there were more.
 */
static uint8_t tokenize(char *line, const char *argv[])
{
    uint8_t argc = 0;
    char *p = line;

    for(;;)
    {
        while(*p == ' ')
            ++ p;
        if(*p == '\0')
            return argc;
        if(argc == NOSH_CFG_MAX_ARGS)
            return (uint8_t)(NOSH_CFG_MAX_ARGS + 1u);
        argv[argc++] = p;
        while(*p != ' '  &&  *p != '\0')
            ++ p;
        if(*p == '\0')
            return argc;
        *p++ = '\0';
    }
}


static void runLine(void)
{
    const char *argv[NOSH_CFG_MAX_ARGS];
    uint8_t argc;
    nosh_cmd_t *pCmd;

    _line[_lineLen] = '\0';
    argc = tokenize(_line, argv);
    if(argc == 0)
        return;
    if(argc > NOSH_CFG_MAX_ARGS)
    {
        nosh_putsP(NOSH_STR("nosh: too many arguments"));
        nosh_putEol();
        return;
    }
    for(pCmd = _pCmdHead; pCmd != NULL; pCmd = pCmd->pNext)
    {
        if(nosh_strEqP(argv[0], pCmd->name))
        {
            pCmd->run(argc, argv);
            return;
        }
    }
    nosh_putsP(NOSH_STR("nosh: "));
    nosh_puts(argv[0]);
    nosh_putsP(NOSH_STR(": not found"));
    nosh_putEol();
}


/** One received character through the line editor. */
static void feed(char c)
{
    const bool wasCr = _lastWasCr;
    _lastWasCr = (c == CH_CR);

    if(c == CH_CR  ||  c == CH_LF)
    {
        if(c == CH_LF  &&  wasCr)
            return;                             /* second half of CRLF */
        nosh_putEol();
        if(_overflow)
        {
            nosh_putsP(NOSH_STR("nosh: line too long"));
            nosh_putEol();
            _overflow = false;
        }
        else
            runLine();
        _lineLen = 0;
        prompt();
    }
    else if(c == CH_BS  ||  c == CH_DEL)
    {
        if(_lineLen > 0)
        {
            -- _lineLen;
#if NOSH_CFG_ECHO
            nosh_io.pIo->write("\b \b", 3);
#endif
        }
    }
    else if(c == CH_CTRL_U)
    {
#if NOSH_CFG_ECHO
        while(_lineLen > 0)
        {
            nosh_io.pIo->write("\b \b", 3);
            -- _lineLen;
        }
#else
        _lineLen = 0;
#endif
        _overflow = false;
    }
    else if(c >= ' '  &&  c < CH_DEL)
    {
        if(_lineLen < NOSH_CFG_LINE_LEN - 1u)
        {
            _line[_lineLen++] = c;
#if NOSH_CFG_ECHO
            nosh_putc(c);
#endif
        }
        else
            _overflow = true;
    }
    /* other control characters: ignored */
}


static void shellTask(void *arg)
{
    (void)arg;
    nosh_putEol();
    prompt();
    for(;;)
    {
        char c;
        if(nosh_io.pIo->read(&c, 1) == 0)
            rtos_delay(RTOS_MS(NOSH_CFG_POLL_MS));
        else
            feed(c);
    }
}


/* ---- built-in help ---------------------------------------------------------------- */

static void cmdHelp(uint8_t argc, const char *const argv[])
{
    const nosh_cmd_t *pCmd;
    (void)argc; (void)argv;
    for(pCmd = _pCmdHead; pCmd != NULL; pCmd = pCmd->pNext)
    {
        /* Name padded to 10 columns, read from flash byte by byte. */
        uint8_t n = 0;
        char c;
        const char *p = pCmd->name;
        while((c = nosh_strByte(p++)) != '\0')
        {
            nosh_putc(c);
            ++ n;
        }
        if(nosh_strByte(pCmd->help) != '\0')
        {
            do
                nosh_putc(' ');
            while(++n < 10u);
            nosh_putsP(pCmd->help);
        }
        nosh_putEol();
    }
}

NOSH_CMD_STATIC(_cmdHelp, "help", "list the commands", cmdHelp);


/* ---- setup -------------------------------------------------------------------------- */

void nosh_add(nosh_cmd_t *pCmd)
{
    pCmd->pNext = NULL;
    if(_pCmdTail == NULL)
        _pCmdHead = pCmd;
    else
        _pCmdTail->pNext = pCmd;
    _pCmdTail = pCmd;
}


int nosh_enable(const nosh_io_t *pIo, uint8_t prio, void *pStack, size_t stackSize)
{
    rtos_taskInfo_t info;

    if(pIo == NULL  ||  pIo->read == NULL  ||  pIo->write == NULL  ||  pStack == NULL
       ||  stackSize < RTOS_STACK_MIN)
        return NOSH_ERR_ARG;
    if(prio < 1u  ||  prio > RTOS_CFG_NUM_TASKS)
        return NOSH_ERR_PRIO;
    if(!rtos_getTaskInfo(prio, &info)  ||  info.state != RTOS_TASK_NONE)
        return NOSH_ERR_PRIO;

    nosh_io.pIo = pIo;
    nosh_add(&_cmdHelp);
    rtos_createTask(prio, shellTask, NULL, (uint8_t *)pStack, stackSize);
    return NOSH_OK;
}
