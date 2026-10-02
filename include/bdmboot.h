#ifndef __BDMBOOT_H
#define __BDMBOOT_H

#include "include/bdmsupport.h" // u32 (same chain bdmint.h relies on)

// Boot directory resolution on BDM (src/sources/bdmboot.cpp): maps a launcher path on a
// mass/ilink/mx4sio/udpbd device to the concrete mass slot and settings home. The resolver is
// stateless -- every input arrives through the parameters -- but the resolution ladder is one
// cohesive unit, owned by this class. One instance; the bdmResolveBootDir* entry points
// declared in bdmsupport.h stay free forwarders here so callers (settingsio, autolaunch) do not
// change.
class BdmBoot
{
public:
    // Full-budget resolver for explicit Custom Settings Path and save-time hotplug recovery
    // (3000 + 10000 + 3000 + 8000 ms ladder). Contract: see bdmResolveBootDir in bdmsupport.h.
    int resolve(char *bootDir, int bootDirSize, const char *elfName, int *ioBdmType);
    // First-config-read variant on a shorter bounded ladder (1500 + 5000 + 1500 + 3500 ms), so a
    // missing first-run config reaches defaults promptly. Contract: see bdmResolveBootDirBootstrap.
    int resolveBootstrap(char *bootDir, int bootDirSize, const char *elfName, int *ioBdmType);

private:
    int resolveWithBudget(char *bootDir, int bootDirSize, const char *elfName, int *ioBdmType,
                          u32 initialBudgetMs, u32 ataInitialBudgetMs,
                          u32 mx4sioExtraBudgetMs, u32 expensiveExtraBudgetMs);
    int resolveLiteralMassSlot(int slot, int *ioBdmType,
                               u32 initialBudgetMs, u32 ataInitialBudgetMs,
                               u32 mx4sioExtraBudgetMs, u32 expensiveExtraBudgetMs);
    static int readLiteralMassIdentity(int slot, int *outType);
    static int bootDirPresent(int slot, const char *tail);
    static int bootElfPresent(int slot, const char *tail, const char *elfName);
    static void rewriteBootDir(char *bootDir, int bootDirSize, int slot, const char *tail);
};

extern BdmBoot gBdmBoot;

#endif
