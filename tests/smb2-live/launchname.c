/* Host check, no server: the in-game SMB reader opens every image name the menu can launch whole.

   The EE's SMB launch (in its ethsupport source) writes the image's file name into the settings
   block, cdvdman_settings_smb.filename, with snprintf, and modules/iopcore/cdvdman/device-smb.c builds the
   path it opens from that block: "\<prefix>\DVD\<file name>" (CD for a CD), or for a USBExtreme game
   "\<prefix>\<file name>.00" and on. This compiles device-smb.c whole, with the real cdvd_config.h and
   an SMB layer that only records the paths it is asked to open, then for each format, name length
   (1, either side of each length the old 160-byte field cut, and ISO_GAME_NAME_MAX), prefix (none and
   the longest the block holds) and medium, fills the block with the EE's own snprintf calls, as below,
   and checks that it holds the whole file name and that device-smb.c opens exactly the expected path.
   .github/scripts/test_smb_launch_name.py checks that the snprintf calls below are the EE's, builds
   this with AddressSanitizer and UBSan, and runs it; longname.c runs the longest names against Samba. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smstcpip.h"
#include "internal.h"

#ifndef ISO_GAME_NAME_MAX
#error test_smb_launch_name.py passes ISO_GAME_NAME_MAX from include/supportbase.h
#endif

// cdvdman.c's patch zone, which the EE fills before the IOP reset.
struct cdvdman_settings_smb cdvdman_settings;

// What device-smb.c needs from the rest of cdvdman and from the IOP kernel.
struct irx_export_table _exp_oplsmb;

int RegisterLibraryEntries(struct irx_export_table *exports)
{
    (void)exports;
    return 0;
}

void cdvdman_vmc_io_begin(int is_write)
{
    (void)is_write;
}

void cdvdman_vmc_io_end(void)
{
}

static int ps2ip_missing;

int getModInfo(char *modname, modinfo_t *info)
{
    static void *exports[32];

    (void)modname;
    if (ps2ip_missing)
        return 0;
    info->version = 0x0100;
    info->exports = exports;
    return 1;
}

// The SMB layer (smb.c or smb2.c in the game): it records the paths device-smb.c opens.
int smb_io_sema = -1;
static int negotiate_calls;
static char opened[ISO_MAX_PARTS + 1][1024];
static int nopened;
static int failure_mode, fail_open_at = -1;
static int session_calls, tree_calls, disconnect_calls, close_all_calls, read_calls;

int smb_NegotiateProtocol(char *SMBServerIP, int SMBServerPort, char *Username, char *Password, u32 *capabilities, OplSmbPwHashFunc_t hash_callback)
{
    (void)SMBServerIP;
    (void)SMBServerPort;
    (void)Username;
    (void)Password;
    (void)hash_callback;
    *capabilities = 0;
    negotiate_calls++;
    return failure_mode == 7 ? -1 : 1;
}

int smb_SessionSetupAndX(u32 capabilities)
{
    (void)capabilities;
    session_calls++;
    return failure_mode == 1 ? -1 : 1;
}

int smb_TreeConnectAndX(char *ShareName)
{
    (void)ShareName;
    tree_calls++;
    return failure_mode == 2 ? -1 : 1;
}

int smb_OpenAndX(char *filename, u8 *FID, int Write)
{
    (void)Write;
    if (nopened == fail_open_at)
        return -1;
    if (nopened <= ISO_MAX_PARTS)
        snprintf(opened[nopened], sizeof(opened[nopened]), "%s", filename);
    FID[0] = (u8)nopened++;
    FID[1] = 0;
    return 1;
}

void smb_CloseAll(void)
{
    close_all_calls++;
}

int smb_Disconnect(void)
{
    disconnect_calls++;
    return 0;
}

int smb_ReadCD(unsigned int lsn, unsigned int nsectors, void *buf, int part_num)
{
    (void)lsn;
    (void)buf;
    (void)part_num;
    read_calls++;
    return (int)nsectors * 2048;
}

int smb_ReadFile(u16 FID, u32 offsetlow, u32 offsethigh, void *readbuf, int nbytes)
{
    (void)FID;
    (void)offsetlow;
    (void)offsethigh;
    (void)readbuf;
    return nbytes;
}

int smb_WriteFile(u16 FID, u32 offsetlow, u32 offsethigh, void *writebuf, int nbytes)
{
    (void)FID;
    (void)offsetlow;
    (void)offsethigh;
    (void)writebuf;
    return nbytes;
}

enum {
    OLD_ISO,
    ISO,
    USBLD,
};

static int fails;

static void fail(const char *what, int format, int namelen, const char *prefix, int media)
{
    static const char *const formats[] = {"old", "iso", "ul"};

    printf("FAIL: %s (format %s, name %d bytes, prefix %d bytes, %s)\n", what, formats[format], namelen, (int)strlen(prefix),
           media == 0x12 ? "CD" : "DVD");
    fails++;
}

static void launch(int format, int namelen, const char *prefix, int media)
{
    static const char startup[] = "SLUS_209.46"; // the scan keeps an old-format startup's first 11 bytes
    static const char extension[] = ".iso";
    const unsigned int crc = 0x1234ABCDu; // USBA_crc32(game->name) on the EE
    char name[ISO_GAME_NAME_MAX + 1], file[512], want[1024];
    int i, parts = format == USBLD ? 2 : 1;

    for (i = 0; i < namelen; i++)
        name[i] = (i % 10 == 4) ? ' ' : (char)('a' + (i * 7) % 26);
    name[namelen] = '\0';

    memset(&cdvdman_settings, 0, sizeof(cdvdman_settings));
    cdvdman_settings.common.NumParts = (u8)parts;
    cdvdman_settings.common.media = (u8)media;
    // The EE's launch, case by case (game->startup, game->name, game->extension):
    switch (format) {
        case OLD_ISO:
            snprintf(file, sizeof(file), "%s.%s%s", startup, name, extension);
            snprintf(cdvdman_settings.filename, sizeof(cdvdman_settings.filename), "%s.%s%s", startup, name, extension);
            break;
        case ISO:
            snprintf(file, sizeof(file), "%s%s", name, extension);
            snprintf(cdvdman_settings.filename, sizeof(cdvdman_settings.filename), "%s%s", name, extension);
            break;
        default: // USBExtreme format.
            snprintf(file, sizeof(file), "ul.%08X.%s", crc, startup);
            snprintf(cdvdman_settings.filename, sizeof(cdvdman_settings.filename), "ul.%08X.%s", crc, startup);
            cdvdman_settings.common.flags |= IOPCORE_SMB_FORMAT_USBLD;
    }
    snprintf(cdvdman_settings.smb_ip, sizeof(cdvdman_settings.smb_ip), "192.168.100.200");
    cdvdman_settings.smb_port = 445;
    snprintf(cdvdman_settings.smb_share, sizeof(cdvdman_settings.smb_share), "PS2SMB");
    snprintf(cdvdman_settings.smb_prefix, sizeof(cdvdman_settings.smb_prefix), "%s", prefix);

    if (strcmp(cdvdman_settings.filename, file) != 0) {
        snprintf(want, sizeof(want), "the settings block holds %d of the file name's %d bytes", (int)strlen(cdvdman_settings.filename),
                 (int)strlen(file));
        fail(want, format, namelen, prefix, media);
        return;
    }

    nopened = 0;
    failure_mode = 0;
    fail_open_at = -1;
    DeviceInit(); // Each launch is a new IOP instance on real hardware.
    smb_NegotiateProt(NULL);
    DeviceFSInit();
    if (nopened != parts) {
        fail("device-smb.c opened another number of files", format, namelen, prefix, media);
        return;
    }
    for (i = 0; i < parts; i++) {
        if (format == USBLD)
            snprintf(want, sizeof(want), "%s%s\\%s.%02x", prefix[0] ? "\\" : "", prefix, file, i);
        else
            snprintf(want, sizeof(want), "%s%s\\%s\\%s", prefix[0] ? "\\" : "", prefix, media == 0x12 ? "CD" : "DVD", file);
        if (strcmp(opened[i], want) != 0) {
            printf("  opened %s\n  wanted %s\n", opened[i], want);
            fail("device-smb.c opened another path", format, namelen, prefix, media);
        }
    }
    DeviceUnmount();
}

/* Failed SMB mounts must never expose aliased configuration bytes as open FIDs,
   parse a PVD, silently re-enter a dead socket, or close unknown file handles. */
static void failed_mount_case(int mode)
{
    char sector[2048];
    int prior_sessions, prior_trees, prior_opens;

    memset(&cdvdman_settings, 0, sizeof(cdvdman_settings));
    cdvdman_settings.common.NumParts = (mode == 4) ? 2 : 1;
    cdvdman_settings.common.flags = (mode == 4) ? IOPCORE_SMB_FORMAT_USBLD : 0;
    if (mode == 5)
        cdvdman_settings.common.NumParts = 0;
    if (mode == 6)
        cdvdman_settings.common.NumParts = ISO_MAX_PARTS + 1;
    strcpy(cdvdman_settings.filename, "TEST.iso");
    strcpy(cdvdman_settings.smb_ip, "192.168.0.2");
    strcpy(cdvdman_settings.smb_share, "PS2SMB");

    failure_mode = mode;
    fail_open_at = (mode == 3) ? 0 : (mode == 4 ? 1 : -1);
    nopened = session_calls = tree_calls = disconnect_calls = close_all_calls = read_calls = 0;
    DeviceInit();
    DeviceFSInit();

    if (DeviceReady() != SCECdNotReady || disconnect_calls != 1 ||
        close_all_calls != 0 || session_calls != (mode >= 5 ? 0 : 1) ||
        tree_calls != ((mode == 1 || mode >= 5) ? 0 : 1) ||
        nopened != (mode == 4 ? 1 : 0) ||
        DeviceReadSectors(0, sector, 1) != SCECdErREAD || read_calls != 0) {
        printf("FAIL: mount failure mode %d did not fail closed\n", mode);
        fails++;
    }

    prior_sessions = session_calls;
    prior_trees = tree_calls;
    prior_opens = nopened;
    DeviceFSInit(); // Must not attempt to reuse a disconnected partial session.
    if (session_calls != prior_sessions || tree_calls != prior_trees ||
        nopened != prior_opens) {
        printf("FAIL: mount failure mode %d retried a dead session\n", mode);
        fails++;
    }

    DeviceUnmount();
    if (close_all_calls != 0) {
        printf("FAIL: mount failure mode %d closed invalid FIDs\n", mode);
        fails++;
    }
}

/* Failed negotiation must latch the device unusable before CDVDMAN tries
   session setup, tree connect, or filesystem parsing. */
static void failed_negotiation_case(void)
{
    char sector[2048];
    int previous_sessions = session_calls;
    int previous_trees = tree_calls;
    int previous_opens = nopened;

    failure_mode = 7;
    negotiate_calls = 0;
    DeviceInit();
    if (smb_NegotiateProt(NULL) >= 0 || negotiate_calls != 1 ||
        DeviceReady() != SCECdNotReady ||
        DeviceReadSectors(0, sector, 1) != SCECdErREAD) {
        puts("FAIL: failed negotiation did not mark the device unavailable");
        fails++;
    }
    DeviceFSInit();
    if (session_calls != previous_sessions || tree_calls != previous_trees ||
        nopened != previous_opens) {
        puts("FAIL: failed negotiation proceeded to mount");
        fails++;
    }
    DeviceUnmount();
    failure_mode = 0;
}

/* No registered ps2ip module: never dereference uninitialized export pointers
   or enter session setup after the launch negotiation failed. */
static void missing_ps2ip_case(void)
{
    char sector[2048];
    int previous_sessions = session_calls;
    int previous_opens = nopened;

    ps2ip_missing = 1;
    negotiate_calls = 0;
    DeviceInit();
    if (smb_NegotiateProt(NULL) >= 0 ||
        negotiate_calls != 0 || DeviceReady() != SCECdNotReady ||
        DeviceReadSectors(0, sector, 1) != SCECdErREAD) {
        puts("FAIL: absent ps2ip library was not rejected");
        fails++;
    }
    DeviceFSInit();
    if (session_calls != previous_sessions || nopened != previous_opens) {
        puts("FAIL: absent ps2ip library proceeded to mount");
        fails++;
    }
    DeviceUnmount();
    ps2ip_missing = 0;
}

int main(void)
{
    // 1, either side of each length the old 160-byte field cut (old format from 144, plain from 156),
    // and the longest.
    const int lengths[] = {1, 143, 144, 155, 156, ISO_GAME_NAME_MAX - 1, ISO_GAME_NAME_MAX};
    char longest[sizeof(cdvdman_settings.smb_prefix)];
    const char *prefixes[2] = {"", longest};
    int f, l, p, m, cases = 0;

    // The longest prefix the settings block holds (gETHPrefix is as long).
    for (p = 0; p < (int)sizeof(longest) - 1; p++)
        longest[p] = "PS2-Games-"[p % 10];
    longest[sizeof(longest) - 1] = '\0';

    for (f = OLD_ISO; f <= USBLD; f++)
        for (l = 0; l < (int)(sizeof(lengths) / sizeof(lengths[0])); l++)
            for (p = 0; p < 2; p++)
                for (m = 0; m < 2; m++, cases++)
                    launch(f, lengths[l], prefixes[p], m ? 0x12 : 0x14);
    for (f = 1; f <= 6; ++f)
        failed_mount_case(f);
    failed_negotiation_case();
    missing_ps2ip_case();
    printf("%d launches, %d failed (settings block %d bytes, file name field %d bytes)\n", cases, fails, (int)sizeof(cdvdman_settings),
           (int)sizeof(cdvdman_settings.filename));
    return fails ? 1 : 0;
}
