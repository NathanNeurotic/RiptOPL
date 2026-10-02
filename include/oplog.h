#ifndef __OPLOG_H
#define __OPLOG_H

// Logging bring-up used by opl.cpp and lwnbdctl.cpp: LOG_INIT() starts the log sink, LOG_ENABLE() turns it on
// (EESIO serial, the DEBUG build's debug thread, or nothing in a release build).

#include "ioman.h"

#ifdef __EESIO_DEBUG
#include "SIOCookie.h"
#define LOG_INIT() ee_sio_start(38400, 0, 0, 0, 0, 1)
#define LOG_ENABLE() \
    do {             \
    } while (0)
#else
#ifdef __DEBUG
#include "include/debug.h"
#define LOG_INIT() \
    do {           \
    } while (0)
#define LOG_ENABLE() ioPutRequest(IO_CUSTOM_SIMPLEACTION, reinterpret_cast<void *>(&debugSetActive))
#else
#define LOG_INIT() \
    do {           \
    } while (0)
#define LOG_ENABLE() \
    do {             \
    } while (0)
#endif
#endif

#endif
