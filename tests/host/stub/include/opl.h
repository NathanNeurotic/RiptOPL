// Host stub for include/opl.h: only what util.cpp / config.cpp touch. The real header pulls in gsKit,
// themes and ~140 settings globals that a host build cannot provide.
#pragma once
#include <dirent.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "tamtypes.h"
#include "include/config.h"
#define ALIGNED(x) __attribute__((aligned(x)))
extern int gEnableDebug;
extern char gBaseMCDir[];
extern char gOPLPart[];
extern int gLastSaveErrno;
extern struct { unsigned char ip[4]; } g_unused_ip_marker;
extern unsigned char ps2_ip[4], ps2_netmask[4], ps2_gateway[4];
