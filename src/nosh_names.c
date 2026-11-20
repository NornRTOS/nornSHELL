/**
 * @file nosh_names.c
 * Task names. The kernel knows tasks by id only; the application hands nosh a table of
 * names indexed by id, and nosh keeps the pointer. Only the commands that print names
 * (`ps`, later `run`) reference this file, so it is linked in with them.
 */

#include "nosh.h"

static const char *const *_ppNames = NULL;
static uint8_t _nameCount = 0;


void nosh_setTaskNames(const char *const *ppNames, uint8_t count)
{
    _ppNames = ppNames;
    _nameCount = count;
}


static const nosh_program_t *_pPrograms = NULL;
static uint8_t _programCount = 0;


void nosh_setPrograms(const nosh_program_t *pTable, uint8_t count)
{
    _pPrograms = pTable;
    _programCount = count;
}


const nosh_program_t *nosh_programByName(const char *name)
{
    uint8_t i;
    for(i = 0; i < _programCount; ++i)
        if(nosh_strEq(_pPrograms[i].name, name))
            return &_pPrograms[i];
    return NULL;
}


const char *nosh_taskName(uint8_t taskId)
{
    if(taskId < _nameCount  &&  _ppNames != NULL  &&  _ppNames[taskId] != NULL)
        return _ppNames[taskId];
#if RTOS_CFG_DYNAMIC_TASKS
    /* A program's task is named after the program when the table has no name for it. */
    {
        uint8_t i;
        for(i = 0; i < _programCount; ++i)
            if(_pPrograms[i].prio == taskId)
                return _pPrograms[i].name;
    }
#endif
    return NULL;
}
