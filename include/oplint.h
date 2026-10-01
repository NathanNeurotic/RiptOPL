#ifndef __OPLINT_H
#define __OPLINT_H

#include "opl.h"

// Internals shared between opl.cpp (lifecycle, source registry, settings persistence) and the files split out of it
// (autolaunch.cpp, lwnbdctl.cpp). Not public API.

// Boot-time retries while waiting for mass0 to appear before an auto-launch gives up.
constexpr int AUTOLAUNCH_MASS0_RETRIES = 8;

extern char gExportName[32]; // NBD default export (CONFIG_NET_NBD_DEFAULT_EXPORT)

void miniInit(int mode);
void reset(void);
void initAllSupport(int force_reinit);
void deinitAllSupport(int exception, int modeSelected);
void clearErrorMessage(void);

// settingsio.cpp (settings persistence)
void _loadConfig();
void resolveBootDirToMass(void);
int tryAlternateDevice(int types);
void configReadNeutrinoGlobals(config_set_t *configOPL);
extern char gBootElfName[64];
extern const char *gBootHomeWhy; // why the boot home was chosen (diagnostics)

// opl.cpp, used by settingsio.cpp
void menuUpdateHook();
void moduleUpdateMenuInternal(opl_io_module_t *mod, int themeChanged, int langChanged);

// autolaunch.cpp
void autoLaunchHDDGame(char *argv[]);
void autoLaunchBDMGame(char *argv[]);

#endif
