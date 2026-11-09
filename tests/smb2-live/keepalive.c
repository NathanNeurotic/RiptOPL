/* Host harness for smb2man.c's keepalive: a session lost under open handles (a keepalive ECHO, an
   ECHO devctl, a read or a write that fails), the reconnect and its backoff, and LOGOFF /
   CLOSESHARE while the session is lost. It includes smb2man.c, so it can run keepalive passes by
   hand (on the host no thread or alarm runs) and see the driver's state. The connection is cut on
   the client's side: shutdown() both ways, so the next request fails as it does when a server goes
   away, or the socket swapped for a dead end that takes requests and never answers, so requests
   still go out and their replies never come.
   The driver's SMB2MAN_AFTER_WAIT hook lets alarm ticks fire while an ECHO or a connect waits, as
   they do when a request times out on the IOP.
   Built with AddressSanitizer, whose leak check at exit fails the run if a dropped session leaves a
   session or a directory allocated. Built and run by run.sh. */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Alarm ticks that fire while a request waits. On the IOP a failed echo or a refused reconnect can
   wait out the reply timeout, several ticks; on the host the alarm never fires by itself, so the
   driver's SMB2MAN_AFTER_WAIT hook runs the alarm handler ticks_during_call times after an ECHO and
   after a connect attempt. */
static int ticks_during_call;
static unsigned int smb2man_keepalive_alarm(void *arg);
static void ticks_while_waiting(void)
{
    int i;

    for (i = 0; i < ticks_during_call; i++)
        smb2man_keepalive_alarm(NULL);
}
#define SMB2MAN_AFTER_WAIT() ticks_while_waiting()
/* Replies on a signed session: the driver's SMB2MAN_VERIFY_HOOK sees each one before it is verified.
   unsigned_left > 0 makes that many of the next replies to unsigned_cmd look unsigned (SMB2_FLAGS_SIGNED
   cleared), as a server that left them unsigned would send them; -1 makes all of them look so. */
static int verified_replies, unsigned_cmd = -1, unsigned_left;
static void verify_hook(uint8_t *hdr)
{
    verified_replies++;
    if (unsigned_left != 0 && hdr[12] == unsigned_cmd) { // the header's Command (low byte)
        hdr[16] &= ~0x08;
        if (unsigned_left > 0)
            unsigned_left--;
    }
}
#define SMB2MAN_VERIFY_HOOK(hdr) verify_hook(hdr)
#include "smb2man.c"

int AddDrv(iop_device_t *d)
{
    return d->ops->init(d);
}
int DelDrv(const char *n)
{
    (void)n;
    return 0;
}

static int fails;
#define CHECK(c, ...)         \
    do {                      \
        if (!(c)) {           \
            fails++;          \
            printf("FAIL: "); \
        } else {              \
            printf("ok: ");   \
        }                     \
        printf(__VA_ARGS__);  \
        printf("\n");         \
    } while (0)

#define OPS (&smb2man_ops)

static int logon(const char *ip, int port, const char *user, const char *pw)
{
    smbLogOn_in_t l;
    smbGetPasswordHashes_in_t in;
    smbGetPasswordHashes_out_t out;

    memset(&l, 0, sizeof(l));
    strcpy(l.serverIP, ip);
    l.serverPort = port;
    strcpy(l.User, user);
    strcpy(in.password, pw);
    if (OPS->devctl(NULL, "", SMB_DEVCTL_GETPASSWORDHASHES, &in, sizeof(in), &out, sizeof(out)) == 0) {
        l.PasswordType = HASHED_PASSWORD;
        memcpy(l.Password, &out, 32);
    } else {
        l.PasswordType = PLAINTEXT_PASSWORD;
        strcpy(l.Password, pw);
    }
    return OPS->devctl(NULL, "", SMB_DEVCTL_LOGON, &l, sizeof(l), NULL, 0);
}

static int openshare(const char *name)
{
    smbOpenShare_in_t o;

    memset(&o, 0, sizeof(o));
    strcpy(o.ShareName, name);
    o.PasswordType = NO_PASSWORD;
    return OPS->devctl(NULL, "", SMB_DEVCTL_OPENSHARE, &o, sizeof(o), NULL, 0);
}

static int echo(void)
{
    return OPS->devctl(NULL, "", SMB_DEVCTL_ECHO, NULL, 0, NULL, 0);
}

// One alarm tick and the keepalive pass it wakes.
static void tick(void)
{
    smb2man_keepalive_alarm(NULL);
    smb2man_keepalive_pass();
}

// The server goes away: the next request on this session fails. After an earlier failed check
// smb2 can be NULL: then there is nothing to cut, and the checks that follow report the state.
static void cut(void)
{
    if (smb2 != NULL)
        shutdown(smb2->fd, SHUT_RDWR);
    last_use_tick = keepalive_ticks - 2; // ... while idle: the next tick's pass sends its ECHO
}

/* Requests still go out, and their replies never come. The session's socket is swapped for one end
   of a socket pair whose other end takes the requests, never answers and has shut its sending side,
   so the client reads an end of file whatever the timing. A shutdown(SHUT_RD) of the TCP socket
   would not do: Linux still delivers a reply that arrives before the client reads, and on a busy or
   one-CPU machine the server can answer first, so the request would succeed. */
static int dead_end = -1;
static void cut_read(void)
{
    int sv[2];

    if (smb2 == NULL || socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0)
        return;
    dup2(sv[0], smb2->fd); // this also closes the TCP connection to the server
    close(sv[0]);
    shutdown(sv[1], SHUT_WR);
    if (dead_end >= 0)
        close(dead_end);
    dead_end = sv[1]; // kept open, so requests can still be written to it
}

static int lost(void)
{
    return smb2 == NULL && !share_open && session_lost;
}

int main(int argc, char **argv)
{
    /* argv: ip port user password share [mandatory: the server requires signing] */
    const char *ip, *user, *pw, *share;
    unsigned char buf[64];
    iox_dirent_t de;
    iop_file_t f = {0}, f2 = {0}, d = {0}, d2 = {0};
    int server_port;
    u32 retry;
    int port = 0, i, attempts, signing_mandatory;

    if (argc < 6) {
        fprintf(stderr, "usage: keepalive ip port user password share [mandatory]\n");
        return 2;
    }
    ip = argv[1];
    user = argv[3];
    pw = argv[4];
    share = argv[5];
    signing_mandatory = argc > 6 && strcmp(argv[6], "mandatory") == 0;
    for (i = 0; argv[2][i] >= '0' && argv[2][i] <= '9'; i++)
        port = port * 10 + (argv[2][i] - '0');
    signal(SIGPIPE, SIG_IGN);         // a write on the cut socket must fail, not end the process
    setvbuf(stdout, NULL, _IONBF, 0); // the leak check ends the process without flushing stdout

    CHECK(smb2man_start(0, NULL) == 0, "module start");
    CHECK(logon(ip, port, user, pw) == 0 && openshare(share) == 0, "logon + openshare");

    /* A live session: the pass echoes and keeps it */
    tick();
    CHECK(smb2 != NULL && share_open && !session_lost && echo() == 0, "a pass on a live session keeps it");

    /* Lost with a file and a directory open */
    CHECK(OPS->open(&f, "\\DVD\\TEST.iso", FIO_O_RDONLY, 0) >= 0 && OPS->read(&f, buf, 16) == 16, "open + read");
    CHECK(OPS->dopen(&d, "\\DVD") == 0 && OPS->dread(&d, &de) == 1, "dopen + one dread");
    /* No ECHO right after a request: the request has just shown the session is alive */
    if (smb2 != NULL)
        shutdown(smb2->fd, SHUT_RDWR);
    last_use_tick = keepalive_ticks;
    tick();
    CHECK(smb2 != NULL && share_open, "no keepalive echo in the tick after a request");
    cut();
    tick();
    CHECK(lost(), "a failed keepalive echo drops the session");
    CHECK(echo() == -EIO, "ECHO reports the lost session");
    CHECK(OPS->read(&f, buf, 16) == -EBADF && OPS->close(&f) == -EBADF, "the file handle is stale");
    CHECK(OPS->dread(&d, &de) == -EBADF, "the directory handle is stale");
    CHECK(OPS->dclose(&d) == 0 && d.privdata == NULL, "dclose of the stale directory handle");

    /* The first reconnect waits for the next tick: ticks that came during the failed echo have
       already signalled the thread, so it runs another pass at once, and a reconnect the menu
       starts after ECHO reported the loss must go first. */
    smb2man_keepalive_pass();
    CHECK(lost(), "no reconnect in the pass right after the drop");
    tick();
    CHECK(smb2 != NULL && share_open && !session_lost, "reconnected on the next tick");
    CHECK(echo() == 0, "ECHO after the reconnect");
    CHECK(OPS->open(&f, "\\DVD\\TEST.iso", FIO_O_RDONLY, 0) >= 0 && OPS->read(&f, buf, 16) == 16 && buf[1] == 10,
          "open + read after the reconnect");
    CHECK(OPS->close(&f) == 0, "close");
    CHECK(OPS->dopen(&d, "\\DVD") == 0 && OPS->dread(&d, &de) == 1 && OPS->dclose(&d) == 0, "list after the reconnect");

    /* A failed reconnect is retried only after the backoff */
    cut();
    tick();
    CHECK(lost(), "dropped again");
    server_port = conn_port;
    conn_port = 9; // nothing listens there: the reconnect is refused
    tick();
    retry = keepalive_retry_at;
    CHECK(lost() && retry == keepalive_ticks + KEEPALIVE_RETRY_SECONDS / KEEPALIVE_SECONDS, "a refused reconnect sets the backoff");
    for (attempts = 0, i = 1; i < KEEPALIVE_RETRY_SECONDS / KEEPALIVE_SECONDS; i++) {
        tick();
        if (keepalive_retry_at != retry || !lost())
            attempts++;
    }
    CHECK(attempts == 0, "no attempt during the backoff (%d)", attempts);
    conn_port = server_port;
    tick();
    CHECK(smb2 != NULL && share_open && !session_lost, "reconnected when the backoff ran out");

    /* LOGOFF while lost: nothing reconnects */
    cut();
    tick();
    CHECK(lost(), "dropped again");
    CHECK(OPS->devctl(NULL, "", SMB_DEVCTL_LOGOFF, NULL, 0, NULL, 0) == 0, "logoff while lost");
    for (i = 0; i < KEEPALIVE_RETRY_SECONDS / KEEPALIVE_SECONDS + 2; i++)
        tick();
    CHECK(smb2 == NULL && !share_open && !session_lost && echo() == -ENODEV, "no reconnect after logoff");

    /* CLOSESHARE while lost: nothing reconnects, the logon stays */
    CHECK(logon(ip, port, user, pw) == 0 && openshare(share) == 0, "logon + openshare");
    cut();
    tick();
    CHECK(lost(), "dropped again");
    CHECK(OPS->devctl(NULL, "", SMB_DEVCTL_CLOSESHARE, NULL, 0, NULL, 0) == 0, "closeshare while lost");
    for (i = 0; i < KEEPALIVE_RETRY_SECONDS / KEEPALIVE_SECONDS + 2; i++)
        tick();
    CHECK(!share_open && !session_lost && echo() == 0, "no reconnect after closeshare");
    CHECK(openshare(share) == 0 && echo() == 0, "openshare again");

    /* A share change and a logoff free the listings of directories still open */
    CHECK(OPS->dopen(&d, "\\DVD") == 0 && OPS->dopen(&d2, "\\") == 0, "two directories open");
    CHECK(OPS->devctl(NULL, "", SMB_DEVCTL_CLOSESHARE, NULL, 0, NULL, 0) == 0 && openshare(share) == 0, "share closed and opened again");
    CHECK(OPS->dread(&d, &de) == -EBADF && OPS->dclose(&d) == 0, "a directory open across the share change is stale");
    CHECK(OPS->dopen(&d, "\\DVD") == 0, "a third directory");
    CHECK(OPS->devctl(NULL, "", SMB_DEVCTL_LOGOFF, NULL, 0, NULL, 0) == 0, "logoff with two directories open");
    CHECK(OPS->dread(&d, &de) == -EBADF && OPS->dclose(&d) == 0 && OPS->dclose(&d2) == 0, "both are stale and close");

    /* An ECHO that fails drops the session too, with a file open */
    CHECK(logon(ip, port, user, pw) == 0 && openshare(share) == 0, "logon + openshare");
    CHECK(OPS->open(&f, "\\DVD\\TEST.iso", FIO_O_RDONLY, 0) >= 0, "open");
    cut();
    CHECK(echo() == -EIO && lost(), "a failed ECHO drops the session");
    smb2man_keepalive_pass();
    CHECK(lost(), "no reconnect in the pass right after the drop");
    CHECK(OPS->close(&f) == -EBADF, "the file handle is stale");
    CHECK(OPS->devctl(NULL, "", SMB_DEVCTL_LOGOFF, NULL, 0, NULL, 0) == 0, "logoff");

    /* A read or a write that fails drops the session too: after cut_read() the request goes out
       and its reply never comes. The leak check sees whether the session was freed. */
    CHECK(logon(ip, port, user, pw) == 0 && openshare(share) == 0, "logon + openshare");
    CHECK(OPS->open(&f, "\\DVD\\TEST.iso", FIO_O_RDONLY, 0) >= 0 && OPS->open(&f2, "\\VMC\\card.bin", FIO_O_RDONLY, 0) >= 0,
          "two files open");
    CHECK(OPS->dopen(&d, "\\DVD") == 0 && OPS->dread(&d, &de) == 1, "dopen + one dread");
    cut_read();
    CHECK(OPS->read(&f, buf, 16) == -EIO && lost(), "a failed read drops the session");
    CHECK(OPS->read(&f2, buf, 16) == -EBADF && OPS->close(&f) == -EBADF && OPS->close(&f2) == -EBADF,
          "both file handles are stale");
    CHECK(OPS->dread(&d, &de) == -EBADF && OPS->dclose(&d) == 0, "the directory handle is stale");
    smb2man_keepalive_pass();
    CHECK(lost(), "no reconnect in the pass right after the drop");
    tick();
    CHECK(smb2 != NULL && share_open && !session_lost && echo() == 0, "reconnected on the next tick");
    CHECK(OPS->open(&f, "\\VMC\\ka.bin", FIO_O_RDWR | FIO_O_CREAT | FIO_O_TRUNC, 0666) >= 0 && OPS->write(&f, buf, 16) == 16,
          "a file open for writing");
    cut_read();
    CHECK(OPS->write(&f, buf, 16) == -EIO && lost(), "a failed write drops the session");
    CHECK(OPS->close(&f) == -EBADF, "the file handle is stale");
    tick();
    CHECK(smb2 != NULL && share_open && !session_lost && echo() == 0, "reconnected on the next tick");
    CHECK(OPS->devctl(NULL, "", SMB_DEVCTL_LOGOFF, NULL, 0, NULL, 0) == 0, "logoff");

    /* Ticks that fire while a request waits: the first reconnect still waits for the tick after the
       drop, and the backoff runs from the end of the refused reconnect, not from its start. */
    CHECK(logon(ip, port, user, pw) == 0 && openshare(share) == 0, "logon + openshare");
    server_port = conn_port;
    ticks_during_call = 5;
    cut();
    tick();
    CHECK(lost() && keepalive_retry_at == keepalive_ticks + 1, "a drop after a slow echo waits for the next tick");
    smb2man_keepalive_pass();
    CHECK(lost(), "no reconnect in the pass right after a slow drop");
    conn_port = 9; // refused
    tick();
    CHECK(lost() && keepalive_retry_at == keepalive_ticks + KEEPALIVE_RETRY_SECONDS / KEEPALIVE_SECONDS,
          "the backoff runs from the end of a slow refused reconnect");
    ticks_during_call = 0;
    conn_port = server_port;
    for (i = 0; i < KEEPALIVE_RETRY_SECONDS / KEEPALIVE_SECONDS && lost(); i++)
        tick();
    CHECK(smb2 != NULL && share_open && !session_lost && i == KEEPALIVE_RETRY_SECONDS / KEEPALIVE_SECONDS,
          "reconnected when the backoff ran out (%d ticks)", i);
    CHECK(OPS->devctl(NULL, "", SMB_DEVCTL_LOGOFF, NULL, 0, NULL, 0) == 0, "logoff");

    /* OPENSHARE_ASYNC: the devctl returns at once, the worker's pass runs the handshake, and
       until it has, every call that needs the session is refused instead of waiting behind it. */
    {
        smbOpenShare_in_t o;
        u32 mem[2];

        memset(&o, 0, sizeof(o));
        strcpy(o.ShareName, share);
        o.PasswordType = NO_PASSWORD;
        CHECK(logon(ip, port, user, pw) == 0, "logon for the async connect");
        CHECK(OPS->devctl(NULL, "", SMB2_DEVCTL_OPENSHARE_ASYNC, &o, sizeof(o), NULL, 0) == 0 &&
                  OPS->devctl(NULL, "", SMB2_DEVCTL_CONNECTSTATE, NULL, 0, NULL, 0) == SMB2_CONNECT_PENDING,
              "OPENSHARE_ASYNC returns at once and the connect is pending");
        CHECK(echo() == -EBUSY && OPS->open(&f, "\\DVD\\TEST.iso", FIO_O_RDONLY, 0) == -EBUSY &&
                  OPS->devctl(NULL, "", SMB_DEVCTL_LOGOFF, NULL, 0, NULL, 0) == -EBUSY,
              "calls that need the session are refused while the connect is pending");
        CHECK(OPS->devctl(NULL, "", SMB2_DEVCTL_GETIOPFREE, NULL, 0, mem, sizeof(mem)) == 0,
              "GETIOPFREE answers while the connect is pending");
        smb2man_keepalive_pass();
        CHECK(OPS->devctl(NULL, "", SMB2_DEVCTL_CONNECTSTATE, NULL, 0, NULL, 0) == 0 && share_open && echo() == 0,
              "the worker's pass opens the share");
        CHECK(OPS->open(&f, "\\DVD\\TEST.iso", FIO_O_RDONLY, 0) >= 0 && OPS->read(&f, buf, 16) == 16 && OPS->close(&f) == 0,
              "the share opened by the worker serves files");

        strcpy(o.ShareName, "NOSUCHSHARE");
        CHECK(OPS->devctl(NULL, "", SMB2_DEVCTL_OPENSHARE_ASYNC, &o, sizeof(o), NULL, 0) == 0, "async connect to a missing share");
        smb2man_keepalive_pass();
        CHECK(OPS->devctl(NULL, "", SMB2_DEVCTL_CONNECTSTATE, NULL, 0, NULL, 0) == -SMB_DEVCTL_LOGON_ERR_LOGON && smb2 == NULL,
              "a failed async connect reports the OPENSHARE error and frees its session");
        CHECK(OPS->devctl(NULL, "", SMB_DEVCTL_LOGOFF, NULL, 0, NULL, 0) == 0 &&
                  OPS->devctl(NULL, "", SMB2_DEVCTL_CONNECTSTATE, NULL, 0, NULL, 0) == -ENODEV,
              "logoff clears the connect state");
    }

    /* Signed session (run.sh passes signing mandatory): every reply is verified but ECHO's, which
       carries no data (a server that left it unsigned would otherwise drop the session at every
       keepalive). A reply the server sent unsigned, as it may STATUS_NETWORK_SESSION_EXPIRED for a
       session it has dropped, fails its request and drops the session; the next tick builds a
       fresh one, and that one is kept: one drop, not a reconnect loop. */
    if (signing_mandatory) {
        int n;

        CHECK(logon(ip, port, user, pw) == 0 && openshare(share) == 0 && smb2 != NULL && smb2->signing,
              "signed session: logon + openshare");
        n = verified_replies;
        CHECK(OPS->open(&f, "\\DVD\\TEST.iso", FIO_O_RDONLY, 0) >= 0 && OPS->read(&f, buf, 16) == 16 &&
                  OPS->close(&f) == 0 && verified_replies == n + 3,
              "CREATE, READ and CLOSE replies are each verified (%d)", verified_replies - n);
        n = verified_replies;
        last_use_tick = keepalive_ticks - 2;
        tick();
        CHECK(smb2 != NULL && share_open && !session_lost && verified_replies == n,
              "the keepalive ECHO keeps the session, its reply not checked");

        unsigned_cmd = 0x08; // READ
        unsigned_left = 1;
        CHECK(OPS->open(&f, "\\DVD\\TEST.iso", FIO_O_RDONLY, 0) >= 0 && OPS->read(&f, buf, 16) == -EIO && lost() &&
                  unsigned_left == 0,
              "an unsigned READ reply fails the read, its data unused, and drops the session");
        n = verified_replies;
        tick();
        CHECK(smb2 != NULL && share_open && !session_lost && verified_replies > n,
              "reconnected on the next tick, the reconnect's replies verified");
        for (i = 0; i < 20; i++)
            tick();
        CHECK(smb2 != NULL && share_open && !session_lost, "then kept for 20 idle ticks of keepalive ECHOs: no reconnect loop");
        n = verified_replies;
        CHECK(OPS->open(&f, "\\DVD\\TEST.iso", FIO_O_RDONLY, 0) >= 0 && OPS->read(&f, buf, 16) == 16 &&
                  OPS->close(&f) == 0 && verified_replies == n + 3,
              "reads verify again");
        unsigned_cmd = -1;
        CHECK(OPS->devctl(NULL, "", SMB_DEVCTL_LOGOFF, NULL, 0, NULL, 0) == 0, "logoff");
    } else {
        CHECK(verified_replies == 0, "no reply is verified when the server does not require signing");
    }

    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails != 0;
}
