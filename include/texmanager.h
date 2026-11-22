#ifndef __TEXMANAGER_H
#define __TEXMANAGER_H

#include <gsKit.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GSKIT_TEXMGR_BIND_DEFERRED
#define GSKIT_TEXMGR_BIND_DEFERRED 0x80000000u
#endif

void gsKit_TexManager_setQueueLatency(int frames);
void gsKit_TexManager_commitFrame(int completed);
int gsKit_TexManager_starved(void);
void gsKit_TexManager_release(GSGLOBAL *gsGlobal, GSTEXTURE *tex);
void gsKit_TexManager_flush(void);

#ifdef __cplusplus
}
#endif

#endif
