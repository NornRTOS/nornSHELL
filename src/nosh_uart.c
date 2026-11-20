/**
 * @file nosh_uart.c
 * The console UART as the shell's transport, ready made: nosh_uartIo and
 * nosh_enableUart(). With UART_CFG_RX_STREAM in rtos_config.h (AVR and ESP32) the driver's
 * receive interrupt writes into a stream buffer that read() blocks on; without it the
 * ESP32 gets a receive callback into a queue, and on the AVR, whose driver then has no
 * receive interrupt, read() polls uart_getc() and the shell sleeps NOSH_CFG_POLL_MS
 * between tries. In the two interrupt forms the shell costs nothing while idle. write()
 * is the polling transmitter. An application with its own transport (a second UART, a
 * network socket) writes its own nosh_io_t and never links this file.
 */

#include "nosh.h"
#include "uart.h"

/* Room for a pasted line: the hardware FIFO holds 128 bytes on the ESP32 and the shell
   echoes each byte through the polling transmitter while more arrive. */
#define RX_LEN      96u

#if UART_CFG_RX_STREAM
/* ---- the driver's receive interrupt -> stream buffer -> blocking read() ------------ */
static uint8_t _rxStorage[RX_LEN];
static rtos_stream_t _rxStream;

static size_t ioRead(void *buf, size_t n)
{
    return rtos_streamRead(&_rxStream, buf, (uint16_t)n, RTOS_FOREVER);
}

void nosh_uartInit(void)
{
    rtos_streamInit(&_rxStream, _rxStorage, sizeof _rxStorage, 1);  /* wake per byte */
    uart_setRxStream(&_rxStream);
}
#elif defined(__XTENSA__)
/* ---- receive callback -> queue -> blocking read() ---------------------------------- */
static uint8_t _rxStorage[RX_LEN];
static rtos_queue_t _rxQueue;

static void uartRx(uint8_t c, void *arg)
{
    (void)arg;
    (void)rtos_queueSendFromIsr(&_rxQueue, &c);     /* a very long paste loses its tail */
}

static size_t ioRead(void *buf, size_t n)
{
    (void)n;
    return rtos_queueReceive(&_rxQueue, buf, RTOS_FOREVER) ? 1u : 0u;
}

void nosh_uartInit(void)
{
    rtos_queueInit(&_rxQueue, _rxStorage, 1, RX_LEN);
    uart_onReceive(uartRx, NULL);
}
#else
/* ---- polling: the shell task asks the driver and sleeps between tries --------------- */
static size_t ioRead(void *buf, size_t n)
{
    const int16_t c = uart_getc();
    (void)n;
    if(c < 0)
        return 0;
    *(uint8_t *)buf = (uint8_t)c;
    return 1;
}

void nosh_uartInit(void)
{
}
#endif

static void ioWrite(const void *buf, size_t n)
{
    const char *p = buf;
    while(n-- > 0)
        uart_putc(*p++);
}

const nosh_io_t nosh_uartIo = { ioRead, ioWrite };

static uint8_t _stack[NOSH_CFG_UART_STACK] NOSH_STACK_ALIGN;

int nosh_enableUart(uint8_t prio)
{
    nosh_uartInit();
    return nosh_enableDefault(&nosh_uartIo, prio == 0 ? 1u : prio, _stack, sizeof _stack);
}
