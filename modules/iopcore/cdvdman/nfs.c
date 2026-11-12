/*
  nfs.c: the in-game NFS reader. Built into nfs_cdvdman in place of smb.c, as smb2.c is into
  smb2_cdvdman: it provides smb.c's functions under the same names, so device-smb.c, the oplutils
  exports and smb_mcemu (VMC) work unchanged. The settings are the SMB block with the export after
  it (struct cdvdman_settings_nfs, cdvd_config.h); the share, user and password are not used.

  The protocol is Ronnie Sahlberg's unfs4 (NFSv4.0, BSD 2-Clause), compiled in from
  modules/network/nfsman/unfs4 as the menu's nfsman does, and this file reaches the server as
  nfsman does:
  - AUTH_SYS as uid 0 and gid 0, from a port below 1024 when one can be bound, so an export that
    lets the menu in lets the game in.
  - The anonymous stateid for reads and writes: nothing is opened on the server, and a filehandle
    stays good on a new connection. A call whose connection failed connects again once and is
    repeated, so a server that closes a connection left idle while the game plays does not end it.
  - One request in flight at a time, as smb.c. A READ or WRITE moves at most 8192 bytes, for the
    reason of smb.c's CLIENT_MAX_RECV_SIZE (the IOP TCP window).
  - The socket is polled with MSG_DONTWAIT, as smb2.c's: SMSTCPIP has no SO_RCVTIMEO and its
    select is a stub. A reply that stays silent for NFS_RECV_TIMEOUT_MS fails the call.
  - Writes come from smb_mcemu only. The menu launches a VMC on NFS only while NFS Writes is On.

  Return values match smb.c: smb_ReadFile returns the byte count, a short count at end of file,
  or a negative error; smb_WriteFile returns nbytes or <= 0; smb_OpenAndX returns 1 or < 0.

  Licenced under Academic Free License version 3.0.
*/

#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <sysclib.h>
#include "smsutils.h" // memcpy and memset are smsutils.S's, as in smb.c and smb2.c (sysclib's memset is not imported)
#include "smstcpip.h"
#include <thbase.h>
#include <thsemap.h>

#include "oplsmb.h"
#include "smb.h"
#include "cdvd_config.h"

#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0x40 // SMSTCPIP's lwip/sockets.h, as in smb2.c
#endif

#define NFS_PORT                2049
#define NFS_IO_CHUNK            8192
#define NFS_RECV_TIMEOUT_MS     10000
#define NFS_CONNECT_ATTEMPTS    3
#define NFS_CONNECT_RETRY_DELAY 500000
#define NFS_MAX_FILES           (ISO_MAX_PARTS + 2) // the image's parts and the two VMCs

int smb_io_sema = -1;

// !!! ps2ip exports functions pointers !!! (set by device-smb.c)
extern int (*plwip_close)(int s);
extern int (*plwip_connect)(int s, struct sockaddr *name, socklen_t namelen);
extern int (*plwip_bind)(int s, struct sockaddr *name, socklen_t namelen);
extern int (*plwip_recv)(int s, void *mem, int len, unsigned int flags);
extern int (*plwip_send)(int s, void *dataptr, int size, unsigned int flags);
extern int (*plwip_socket)(int domain, int type, int protocol);
extern int (*plwip_setsockopt)(int s, int level, int optname, const void *optval, socklen_t optlen);
extern u32 (*pinet_addr)(const char *cp);

extern struct cdvdman_settings_nfs cdvdman_settings;

//-------------------------------------------------------------------------
// unfs4's socket calls. A read returns what has come, waiting up to NFS_RECV_TIMEOUT_MS for the first
// byte; 0 (the server closed the connection) and -1 both fail unfs4's call, which then closes the socket
// (fd -1), and the rest of that call fails at once.
static int nfs_sock_read(int fd, void *buf, int len)
{
    int result, waited;

    for (waited = 0; fd >= 0 && waited < NFS_RECV_TIMEOUT_MS; waited++) {
        result = plwip_recv(fd, buf, len, MSG_DONTWAIT);
        if (result >= 0)
            return result;
        DelayThread(1000); // 1 ms
    }

    return -1;
}

static int nfs_sock_write(int fd, const void *buf, int len)
{
    return (fd >= 0) ? plwip_send(fd, (void *)buf, len, 0) : -1;
}

/*
  unfs4 compiled in. Its API is declared static first, so these definitions are this file's alone and
  the compiler drops what is not called (C11 6.2.2: they take the linkage of the first declaration):
  unfs4_init_context(), unfs4_open() and unfs4_size(), which allocate or are not needed here. nfs.c keeps
  the one context and the filehandles itself; the allocator below is never reached.
*/
struct unfs4_context;
static struct unfs4_context *unfs4_init_context(uint32_t ip, uint32_t uid, uint32_t gid);
static int unfs4_mount(struct unfs4_context *unfs4, const char *path);
static uint8_t *unfs4_open(struct unfs4_context *unfs4, const char *name, int mode);
static int32_t unfs4_prw(struct unfs4_context *unfs4, int op, uint8_t *fh, uint8_t *rbuf, uint8_t *wbuf, int count, uint64_t offset);
static int64_t unfs4_size(struct unfs4_context *unfs4, uint8_t *fh);

#define PS2IOP_COMPAT_H // nfsman's platform header is for the ps2ip stack: these mappings replace it
#define malloc(size)    NULL
#define calloc(n, size) NULL
#define free(ptr)       ((void)(ptr))
#undef socket
#undef connect
#define socket(d, t, p)  plwip_socket(d, t, p)
#define connect(s, a, l) plwip_connect(s, a, l)
#define read(a, b, c)    nfs_sock_read(a, b, c)
#define write(a, b, c)   nfs_sock_write(a, b, c)
#define close(a)         plwip_close(a)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "unfs4/unfs4.c" // through modules/network/nfsman on the include path, as nfsman.c
#pragma GCC diagnostic pop
#undef malloc
#undef calloc
#undef free
#undef socket
#undef connect
#undef read
#undef write
#undef close

static struct unfs4_context nfs;
// The open files' filehandles, as XDR opaque: the length, then the handle. A length of 0 is a free slot.
static uint8_t files[NFS_MAX_FILES][4 + UNFS4_MAX_FH];
static struct in_addr server_addr;
static u16 server_port, local_port;

#define FILE_USED(slot) (get32(files[slot]) != 0)

//-------------------------------------------------------------------------
// Connect to the server, from a port below 1024 if one can be bound (an "insecure" export takes any).
static int nfs_connect(void)
{
    struct sockaddr_in sin;
    int fd, opt = 1;

    if (nfs.fd >= 0)
        plwip_close(nfs.fd);
    nfs.fd = -1;

    fd = plwip_socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
        return -1;

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    // A new port each time: the server can still hold the last connection's.
    local_port = 512 + (local_port + 37) % 512;
    sin.sin_port = htons(local_port);
    plwip_bind(fd, (struct sockaddr *)&sin, sizeof(sin));
    plwip_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *)&opt, sizeof(opt));

    sin.sin_addr = server_addr;
    sin.sin_port = htons(server_port);
    if (plwip_connect(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        plwip_close(fd);
        return -1;
    }

    nfs.fd = fd;
    return 0;
}

// After a failed call: nonzero when the call's connection had failed and a new one is up, so the call is
// worth repeating. A failure the server reported (a missing file, a refused write) stands.
static int nfs_reconnected(void)
{
    return nfs.fd < 0 && nfs_connect() == 0;
}

/**
 * Connect to the NFS server. Only the address and port are used: NFS has no logon (AUTH_SYS, see the
 * top of this file). A few attempts, as smb2.c's negotiation, then a failure: this runs inside
 * smbinit's synchronous module load. Returns 1 or a negative value.
 */
int smb_NegotiateProtocol(char *SMBServerIP, int SMBServerPort, char *Username, char *Password, u32 *capabilities, OplSmbPwHashFunc_t hash_callback)
{
    iop_sema_t smp;
    iop_sys_clock_t clk;
    int attempt;

    (void)Username;
    (void)Password;
    (void)hash_callback;

    nfs.fd = -1; // before smb_io_sema, which smb_Disconnect reads as "nfs.fd is set"
    smp.initial = 1;
    smp.max = 1;
    smp.option = 0;
    smp.attr = 1;
    smb_io_sema = CreateSema(&smp);
    if (smb_io_sema < 0)
        return -1;

    *capabilities = 0;
    server_addr.s_addr = pinet_addr(SMBServerIP);
    server_port = (u16)(SMBServerPort > 0 ? SMBServerPort : NFS_PORT);
    // unfs4.h asks for xids from a clock, so that a session does not reuse the last one's.
    GetSystemTime(&clk);
    nfs.xid = clk.lo;
    local_port = (u16)clk.lo;

    for (attempt = 0; attempt < NFS_CONNECT_ATTEMPTS; attempt++) {
        if (nfs_connect() == 0)
            return 1;
        if (attempt + 1 < NFS_CONNECT_ATTEMPTS)
            DelayThread(NFS_CONNECT_RETRY_DELAY);
    }

    DeleteSema(smb_io_sema);
    smb_io_sema = -1;
    return -1;
}

int smb_SessionSetupAndX(u32 capabilities)
{
    (void)capabilities;
    return 1;
}

// Looks up the export, whatever device-smb.c names ("\\<ip>\<share>"). Returns 1 or a negative value.
int smb_TreeConnectAndX(char *ShareName)
{
    (void)ShareName;

    if (unfs4_mount(&nfs, cdvdman_settings.nfs_export) == 0 || (nfs_reconnected() && unfs4_mount(&nfs, cdvdman_settings.nfs_export) == 0))
        return 1;

    return -1000;
}

//-------------------------------------------------------------------------
// Looks up filename (from the export, '\' separated) and keeps its filehandle. Nothing is opened on the
// server, so Write changes nothing here. Returns 1 or a negative value.
int smb_OpenAndX(char *filename, u8 *FID, int Write)
{
    char path[CDVDMAN_SMB_PATH_MAX];
    int i, slot, result;
    u16 fid;

    (void)Write;

    for (i = 0; filename[i] != '\0'; i++) {
        if (i == (int)sizeof(path) - 1)
            return -ENAMETOOLONG;
        path[i] = (filename[i] == '\\') ? '/' : filename[i];
    }
    path[i] = '\0';

    WaitSema(smb_io_sema);

    for (slot = 0; slot < NFS_MAX_FILES && FILE_USED(slot); slot++)
        ;
    if (slot == NFS_MAX_FILES) {
        SignalSema(smb_io_sema);
        return -EMFILE;
    }

    result = unfs4_lookup(&nfs, path, files[slot]);
    if (result != 0 && nfs_reconnected())
        result = unfs4_lookup(&nfs, path, files[slot]);
    if (result != 0 || !FILE_USED(slot)) { // an empty filehandle would read the server's root
        memset(files[slot], 0, 4);         // the slot stays free
        SignalSema(smb_io_sema);
        return -1000;
    }

    fid = (u16)slot;
    memcpy(FID, &fid, 2);

    SignalSema(smb_io_sema);

    return 1;
}

// Do not call WaitSema() from this function: as with smb.c, callers already hold it or run alone.
int smb_Close(int FID)
{
    if (FID < 0 || FID >= NFS_MAX_FILES || !FILE_USED(FID))
        return -EBADF;
    memset(files[FID], 0, 4);

    return 0;
}

//-------------------------------------------------------------------------
// READ or WRITE nbytes at offset, in NFS_IO_CHUNK pieces. A read that reaches the end of the file returns
// the bytes before it, a short count (see smb.c); a write the server takes none of has failed.
static int nfs_io(int op, u16 FID, u64 offset, u8 *buf, int nbytes)
{
    int result, count, remaining = nbytes;

    if (FID >= NFS_MAX_FILES || !FILE_USED(FID))
        return -EBADF;

    WaitSema(smb_io_sema);

    while (remaining > 0) {
        count = (remaining > NFS_IO_CHUNK) ? NFS_IO_CHUNK : remaining;
        result = unfs4_prw(&nfs, op, files[FID], (op == OP_READ) ? buf : NULL, (op == OP_WRITE) ? buf : NULL, count, offset);
        if (result < 0 && nfs_reconnected())
            result = unfs4_prw(&nfs, op, files[FID], (op == OP_READ) ? buf : NULL, (op == OP_WRITE) ? buf : NULL, count, offset);
        if (result <= 0) {
            SignalSema(smb_io_sema);
            return (result == 0 && op == OP_READ) ? nbytes - remaining : -EIO;
        }

        offset += result;
        buf += result;
        remaining -= result;
    }

    SignalSema(smb_io_sema);

    return nbytes;
}

int smb_ReadFile(u16 FID, u32 offsetlow, u32 offsethigh, void *readbuf, int nbytes)
{
    return nfs_io(OP_READ, FID, ((u64)offsethigh << 32) | offsetlow, (u8 *)readbuf, nbytes);
}

int smb_WriteFile(u16 FID, u32 offsetlow, u32 offsethigh, void *writebuf, int nbytes)
{
    return nfs_io(OP_WRITE, FID, ((u64)offsethigh << 32) | offsetlow, (u8 *)writebuf, nbytes);
}

//-------------------------------------------------------------------------
int smb_ReadCD(unsigned int lsn, unsigned int nsectors, void *buf, int part_num)
{
    return smb_ReadFile(cdvdman_settings.FIDs[part_num], lsn * 2048, lsn >> 21, buf, (int)(nsectors * 2048));
}

void smb_CloseAll(void)
{
    int i, fd;

    for (i = 0; i < cdvdman_settings.common.NumParts; i++) {
        fd = cdvdman_settings.FIDs[i];
        if (fd >= 0) {
            smb_Close(fd);
            cdvdman_settings.FIDs[i] = -1;
        }
    }
}

int smb_Disconnect(void)
{
    // nfs.fd holds a socket only once negotiation has run (smb_io_sema). Before that, as when the network
    // never came up, it is bss's 0, and the stack's close was never linked.
    if (smb_io_sema >= 0 && nfs.fd >= 0) {
        plwip_close(nfs.fd);
        nfs.fd = -1;
    }

    return 1;
}
