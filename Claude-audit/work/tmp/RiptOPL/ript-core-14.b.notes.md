# ript-core-14.b notes (lens defects)

Done: all 11 files read in full (artindex.h, artindex.cpp, artload.cpp, artqueue.h, artqueue.cpp, artworker.h, artworker.cpp, atlas.h, atlas.cpp, coverflow.cpp, fntsys.h)

Candidates (checked):
- KEEP atlas.h:42-52 / atlas.cpp:42-47 | UniquePtr<Node> tree destructor inlined recursively: nm Atlas::~Atlas 0x1958 + unique_ptr<Node>::~unique_ptr 0x1d10 + DestroyFree part 0xa8 = 14,096 B; atlas.o text 15,448 (per-object.tsv:92), -O2
- KEEP artload.cpp:743-751 | every non-ERR_BAD_FILE result treated transient -> undecodable file (corrupt/truncated, 16-bit, >1024px PNG, empty file textures.cpp:723-726) re-requested every frame; settle gate SIO2-only texcache.cpp:372-378; texLoadArtFile re-reads as JPG textures.cpp:1328-1334
- KEEP artindex.cpp:191-201,275-286 | 16 KB hashes + 32 KB pics + 16 KB names floor per slot, never trimmed; invalidate keeps buffers
- dropped: ChangeThreadPriority(gArtW.threadId) (WakeupThread uses same id and works); loadsCancelled early-out leaves qr dangling (no menu frame after shutdown; guiFadeOutForHandoff draws text only); cacheShutdownArtLoads double read of current (caller higher prio); FreeType mono strike pitch (speculative, fntsys not my unit); atlas OOM both leaves (theoretical); coverflow per-frame warming (cheap, not measured); gArtIndex 672 B .data (packed ELF)
