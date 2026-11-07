#ifndef HOST_THBASE
#define HOST_THBASE
#include <unistd.h>
#include <time.h>
#include "tamtypes.h"
typedef struct
{
    u32 lo, hi;
} iop_sys_clock_t;
static inline int DelayThread(int us)
{
    usleep(us);
    return 0;
}
static inline void GetSystemTime(iop_sys_clock_t *c)
{
    struct timespec t;
    u64 ticks;

    clock_gettime(CLOCK_MONOTONIC, &t);
    ticks = (u64)t.tv_sec * 36864000ULL + (u64)t.tv_nsec * 36864000ULL / 1000000000ULL;
    c->lo = (u32)ticks;
    c->hi = (u32)(ticks >> 32);
}
#endif
