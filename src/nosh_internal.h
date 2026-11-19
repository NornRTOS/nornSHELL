#ifndef NOSH_INTERNAL_INCLUDED
#define NOSH_INTERNAL_INCLUDED
/**
 * @file nosh_internal.h
 * Shared between the source files of nosh; not for applications or commands.
 */

#include "nosh.h"

/** The transport, set by nosh_enable(). One object so that every output function
    addresses it from a single base. */
typedef struct nosh_ioState
{
    const nosh_io_t *pIo;
} nosh_ioState_t;

extern nosh_ioState_t nosh_io;

#endif  /* NOSH_INTERNAL_INCLUDED */
