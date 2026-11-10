// Host stub for the EE <kernel.h> semaphore calls; counts holds so tests can assert balance.
#pragma once
inline int g_semaDepth[16];
inline int g_semaWaits = 0;
inline void WaitSema(int id) { g_semaDepth[id]++; g_semaWaits++; }
inline void SignalSema(int id) { g_semaDepth[id]--; }
typedef struct
{
    int count, max_count, init_count, wait_threads;
    unsigned int attr, option;
} ee_sema_t;
inline int CreateSema(ee_sema_t *) { return 0; }
