#ifndef __OPLINT_H
#define __OPLINT_H

#include "opl.h"

// Internals shared between opl.cpp (lifecycle, source registry, settings persistence) and the files split out of it
// (autolaunch.cpp, lwnbdctl.cpp). Not public API.

// Boot-time retries while waiting for mass0 to appear before an auto-launch gives up.
constexpr int AUTOLAUNCH_MASS0_RETRIES = 8;

extern char gExportName[32]; // NBD default export (CONFIG_NET_NBD_DEFAULT_EXPORT)

void miniInit(int mode, int ataOnly = 0); // ataOnly: BDM_MODE loads only the ATA transport
void reset(void);
void initAllSupport(int force_reinit);
void deinitAllSupport(int exception, int modeSelected);
void clearErrorMessage(void);

// settingsio.cpp (settings persistence)
void _loadConfig();
void resolveBootDirToMass(void);
int tryAlternateDevice(int types);
void configReadNeutrinoGlobals(config_set_t *configOPL);
void configReadRA(config_set_t *configOPL);    // RA settings, shared with Auto Loading
void normalizeMcPath(char *path, size_t size); // "mc1:" / "mc1:X" -> "mc1:/" / "mc1:/X"
extern char gBootElfName[64];
extern const char *gBootHomeWhy; // why the boot home was chosen (diagnostics)

// opl.cpp, used by settingsio.cpp
void menuUpdateHook();
void moduleUpdateMenuInternal(opl_io_module_t *mod, int themeChanged, int langChanged);

// appscan.cpp
extern unsigned char gShouldAppsUpdate;

// menuitems.cpp: button handlers installed on each source's menu item
void itemExecRefresh(struct menu_item *curMenu);
void itemExecCross(struct menu_item *curMenu);
void itemExecTriangle(struct menu_item *curMenu);
void itemExecSquare(struct menu_item *curMenu);
void itemExecCircle(struct menu_item *curMenu);
void itemExecFav(struct menu_item *curMenu);
void itemExecToggleView(struct menu_item *curMenu);

// autolaunch.cpp
void autoLaunchHDDGame(char *argv[]);
void autoLaunchBDMGame(char *argv[], int ataOnly = 0); // argv[4] "ata": internal HDD only

#endif
