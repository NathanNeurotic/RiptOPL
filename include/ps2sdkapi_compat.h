#ifndef RIPTOPL_PS2SDKAPI_COMPAT_H
#define RIPTOPL_PS2SDKAPI_COMPAT_H

// Some published SDK suites predate the C++ linkage guards in ps2sdkapi.h.
// These functions are supplied by libcglue with C linkage in every suite.
#ifdef __cplusplus
extern "C" {
#endif
#include <ps2sdkapi.h>
#ifdef __cplusplus
}
#endif

#endif
