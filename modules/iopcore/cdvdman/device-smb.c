/*
  Copyright 2009-2010, jimmikaelkael
  Licenced under Academic Free License version 3.0
  Review Open PS2 Loader README & LICENSE files for further details.
*/

#include <stdio.h>

#include "smstcpip.h"
#include "internal.h"

#include "device.h"

extern struct cdvdman_settings_smb cdvdman_settings;

extern struct irx_export_table _exp_oplsmb;

extern int smb_io_sema;

static int ps2ip_init(void);

// !!! ps2ip exports functions pointers !!!
// Note: recvfrom() used here is not a standard recvfrom() function.
int (*plwip_close)(int s);                                                                                                                 // #6
int (*plwip_connect)(int s, struct sockaddr *name, socklen_t namelen);                                                                     // #7
int (*plwip_recv)(int s, void *mem, int len, unsigned int flags);                                                                          // #9
int (*plwip_recvfrom)(int s, void *mem, int hlen, void *payload, int plen, unsigned int flags, struct sockaddr *from, socklen_t *fromlen); // #10
int (*plwip_send)(int s, void *dataptr, int size, unsigned int flags);                                                                     // #11
int (*plwip_socket)(int domain, int type, int protocol);                                                                                   // #13
int (*plwip_setsockopt)(int s, int level, int optname, const void *optval, socklen_t optlen);                                              // #19
u32 (*pinet_addr)(const char *cp);                                                                                                         // #24

static u32 ServerCapabilities;
// 0 = not attempted, 1 = all image files opened, -1 = failed/unmounted.
static int smb_mount_state;

static int ps2ip_init(void)
{
    modinfo_t info;
    // getModInfo leaves info.exports undefined when the library is absent.
    // Never dereference a stale loadcore pointer after an IOP module failure.
    if (!getModInfo("ps2ip\0\0\0", &info) || !info.exports)
        return 0;

    // Set functions pointers here
    plwip_close = info.exports[6];
    plwip_connect = info.exports[7];
    plwip_recv = info.exports[9];
    plwip_recvfrom = info.exports[10];
    plwip_send = info.exports[11];
    plwip_socket = info.exports[13];
    plwip_setsockopt = info.exports[19];
    pinet_addr = info.exports[24];
    return 1;
}

/** Negotiate with cdvdman's saved server settings and return the result to smbinit. */
int smb_NegotiateProt(OplSmbPwHashFunc_t hash_callback)
{
    if (!ps2ip_init()) {
        smb_mount_state = -1;
        return -1;
    }
    return smb_NegotiateProtocol(cdvdman_settings.smb_ip, cdvdman_settings.smb_port, cdvdman_settings.smb_user, cdvdman_settings.smb_password, &ServerCapabilities, hash_callback);
}

void DeviceInit(void)
{
    smb_mount_state = 0;
    RegisterLibraryEntries(&_exp_oplsmb);
}

void DeviceDeinit(void)
{ // Close all files and disconnect before IOP reboots. Note that this seems to help prevent VMC corruption in some games.
    DeviceUnmount();
}

int DeviceReady(void)
{
    return smb_mount_state < 0 ? SCECdNotReady : SCECdComplete;
}

/**
 * Open the complete image before allowing CDVDMAN to initialize the ISO filesystem.
 * A failed attempt is terminal until a fresh IOP/device initialization: the FIDs
 * overlay the server settings, so a blind same-IOP reconnect is not safe.
 */
void DeviceFSInit(void)
{
    int i = 0;
    char tmp_str[CDVDMAN_SMB_PATH_MAX];

    if (smb_mount_state != 0)
        return;
    smb_mount_state = -1;

    if (cdvdman_settings.common.NumParts == 0 || cdvdman_settings.common.NumParts > ISO_MAX_PARTS) {
        printf("DeviceFSInit: invalid part count\n");
        goto failed;
    }

    if (smb_SessionSetupAndX(ServerCapabilities) < 0) {
        printf("DeviceFSInit: session setup failed\n");
        goto failed;
    }

    sprintf(tmp_str, "\\\\%s\\%s", cdvdman_settings.smb_ip, cdvdman_settings.smb_share);
    if (smb_TreeConnectAndX(tmp_str) < 0) {
        printf("DeviceFSInit: tree connect failed\n");
        goto failed;
    }

    if (!(cdvdman_settings.common.flags & IOPCORE_SMB_FORMAT_USBLD)) {
        if (cdvdman_settings.smb_prefix[0]) {
            sprintf(tmp_str, "\\%s\\%s\\%s", cdvdman_settings.smb_prefix, cdvdman_settings.common.media == 0x12 ? "CD" : "DVD", cdvdman_settings.filename);
        } else {
            sprintf(tmp_str, "\\%s\\%s", cdvdman_settings.common.media == 0x12 ? "CD" : "DVD", cdvdman_settings.filename);
        }

        if (smb_OpenAndX(tmp_str, (u8 *)&cdvdman_settings.FIDs[i++], 0) <= 0) {
            printf("DeviceFSInit: image open failed\n");
            goto failed;
        }
    } else {
        // Open all parts files
        for (i = 0; i < cdvdman_settings.common.NumParts; i++) {
            if (cdvdman_settings.smb_prefix[0])
                sprintf(tmp_str, "\\%s\\%s.%02x", cdvdman_settings.smb_prefix, cdvdman_settings.filename, i);
            else
                sprintf(tmp_str, "\\%s.%02x", cdvdman_settings.filename, i);

            if (smb_OpenAndX(tmp_str, (u8 *)&cdvdman_settings.FIDs[i], 0) <= 0) {
                printf("DeviceFSInit: image part %d open failed\n", i);
                goto failed;
            }
        }
    }

    smb_mount_state = 1;
    return;

failed:
    // Closing the session also releases earlier successful opens on the server.
    // Do not inspect FIDs on partial failure: they alias IP/share/credentials.
    smb_Disconnect();
}

void DeviceLock(void)
{
    // Negotiation may fail before the SMB I/O semaphore is created.
    if (smb_io_sema >= 0)
        WaitSema(smb_io_sema);
}

void DeviceUnmount(void)
{
    if (smb_mount_state == 1)
        smb_CloseAll();
    smb_Disconnect();
    smb_mount_state = -1;
}

void DeviceStop(void)
{
}

int DeviceReadSectors(u64 lsn, void *buffer, unsigned int sectors)
{
    register u32 r, sectors_to_read, lbound, ubound, nlsn, offslsn;
    register int i, esc_flag = 0;
    u8 *p = (u8 *)buffer;
    int rv = SCECdErNO;

    if (smb_mount_state != 1)
        return SCECdErREAD;

    lbound = 0;
    ubound = (cdvdman_settings.common.NumParts > 1) ? 0x80000 : 0xFFFFFFFF;
    offslsn = (u32)lsn;
    r = nlsn = 0;
    sectors_to_read = sectors;

    for (i = 0; i < cdvdman_settings.common.NumParts; i++, lbound = ubound, ubound += 0x80000, offslsn -= 0x80000) {

        if ((u32)lsn >= lbound && (u32)lsn < ubound) {
            if (((u32)lsn + sectors) > (ubound - 1)) {
                sectors_to_read = ubound - (u32)lsn;
                sectors -= sectors_to_read;
                nlsn = ubound;
            } else
                esc_flag = 1;

            // A ZSO file seldom ends on a sector boundary, so the read of its last sector stops at the
            // end of the file. Pad that sector with zeros, as a block device returns a whole sector.
            // Any larger shortfall is a failed read.
            int want = (int)(sectors_to_read * 2048);
            int got = smb_ReadCD(offslsn, sectors_to_read, &p[r], i);
            if (got < 0 || got <= want - 2048) {
                rv = SCECdErREAD;
                break;
            }
            if (got < want)
                memset(&p[r + got], 0, want - got);

            r += sectors_to_read * 2048;
            offslsn += sectors_to_read;
            sectors_to_read = sectors;
            lsn = nlsn;
        }

        if (esc_flag)
            break;
    }

    return rv;
}

//
// oplutils exported functions, used by MCEMU for VMC pages. They take the oplutils slots that
// smb_ReadFile and smb_WriteFile had, and report each call so that oplShutdown can let a save
// finish. (DeviceReadSectors also uses smb_ReadFile, for the game image, which is not a VMC call.)
//

int smb_vmcReadFile(u16 FID, u32 offsetlow, u32 offsethigh, void *readbuf, int nbytes)
{
    int res;

    cdvdman_vmc_io_begin(0);
    res = smb_ReadFile(FID, offsetlow, offsethigh, readbuf, nbytes);
    cdvdman_vmc_io_end();

    return res;
}

int smb_vmcWriteFile(u16 FID, u32 offsetlow, u32 offsethigh, void *writebuf, int nbytes)
{
    int res;

    cdvdman_vmc_io_begin(1);
    res = smb_WriteFile(FID, offsetlow, offsethigh, writebuf, nbytes);
    cdvdman_vmc_io_end();

    return res;
}
