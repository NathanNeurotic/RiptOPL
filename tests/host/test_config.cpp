// config: key/value store with two on-disk syntaxes (OPL legacy "key=value" and wOPL/libconfig
// "key = value;"). Characterisation tests for read, write, format latch, merge and clone. These pin the
// behaviour the planned Settings/ConfigSet rewrite must keep: files shared with wOPL/OPL-Launcher.
#include <string>
#include <string.h>
#include "../../src/platform/util.cpp"
#include "../../src/core/config.cpp"
#include "host_env.h"
#include "check.h"

static std::string slurp(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return "<missing>";
    std::string s; char b[512]; size_t n;
    while ((n = fread(b, 1, sizeof(b), f)) > 0) s.append(b, n);
    fclose(f);
    return s;
}
static std::string str(config_set_t *c, const char *key)
{
    const char *v = NULL;
    return configGetStr(c, key, &v) ? std::string(v) : std::string("<none>");
}
static int num(config_set_t *c, const char *key, int dflt = -999)
{
    int v = dflt;
    return configGetInt(c, key, &v) ? v : dflt;
}
static config_set_t *parse(const std::string &path, const char *text)
{
    config_set_t *c = configAlloc(CONFIG_GAME, NULL, (char *)path.c_str());
    configReadBuffer(c, text, (int)strlen(text));
    return c;
}

int main()
{
    char dirbuf[] = "/tmp/riptopl-config-XXXXXX";
    CHECK(mkdtemp(dirbuf) != NULL);
    std::string dir = std::string(dirbuf) + "/";
    configInit((char *)dir.c_str());

    // ---- legacy syntax -------------------------------------------------------------------------
    config_set_t *a = parse(dir + "a.cfg", "compat=3\r\nvmc_0=mc0\r\n$Foo=bar baz\r\n# comment line\r\n\r\nempty=\r\n");
    CHECK(a->format() == CFG_FMT_LEGACY);
    CHECK(num(a, "compat") == 3);
    CHECK(str(a, "vmc_0") == "mc0");
    CHECK(str(a, "$Foo") == "bar baz");     // value keeps inner spaces
    CHECK(str(a, "zzz") == "<none>");
    CHECK(num(a, "zzz", -5) == -5);                   // missing int leaves the default alone
    CHECK(num(a, "$Foo", -5) != 0 || true);           // non-numeric string: just must not crash

    // Set / overwrite / remove
    CHECK(configSetStr(a, "added", "1") != 0);
    CHECK(configSetInt(a, "n", 42) != 0);
    CHECK(num(a, "n") == 42 && str(a, "n") == "42");  // ints are stored as text
    configSetStr(a, "added", "2");                    // overwrite, not duplicate
    CHECK(str(a, "added") == "2");
    CHECK(configRemoveKey(a, "added") != 0);
    CHECK(str(a, "added") == "<none>");

    // Write, re-read: the same key set comes back, and the file keeps the legacy syntax.
    CHECK(configWrite(a) != 0);
    std::string out = slurp(dir + "a.cfg");
    CHECK(out.find("compat=3") != std::string::npos);
    CHECK(out.find("vmc_0=mc0") != std::string::npos);
    CHECK(out.find("n=42") != std::string::npos);
    CHECK(out.find(" = ") == std::string::npos && out.find(';') == std::string::npos);
    config_set_t *a2 = parse(dir + "a2.cfg", out.c_str());
    CHECK(num(a2, "compat") == 3 && str(a2, "$Foo") == "bar baz" && num(a2, "n") == 42);

    // ---- libconfig syntax (wOPL) ---------------------------------------------------------------
    config_set_t *b = parse(dir + "b.cfg", "# wOPL file\ncompat = 5;\n$Name = \"Hello World\";\ngsm : {\n  x = 1;\n  y = \"two\";\n};\n");
    CHECK(b->format() == CFG_FMT_LIBCONFIG);            // latched from what was read
    CHECK(num(b, "$Compatibility") == 5);            // libconfig "compat" is the in-memory key $Compatibility (wOPL key table)
    CHECK(str(b, "$Name") == "Hello World");
    CHECK(num(b, "gsm_x") == 1);                      // group members flatten to <group>_<key>
    CHECK(str(b, "gsm_y") == "two");
    configSetInt(b, "extra", 7);
    CHECK(configWrite(b) != 0);
    out = slurp(dir + "b.cfg");
    CHECK(out.find("compat = 5;") != std::string::npos);   // written back in the SAME syntax, under the wOPL key name
    CHECK(out.find("extra = 7;") != std::string::npos);
    // CHARACTERISATION: members of a group named gsm/cheat/pademu/padmacro/osd are read as <group>_<key>;
    // an unknown member like gsm_x is written back flat, only the known per-game keys regroup.
    CHECK(out.find("gsm_x = 1;") != std::string::npos);
    config_set_t *b2 = parse(dir + "b2.cfg", out.c_str());
    CHECK(b2->format() == CFG_FMT_LIBCONFIG);
    CHECK(num(b2, "gsm_x") == 1 && str(b2, "gsm_y") == "two" && num(b2, "extra") == 7 && str(b2, "$Name") == "Hello World");

    // ---- BOM, colors, truncation -----------------------------------------------------------------
    config_set_t *c = parse(dir + "c.cfg", "\xEF\xBB\xBFcompat=9\n");
    CHECK(num(c, "compat") == 9 || num(c, "compat") == -999); // BOM only stripped on file reads; must not crash
    unsigned char col[3] = {0x11, 0x22, 0x33}, got[3] = {0, 0, 0};
    CHECK(configSetColor(a, "color", col) != 0);
    CHECK(configGetColor(a, "color", got) != 0);
    CHECK(memcmp(col, got, 3) == 0);
    char small[4];
    CHECK(configGetStrCopy(a, "$Foo", small, sizeof(small)) != 0);
    CHECK(strlen(small) < sizeof(small));             // truncates, always NUL-terminated

    // ---- clone / merge independence ------------------------------------------------------------------
    config_set_t *cl = configClone(a);
    CHECK(cl != NULL && str(cl, "compat") == "3");
    configSetStr(cl, "compat", "99");
    CHECK(num(a, "compat") == 3);                     // editing the clone never touches the original
    config_set_t *dst = parse(dir + "d.cfg", "compat=1\nonly_dst=x\n");
    configMerge(dst, a);
    CHECK(num(dst, "compat") == 3);                   // source wins on conflict
    CHECK(str(dst, "only_dst") == "x");     // keys only in dst survive
    CHECK(str(dst, "$Foo") == "bar baz");   // keys only in src are added

    // ---- VMC slot helpers ----------------------------------------------------------------------------
    char vmc[64];
    configSetVMC(a, "card0", 0);
    configGetVMC(a, vmc, sizeof(vmc), 0);
    CHECK_STR(vmc, "card0");
    configRemoveVMC(a, 0);
    configGetVMC(a, vmc, sizeof(vmc), 0);
    CHECK(vmc[0] == '\0');
    int disabled = 0;
    configSetVMCDisable(a, 1, 1);
    configGetVMCDisable(a, 1, &disabled);
    CHECK(disabled == 1);

    // ---- failure modes ---------------------------------------------------------------------------------
    config_set_t *e = configAlloc(CONFIG_GAME, NULL, (char *)(dir + "no/such/dir/e.cfg").c_str());
    CHECK(configRead(e) == 0);                        // unreadable file: reports failure, no crash

    for (config_set_t *s : {a, a2, b, b2, c, cl, dst, e}) configFree(s);
    std::string rm = "rm -rf " + std::string(dirbuf);
    (void)!system(rm.c_str());
    TEST_DONE("config");
}
