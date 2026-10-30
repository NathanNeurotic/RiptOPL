/*
  Video mode round-trip self-test (diagnostic builds only). See include/vmodetest.h.

  Written for issue #132: NTSC 640x224p looks right after a cold boot but stays "crunchy" after a
  trip through PAL 640x512i (FLICKER-FREE) and back. Nothing in a code review of the mode switch
  survives it, so this measures instead of guessing: the same switch the Interface settings make,
  then the framebuffer the GS is actually showing, read back and compared byte for byte.

  If the 224p captures match, the drawn picture is identical and what changes is how the GS sends
  it to the TV (the state lines then show which value moved). If they differ, the diff box says
  where, and the TGA files show what.
*/

#ifdef __OPLDIAG

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <kernel.h>
#include <gsKit.h>

#include "include/opl.h"
#include "include/config.h"
#include "include/gui.h"
#include "include/guiint.h"
#include "include/lang.h"
#include "include/themes.h"
#include "include/fntsys.h"
#include "include/textures.h"
#include "include/uidraw.h"
#include "include/diaglog.h"
#include "include/renderman.h"
#include "include/host.h"
#include "include/vmodetest.h"

extern GSGLOBAL *gsGlobal;

#define VMT_MODE_224P  13  // rm_mode_table rows, see renderman.cpp
#define VMT_FIRST_WAIT 600 // frames before the first capture: device scans and cover art settle
#define VMT_STEP_WAIT  240 // frames after each switch: theme reload and art settle
#define VMT_MAX_W      640
#define VMT_MAX_H      512
#define VMT_LOG_SIZE   (16 * 1024)

static const int vmtSteps[] = {13, 14, 13, 1, 13, 15, 13};
#define VMT_STEP_COUNT ((int)(sizeof(vmtSteps) / sizeof(vmtSteps[0])))

enum { VMT_OFF = 0,
       VMT_UNCHECKED,
       VMT_WAITING,
       VMT_DONE };

static int vmtState = VMT_UNCHECKED;
static int vmtStep, vmtFrames, vmtBootMode;
static u8 *vmtCap;     // the capture being taken
static u8 *vmtBase[2]; // the first 224p capture: [0] menu, [1] test screen
static int vmtBaseW, vmtBaseH, vmtBaseBpp;
static int vmtMismatch[2], vmtCompared;
static char *vmtLog;
static int vmtLogLen;
static char vmtDir[256];

static void vmtPrintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void vmtPrintf(const char *fmt, ...)
{
    va_list ap;

    if (vmtLog == NULL || vmtLogLen >= VMT_LOG_SIZE - 1)
        return;
    va_start(ap, fmt);
    int n = vsnprintf(vmtLog + vmtLogLen, VMT_LOG_SIZE - vmtLogLen, fmt, ap);
    va_end(ap);
    if (n > 0)
        vmtLogLen += (n < VMT_LOG_SIZE - vmtLogLen) ? n : VMT_LOG_SIZE - 1 - vmtLogLen;
}

static void vmtPath(char *out, int size, const char *name)
{
    size_t len = strlen(vmtDir);
    snprintf(out, size, "%s%s%s", vmtDir, len == 0 || vmtDir[len - 1] == '/' || vmtDir[len - 1] == ':' ? "" : "/", name);
}

static void vmtSaveLog(void)
{
    char path[300];
    int fd;

    vmtPath(path, sizeof(path), "VMODERES.TXT");
    if ((fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0)
        return;
    write(fd, vmtLog, vmtLogLen);
    close(fd);
}

/* GS local -> host transfer of the whole buffer in one DMA. Same register sequence as the ps2sdk
   libdebug ps2_screenshot() (AFL 2.0, ps2dev), but into a 64-byte aligned buffer: its own per-line
   buffers are plain static u32 arrays, and the DMAC ignores the low 4 address bits. */
#define VMT_D1_CHCR  ((volatile u32 *)0x10009000)
#define VMT_D1_MADR  ((volatile u32 *)0x10009010)
#define VMT_D1_QWC   ((volatile u32 *)0x10009020)
#define VMT_GS_CSR   ((volatile u64 *)0x12001000)
#define VMT_BUSDIR   ((volatile u64 *)0x12001040)
#define VMT_VIF1STAT ((volatile u32 *)0x10003c00)
#define VMT_VIF1FIFO ((volatile u128 *)0x10005000)

static int vmtDownload(void *dest, u32 vramByteAddr, int w, int h, int psm, int bpp)
{
    static union
    {
        u32 w[4];
        u128 q;
    } enablePath3 __attribute__((aligned(16))) = {{0x06000000, 0, 0, 0}}; // MSKPATH3(0)
    u64 chain[14] __attribute__((aligned(16)));
    u32 *c32 = (u32 *)chain;
    u64 *c64 = chain + 2;
    u32 qwc = (u32)(w * h * bpp) / 16;
    u32 prevImr;

    c32[0] = 0;                               // NOP
    c32[1] = 0x06008000;                      // MSKPATH3(1)
    c32[2] = 0x13000000;                      // FLUSHA
    c32[3] = 0x50000006;                      // DIRECT 6 qwords
    c64[0] = 5 | (1ULL << 15) | (1ULL << 60); // GIFTAG NLOOP 5, EOP, PACKED, NREG 1
    c64[1] = 0x0e;                            // A+D
    c64[2] = ((u64)(vramByteAddr / 256)) | ((u64)(w / 64) << 16) | ((u64)psm << 24);
    c64[3] = 0x50; // BITBLTBUF
    c64[4] = 0;
    c64[5] = 0x51; // TRXPOS
    c64[6] = (u64)w | ((u64)h << 32);
    c64[7] = 0x52; // TRXREG
    c64[8] = 0;
    c64[9] = 0x61; // FINISH
    c64[10] = 1;
    c64[11] = 0x53; // TRXDIR: local -> host

    if (*VMT_D1_CHCR & 0x0100)
        return 0;
    prevImr = GsPutIMR(GsGetIMR() | 0x0200);
    u32 prevChcr = *VMT_D1_CHCR;
    *VMT_GS_CSR = 2; // clear FINISH
    FlushCache(0);
    *VMT_D1_QWC = 7;
    *VMT_D1_MADR = (u32)chain;
    *VMT_D1_CHCR = 0x101;
    asm volatile("sync.l");
    while (*VMT_D1_CHCR & 0x0100)
        ;
    while ((*VMT_GS_CSR & 2) == 0)
        ;
    while (*VMT_VIF1STAT & 0x1f000000)
        ;
    *VMT_VIF1STAT = 1 << 23; // FDR: VIF1 FIFO reads from the GS
    *VMT_BUSDIR = 1;
    FlushCache(0);
    *VMT_D1_QWC = qwc;
    *VMT_D1_MADR = (u32)dest;
    *VMT_D1_CHCR = 0x100;
    asm volatile("sync.l");
    while (*VMT_D1_CHCR & 0x0100)
        ;
    *VMT_D1_CHCR = prevChcr;
    asm volatile("sync.l");
    *VMT_VIF1STAT = 0;
    *VMT_BUSDIR = 0;
    GsPutIMR(prevImr);
    *VMT_GS_CSR = 2;
    *VMT_VIF1FIFO = enablePath3.q;
    return 1;
}

static int vmtBpp(int psm)
{
    return psm == GS_PSM_CT32 ? 4 : psm == GS_PSM_CT24 ? 3 :
                                                         2;
}

// 24-bit TGA, top-left origin. Width and height come from the framebuffer, not the 640x480 grid.
static void vmtSaveTga(const char *name, const u8 *px, int w, int h, int bpp)
{
    static u8 row[VMT_MAX_W * 3];
    u8 head[18] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, (u8)w, (u8)(w >> 8), (u8)h, (u8)(h >> 8), 24, 0x20};
    char path[300];
    int fd;

    vmtPath(path, sizeof(path), name);
    if ((fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) {
        vmtPrintf("  cannot write %s\r\n", path);
        return;
    }
    write(fd, head, sizeof(head));
    for (int y = 0; y < h; y++) {
        const u8 *s = px + y * w * bpp;
        for (int x = 0; x < w; x++, s += bpp) {
            if (bpp == 2) { // CT16(S): 5:5:5 RGB
                u16 v = s[0] | (s[1] << 8);
                row[x * 3 + 0] = ((v >> 10) & 31) << 3;
                row[x * 3 + 1] = ((v >> 5) & 31) << 3;
                row[x * 3 + 2] = (v & 31) << 3;
            } else { // GS memory order is R, G, B(, A); TGA wants B, G, R
                row[x * 3 + 0] = s[2];
                row[x * 3 + 1] = s[1];
                row[x * 3 + 2] = s[0];
            }
        }
        write(fd, row, w * 3);
    }
    close(fd);
}

static void vmtLogState(const char *what)
{
    int wn, hn;
    char fonts[512];

    rmGetScreenExtentsNative(&wn, &hn);
    fntDiagDescribe(fonts, sizeof(fonts));
    vmtPrintf("%s: vmode %d, %d s after boot\r\n", what, gVMode, (int)(clock() / CLOCKS_PER_SEC));
    vmtPrintf("  gs Mode %d Interlace %d Field %d %dx%d PSM 0x%x DoubleBuffering %d Dithering %d\r\n",
              gsGlobal->Mode, gsGlobal->Interlace, gsGlobal->Field, gsGlobal->Width, gsGlobal->Height, gsGlobal->PSM,
              gsGlobal->DoubleBuffering, gsGlobal->Dithering);
    vmtPrintf("  gs StartX %d+%d StartY %d+%d MagH %d MagV %d DW %d DH %d OffsetX %d OffsetY %d\r\n",
              gsGlobal->StartX, gsGlobal->StartXOffset, gsGlobal->StartY, gsGlobal->StartYOffset, gsGlobal->MagH,
              gsGlobal->MagV, gsGlobal->DW, gsGlobal->DH, gsGlobal->OffsetX, gsGlobal->OffsetY);
    vmtPrintf("  gs ScreenBuffer 0x%x 0x%x ActiveBuffer %d FirstFrame %d TexturePointer 0x%x CurrentPointer 0x%x\r\n",
              (unsigned)gsGlobal->ScreenBuffer[0], (unsigned)gsGlobal->ScreenBuffer[1], gsGlobal->ActiveBuffer,
              gsGlobal->FirstFrame, (unsigned)gsGlobal->TexturePointer, (unsigned)gsGlobal->CurrentPointer);
    vmtPrintf("  rm native %dx%d PAR %.4f scale(640,480) -> (%d,%d) wide %d\r\n", wn, hn, (double)rmGetPAR(),
              rmScaleX(640), rmScaleY(480), gWideScreen);
    vmtPrintf("  pos x %d y %d overscan %d, font size setting %d, theme usedHeight %d font0 %d\r\n", gXOff, gYOff,
              gOverscan, gFontSize, gTheme ? gTheme->usedHeight : -1, gTheme ? gTheme->fonts[0] : -1);
    vmtPrintf("  fonts%s\r\n", fonts);
}

static void vmtDrawTestScreen(void)
{
    int font = gTheme ? gTheme->fonts[0] : FNT_DEFAULT;

    rmStartFrame();
    rmDrawRect(0, 0, gGuiScreenWidth, gGuiScreenHeight, gColBlack);
    fntRenderString(font, 40, 60, ALIGN_NONE, 0, 0, "The quick brown fox jumps over the lazy dog 0123456789", gColWhite);
    fntRenderString(font, 40, 110, ALIGN_NONE, 0, 0, "ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz", gColWhite);
    fntRenderString(FNT_DEFAULT, 40, 160, ALIGN_NONE, 0, 0, "Built-in font: Settings > Interface > Video Mode", gColWhite);
    guiDrawIconAndText(CROSS_ICON, _STR_BACK, font, 40, 220, gColWhite);
    guiDrawIconAndText(CIRCLE_ICON, _STR_BACK, font, 240, 220, gColWhite);
    for (int i = 0; i < 8; i++) {
        rmDrawLine(40, 280 + i * 8, 600, 280 + i * 8, gColWhite); // one line every 8 rows
        rmDrawLine(40 + i * 70, 280, 40 + i * 70, 400, gColWhite);
    }
    rmDrawRect(40, 410, 560, 30, gColWhite);
    rmEndFrame();
}

// Read back the buffer the GS is displaying, save it, and compare it with the first 224p capture.
static void vmtCapture(int kind)
{
    char name[32];
    int w = gsGlobal->Width, h = gsGlobal->Height, psm = gsGlobal->PSM, bpp = vmtBpp(psm);
    // rmEndFrame pointed DISPFB2 at the buffer it just drew and then flipped ActiveBuffer to the other one.
    u32 shown = gsGlobal->ScreenBuffer[(gsGlobal->ActiveBuffer ^ 1) & 1];

    if (w > VMT_MAX_W || h > VMT_MAX_H || (w % 64) != 0) {
        vmtPrintf("  %s: %dx%d not captured\r\n", kind ? "test screen" : "menu", w, h);
        return;
    }
    if (!vmtDownload(vmtCap, shown, w, h, psm, bpp)) {
        vmtPrintf("  %s: GS download busy, not captured\r\n", kind ? "test screen" : "menu");
        return;
    }
    snprintf(name, sizeof(name), "VMT%02d_%02d%c.TGA", vmtStep, gVMode, kind ? 'P' : 'M');
    vmtSaveTga(name, vmtCap, w, h, bpp);

    u32 sum = 2166136261u; // FNV-1a over the pixels
    for (int i = 0; i < w * h * bpp; i++)
        sum = (sum ^ vmtCap[i]) * 16777619u;
    vmtPrintf("  %s: %s %dx%d hash %08x\r\n", kind ? "test screen" : "menu", name, w, h, (unsigned)sum);

    if (gVMode != VMT_MODE_224P)
        return;
    if (vmtBase[kind] == NULL) {
        if ((vmtBase[kind] = (u8 *)memalign(64, w * h * bpp)) != NULL) {
            memcpy(vmtBase[kind], vmtCap, w * h * bpp);
            vmtBaseW = w, vmtBaseH = h, vmtBaseBpp = bpp;
        }
        return;
    }
    if (w != vmtBaseW || h != vmtBaseH || bpp != vmtBaseBpp) {
        vmtPrintf("    DIFFERS from the first 224p capture: %dx%d %d bytes/px there\r\n", vmtBaseW, vmtBaseH, vmtBaseBpp);
        vmtMismatch[kind]++;
        return;
    }
    int diff = 0, x0 = w, y0 = h, x1 = -1, y1 = -1, maxd = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const u8 *a = vmtCap + (y * w + x) * bpp, *b = vmtBase[kind] + (y * w + x) * bpp;
            int d = 0;
            for (int c = 0; c < (bpp == 4 ? 3 : bpp); c++) {
                int e = a[c] > b[c] ? a[c] - b[c] : b[c] - a[c];
                if (e > d)
                    d = e;
            }
            if (d) {
                diff++;
                if (d > maxd)
                    maxd = d;
                if (x < x0)
                    x0 = x;
                if (x > x1)
                    x1 = x;
                if (y < y0)
                    y0 = y;
                if (y > y1)
                    y1 = y;
            }
        }
    if (kind)
        vmtCompared++;
    if (diff == 0) {
        vmtPrintf("    identical to the first 224p capture\r\n");
    } else {
        vmtPrintf("    DIFFERS from the first 224p capture: %d px, box (%d,%d)-(%d,%d), max channel diff %d\r\n", diff, x0, y0, x1, y1, maxd);
        vmtMismatch[kind]++;
    }
}

static void vmtSwitch(int mode)
{
    int prev = gVMode;

    gVMode = mode;
    screenPosModeChanged(prev, gXOff, gYOff, gOverscan); // as the Interface dialog does, before applyConfig
    host::app().applyConfig(-1, -1, 1); // the app layer, as guisettings reaches it
}

static void vmtFinish(void)
{
    vmtPrintf("\r\nboot mode %d%s\r\n", vmtBootMode, vmtBootMode == VMT_MODE_224P ? " (cold 224p baseline)" : " (NOT a cold 224p baseline: the first 224p capture came from a switch)");
    if (vmtCompared == 0)
        vmtPrintf("VERDICT: nothing compared\r\n");
    else if (vmtMismatch[1] == 0)
        vmtPrintf("VERDICT: the 224p test screen is pixel-identical after every round trip (%d compared); menu mismatches %d (animation and the clock also move these)\r\n", vmtCompared, vmtMismatch[0]);
    else
        vmtPrintf("VERDICT: the 224p test screen CHANGED after a round trip (%d of %d differ); menu mismatches %d\r\n", vmtMismatch[1], vmtCompared, vmtMismatch[0]);
    vmtSaveLog();
    diagLog("vmodetest: finished, %d of %d test screens differ", vmtMismatch[1], vmtCompared);
    guiWarning("Video mode self-test finished: VMODERES.TXT", 6);
    free(vmtCap);
    free(vmtBase[0]);
    free(vmtBase[1]);
    free(vmtLog);
    vmtCap = vmtBase[0] = vmtBase[1] = NULL;
    vmtLog = NULL;
    vmtState = VMT_DONE;
}

void vmodeTestFrame(void)
{
    if (vmtState == VMT_OFF || vmtState == VMT_DONE)
        return;

    if (vmtState == VMT_UNCHECKED) {
        char path[300];
        int fd;

        vmtState = VMT_OFF;
        snprintf(vmtDir, sizeof(vmtDir), "%s", configGetHomePath());
        vmtPath(path, sizeof(path), "VMODETEST");
        if ((fd = open(path, O_RDONLY)) < 0)
            return;
        close(fd);
        vmtCap = (u8 *)memalign(64, VMT_MAX_W * VMT_MAX_H * 4);
        vmtLog = (char *)malloc(VMT_LOG_SIZE);
        if (vmtCap == NULL || vmtLog == NULL) {
            free(vmtCap);
            free(vmtLog);
            vmtCap = NULL;
            vmtLog = NULL;
            return;
        }
        vmtLogLen = 0;
        vmtBootMode = gVMode;
        vmtPrintf("RiptOPL %s video mode self-test (issue #132), settings folder %s\r\n", OPL_VERSION, vmtDir);
        vmtLogState("boot");
        diagLog("vmodetest: armed by %s", path);
        vmtStep = 0;
        vmtFrames = VMT_FIRST_WAIT;
        vmtState = VMT_WAITING;
        if (gVMode != vmtSteps[0])
            vmtSwitch(vmtSteps[0]);
        return;
    }

    if (--vmtFrames > 0)
        return;

    char what[48];
    snprintf(what, sizeof(what), "\r\nstep %d", vmtStep);
    vmtLogState(what);
    vmtCapture(0); // the menu frame that was just shown
    vmtDrawTestScreen();
    vmtCapture(1);
    vmtSaveLog(); // after every step, so a hang still leaves the steps before it

    if (++vmtStep >= VMT_STEP_COUNT) {
        vmtFinish();
        return;
    }
    vmtSwitch(vmtSteps[vmtStep]);
    vmtFrames = VMT_STEP_WAIT;
}

#endif
