// Link-time stand-ins for the globals/functions config.cpp and util.cpp reach for. Include once, after the
// real sources, in a test that exercises them.
#pragma once
#include <sys/stat.h>
#ifndef HOST_ENV_EXTERN_DEBUG
int gEnableDebug = 0;
#endif
char gBaseMCDir[64] = "mc0:/";
char gOPLPart[64] = "";
int gLastSaveErrno = 0;
unsigned char ps2_ip[4], ps2_netmask[4], ps2_gateway[4];
void bgmMute(void) {}
void bgmUnMute(void) {}
int hddReadSectors(u32, u32, void *) { return -1; }
int fileXioGetStat(const char *path, iox_stat_t *st)
{
    struct stat s;
    if (stat(path, &s) < 0) return -1;
    st->mode = s.st_mode; st->size = s.st_size;
    return 0;
}
// util.cpp also drags in bin2c blobs, the zso reader and a GUI warning hook; none are exercised here.
unsigned char icon_sys[1]; unsigned int size_icon_sys = 0;
unsigned char save_icn[1]; unsigned int size_save_icn = 0;
int probed_fd = -1; u32 probed_lba = 0;
void ziso_init(ZISO_header *, u32) {}
int ziso_read_sector(u8 *, u32, unsigned int) { return 0; }
// util.cpp reaches the GUI through host::ui(); the tests give it a recording no-op.
#include "include/host.h"
namespace
{
struct NullUi final : UiServices
{
    int msgBox(const char *, int, struct UIItem *) override { return 0; }
    void warning(const char *, int) override {}
    void warningId(int, int) override {}
    void raNotice(const char *, const char *) override {}
    void bootStatusSticky(const char *) override {}
    void bootStatusStickyCopy(const char *) override {}
    void renderTextScreen(const char *) override {}
    void manageCheats() override {}
    int showVcdUsbMode() override { return 0; }
    int promptRebootIop() override { return 0; }
    void drawBackgroundPlasma() override {}
    void lock() override {}
    void unlock() override {}
} gNullUi;
} // namespace
namespace host
{
UiServices &ui() { return gNullUi; }
} // namespace host
