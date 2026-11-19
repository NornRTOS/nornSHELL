/**
 * @file nosh_out.c
 * Output and small string helpers of nosh on top of the application's write(): numbers,
 * padding, flash strings. No libc formatting: on the AVR vfprintf alone is bigger than
 * the whole shell.
 */

#include "nosh.h"
#include "nosh_internal.h"

void nosh_putc(char c)
{
    nosh_io.pIo->write(&c, 1);
}


void nosh_puts(const char *s)
{
    size_t n = 0;
    while(s[n] != '\0')
        ++ n;
    if(n > 0)
        nosh_io.pIo->write(s, n);
}


void nosh_putsP(const char *s)
{
    char c;
    while((c = nosh_strByte(s++)) != '\0')
        nosh_putc(c);
}


/** Decimal digits of n into the end of buf (size >= 11); returns the first digit. */
static char *toDecimal(uint32_t n, char buf[11])
{
    uint8_t i = 10;
    buf[i] = '\0';
    do
    {
        buf[--i] = (char)('0' + n % 10u);
        n /= 10u;
    }
    while(n != 0);
    return &buf[i];
}


void nosh_putU32(uint32_t n)
{
    char buf[11];
    nosh_puts(toDecimal(n, buf));
}


void nosh_putI32(int32_t n)
{
    if(n < 0)
    {
        nosh_putc('-');
        /* -(INT32_MIN) does not exist in int32_t: negate in unsigned arithmetic. */
        nosh_putU32(0u - (uint32_t)n);
    }
    else
        nosh_putU32((uint32_t)n);
}


void nosh_putHex8(uint8_t b)
{
    static const char digits[] = "0123456789abcdef";
    nosh_putc(digits[b >> 4]);
    nosh_putc(digits[b & 0x0Fu]);
}


void nosh_putPad(const char *s, uint8_t width)
{
    uint8_t n = 0;
    while(s[n] != '\0')
        ++ n;
    nosh_puts(s);
    while(n < width)
    {
        nosh_putc(' ');
        ++ n;
    }
}


void nosh_putPadP(const char *s, uint8_t width)
{
    uint8_t n = 0;
    char c;
    while((c = nosh_strByte(s++)) != '\0')
    {
        nosh_putc(c);
        ++ n;
    }
    while(n < width)
    {
        nosh_putc(' ');
        ++ n;
    }
}


void nosh_putU32Pad(uint32_t n, uint8_t width)
{
    char buf[11];
    const char *p = toDecimal(n, buf);
    uint8_t len = (uint8_t)(&buf[10] - p);
    while(len < width)
    {
        nosh_putc(' ');
        ++ len;
    }
    nosh_puts(p);
}


void nosh_putEol(void)
{
    nosh_io.pIo->write("\r\n", 2);
}


uint32_t nosh_div64by32(uint32_t hi, uint32_t lo, uint32_t d)
{
    uint32_t q = 0, r;
    uint8_t i;

    if(hi >= d)
        return UINT32_MAX;
    r = hi;
    for(i = 0; i < 32; ++i)
    {
        const uint32_t carry = r >> 31;
        r = (r << 1) | (lo >> 31);
        lo <<= 1;
        q <<= 1;
        if(carry != 0  ||  r >= d)
        {
            r -= d;
            q |= 1u;
        }
    }
    return q;
}


uint32_t nosh_runtimeMs(rtos_runtime_t t)
{
    const uint32_t div = (uint32_t)(RTOS_RUNTIME_HZ / 1000ul);
#if RTOS_RUNTIME_HZ % 1000 != 0
#error "nosh_runtimeMs assumes a run time counter rate that is a multiple of 1000 Hz"
#endif
    if(sizeof(t) > 4)
    {
        /* Two halves, each a 32 bit value even when t is 64 bits wide. */
        const uint32_t lo = (uint32_t)t;
        const uint32_t hi = (uint32_t)((t >> 16) >> 16);
        return nosh_div64by32(hi, lo, div);
    }
    return (uint32_t)t / div;
}


bool nosh_strEqP(const char *s, const char *p)
{
    for(;;)
    {
        const char c = nosh_strByte(p++);
        if(c != *s)
            return false;
        if(c == '\0')
            return true;
        ++ s;
    }
}


bool nosh_strEq(const char *a, const char *b)
{
    while(*a == *b)
    {
        if(*a == '\0')
            return true;
        ++ a; ++ b;
    }
    return false;
}


bool nosh_parseU32(const char *s, uint32_t *pValue)
{
    uint32_t v = 0;
    if(*s == '\0')
        return false;
    while(*s != '\0')
    {
        const char c = *s++;
        if(c < '0'  ||  c > '9')
            return false;
        if(v > (UINT32_MAX - 9u) / 10u)
            return false;
        v = v * 10u + (uint32_t)(c - '0');
    }
    *pValue = v;
    return true;
}
