/* Host harness for modules/iopcore/cdvdman/nfs.c (nfs_cdvdman, the in-game NFS reader): calls it the way
   device-smb.c and smb_mcemu do, against a live NFS server. nfs.c is included, so the harness can cut its
   connection under it. Built and run by run.sh.

   argv: ip port export iso_path vmc_path mode [utf8_path local_copy]
     mode  rw: the server allows writes; ro: it refuses them (the VMC writes must fail, not loop) */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "tamtypes.h"
#include "smstcpip.h"
#include "oplsmb.h"
#include "smb.h"
#include "cdvd_config.h"

int (*plwip_close)(int s);
int (*plwip_connect)(int s, struct sockaddr *name, socklen_t namelen);
int (*plwip_bind)(int s, struct sockaddr *name, socklen_t namelen);
int (*plwip_recv)(int s, void *mem, int len, unsigned int flags);
int (*plwip_send)(int s, void *dataptr, int size, unsigned int flags);
int (*plwip_socket)(int domain, int type, int protocol);
int (*plwip_setsockopt)(int s, int level, int optname, const void *optval, socklen_t optlen);
u32 (*pinet_addr)(const char *cp);
struct cdvdman_settings_nfs cdvdman_settings;

#include "nfs.c"

static int h_close(int s)
{
    return close(s);
}
static int h_connect(int s, struct sockaddr *n, socklen_t l) { return connect(s, n, l); }
static int h_bind(int s, struct sockaddr *n, socklen_t l) { return bind(s, n, l); }
static int h_recv(int s, void *m, int l, unsigned f) { return (int)recv(s, m, l, f); }
static int h_send(int s, void *d, int l, unsigned f) { return (int)send(s, d, l, f); }
static u32 h_inet(const char *c) { return inet_addr(c); }

static int connects;
static int h_connect_counted(int s, struct sockaddr *n, socklen_t l)
{
    connects++;
    return connect(s, n, l);
}

// Cuts the reader's connection as a server that closes it would: the next call must connect again.
static void cut(void)
{
    shutdown(nfs.fd, SHUT_RDWR);
}

int main(int argc, char **argv)
{
    if (argc < 7) {
        fprintf(stderr, "usage\n");
        return 2;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGPIPE, SIG_IGN); // a send on the cut connection fails instead
    plwip_close = h_close;
    plwip_connect = h_connect_counted;
    plwip_bind = h_bind;
    plwip_recv = h_recv;
    plwip_send = h_send;
    plwip_socket = socket;
    plwip_setsockopt = setsockopt;
    pinet_addr = h_inet;
    (void)h_connect;
    const int rw = strcmp(argv[6], "rw") == 0;
    u32 caps = 1;
    int r, fails = 0;

    snprintf(cdvdman_settings.nfs_export, sizeof(cdvdman_settings.nfs_export), "%s", argv[3]);
    r = smb_NegotiateProtocol(argv[1], atoi(argv[2]), "", "", &caps, NULL);
    printf("negotiate %d caps %u\n", r, caps);
    if (r <= 0)
        return 3;
    struct sockaddr_in local;
    socklen_t locallen = sizeof(local);
    getsockname(nfs.fd, (struct sockaddr *)&local, &locallen);
    printf("local port %u (below 1024 when run as root)\n", ntohs(local.sin_port));
    if (geteuid() == 0 && ntohs(local.sin_port) >= 1024) {
        printf("FAIL: not from a port below 1024\n");
        fails++;
    }
    r = smb_SessionSetupAndX(caps);
    printf("session setup %d\n", r);
    r = smb_TreeConnectAndX("\\\\ignored\\share");
    printf("tree connect %d\n", r);
    if (r <= 0)
        return 4;

    u16 fid = 0xFFFF;
    r = smb_OpenAndX(argv[4], (u8 *)&fid, 0);
    printf("open iso %d fid %u\n", r, fid);
    if (r <= 0)
        return 5;
    /* Whole-file read through smb_ReadCD-sized requests, compared with the file by run.sh. */
    FILE *out = fopen("readback.bin", "wb");
    static u8 buf[16 * 2048];
    u32 lsn = 0;
    long total = 0;
    int shortcount = -1;
    for (;;) {
        int got = smb_ReadFile(fid, lsn * 2048, lsn >> 21, buf, sizeof(buf));
        if (got < 0) {
            printf("read error %d at lsn %u\n", got, lsn);
            return 6;
        }
        fwrite(buf, 1, got, out);
        total += got;
        if (got < (int)sizeof(buf)) {
            shortcount = got;
            break;
        }
        lsn += sizeof(buf) / 2048;
        if (lsn == 64) { // part way: the server closes the connection, and the read carries on
            int before = connects;
            cut();
            got = smb_ReadFile(fid, lsn * 2048, lsn >> 21, buf, 2048);
            printf("read after the connection was cut %d, connected again %d time(s)\n", got, connects - before);
            fails += (got != 2048 || connects - before != 1);
        }
    }
    fclose(out);
    printf("read total %ld, final short count %d\n", total, shortcount);
    /* Read entirely past EOF: must be 0 (a short count of nothing), not an error. */
    r = smb_ReadFile(fid, 0x7FFFF000u, 0, buf, 2048);
    printf("past-eof read %d\n", r);
    fails += (r != 0);
    /* Past 4 GiB: offsethigh reaches the server (still past EOF of this small image). */
    r = smb_ReadFile(fid, 0, 1, buf, 2048);
    printf("read past 4 GiB %d\n", r);
    fails += (r != 0);
    r = smb_Close(fid);
    printf("close %d\n", r);
    fails += (r < 0);
    r = smb_Close(fid);
    printf("double close %d (expect <0)\n", r);
    fails += (r >= 0);
    r = smb_ReadFile(fid, 0, 0, buf, 2048);
    printf("read on a closed fid %d (expect <0)\n", r);
    fails += (r >= 0);

    /* VMC-style: open for write, write pages, read them back. */
    u16 vfid = 0xFFFF;
    r = smb_OpenAndX(argv[5], (u8 *)&vfid, 1);
    printf("open vmc %d fid %u\n", r, vfid);
    if (r <= 0)
        return 7;
    u8 page[528], back[528];
    int bad = 0, vmc_connects = connects;
    for (int p = 0; p < 64; p++) {
        for (int i = 0; i < 528; i++)
            page[i] = (u8)(p * 31 + i);
        r = smb_WriteFile(vfid, p * 528, 0, page, 528);
        if (rw ? r != 528 : r > 0) {
            printf("write %d -> %d\n", p, r);
            bad++;
        }
    }
    if (rw) {
        for (int p = 0; p < 64; p++) {
            r = smb_ReadFile(vfid, p * 528, 0, back, 528);
            for (int i = 0; i < 528; i++)
                if (back[i] != (u8)(p * 31 + i)) {
                    bad++;
                    break;
                }
            if (r != 528)
                bad++;
        }
        /* A 100 KB write: crosses the 8 KB chunk, a connection cut before it. */
        static u8 big[100000];
        for (int i = 0; i < (int)sizeof(big); i++)
            big[i] = (u8)(i ^ 0x5A);
        cut();
        r = smb_WriteFile(vfid, 1 << 20, 0, big, sizeof(big));
        printf("big write after a cut %d\n", r);
        fails += (r != (int)sizeof(big));
        static u8 bigb[100000];
        r = smb_ReadFile(vfid, 1 << 20, 0, bigb, sizeof(bigb));
        printf("big read %d match %d\n", r, memcmp(big, bigb, sizeof(big)) == 0);
        fails += (r != (int)sizeof(bigb) || memcmp(big, bigb, sizeof(big)) != 0);
    } else {
        r = smb_ReadFile(vfid, 0, 0, back, 528);
        printf("read-only server: VMC writes refused, the connection kept (%d reconnects), a read %d\n", connects - vmc_connects, r);
        fails += (connects != vmc_connects || r != 528);
    }
    printf("vmc page mismatches %d\n", bad);
    fails += (bad != 0);
    smb_Close(vfid);

    /* A name outside ASCII, as the menu stores it (UTF-8 from nfsman's listing). */
    if (argc > 8) {
        u16 uf = 0xFFFF;
        static u8 want[2048];
        FILE *ref = fopen(argv[8], "rb");
        r = smb_OpenAndX(argv[7], (u8 *)&uf, 0);
        printf("open utf-8 name %d\n", r);
        fails += (r <= 0);
        if (r > 0) {
            r = smb_ReadFile(uf, 0, 0, buf, 2048);
            printf("read utf-8 name %d\n", r);
            fails += (r != 2048 || ref == NULL || fread(want, 1, 2048, ref) != 2048 || memcmp(buf, want, 2048) != 0);
            smb_Close(uf);
        }
        if (ref)
            fclose(ref);
    }

    /* Every slot: the image's parts and two VMCs, then one too many. */
    u16 many[NFS_MAX_FILES + 1];
    int opened = 0;
    for (int i = 0; i <= NFS_MAX_FILES; i++)
        opened += smb_OpenAndX(argv[4], (u8 *)&many[i], 0) > 0;
    printf("opened %d of %d (expect %d)\n", opened, NFS_MAX_FILES + 1, NFS_MAX_FILES);
    fails += (opened != NFS_MAX_FILES);
    for (int i = 0; i < NFS_MAX_FILES; i++)
        smb_Close(many[i]);

    /* A file that does not exist must fail cleanly, and keep the connection. */
    u16 nf;
    int before = connects;
    r = smb_OpenAndX("\\DVD\\NOPE.iso", (u8 *)&nf, 0);
    printf("open missing %d (expect <0), reconnects %d (expect 0)\n", r, connects - before);
    fails += (r >= 0 || connects != before);
    /* A path longer than device-smb.c's buffer is refused before it is sent. */
    static char longpath[CDVDMAN_SMB_PATH_MAX + 8];
    memset(longpath, 'a', sizeof(longpath) - 1);
    r = smb_OpenAndX(longpath, (u8 *)&nf, 0);
    printf("open too long %d (expect %d)\n", r, -ENAMETOOLONG);
    fails += (r != -ENAMETOOLONG);

    /* An export that is not there: the tree connect fails. */
    snprintf(cdvdman_settings.nfs_export, sizeof(cdvdman_settings.nfs_export), "/no/such/export");
    r = smb_TreeConnectAndX("");
    printf("tree connect to a missing export %d (expect <0)\n", r);
    fails += (r >= 0);
    smb_Disconnect();

    /* A server that is not listening: the negotiation gives up, it does not hang. */
    r = smb_NegotiateProtocol(argv[1], 1, "", "", &caps, NULL);
    printf("negotiate with nothing listening %d (expect <0), %d connects\n", r, connects);
    fails += (r >= 0);

    printf("DONE (%d failed checks)\n", fails);
    return fails ? 10 : 0;
}
