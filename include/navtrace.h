#ifndef OPL_NAV_TRACE_H
#define OPL_NAV_TRACE_H

enum NavTraceWork { NAVTRACE_ART = 1,
                    NAVTRACE_HIDDEN,
                    NAVTRACE_SAVEICON,
                    NAVTRACE_INFO,
                    NAVTRACE_SFX,
                    NAVTRACE_ACT };

#ifdef OPL_NAV_INVESTIGATION
void navTraceBegin();
void navTracePad(unsigned int index, int port, int slot, int state, int length, int mode,
                 unsigned int buttons, const void *dma);
unsigned int navTraceSample(unsigned int index, int connected, int valid, unsigned int buttons);
unsigned int navTraceUnknown();
void navTraceFrame(unsigned int before, unsigned int after, unsigned int elapsed, int clean);
void navTraceKey(int id, int edge, int timer);
void navTraceWork(int kind, int begin, const char *path, int result);
void navTraceSelection(int page, int source, int row);
void navTraceAutoSave();
const char *navTraceStatus();
int navTraceCandidate();
void navTraceDraw();
#else
inline void navTraceBegin()
{
}
inline void navTracePad(unsigned int, int, int, int, int, int, unsigned int, const void *) {}
inline unsigned int navTraceSample(unsigned int, int, int valid, unsigned int buttons) { return valid ? buttons : 0; }
inline unsigned int navTraceUnknown() { return 0; }
inline void navTraceFrame(unsigned int, unsigned int, unsigned int, int) {}
inline void navTraceKey(int, int, int) {}
inline void navTraceWork(int, int, const char *, int) {}
inline void navTraceSelection(int, int, int) {}
inline void navTraceAutoSave() {}
inline const char *navTraceStatus() { return ""; }
inline int navTraceCandidate() { return 0; }
inline void navTraceDraw() {}
#endif

#endif
