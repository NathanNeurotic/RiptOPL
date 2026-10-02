#ifndef __SUPPORT_BASE_INT_H
#define __SUPPORT_BASE_INT_H

#include <stdio.h> // FILE (the RA hash log)

// The mutable supportbase.cpp state that is NOT pinned into host-test extraction spans.
// .github/scripts/test_iso_cache.py, test_bdm_scan_retry.py, test_cheat_load.py and test_ra_disc.py
// compile the scan/cache state (sbReadListErrno, sbBrowseSub), the cheat search log and the RA
// deferred-hash parameters verbatim out of supportbase.cpp, so those stay file-scope free functions
// there. Everything the instance owns keeps its existing sb*/raHash* free entry points, which remain
// as thin forwarders so callers (menusys, the launch legs, ranet, discsupport) do not change.
class SupportBase
{
public:
    // Gate for sbPopulateConfig's per-game size stat (see sbSetConfigStatSize in supportbase.h).
    void setConfigStatSize(int enable) { configStatSize_ = enable; }
    int configStatSizeEnabled() const { return configStatSize_; }

    // Offer FMCB's USB IGR driver pair once per session (see sbEnsureIgrUsbDrivers in supportbase.h).
    void ensureIgrUsbDrivers(int compatmask);

#ifdef RETROACHIEVEMENTS
    // The <path>RA/hashes.txt crumb trail (see raHashLogOpen and friends in supportbase.h).
    void hashLogOpen(const char *path);
    void hashStep(const char *what);
    void hashLogAdd(const char *name, const char *startup, const char *hash);
    void hashLogClose();
#endif

private:
    int configStatSize_ = 0;
    int igrUsbAsked_ = 0; // ensureIgrUsbDrivers asks once per session
#ifdef RETROACHIEVEMENTS
    FILE *raHashlog_ = NULL;
#endif
};

extern SupportBase gSupportBase;

#endif
