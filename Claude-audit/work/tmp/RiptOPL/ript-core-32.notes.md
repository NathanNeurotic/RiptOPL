# ript-core-32 notes

Done (all lines read, re-read on resume): src/sources/supportbase.cpp 1-3181; include/udpfssupport.h 1-78
Verification done; writing findings JSON.

Kept: SupportBase class+supportbaseint.h (only supportbase.cpp uses it); sbPopulateConfigIn size path dup of sbCreatePath_name; mc/mmce + pfs/rom alias dup; 3 neutrino case tables; updateISOGameList unreachable branch; commented-out USBLD size 650-665; sbNeutrinoDeinitException forwarder (7 callers + test_handoff_probe.py:74); RA hash log epilogue x5; small redundancies; stale comments; udpfssupport.h private class exposure (~25 includers, class only used in udpfssupport.cpp).
Dropped: sbIsSameSize/sbCreateSemaphore/udpfs header names all used; 2794 zeroing not redundant (path NULL early return); sbPopulateConfig wrapper (5 callers, keep).
