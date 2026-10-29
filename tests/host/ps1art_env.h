// Stand-ins for the art pipeline's kernel calls and the GUI/device globals it reads, so the real
// texcache.cpp and artload.cpp run on the host (see test_ps1art.cpp).
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
typedef struct
{
    int attr, option, stack_size, initial_priority;
    void *gp_reg, *func, *stack;
} ee_thread_t;
static inline int GetThreadId(void) { return 1; }
static inline int SleepThread(void) { return 0; }
static inline int WakeupThread(int) { return 0; }
static inline int iWakeupThread(int) { return 0; }
static inline int ChangeThreadPriority(int, int) { return 0; }
static inline int RotateThreadReadyQueue(int) { return 0; }
static inline void ExitDeleteThread(void) {}
static inline int CreateThread(ee_thread_t *) { return 1; }
static inline int StartThread(int, void *) { return 0; }
static inline int TerminateThread(int) { return 0; }
static inline int DeleteThread(int) { return 0; }
static inline int DIntr(void) { return 0; }
static inline int EIntr(void) { return 0; }
