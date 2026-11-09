/*
  smb2man -- the SMB2 browse driver for the menu (game lists, art, configs, VMC files, the
  file manager). Chosen instead of smbman when Settings > Network > SMB Version is SMB2.

  It registers the same iomanX device as smbman ("smb", so every "smb0:" path is unchanged) and
  answers the same devctl interface from ps2smb.h (LOGON, OPENSHARE, ECHO, ...), so the EE side
  only chooses which of the two modules to load. Only one of them is ever resident.

  The protocol work is Ronnie Sahlberg's usmb2, a tiny SMB2 client, vendored under usmb2/ (MIT;
  see usmb2/VENDORED.txt for the commit and the small local patch). usmb2.c is compiled into this
  file so the driver can use its request builder for the few requests usmb2 has no call for:
  NEGOTIATE offering 2.0.2, 2.1, 3.0 and 3.0.2, ECHO, a 64-bit file size, and directory listings
  read straight into a per-handle buffer. This file adds the iomanX/devctl front end, the socket
  calls with timeouts, the keepalive, and the share open on the driver's own thread (so fileXio's
  single RPC thread is never held for a network handshake).

  Compared with the libsmb2-based driver it replaces (about 150 KB resident, this is a small
  fraction of that): SMB 2.x message signing when the server requires it (Windows 11 24H2 by
  default, Samba with "server signing = mandatory"): requests are signed and replies verified, and a
  3.x server that requires signing is refused; no share list (the share
  name must be set in Network Settings); the server must be given as an IP address (the menu
  resolves NetBIOS names itself); names on the share up to 440 bytes.

  The menu's SMB1 driver (smbman) is unchanged.
*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdint.h>
#include <stddef.h>

#include <errno.h>
#include <fcntl.h>
#include <intrman.h>
#include <iomanX.h>
#include <loadcore.h>
#include <sifman.h>
#include <stdio.h>
#include <sysclib.h>
#include <sysmem.h>
#include <thbase.h>
#include <thsemap.h>

#include <ps2smb.h>
#include "smb2man_devctl.h"
#include "smb2auth.h" // modules/iopcore/cdvdman: MD4/MD5/SHA-256 and HMAC, shared with the in-game reader

#ifdef _IOP
#include <ps2ip.h>
#define SMB2MAN_IOCTL(fd, cmd, arg) lwip_ioctl(fd, cmd, arg)
#define SMB2MAN_CLOSE(fd)           lwip_close(fd)
#else
// The host build (tests/smb2-live): the same code over POSIX sockets.
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define SMB2MAN_IOCTL(fd, cmd, arg) ioctl(fd, cmd, arg)
#define SMB2MAN_CLOSE(fd)           close(fd)
#endif

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

#define MODNAME "smb2man"
IRX_ID(MODNAME, 2, 0);

#define MAX_OPEN_FILES     8
#define MAX_PATH_LEN       512
// Longest name on the share, in UTF-8 bytes: CREATE carries it as UTF-16 in usmb2's 1 KB buffer.
#define SMB2MAN_NAME_MAX   440
// How long the driver waits for the server: a connect, then any wait with no data moving (a
// keepalive ECHO waits less). Every wait is bounded, so a dead server can never hold the driver lock for good.
#define SMB2MAN_CONNECT_MS 5000
#define SMB2MAN_REPLY_MS   15000
#define SMB2MAN_ECHO_MS    5000
// One READ or WRITE moves at most this much (64 KB is the most a single credit covers).
#define SMB2MAN_IO_MAX     65536
// Directory listing bytes fetched per QUERY_DIRECTORY, kept with the open directory.
#define SMB2MAN_DIRBUF     4096

//-------------------------------------------------------------------------
// Socket calls for usmb2, bounded by smb2man_wait_ms. usmb2 reads and writes the socket with
// read()/write() and closes it with close() (mapped to these below, for usmb2.c only).
static int smb2man_wait_ms = SMB2MAN_REPLY_MS;

static int smb2man_wait(int fd, int for_write, int ms)
{
    fd_set set, err;
    struct timeval tv;

    FD_ZERO(&set);
    FD_ZERO(&err);
    FD_SET(fd, &set);
    FD_SET(fd, &err);
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;

    return select(fd + 1, for_write ? NULL : &set, for_write ? &set : NULL, &err, &tv);
}

/*
  The socket stays non-blocking after the connect, so a recv() or send() never waits inside the
  stack: each call moves what it can, and the wait for more is the bounded select() above. A peer
  that stops sending, or stops taking data (no ACKs), fails the call after smb2man_wait_ms of no
  progress. A recv()/send() that fails right after select() said ready is retried a few times
  (a spurious wake-up), then taken as a dead socket.
*/
#define SMB2MAN_SOCK_RETRIES 8

static int smb2man_sock_read(int fd, void *buf, int len)
{
    int n, tries;

    for (tries = 0; fd >= 0 && tries < SMB2MAN_SOCK_RETRIES; tries++) {
        if (smb2man_wait(fd, 0, smb2man_wait_ms) <= 0)
            return -1;
        n = recv(fd, buf, len, 0);
        if (n >= 0)
            return n; // 0: the peer closed
    }
    return -1;
}

static int smb2man_sock_write(int fd, const void *buf, int len)
{
    int n, tries;

    for (tries = 0; fd >= 0 && tries < SMB2MAN_SOCK_RETRIES; tries++) {
        if (smb2man_wait(fd, 1, smb2man_wait_ms) <= 0)
            return -1;
        n = send(fd, (void *)buf, len, 0);
        if (n > 0)
            return n;
    }
    return -1;
}

static int smb2man_sock_close(int fd)
{
    return SMB2MAN_CLOSE(fd);
}

// SMB 2.x signing (HMAC-SHA256 over the message, keyed by the session base key), with the in-game
// reader's hashing. Called from usmb2_build_request() under the driver lock: a request is signed
// whole before its reply is read, so the two share one context.
static smb2auth_hmac_t smb2man_mac;
static u8 smb2man_digest[32], smb2man_reply_sig[16];

static void smb2man_sign(const uint8_t *key, uint8_t *hdr, int hdrlen, const uint8_t *data, int datalen)
{
    smb2auth_hmac_t *mac = &smb2man_mac;
    u8 *digest = smb2man_digest;

    hdr[16] |= 0x08; // SMB2_FLAGS_SIGNED
    memset(hdr + 48, 0, 16);
    smb2auth_hmac_init(mac, SMB2AUTH_SHA256, key, 16);
    smb2auth_hmac_update(mac, hdr, hdrlen);
    if (data != NULL && datalen > 0)
        smb2auth_hmac_update(mac, data, datalen);
    smb2auth_hmac_final(mac, digest);
    memcpy(hdr + 48, digest, 16);
}
#define USMB2_SIGN(u, hdr, hdrlen, data, datalen) smb2man_sign((u)->session_key, hdr, hdrlen, data, datalen)

// A reply on a signed session: refused unless it is signed (SMB2_FLAGS_SIGNED), then its 64-byte header,
// signature zeroed, starts the HMAC that usmb2_build_request() feeds every byte it reads after it.
static int smb2man_verify_begin(const uint8_t *key, uint8_t *hdr)
{
    if (!(hdr[16] & 0x08))
        return -1;
    memcpy(smb2man_reply_sig, hdr + 48, 16);
    memset(hdr + 48, 0, 16);
    smb2auth_hmac_init(&smb2man_mac, SMB2AUTH_SHA256, key, 16);
    smb2auth_hmac_update(&smb2man_mac, hdr, 64);
    return 0;
}

static int smb2man_verify_end(void)
{
    u8 diff = 0;
    int i;

    smb2auth_hmac_final(&smb2man_mac, smb2man_digest);
    for (i = 0; i < 16; i++)
        diff |= smb2man_digest[i] ^ smb2man_reply_sig[i];
    return diff ? -1 : 0;
}
#define USMB2_VERIFY_BEGIN(u, hdr) smb2man_verify_begin((u)->session_key, hdr)
#define USMB2_VERIFY_UPDATE(p, n)  smb2auth_hmac_update(&smb2man_mac, p, n)
#define USMB2_VERIFY_END()         smb2man_verify_end()

#undef read
#undef write
#undef close
#define read(a, b, c)  smb2man_sock_read(a, b, c)
#define write(a, b, c) smb2man_sock_write(a, b, c)
#define close(a)       smb2man_sock_close(a)
#include "usmb2/usmb2.c"
// The IOP faults on misaligned word and halfword access, which usmb2 makes all over buf.
_Static_assert(offsetof(struct usmb2_context, buf) % 8 == 0, "usmb2_context.buf must stay aligned");
#undef read
#undef write
#undef close

#define CMD_ECHO                     13
#define STATUS_NO_MORE_FILES         0x80000006
#define STATUS_NO_SUCH_FILE          0xC000000F
#define STATUS_OBJECT_NAME_INVALID   0xC0000033
#define STATUS_OBJECT_NAME_NOT_FOUND 0xC0000034
#define STATUS_OBJECT_NAME_COLLISION 0xC0000035
#define STATUS_OBJECT_PATH_NOT_FOUND 0xC000003A
#define STATUS_ACCESS_DENIED         0xC0000022
#define STATUS_SHARING_VIOLATION     0xC0000043
#define STATUS_DELETE_PENDING        0xC0000056
#define STATUS_DISK_FULL             0xC000007F
#define STATUS_FILE_IS_A_DIRECTORY   0xC00000BA
#define STATUS_DIRECTORY_NOT_EMPTY   0xC0000101
#define STATUS_NOT_A_DIRECTORY       0xC0000103

//-------------------------------------------------------------------------
static struct usmb2_context *smb2 = NULL; // the open share's session, or NULL
static int smb2_sema = -1;
static int smb2_dialect = 0; // negotiated for the open share
static uint32_t io_read_max = SMB2MAN_IO_MAX, io_write_max = SMB2MAN_IO_MAX;
static unsigned session_gen = 0; // bumped whenever a session ends; open directories keep it

// LOGON only records the server and credentials: the logon happens when a share is opened.
static uint32_t conn_ip; // network order
static int conn_port = 445;
static char conn_ipstr[16];
static char conn_user[64];
static char conn_password[64];
static char conn_share[256]; // the share OPENSHARE connected, for the keepalive's reconnect
static int conn_valid = 0;
static int share_open = 0;
// Set when a share that was open is gone (a reply that never came, a failed keepalive) and
// nothing has connected it again. ECHO reports it, so the menu's Refresh reconnects at once, as
// it does with smbman; LOGON, OPENSHARE and LOGOFF clear it.
static int session_lost = 0;

/*
  Fix for the menu freeze on SMB2: the share open (TCP connect, NEGOTIATE, NTLM logon, TREE
  CONNECT) runs on the driver's worker thread, not on fileXio's RPC thread, which serves every
  other device the menu uses. The EE asks with SMB2_DEVCTL_OPENSHARE_ASYNC and polls
  SMB2_DEVCTL_CONNECTSTATE. While the worker connects (that, or a keepalive reconnect) every call
  that needs the session returns -EBUSY at once instead of queueing behind it.
*/
static volatile int connect_state = -ENODEV; // SMB2_CONNECT_PENDING, 0, or the OPENSHARE error
static volatile int worker_busy = 0;         // the worker is connecting
static volatile int connect_requested = 0;
static char pending_share[256];
static int worker_ok = 0;

/*
  Keepalive, as smbman's: every KEEPALIVE_SECONDS an alarm wakes the worker thread, which sends an
  SMB2 ECHO on the open session. It keeps a Windows server's idle autodisconnect (15 minutes by
  default) from dropping the session while the menu sits still, and when the server did drop it,
  the worker tears the session down and connects the share again, on the next tick. A failed
  reconnect is retried KEEPALIVE_RETRY_SECONDS after the attempt ends.
*/
#define KEEPALIVE_SECONDS       3
#define KEEPALIVE_RETRY_SECONDS 30
static int keepalive_sema = -1;
static int keepalive_tid = -1;
static iop_sys_clock_t keepalive_clock;
static u32 keepalive_ticks = 0;    // alarm ticks since start, for the retry backoff
static u32 keepalive_retry_at = 0; // tick at which the next reconnect may be tried
static u32 last_use_tick = 0;      // tick of the last call that used the share (no ECHO right after)

struct smb2man_file
{
    uint8_t fid[16];
    uint64_t pos, size;
    int used, writable;
    unsigned gen; // bumped on every open of this slot; privdata carries it (see smb2man_slot)
};

// privdata = slot | gen << SMB2MAN_SLOT_BITS. MAX_OPEN_FILES must fit in the slot bits.
#define SMB2MAN_SLOT_BITS 4
#define SMB2MAN_GEN_MASK  (0x7FFFFFFFu >> SMB2MAN_SLOT_BITS)

static struct smb2man_file file_table[MAX_OPEN_FILES];

// An open directory: its handle on the server and the part of the listing not yet returned.
struct smb2man_dir
{
    uint8_t fid[16];
    unsigned session; // session_gen at dopen: a handle from an ended session gets -EBADF
    int len, off, eof;
    uint8_t data[SMB2MAN_DIRBUF];
};

#define LOCK()   WaitSema(smb2_sema)
#define UNLOCK() SignalSema(smb2_sema)

//-------------------------------------------------------------------------
// Allocator for usmb2 (the IOP has no libc heap).
void *malloc(size_t size)
{
    void *result;
    int OldState;

    CpuSuspendIntr(&OldState);
    result = AllocSysMemory(ALLOC_FIRST, (int)size, NULL);
    CpuResumeIntr(OldState);

    return result;
}

void free(void *ptr)
{
    int OldState;

    if (ptr == NULL)
        return;

    CpuSuspendIntr(&OldState);
    FreeSysMemory(ptr);
    CpuResumeIntr(OldState);
}

void *calloc(size_t nmemb, size_t size)
{
    size_t s = nmemb * size;
    void *ptr;

    if (size != 0 && s / size != nmemb)
        return NULL;

    ptr = malloc(s);
    if (ptr != NULL)
        memset(ptr, 0, s);

    return ptr;
}

//-------------------------------------------------------------------------
/*
  A user name or password from the EE (UTF-8) as usmb2's NTLM code takes it: one byte per UTF-16
  code unit, so Latin-1. Characters above U+00FF, malformed UTF-8 and anything that does not fit
  fail: the logon is refused rather than sent with credentials the server would not recognise.
*/
static int smb2man_latin1(char *dst, const char *src, unsigned size)
{
    const unsigned char *s = (const unsigned char *)src;
    unsigned n = 0;

    while (*s) {
        unsigned c = *s++;

        if (c >= 0x80) {
            if ((c & 0xe0) != 0xc0 || (*s & 0xc0) != 0x80)
                return -1;
            c = ((c & 0x1f) << 6) | (*s++ & 0x3f);
            if (c < 0x80 || c > 0xff)
                return -1;
        }
        if (n + 1 >= size)
            return -1;
        dst[n++] = (char)c;
    }
    dst[n] = '\0';
    return (int)n;
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

static uint16_t smb2man_utf16[SMB2MAN_NAME_MAX + 2]; // fixpath's check, used under the lock

/*
  OPL builds share paths with backslashes and a leading separator ("\CD\FOO.ISO"); SMB2 wants
  backslashes, relative to the share root, with no separator at either end. Returns -ENAMETOOLONG
  rather than truncating, so an over-long name can never open a different, shorter file.
*/
static int smb2man_fixpath(char *out, const char *in)
{
    int i = 0;

    while (*in == '\\' || *in == '/')
        in++;

    while (*in != '\0') {
        if (i >= SMB2MAN_NAME_MAX)
            return -ENAMETOOLONG;
        out[i++] = (*in == '/') ? '\\' : *in;
        in++;
    }
    while (i > 0 && out[i - 1] == '\\')
        i--;

    out[i] = '\0';
    // usmb2 writes the name as UTF-16 and trusts it to be UTF-8: a byte sequence that is not would
    // come back as a negative length. Refuse it here instead.
    if (utf8_to_utf16(out, smb2man_utf16) < 0)
        return -EINVAL;
    return 0;
}

// A negative errno for the last failed request, from the server's status.
static int smb2man_errno(int fallback)
{
    if (smb2 == NULL || smb2->fd < 0)
        return -EIO;

    switch (smb2->status) {
        case STATUS_NO_SUCH_FILE:
        case STATUS_OBJECT_NAME_NOT_FOUND:
        case STATUS_OBJECT_PATH_NOT_FOUND:
            return -ENOENT;
        case STATUS_OBJECT_NAME_COLLISION:
            return -EEXIST;
        case STATUS_ACCESS_DENIED:
            return -EACCES;
        case STATUS_SHARING_VIOLATION:
        case STATUS_DELETE_PENDING:
            return -EBUSY;
        case STATUS_DISK_FULL:
            return -ENOSPC;
        case STATUS_FILE_IS_A_DIRECTORY:
            return -EISDIR;
        case STATUS_NOT_A_DIRECTORY:
            return -ENOTDIR;
        case STATUS_DIRECTORY_NOT_EMPTY:
            return -ENOTEMPTY;
        case STATUS_OBJECT_NAME_INVALID:
            return -EINVAL;
    }
    return fallback;
}

//-------------------------------------------------------------------------
// End the session without waiting on the network. Called with the lock held.
static void smb2man_free_session(void)
{
    int i;

    for (i = 0; i < MAX_OPEN_FILES; i++)
        file_table[i].used = 0;
    if (smb2 != NULL) {
        if (smb2->fd >= 0)
            smb2man_sock_close(smb2->fd);
        free(smb2);
        smb2 = NULL;
    }
    session_gen++;
}

/*
  The socket died under a request (usmb2 closes it on a failed read or write, and on a reply that
  never came): forget the session and its handles; the menu's next OPENSHARE, or the keepalive,
  builds a fresh one. Called with the lock held after every failed request.
*/
static void smb2man_check_session(void)
{
    if (smb2 == NULL || smb2->fd >= 0)
        return;

    smb2man_free_session();
    if (share_open) {
        share_open = 0;
        session_lost = 1;
        // The first reconnect waits for the next tick; a reconnect the menu starts as soon as ECHO
        // reports the loss (its Refresh) should go first.
        keepalive_retry_at = keepalive_ticks + 1;
    }
}

// Close every open handle and the session. Called with the lock held.
static void smb2man_teardown(void)
{
    int i;

    if (smb2 != NULL && smb2->fd >= 0 && share_open) {
        for (i = 0; i < MAX_OPEN_FILES && smb2->fd >= 0; i++) {
            if (file_table[i].used)
                usmb2_close(smb2, file_table[i].fid);
        }
    }
    smb2man_free_session();
    share_open = 0;
}

//-------------------------------------------------------------------------
/*
  NEGOTIATE offering two dialects, as the in-game reader does: 2.0.2 and 2.1 first, then 3.0 and
  3.0.2 on a new connection when a server refuses 2.x. 2.x signs with HMAC-SHA256, which is all
  smb2man implements, so a server that requires signing (Windows 11 24H2) stays on 2.1. Returns the
  dialect, -2 when the server does not support either, or -1. *sign_required: the server's ask.
*/
static int smb2man_negotiate(struct usmb2_context *u, uint16_t d0, uint16_t d1, int *sign_required)
{
    uint8_t *p = &u->buf[4 + 64];
    int dialect;

    u->message_id = 0;
    u->session_id = 0;
    u->tree_id = 0;
    u->signing = 0;
    memset(u->buf, 0, sizeof(u->buf));
    // StructureSize 36, DialectCount 2, SecurityMode SIGNING_ENABLED, no capabilities
    *(uint32_t *)p = htole32(0x00020024);
    p[4] = 0x01;
    p[12] = 0xaa; // ClientGuid, as usmb2's
    p[27] = 0xbb;
    p[36] = (uint8_t)d0;
    p[37] = (uint8_t)(d0 >> 8);
    p[38] = (uint8_t)d1;
    p[39] = (uint8_t)(d1 >> 8);

    if (usmb2_build_request(u, CMD_NEGOTIATE_PROTOCOL, 40, 64, NULL, 0, NULL, 0)) {
        if (u->fd >= 0 && u->status == 0xC00000BB) // STATUS_NOT_SUPPORTED
            return -2;
        return -1;
    }

    dialect = u->buf[4] | (u->buf[5] << 8);
    if (dialect != d0 && dialect != d1)
        return -1;
    *sign_required = (u->buf[2] & 0x02) ? 1 : 0; // SMB2_NEGOTIATE_SIGNING_REQUIRED
    io_read_max = rd32(&u->buf[32]);
    io_write_max = rd32(&u->buf[36]);
    if (io_read_max == 0 || io_read_max > SMB2MAN_IO_MAX)
        io_read_max = SMB2MAN_IO_MAX;
    if (io_write_max == 0 || io_write_max > SMB2MAN_IO_MAX)
        io_write_max = SMB2MAN_IO_MAX;

    return dialect;
}

// TCP connect with a deadline: non-blocking connect, then wait until the socket is writable.
static int smb2man_tcp_connect(void)
{
    struct sockaddr_in sin;
    int fd, rv, err = 0, on = 1, off = 0;
    socklen_t errlen = sizeof(err);

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((uint16_t)conn_port);
    sin.sin_addr.s_addr = conn_ip;

    SMB2MAN_IOCTL(fd, FIONBIO, &on);
    rv = connect(fd, (struct sockaddr *)&sin, sizeof(sin));
    if (rv != 0) {
        if (smb2man_wait(fd, 1, SMB2MAN_CONNECT_MS) <= 0 ||
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen) != 0 || err != 0) {
            smb2man_sock_close(fd);
            return -1;
        }
    }
    (void)off; // stays non-blocking: see smb2man_sock_read()

    return fd;
}

// tests/smb2-live/keepalive.c runs alarm ticks here, as they fire on the IOP while a request waits.
#ifndef SMB2MAN_AFTER_WAIT
#define SMB2MAN_AFTER_WAIT()
#endif

// Connect conn_share and log on: the new session, or NULL. Called with the lock held.
static struct usmb2_context *smb2man_connect(int *dialect)
{
    struct usmb2_context *u;
    char unc[2 + sizeof(conn_ipstr) + 1 + sizeof(conn_share)];
    int rv, sign_required = 0;

    if (strlen(conn_ipstr) + strlen(conn_share) + 3 > 127) // TREE_CONNECT's path length is one byte
        return NULL;
    sprintf(unc, "\\\\%s\\%s", conn_ipstr, conn_share);

    u = calloc(1, sizeof(*u));
    if (u == NULL)
        return NULL;

    u->fd = smb2man_tcp_connect();
    if (u->fd < 0) {
        free(u);
        return NULL;
    }
    // A blank user logs on as "Guest" with the password, as the in-game reader does.
    strcpy(u->username, conn_user[0] ? conn_user : "Guest");
    strcpy(u->password, conn_password);

    rv = smb2man_negotiate(u, 0x0202, 0x0210, &sign_required);
    if (rv == -2) {
        smb2man_sock_close(u->fd);
        u->fd = smb2man_tcp_connect();
        rv = u->fd < 0 ? -1 : smb2man_negotiate(u, 0x0300, 0x0302, &sign_required);
    }
    // 3.x signs with AES-CMAC, which smb2man does not implement: refuse rather than send unsigned.
    if (rv >= 0x0300 && sign_required)
        rv = -1;
    if (rv >= 0 && usmb2_sessionsetup(u) == 0) {
        // A guest or null session has no key to sign with; servers do not require it of them.
        u->signing = (sign_required && !(rd16(&u->buf[2]) & 0x0003)) ? 1 : 0;
        if (usmb2_treeconnect(u, unc) != 0)
            rv = -1;
    } else {
        rv = -1;
    }
    if (rv < 0) {
        if (u->fd >= 0)
            smb2man_sock_close(u->fd);
        free(u);
        return NULL;
    }

    *dialect = rv;
    return u;
}

// Open conn_share as the session. Returns 0 or OPENSHARE's error. Called with the lock held.
static int smb2man_open_share(void)
{
    struct usmb2_context *u;
    int dialect = 0;

    u = smb2man_connect(&dialect);
    SMB2MAN_AFTER_WAIT();
    if (u == NULL)
        return -SMB_DEVCTL_LOGON_ERR_LOGON;

    smb2man_free_session();
    smb2 = u;
    smb2_dialect = dialect;
    share_open = 1;
    session_lost = 0;
    printf("smb2man: share %s open, SMB dialect 0x%04x\n", conn_share, dialect);
    return 0;
}

static int smb2man_echo(void)
{
    int rv;

    memset(smb2->buf, 0, sizeof(smb2->buf));
    smb2->buf[4 + 64] = 4; // StructureSize
    smb2man_wait_ms = SMB2MAN_ECHO_MS;
    rv = usmb2_build_request(smb2, CMD_ECHO, 4, 4, NULL, 0, NULL, 0) ? -1 : 0;
    smb2man_wait_ms = SMB2MAN_REPLY_MS;
    SMB2MAN_AFTER_WAIT();

    return rv;
}

// The file's size from the server (QUERY_INFO FileStandardInformation), all 64 bits.
static int smb2man_query_size(const uint8_t *fid, uint64_t *size)
{
    uint8_t *p = &smb2->buf[4 + 64];

    memset(smb2->buf, 0, sizeof(smb2->buf));
    *(uint32_t *)p = htole32(0x05010029);       // StructureSize 41, SMB2_0_INFO_FILE, FileStandardInformation
    *(uint32_t *)(p + 4) = htole32(0x00000018); // OutputBufferLength: the 24 bytes of the reply
    memcpy(p + 24, fid, 16);
    if (usmb2_build_request(smb2, CMD_QUERYINFO, 40, 8, NULL, 0, NULL, 0))
        return -1;

    *size = rd64(&smb2->buf[16]); // EndOfFile, after AllocationSize
    return 0;
}

//-------------------------------------------------------------------------
static void smb2man_keepalive_thread(void *arg);
static unsigned int smb2man_keepalive_alarm(void *arg);

static int smb2man_init(iop_device_t *dev)
{
    iop_sema_t smp;
    iop_thread_t thread;

    (void)dev;

    smp.initial = 1;
    smp.max = 1;
    smp.option = 0;
    smp.attr = 1;
    smb2_sema = CreateSema(&smp);

    memset(file_table, 0, sizeof(file_table));

    if (smb2_sema < 0)
        return -1;

    // The worker: the alarm (and an async OPENSHARE) signals this semaphore, the thread does the
    // network work.
    smp.initial = 0;
    smp.max = 1;
    keepalive_sema = CreateSema(&smp);

    thread.attr = TH_C;
    thread.option = 0;
    thread.thread = smb2man_keepalive_thread;
    thread.stacksize = 0x2000; // usmb2 is shallow: its deepest path, the NTLMv2 logon, needs well under 2 KB
    thread.priority = 0x64;
    keepalive_tid = CreateThread(&thread);
    if (keepalive_sema >= 0 && keepalive_tid >= 0 && StartThread(keepalive_tid, NULL) == 0) {
        worker_ok = 1;
        USec2SysClock(KEEPALIVE_SECONDS * 1000 * 1000, &keepalive_clock);
        if (SetAlarm(&keepalive_clock, smb2man_keepalive_alarm, NULL) != 0)
            printf("smb2man: no keepalive alarm; sessions are checked only when used\n");
    } else {
        printf("smb2man: no worker thread (sema %d, thread %d)\n", keepalive_sema, keepalive_tid);
    }

    return 0;
}

// iomanX calls this only when the "smb" device is deleted, which never happens while smb2man is
// resident (it goes only with an IOP reset). So it stops the alarm and frees the semaphore, and
// leaves the worker thread and its stack in place.
static int smb2man_deinit(iop_device_t *dev)
{
    int oldstate;

    (void)dev;

    CpuSuspendIntr(&oldstate);
    CancelAlarm(smb2man_keepalive_alarm, NULL);
    CpuResumeIntr(oldstate);

    if (smb2_sema >= 0) {
        DeleteSema(smb2_sema);
        smb2_sema = -1;
    }

    return 0;
}

//-------------------------------------------------------------------------
/*
  A handle whose slot was cleared (share change, logoff, a dropped session) and then reused by a
  later open must not reach the new file: the slot's generation no longer matches the one stored
  in the stale handle's privdata, so it gets -EBADF instead.
*/
static int smb2man_slot(iop_file_t *f)
{
    unsigned v = (unsigned)(uintptr_t)f->privdata;
    unsigned slot = v & ((1u << SMB2MAN_SLOT_BITS) - 1);

    if (slot >= MAX_OPEN_FILES || !file_table[slot].used || file_table[slot].gen != (v >> SMB2MAN_SLOT_BITS))
        return -1;

    return (int)slot;
}

// Take the lock while the worker is not connecting. Returns 0 with the lock held, else -EBUSY.
// The worker marks itself busy before it takes the lock for a connect: a call that got here just
// before that must not then run on (or wait out) a session being replaced.
static int smb2man_lock_idle(void)
{
    if (worker_busy)
        return -EBUSY;
    LOCK();
    if (worker_busy) {
        UNLOCK();
        return -EBUSY;
    }
    return 0;
}

// Take the lock for a call that needs the share, or say why not. Returns 0 with the lock held.
static int smb2man_lock_share(void)
{
    if (smb2man_lock_idle() < 0)
        return -EBUSY;
    if (smb2 == NULL || !share_open) {
        UNLOCK();
        return -ENODEV;
    }
    last_use_tick = keepalive_ticks;
    return 0;
}

/*
  The CREATE disposition from the open flags: without O_CREAT no new file (a per-game config, a
  VMC) could be made, and without O_TRUNC a shorter rewrite would keep the old tail.
*/
static uint8_t smb2man_disposition(int flags)
{
    if (flags & FIO_O_CREAT) {
        if (flags & FIO_O_EXCL)
            return 0x02;                            // FILE_CREATE
        return (flags & FIO_O_TRUNC) ? 0x05 : 0x03; // FILE_OVERWRITE_IF : FILE_OPEN_IF
    }
    return (flags & FIO_O_TRUNC) ? 0x04 : 0x01; // FILE_OVERWRITE : FILE_OPEN
}

static int smb2man_open(iop_file_t *f, const char *filename, int flags, int mode)
{
    char path[MAX_PATH_LEN];
    struct smb2man_file *file;
    uint8_t *fid;
    int slot, rv, writable;

    (void)mode;

    if (filename == NULL)
        return -EINVAL;

    rv = smb2man_lock_share();
    if (rv < 0)
        return rv;

    for (slot = 0; slot < MAX_OPEN_FILES && file_table[slot].used; slot++)
        ;
    if (slot == MAX_OPEN_FILES) {
        UNLOCK();
        return -EMFILE;
    }

    rv = smb2man_fixpath(path, filename);
    if (rv < 0) {
        UNLOCK();
        return rv;
    }

    // FIO_O_WRONLY's bit is in FIO_O_RDWR too. A read-only handle refuses writes itself (see write).
    writable = (flags & FIO_O_WRONLY) != 0;
    // Desired access: READ_DATA, READ_EA, READ_ATTRIBUTES, and WRITE_DATA for a writable handle.
    fid = usmb2_create(smb2, path, writable ? 0x8b : 0x89,
                       writable ? smb2man_disposition(flags) : 0x01, 0x40 /* non-directory */, 0x00, 0, 16);
    if (fid == NULL) {
        rv = smb2man_errno(-ENOENT);
        smb2man_check_session();
        UNLOCK();
        return rv;
    }

    file = &file_table[slot];
    memcpy(file->fid, fid, 16);
    free(fid);
    file->size = rd64(&smb2->buf[48]); // EndOfFile in the CREATE reply
    file->pos = 0;
    file->writable = writable;
    file->used = 1;
    file->gen = (file->gen + 1) & SMB2MAN_GEN_MASK;
    f->privdata = (void *)(uintptr_t)((unsigned)slot | (file->gen << SMB2MAN_SLOT_BITS));

    UNLOCK();

    return slot;
}

static int smb2man_close(iop_file_t *f)
{
    int slot;

    LOCK();

    slot = smb2man_slot(f);
    if (slot < 0) {
        UNLOCK();
        return -EBADF;
    }

    if (smb2 != NULL && smb2->fd >= 0) {
        usmb2_close(smb2, file_table[slot].fid);
        smb2man_check_session();
    }
    file_table[slot].used = 0;

    UNLOCK();

    return 0;
}

// The open file in the slot f names, with the lock held, or a negative errno without it.
static int smb2man_lock_file(iop_file_t *f)
{
    int slot;

    if (worker_busy)
        return -EBUSY;
    LOCK();
    slot = smb2man_slot(f);
    if (slot < 0 || smb2 == NULL) {
        UNLOCK();
        return -EBADF;
    }
    return slot;
}

/*
  One READ or WRITE moves at most io_read_max/io_write_max (64 KB), so loop until the whole
  request is done. A read stops early only at the end of the file. The callers (art, configs, VMC
  pages) expect smbman's behaviour, where a short count means EOF.
*/
static int smb2man_read(iop_file_t *f, void *buffer, int size)
{
    struct smb2man_file *file;
    uint8_t *p = (uint8_t *)buffer;
    int slot, rv, n, done = 0;

    if (size < 0)
        return -EINVAL;

    slot = smb2man_lock_file(f);
    if (slot < 0)
        return slot;
    file = &file_table[slot];

    while (done < size) {
        n = size - done;
        if ((uint32_t)n > io_read_max)
            n = (int)io_read_max;
        rv = usmb2_pread(smb2, file->fid, p + done, n, file->pos);
        if (rv < 0 && smb2->status == STATUS_END_OF_FILE && smb2->fd >= 0)
            break;
        if (rv < 0) {
            smb2man_check_session();
            UNLOCK();
            return (done > 0) ? done : -EIO;
        }
        if (rv == 0)
            break; // end of file
        if (rv > n)
            rv = n;
        done += rv;
        file->pos += (uint64_t)rv;
    }

    UNLOCK();

    return done;
}

static int smb2man_write(iop_file_t *f, void *buffer, int size)
{
    struct smb2man_file *file;
    uint8_t *p = (uint8_t *)buffer;
    int slot, rv, n, done = 0;

    if (size < 0)
        return -EINVAL;

    slot = smb2man_lock_file(f);
    if (slot < 0)
        return slot;
    file = &file_table[slot];
    if (!file->writable) {
        UNLOCK();
        return -EBADF;
    }

    while (done < size) {
        n = size - done;
        if ((uint32_t)n > io_write_max)
            n = (int)io_write_max;
        rv = usmb2_pwrite(smb2, file->fid, p + done, n, file->pos);
        if (rv <= 0) {
            // A server that stores nothing (a full disk) must not spin this loop forever.
            smb2man_check_session();
            UNLOCK();
            return (done > 0) ? done : -EIO;
        }
        if (rv > n)
            rv = n;
        done += rv;
        file->pos += (uint64_t)rv;
        if (file->pos > file->size)
            file->size = file->pos;
    }

    UNLOCK();

    return done;
}

// The new position for lseek/lseek64, or a negative errno. Called with the lock held.
static s64 smb2man_seek(struct smb2man_file *file, s64 offset, int whence)
{
    uint64_t size;
    s64 base;

    switch (whence) {
        case SEEK_SET:
            base = 0;
            break;
        case SEEK_CUR:
            base = (s64)file->pos;
            break;
        case SEEK_END:
            // Another client may have changed the file: ask the server, else use what is known.
            if (smb2man_query_size(file->fid, &size) == 0)
                file->size = size;
            else
                smb2man_check_session();
            base = (s64)file->size;
            break;
        default:
            return -EINVAL;
    }
    if (base + offset < 0)
        return -EINVAL;

    file->pos = (uint64_t)(base + offset);
    return (s64)file->pos;
}

static int smb2man_lseek(iop_file_t *f, int offset, int whence)
{
    s64 pos;
    int slot;

    slot = smb2man_lock_file(f);
    if (slot < 0)
        return slot;
    pos = smb2man_seek(&file_table[slot], offset, whence);
    UNLOCK();

    if (pos < 0)
        return (int)pos;
    // The 32-bit call cannot report a position past 2 GiB; lseek64 serves those.
    if (pos > 0x7fffffffLL)
        return -EOVERFLOW;

    return (int)pos;
}

static s64 smb2man_lseek64(iop_file_t *f, s64 offset, int whence)
{
    s64 pos;
    int slot;

    slot = smb2man_lock_file(f);
    if (slot < 0)
        return slot;
    pos = smb2man_seek(&file_table[slot], offset, whence);
    UNLOCK();

    return pos;
}

//-------------------------------------------------------------------------
/*
  Unix seconds to the packed time in iox_stat_t: [1] second, [2] minute, [3] hour, [4] day,
  [5] month, [6..7] year (little-endian). ethsupport's change detector compares the CD folder's
  mtime between scans, so leaving these at zero would hide games added to the share.
*/
static void smb2man_pack_time(uint64_t filetime, unsigned char *out)
{
    // FILETIME counts 100 ns from 1601; Unix seconds from 1970.
    u32 t = (filetime > 116444736000000000ULL) ? (u32)((filetime - 116444736000000000ULL) / 10000000u) : 0;
    u32 days = t / 86400u;
    u32 rem = t % 86400u;
    u32 y, m, d;

    out[0] = 0;
    out[1] = (unsigned char)(rem % 60u);
    out[2] = (unsigned char)((rem / 60u) % 60u);
    out[3] = (unsigned char)(rem / 3600u);

    // Civil date from days since 1970-01-01 (Howard Hinnant's civil_from_days).
    {
        u32 z = days + 719468u;
        u32 era = z / 146097u;
        u32 doe = z - era * 146097u;
        u32 yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
        u32 doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
        u32 mp = (5u * doy + 2u) / 153u;

        d = doy - (153u * mp + 2u) / 5u + 1u;
        m = mp + (mp < 10u ? 3u : (u32)-9);
        y = yoe + era * 400u + (m <= 2u ? 1u : 0u);
    }

    out[4] = (unsigned char)d;
    out[5] = (unsigned char)m;
    out[6] = (unsigned char)(y & 0xff);
    out[7] = (unsigned char)((y >> 8) & 0xff);
}

/*
  The stat from SMB2's four FILETIMEs (creation, last access, last write, change, in that order at
  times), the size and the attributes. The PS2's ctime is the creation time, or the change time
  when the server does not report one.
*/
static void smb2man_fill_stat(iox_stat_t *stat, const uint8_t *times, uint64_t size, uint32_t attributes)
{
    uint64_t btime = rd64(times);

    memset(stat, 0, sizeof(iox_stat_t));
    stat->mode = (attributes & 0x10) ? (FIO_S_IFDIR | FIO_S_IRUSR | FIO_S_IWUSR | FIO_S_IXUSR) :
                                       (FIO_S_IFREG | FIO_S_IRUSR | FIO_S_IWUSR);
    stat->size = (unsigned int)size;
    stat->hisize = (unsigned int)(size >> 32);
    smb2man_pack_time(btime ? btime : rd64(times + 24), stat->ctime);
    smb2man_pack_time(rd64(times + 8), stat->atime);
    smb2man_pack_time(rd64(times + 16), stat->mtime);
}

// A name from the listing, UTF-16LE, to UTF-8 in out (outmax bytes with the NUL); never splits a
// character. An unpaired surrogate becomes U+FFFD.
static void smb2man_name_utf8(char *out, int outmax, const uint8_t *in, int units)
{
    int i = 0, o = 0, n;
    uint32_t c, c2;

    while (i < units) {
        c = in[2 * i] | (in[2 * i + 1] << 8);
        i++;
        if (c >= 0xd800 && c < 0xdc00 && i < units) {
            c2 = in[2 * i] | (in[2 * i + 1] << 8);
            if (c2 >= 0xdc00 && c2 < 0xe000) {
                c = 0x10000 + ((c - 0xd800) << 10) + (c2 - 0xdc00);
                i++;
            }
        }
        if (c >= 0xd800 && c < 0xe000)
            c = 0xfffd;
        n = (c < 0x80) ? 1 : (c < 0x800) ? 2 :
                         (c < 0x10000)   ? 3 :
                                           4;
        if (o + n >= outmax)
            break;
        if (n == 1) {
            out[o++] = (char)c;
        } else if (n == 2) {
            out[o++] = (char)(0xc0 | (c >> 6));
            out[o++] = (char)(0x80 | (c & 0x3f));
        } else if (n == 3) {
            out[o++] = (char)(0xe0 | (c >> 12));
            out[o++] = (char)(0x80 | ((c >> 6) & 0x3f));
            out[o++] = (char)(0x80 | (c & 0x3f));
        } else {
            out[o++] = (char)(0xf0 | (c >> 18));
            out[o++] = (char)(0x80 | ((c >> 12) & 0x3f));
            out[o++] = (char)(0x80 | ((c >> 6) & 0x3f));
            out[o++] = (char)(0x80 | (c & 0x3f));
        }
    }
    out[o] = '\0';
}

//-------------------------------------------------------------------------
static int smb2man_dopen(iop_file_t *f, const char *dirname)
{
    char path[MAX_PATH_LEN];
    struct smb2man_dir *d;
    uint8_t *fid;
    int rv;

    if (dirname == NULL)
        return -EINVAL;

    rv = smb2man_lock_share();
    if (rv < 0)
        return rv;

    rv = smb2man_fixpath(path, dirname);
    if (rv < 0) {
        UNLOCK();
        return rv;
    }

    d = malloc(sizeof(*d));
    if (d == NULL) {
        UNLOCK();
        return -ENOMEM;
    }
    // LIST_DIRECTORY and READ_ATTRIBUTES, open an existing directory.
    fid = usmb2_create(smb2, path, 0x81, 0x01, 0x01 /* directory */, 0x10, 0, 16);
    if (fid == NULL) {
        rv = smb2man_errno(-ENOENT);
        free(d);
        smb2man_check_session();
        UNLOCK();
        return rv;
    }
    memcpy(d->fid, fid, 16);
    free(fid);
    d->session = session_gen;
    d->len = d->off = d->eof = 0;

    f->privdata = (void *)d;

    UNLOCK();

    return 0;
}

static int smb2man_dclose(iop_file_t *f)
{
    struct smb2man_dir *d;

    LOCK();

    d = (struct smb2man_dir *)f->privdata;
    if (d == NULL) {
        UNLOCK();
        return -EBADF;
    }

    // A directory of an ended session has nothing left on the server to close.
    if (smb2 != NULL && smb2->fd >= 0 && d->session == session_gen) {
        usmb2_close(smb2, d->fid);
        smb2man_check_session();
    }
    free(d);
    f->privdata = NULL;

    UNLOCK();

    return 0;
}

// Fetch the next part of the listing (QUERY_DIRECTORY, FileDirectoryInformation, "*").
static int smb2man_querydir(struct smb2man_dir *d)
{
    uint8_t *p = &smb2->buf[4 + 64];
    uint32_t status, len;

    memset(smb2->buf, 0, sizeof(smb2->buf));
    *(uint32_t *)p = htole32(0x00010021); // StructureSize 33, FileDirectoryInformation, no flags
    memcpy(p + 8, d->fid, 16);
    p[24] = 0x60; // FileNameOffset
    p[26] = 0x02; // FileNameLength: "*" in UTF-16
    *(uint32_t *)(p + 28) = htole32(SMB2MAN_DIRBUF);
    p[32] = '*';

    status = usmb2_build_request(smb2, CMD_QUERYDIR, 40, 8, NULL, 0, d->data, SMB2MAN_DIRBUF);
    if (status == STATUS_NO_MORE_FILES && smb2->fd >= 0) {
        d->eof = 1;
        return 0;
    }
    if (status != 0) {
        smb2man_check_session();
        return -EIO;
    }

    len = rd32(&smb2->buf[4]); // OutputBufferLength
    d->len = (len > SMB2MAN_DIRBUF) ? SMB2MAN_DIRBUF : (int)len;
    d->off = 0;
    if (d->len == 0)
        d->eof = 1;
    return 0;
}

static int smb2man_dread(iop_file_t *f, iox_dirent_t *dirent)
{
    struct smb2man_dir *d;
    const uint8_t *e;
    uint32_t next, namelen;
    int rv;

    if (worker_busy)
        return -EBUSY;
    LOCK();

    d = (struct smb2man_dir *)f->privdata;
    if (d == NULL || smb2 == NULL || d->session != session_gen) {
        UNLOCK();
        return -EBADF;
    }

    while (d->off + 64 > d->len) {
        if (d->eof) {
            UNLOCK();
            return 0; // end of the directory
        }
        rv = smb2man_querydir(d);
        if (rv < 0) {
            UNLOCK();
            return rv;
        }
    }

    // [MS-FSCC] 2.4.10 FileDirectoryInformation
    e = &d->data[d->off];
    next = rd32(e);
    namelen = rd32(e + 60);
    if (namelen > (uint32_t)(d->len - d->off - 64))
        namelen = (uint32_t)(d->len - d->off - 64);
    d->off = (next >= 64 && next <= (uint32_t)(d->len - d->off)) ? d->off + (int)next : d->len;

    smb2man_fill_stat(&dirent->stat, e + 8, rd64(e + 40), rd32(e + 56));
    smb2man_name_utf8(dirent->name, sizeof(dirent->name), e + 64, (int)(namelen / 2));
    dirent->privdata = NULL;

    UNLOCK();

    return 1;
}

static int smb2man_getstat(iop_file_t *f, const char *filename, iox_stat_t *stat)
{
    char path[MAX_PATH_LEN];
    uint8_t st[56]; // FileNetworkOpenInformation
    int rv;

    (void)f;

    if (filename == NULL || stat == NULL)
        return -EINVAL;

    rv = smb2man_lock_share();
    if (rv < 0)
        return rv;

    rv = smb2man_fixpath(path, filename);
    if (rv == 0 && usmb2_stat(smb2, path, st) != 0) {
        smb2man_check_session();
        rv = -ENOENT;
    }
    if (rv == 0)
        smb2man_fill_stat(stat, st, rd64(st + 40), rd32(st + 48));

    UNLOCK();

    return rv;
}

//-------------------------------------------------------------------------
// Directory and file management (the file manager, sbCreateFolders' CFG/ART/VMC/... folders).
static int smb2man_mkdir(iop_file_t *f, const char *dirname, int mode)
{
    char path[MAX_PATH_LEN];
    uint8_t st[56];
    int rv;

    (void)f;
    (void)mode;

    if (dirname == NULL)
        return -EINVAL;

    rv = smb2man_lock_share();
    if (rv < 0)
        return rv;

    rv = smb2man_fixpath(path, dirname);
    if (rv == 0 && usmb2_mkdir(smb2, path) != 0) {
        rv = smb2man_errno(-EIO);
        // sbCreateFolders runs on every refresh; an existing folder is not a failure.
        if (smb2->fd >= 0 && usmb2_stat(smb2, path, st) == 0 && (rd32(st + 48) & 0x10))
            rv = 0;
        smb2man_check_session();
    }

    UNLOCK();

    return rv;
}

static int smb2man_rmdir(iop_file_t *f, const char *dirname)
{
    char path[MAX_PATH_LEN];
    int rv;

    (void)f;

    if (dirname == NULL)
        return -EINVAL;

    rv = smb2man_lock_share();
    if (rv < 0)
        return rv;

    rv = smb2man_fixpath(path, dirname);
    if (rv == 0 && usmb2_rmdir(smb2, path) != 0) {
        rv = smb2man_errno(-EIO);
        smb2man_check_session();
    }

    UNLOCK();

    return rv;
}

static int smb2man_remove(iop_file_t *f, const char *filename)
{
    char path[MAX_PATH_LEN];
    int rv;

    (void)f;

    if (filename == NULL)
        return -EINVAL;

    rv = smb2man_lock_share();
    if (rv < 0)
        return rv;

    rv = smb2man_fixpath(path, filename);
    if (rv == 0 && usmb2_unlink(smb2, path) != 0) {
        rv = smb2man_errno(-EIO);
        smb2man_check_session();
    }

    UNLOCK();

    return rv;
}

static int smb2man_rename(iop_file_t *f, const char *oldname, const char *newname)
{
    char oldpath[MAX_PATH_LEN];
    char newpath[MAX_PATH_LEN];
    int rv;

    (void)f;

    if (oldname == NULL || newname == NULL)
        return -EINVAL;

    rv = smb2man_lock_share();
    if (rv < 0)
        return rv;

    rv = smb2man_fixpath(oldpath, oldname);
    if (rv == 0)
        rv = smb2man_fixpath(newpath, newname);
    if (rv == 0 && usmb2_rename(smb2, oldpath, newpath) != 0) {
        rv = smb2man_errno(-EIO);
        smb2man_check_session();
    }

    UNLOCK();

    return rv;
}

//-------------------------------------------------------------------------
static unsigned int smb2man_keepalive_alarm(void *arg)
{
    (void)arg;

    keepalive_ticks++;
    iSignalSema(keepalive_sema);
    iSetAlarm(&keepalive_clock, smb2man_keepalive_alarm, NULL);

    return 0;
}

/*
  One pass of the worker, run on each wake: an async OPENSHARE if one is waiting; else with a share
  open, an ECHO, and a failed one drops the session; with the session lost, a reconnect once the
  backoff allows it. (tests/smb2-live runs passes by hand: on the host no thread or alarm runs.)
*/
static void smb2man_keepalive_pass(void)
{
    int rv;

    if (connect_requested) { // worker_busy was set by OPENSHARE_ASYNC, before this pass
        LOCK();
        smb2man_teardown();
        strcpy(conn_share, pending_share);
        rv = conn_valid ? smb2man_open_share() : -ENODEV;
        connect_requested = 0;
        connect_state = rv;
        worker_busy = 0;
        UNLOCK();
        return;
    }

    // A reconnect is decided without the lock and marked busy before taking it, so callers get
    // -EBUSY at once instead of waiting out the connect behind the lock.
    if (!(smb2 != NULL && share_open) && session_lost && conn_valid &&
        (int)(keepalive_ticks - keepalive_retry_at) >= 0) {
        worker_busy = 1;
        LOCK();
        if (!(smb2 != NULL && share_open) && session_lost && conn_valid) {
            if (smb2man_open_share() == 0) {
                printf("smb2man: reconnected %s\n", conn_share);
            } else {
                // Counted from now, the end of the attempt, which may have waited out the timeouts.
                keepalive_retry_at = keepalive_ticks + KEEPALIVE_RETRY_SECONDS / KEEPALIVE_SECONDS;
            }
        }
        worker_busy = 0;
        UNLOCK();
        return;
    }

    // The ECHO: only on an idle session. A request in the last tick has just shown it is alive,
    // and an ECHO then would only make the next request wait behind it.
    if (smb2 == NULL || !share_open || (int)(keepalive_ticks - last_use_tick) < 2)
        return;
    LOCK();
    if (smb2 != NULL && share_open) {
        if (smb2man_echo() < 0) {
            printf("smb2man: keepalive echo failed, session dropped\n");
            smb2man_check_session();
            if (smb2 != NULL) { // the server answered with an error: the session is no good either
                smb2man_free_session();
                share_open = 0;
                session_lost = 1;
                keepalive_retry_at = keepalive_ticks + 1;
            }
        }
    }
    UNLOCK();
}

static void smb2man_keepalive_thread(void *arg)
{
    (void)arg;

    for (;;) {
        WaitSema(keepalive_sema);
        smb2man_keepalive_pass();
    }
}

static int smb2man_devctl(iop_file_t *f, const char *name, int cmd, void *arg, unsigned int arglen, void *buf, unsigned int buflen)
{
    int rv;

    (void)f;
    (void)name;

    switch (cmd) {
        case SMB_DEVCTL_GETPASSWORDHASHES:
            // usmb2 runs NTLMv2 itself and needs the password as typed. Failing here makes the menu
            // send it as PLAINTEXT_PASSWORD; it still goes to the server only as an NTLM response.
            return -1;

        case SMB_DEVCTL_LOGON: {
            smbLogOn_in_t *logon = (smbLogOn_in_t *)arg;
            char ip[sizeof(conn_ipstr)];

            if (arg == NULL || arglen < sizeof(smbLogOn_in_t))
                return -EINVAL;
            if (smb2man_lock_idle() < 0)
                return -EBUSY;
            smb2man_teardown();
            conn_valid = 0;
            session_lost = 0;
            connect_state = -ENODEV;

            strncpy(ip, logon->serverIP, sizeof(ip) - 1);
            ip[sizeof(ip) - 1] = '\0';
            conn_ip = inet_addr(ip);
            if (conn_ip == 0xffffffffu || conn_ip == 0) { // an IP address only; the menu resolves names
                UNLOCK();
                return -SMB_DEVCTL_LOGON_ERR_CONN;
            }
            strcpy(conn_ipstr, ip);
            conn_port = (logon->serverPort > 0 && logon->serverPort < 65536) ? logon->serverPort : 445;

            // usmb2 keeps up to 63 bytes of each, one byte per UTF-16 unit: Latin-1. On the
            // no-password path the menu leaves logon->Password uninitialised.
            if (smb2man_latin1(conn_user, logon->User, sizeof(conn_user)) < 0 ||
                smb2man_latin1(conn_password, logon->PasswordType == PLAINTEXT_PASSWORD ? logon->Password : "",
                               sizeof(conn_password)) < 0) {
                UNLOCK();
                return -SMB_DEVCTL_LOGON_ERR_LOGON;
            }

            conn_valid = 1;
            UNLOCK();
            return 0;
        }

        case SMB_DEVCTL_LOGOFF:
            if (smb2man_lock_idle() < 0)
                return -EBUSY;
            smb2man_teardown();
            conn_valid = 0;
            session_lost = 0;
            connect_state = -ENODEV;
            UNLOCK();
            return 0;

        case SMB_DEVCTL_OPENSHARE: {
            smbOpenShare_in_t *openshare = (smbOpenShare_in_t *)arg;

            if (arg == NULL || arglen < sizeof(smbOpenShare_in_t))
                return -EINVAL;
            if (smb2man_lock_idle() < 0)
                return -EBUSY;
            if (!conn_valid) {
                UNLOCK();
                return -ENODEV;
            }
            smb2man_teardown();
            session_lost = 0;
            openshare->ShareName[sizeof(openshare->ShareName) - 1] = '\0';
            strncpy(conn_share, openshare->ShareName, sizeof(conn_share) - 1);
            conn_share[sizeof(conn_share) - 1] = '\0';
            rv = smb2man_open_share();
            connect_state = rv;
            UNLOCK();
            return rv;
        }

        case SMB2_DEVCTL_OPENSHARE_ASYNC: {
            smbOpenShare_in_t *openshare = (smbOpenShare_in_t *)arg;

            if (arg == NULL || arglen < sizeof(smbOpenShare_in_t))
                return -EINVAL;
            if (!worker_ok)
                return -ENOSYS;
            if (worker_busy)
                return -EBUSY;
            if (!conn_valid)
                return -ENODEV;

            openshare->ShareName[sizeof(openshare->ShareName) - 1] = '\0';
            strncpy(pending_share, openshare->ShareName, sizeof(pending_share) - 1);
            pending_share[sizeof(pending_share) - 1] = '\0';
            connect_state = SMB2_CONNECT_PENDING;
            worker_busy = 1;
            connect_requested = 1;
            SignalSema(keepalive_sema);
            return 0;
        }

        case SMB2_DEVCTL_CONNECTSTATE:
            return connect_state;

        case SMB2_DEVCTL_GETDIALECT:
            // The dialect negotiated for the open share (0x0202 .. 0x0302); -ENODEV without one.
            return (share_open && smb2 != NULL) ? smb2_dialect : -ENODEV;

        case SMB2_DEVCTL_GETIOPFREE:
            // IOP RAM left. Kernel queries only, no LOCK().
            if (buf == NULL || buflen < 2 * sizeof(u32))
                return -EINVAL;
            ((u32 *)buf)[0] = QueryTotalFreeMemSize();
            ((u32 *)buf)[1] = QueryMaxFreeMemSize();
            return 0;

        case SMB_DEVCTL_CLOSESHARE:
            if (smb2man_lock_idle() < 0)
                return -EBUSY;
            smb2man_teardown(); // keeps the logon: a later OPENSHARE connects from it
            session_lost = 0;
            UNLOCK();
            return 0;

        case SMB_DEVCTL_ECHO:
            /*
              The menu's Refresh probes the session with this before deciding between "keep the
              open handles" and "log on again" (ethsupport's refresh path), so it has to tell the
              truth. With a share open it is a real SMB2 ECHO round trip; a share that was open and
              is gone reports -EIO until something connects it again. Between LOGON and OPENSHARE
              there is no session yet: 0, and OPENSHARE is the call whose failure the menu reports.
            */
            if (!conn_valid)
                return -ENODEV;
            if (smb2man_lock_idle() < 0)
                return session_lost ? -EIO : -EBUSY;
            if (smb2 != NULL && share_open) {
                rv = 0;
                if (smb2man_echo() < 0) {
                    smb2man_check_session();
                    if (smb2 != NULL) {
                        smb2man_free_session();
                        share_open = 0;
                        session_lost = 1;
                    }
                    rv = -EIO;
                }
            } else {
                rv = session_lost ? -EIO : 0;
            }
            UNLOCK();
            return rv;

        case SMB_DEVCTL_GETSHARELIST:
            // No share list: the share name must be set in Network Settings.
            return -ENOTSUP;

        case SMB_DEVCTL_QUERYDISKINFO:
            // Only used to show free space, which the menu does not do for SMB.
            return -1;
    }

    return -EINVAL;
}

static int smb2man_nulldev(void)
{
    return -EIO;
}

static s64 smb2man_nulldev64(void)
{
    return -EIO;
}

static iop_device_ops_t smb2man_ops = {
    &smb2man_init,
    &smb2man_deinit,
    (void *)&smb2man_nulldev, // format
    &smb2man_open,
    &smb2man_close,
    &smb2man_read,
    &smb2man_write,
    &smb2man_lseek,
    (void *)&smb2man_nulldev, // ioctl
    &smb2man_remove,
    &smb2man_mkdir,
    &smb2man_rmdir,
    &smb2man_dopen,
    &smb2man_dclose,
    &smb2man_dread,
    &smb2man_getstat,
    (void *)&smb2man_nulldev, // chstat
    &smb2man_rename,
    (void *)&smb2man_nulldev, // chdir
    (void *)&smb2man_nulldev, // sync
    (void *)&smb2man_nulldev, // mount
    (void *)&smb2man_nulldev, // umount
    &smb2man_lseek64,
    &smb2man_devctl,
    (void *)&smb2man_nulldev,   // symlink
    (void *)&smb2man_nulldev,   // readlink
    (void *)&smb2man_nulldev64, // ioctl2
};

static iop_device_t smb2man_dev = {
    "smb",
    IOP_DT_FS | IOP_DT_FSEXT,
    1,
    "SMB2",
    &smb2man_ops,
};

int _start(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("smb2man: SMB2 browse driver, usmb2 (c) Ronnie Sahlberg\n");

    DelDrv(smb2man_dev.name);
    if (AddDrv(&smb2man_dev) != 0)
        return MODULE_NO_RESIDENT_END;

    return MODULE_RESIDENT_END;
}
