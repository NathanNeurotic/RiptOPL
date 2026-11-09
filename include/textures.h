#ifndef __TEXTURES_H
#define __TEXTURES_H

enum INTERNAL_TEXTURE {
    LOAD0_ICON = 0,
    LOAD1_ICON,
    LOAD2_ICON,
    LOAD3_ICON,
    LOAD4_ICON,
    LOAD5_ICON,
    LOAD6_ICON,
    LOAD7_ICON,
    BDM_ICON,
    USB_ICON,
    ILINK_ICON,
    MX4SIO_ICON,
    HDD_BD_ICON,
    MMCE_ICON,
    HDD_ICON,
    ETH_ICON,
    UDP_ICON, // UDPBD network-boot device icon (theme override "udp_bd", embedded udp_bd_png)
    APP_ICON,
    INDEX_0,
    INDEX_1,
    INDEX_2,
    INDEX_3,
    INDEX_4,
    LEFT_ICON,
    RIGHT_ICON,
    CROSS_ICON,
    TRIANGLE_ICON,
    CIRCLE_ICON,
    SQUARE_ICON,
    SELECT_ICON,
    START_ICON,
    /* currently unused.
    UP_ICON,
    DOWN_ICON,
    L1_ICON,
    L2_ICON,
    R1_ICON,
    R2_ICON, */
    L3_ICON,     // re-enabled for the VCD "L3 = toggle VCD view" hint
    R3_ICON,     // re-enabled for the Favourites "R3 = Favourite" hint
    FAV_ICON,    // Favourites tab icon
    FAV_MARK,    // star drawn next to favourited items
    SETTINGS_BG, // optional settings/menu background (use_settings_bg theme flag; see guiDrawBGSettings)
    COVER_DEFAULT,
    DISC_DEFAULT,
    SCREEN_DEFAULT,
    INCEBTION_PICTURE,
    IP_PICTURE,
    COVERAPP_DEFAULT,
    MISSING_PICTURE,
    NO_DEVICE_PICTURE,
    NO_RATING_PICTURE,
    SCREENS_OVERLAY,
    ELF_FORMAT,
    HDL_FORMAT,
    ISO_FORMAT,
    VCD_FORMAT, // VCD (PS1-via-POPSTARTER) format glyph -- the VCD equivalent of ISO
    ZSO_FORMAT,
    UL_FORMAT,
    APP_MEDIA,
    CD_MEDIA,
    DVD_MEDIA,
    ASPECT_STD,
    ASPECT_WIDE,
    ASPECT_WIDE1,
    ASPECT_WIDE2,
    DEVICE_1,
    DEVICE_2,
    DEVICE_3,
    DEVICE_4,
    DEVICE_5,
    DEVICE_6,
    DEVICE_ALL,
    RATING_0,
    RATING_1,
    RATING_2,
    RATING_3,
    RATING_4,
    RATING_5,
    SCAN_240P,
    SCAN_240P1,
    SCAN_480I,
    SCAN_480P,
    SCAN_480P1,
    SCAN_480P2,
    SCAN_480P3,
    SCAN_480P4,
    SCAN_480P5,
    SCAN_576I,
    SCAN_576P,
    SCAN_720P,
    SCAN_1080I,
    SCAN_1080I2,
    SCAN_1080P,
    VMODE_MULTI,
    VMODE_NTSC,
    VMODE_PAL,
    LOGO_PICTURE,
    // Optional animated boot-logo frames (embedded build assets). When present
    // they are cycled on the boot splash like the loading icon; when absent the
    // single LOGO_PICTURE is used. Keep contiguous (animation indexes LOGO0+n).
    LOGO0_PICTURE,
    LOGO1_PICTURE,
    LOGO2_PICTURE,
    LOGO3_PICTURE,
    LOGO4_PICTURE,
    LOGO5_PICTURE,
    LOGO6_PICTURE,
    CASE_OVERLAY,
    APPS_CASE_OVERLAY,
    PS1_SYSTEM, // #System console glyph (PS1) -- FR #49
    PS2_SYSTEM, // #System console glyph (PS2)
    // second overlay layer (b2 "foliage") drawn over the case (b1) -- graphics-team two-layer frame
    CASE_OVERLAY2,
    // UDPFS network-boot device icon (theme override "udp_fs", embedded udp_fs_png); UDPBD uses UDP_ICON
    UDPFS_ICON,
    L1_ICON,          // Settings peer-page navigation hint
    R1_ICON,          // Settings peer-page navigation hint
    PSBBN_BACKGROUND, // <PSBBN Flow> nebula stage (theme override "psbbn_bg")
#ifdef BUILTIN_KORIUM
    CASE_PS1,
    DISC_PS1,
    PLAYERS_1,
    PLAYERS_2,
    PLAYERS_3,
    PLAYERS_4,
#endif
    TECHWRITESCODE_LOGO, // boot-splash credit mark, embedded techwritescode_png
    // BdmIndex dots for 2-4 visible mass devices (dot <p> of <n>); five use INDEX_0..INDEX_4.
    // Appended, not placed after INDEX_4: texture ids are positional.
    INDEX2_0,
    INDEX2_1,
    INDEX3_0,
    INDEX3_1,
    INDEX3_2,
    INDEX4_0,
    INDEX4_1,
    INDEX4_2,
    INDEX4_3,
#ifdef RETROACHIEVEMENTS
    // RA mark over a tracked game's cover (theme override "ra_mark"). Appended last:
    // texId is positional.
    RA_MARK,
#endif

    TEXTURES_COUNT
};

// The pictures RiptOPL draws that stock OPL never loaded. A theme made for stock OPL cannot ship them, so its
// use_default=0 ("only my own pictures") cannot mean "no picture" for these: thmLoadResource gives it the built-in
// ones (Berion's old-theme fallback). Host test: tests/host/test_wallpaper.cpp.
static inline int texNewerThanStockOpl(int texId)
{
    switch (texId) {
        case MMCE_ICON:
        case UDP_ICON:
        case UDPFS_ICON:
        case FAV_ICON:
        case FAV_MARK:
        case L1_ICON:
        case R1_ICON:
        case L3_ICON:
        case R3_ICON:
            return 1;
        default:
            return 0;
    }
}

// The first bytes of a PNG file (signature, then the IHDR chunk: 29 bytes) describe a picture of at most maxSide
// pixels on each side with an 8-bit palette, i.e. one that loads as GS_PSM_T8 (User Wallpaper). Checked before
// the picture is decoded, so a too large one never costs the memory to decode it.
#define TEX_PNG_HEADER_LEN 29
static inline int texPngHeaderIsSmallT8(const unsigned char *h, int len, unsigned int maxSide)
{
    static const unsigned char head[16] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13, 'I', 'H', 'D', 'R'};
    if (h == 0 || len < TEX_PNG_HEADER_LEN)
        return 0;
    for (int i = 0; i < 16; i++)
        if (h[i] != head[i])
            return 0;
    unsigned int w = ((unsigned int)h[16] << 24) | ((unsigned int)h[17] << 16) | ((unsigned int)h[18] << 8) | h[19];
    unsigned int ht = ((unsigned int)h[20] << 24) | ((unsigned int)h[21] << 16) | ((unsigned int)h[22] << 8) | h[23];
    return w > 0 && ht > 0 && w <= maxSide && ht <= maxSide && h[24] == 8 /* bit depth */ && h[25] == 3 /* palette */;
}

constexpr int ERR_BAD_FILE = -1; // open() failed = the file is not there (genuine absence)
constexpr int ERR_READ_STRUCT = -2;
constexpr int ERR_INFO_STRUCT = -3;
constexpr int ERR_SET_JMP = -4;
constexpr int ERR_BAD_DIMENSION = -5;
constexpr int ERR_MISSING_ALPHA = -6;
constexpr int ERR_BAD_DEPTH = -7;
constexpr int ERR_LOAD_ABORTED = -8;
// The file OPENED but its bytes could not be staged/read (read() error on a contended bus, OOM, empty
// file). Distinct from ERR_BAD_FILE (absent) so the VCD art miss-memo caches only GENUINE absence and
// re-probes a transient/present failure instead of false-hiding a real cover (#120). Callers that only
// test the sign are unaffected.
constexpr int ERR_FILE_IO = -9;

int texLookupInternalTexId(const char *name);
int texLoadInternal(GSTEXTURE *texture, int texId);
int texLoadFromMemory(GSTEXTURE *texture, const void *buf, u32 size);
int texDiscoverLoad(GSTEXTURE *texture, const char *path, int texId);
// Game art for itemGetImage: <path> as PNG, JPG or JPEG in any letter case, then OPL Manager's name for it.
int texDiscoverArt(GSTEXTURE *texture, const char *path);
// A PNG or JPG game art picture already in RAM (an ART .tar member); a JPG is shrunk to a full screen when
// background is set, to 512 on a side otherwise.
int texLoadArtFromMemory(GSTEXTURE *texture, const void *buf, u32 size, int background);
// OPL Manager's other name for a piece of art in an ART folder (_BG <-> _BG_00, _SCR <-> _SCR_00,
// _SCR2 <-> _SCR_01) into out. 1 when there is one.
int texArtAlias(const char *stem, char *out, int outSize);

// Debug HUD (OE): staged-art opens that failed with errno != ENOENT on a non-MMCE device, i.e. covers
// branded permanently absent on evidence that was not "no such file". See texStagedOpenIsAbsence.
extern unsigned int gTexStagedOpenNonEnoent;

// Debug HUD (O:<hit>/<miss>): cost in ms of the last staged art open() and of the last one that
// FAILED. Separates "the directory walk for a missing file is the cost" from "the transfer is".
extern int gTexLastOpenMs;
extern int gTexLastMissOpenMs;

/* Debug HUD (W<ms>@<pending><m|h>): the WORST staged open() of the session, with the ioman queue
 * depth sampled AT that open and whether it was a miss. Answers the one question the last-value
 * fields cannot: is a multi-second open contending with the io worker, or alone on the device? */
void texDebugWorstOpen(int *ms, int *pending, int *wasMiss, int *menu, int *bgm);
void texSetLoadAbortFlag(volatile int *abortRequested);
void texFree(GSTEXTURE *texture);

#endif
