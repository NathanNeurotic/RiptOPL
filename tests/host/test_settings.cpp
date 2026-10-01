// HOST-TEST: sdk-headers; link=lib_config_host.cpp; globals=settings
// settings.cpp: the table that replaced three hand-written lists in opl.cpp. Built against the REAL headers
// (SDK stubs in tests/host/sdk_stub) and linked with the real config store.
#include <map>
#include <string>
#include <string.h>
#include "include/settings.h"
#include "../../src/settings.cpp"
#include "check.h"

// Variables the table points at, defined from the real declarations (generated per run).
#include "settings_globals.h"

static const char *const kGoldenKeys[] = {
#include "settings_golden_keys.inc"
};

int main()
{
    configInit("/tmp/");

    // 1. Same keys, same order as the old _saveConfig.
    size_t n = sizeof(kGoldenKeys) / sizeof(kGoldenKeys[0]);
    for (size_t i = 0; i < n; i++)
        CHECK_STR(settingsKeyAt((unsigned)i), kGoldenKeys[i]);
    CHECK(settingsKeyAt((unsigned)n) == NULL);

    // 2. Defaults identical to the old setDefaults (one generated check per row).
    settingsApplyDefaults();
#include "settings_golden_defaults.inc"

    // 3. Save -> text -> load round trip with a distinct value in every variable.
    int seed = 1;
    for (const Setting &s : kSettings) {
        switch (s.kind) {
            case Setting::Int: *static_cast<int *>(s.var) = 1000 + seed++; break;
            case Setting::Str: snprintf(static_cast<char *>(s.var), s.size, "str%d", seed++); break;
            case Setting::Color: for (int i = 0; i < 3; i++) static_cast<unsigned char *>(s.var)[i] = (unsigned char)(10 * seed + i); seed++; break;
        }
    }
    config_set_t *cfg = configAlloc(CONFIG_OPL, NULL, "/tmp/riptopl-settings-test.cfg");
    settingsSave(cfg);
    config_set_t *copy = configClone(cfg);

    // Every key was written, none twice.
    for (size_t i = 0; i < n; i++) {
        const char *v = NULL;
        CHECK(configGetStr(copy, kGoldenKeys[i], &v));
    }

    int expectInt[200]; char expectStr[200][256]; unsigned char expectCol[200][3];
    int idx = 0;
    for (const Setting &s : kSettings) {
        if (s.kind == Setting::Int) expectInt[idx] = *static_cast<int *>(s.var);
        else if (s.kind == Setting::Str) snprintf(expectStr[idx], 256, "%s", static_cast<char *>(s.var));
        else memcpy(expectCol[idx], s.var, 3);
        idx++;
    }
    // scramble, then load
    for (const Setting &s : kSettings) {
        if (s.kind == Setting::Int) *static_cast<int *>(s.var) = -77;
        else if (s.kind == Setting::Str) strcpy(static_cast<char *>(s.var), "xx");
        else memset(s.var, 0xEE, 3);
    }
    settingsLoad(copy);
    idx = 0;
    for (const Setting &s : kSettings) {
        if (s.kind == Setting::Int) {
            // loadPlain=false rows (the video mode, read by the recovery-combo code) must NOT be touched by settingsLoad
            if (s.loadPlain) CHECK(*static_cast<int *>(s.var) == expectInt[idx]);
            else CHECK(*static_cast<int *>(s.var) == -77);
        } else if (s.kind == Setting::Str) CHECK_STR(static_cast<char *>(s.var), expectStr[idx]);
        else CHECK(memcmp(s.var, expectCol[idx], 3) == 0);
        idx++;
    }
    CHECK(gVMode == -77); // the one loadPlain=false row

    // 4. Absent keys: ints and colors keep their current value; strings become empty (configGetStrCopy has always
    //    cleared its buffer on a miss; the old inline reads behaved the same).
    config_set_t *empty = configAlloc(CONFIG_OPL, NULL, "/tmp/riptopl-settings-empty.cfg");
    gScrollSpeed = 4242; strcpy(gExitPath, "keep");
    settingsLoad(empty);
    CHECK(gScrollSpeed == 4242);
    CHECK_STR(gExitPath, "");

    configFree(cfg); configFree(copy); configFree(empty);
    TEST_DONE("settings");
}
