#ifndef SETTINGS_H
#define SETTINGS_H

#include <tamtypes.h> // config.h uses u32
#include "include/config.h"

// Plain-key persistence for the CONFIG_OPL set (see src/settings.cpp for what "plain" means).
void settingsApplyDefaults(void);          // starting values of the table's variables (called from setDefaults)
void settingsLoad(config_set_t *cfg);      // read every table key present in cfg; absent keys keep their value
void settingsSave(config_set_t *cfg);      // write every table key
const char *settingsKeyAt(unsigned index); // key of table row `index`, NULL past the end (tests)

#endif
