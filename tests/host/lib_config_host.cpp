// HOST-TEST: library (linked into tests that need the real config store; see test_settings.cpp)
// The real config.cpp + util.cpp file buffers, built against the stub headers.
#define HOST_ENV_EXTERN_DEBUG // gEnableDebug belongs to the settings table; the test defines it
extern int gEnableDebug;
#include "../../src/platform/util.cpp"
#include "../../src/core/config.cpp"
#include "host_env.h"
