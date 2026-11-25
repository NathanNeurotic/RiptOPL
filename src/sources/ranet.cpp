/*
  RA: fetching the watch list from the PC client while the OPL menu is up.

  rcheevos derives the watch list from the achievement set it downloads
  from the RetroAchievements server, and rcheevos cannot run on the PS2.
  So the console asks the PC client (xeRAbora) for the list that belongs to
  the image it is about to launch.

  This runs in the menu, where the full lwIP stack is still available.
  The bypass and hand-built frames belong to the in-game side; nothing
  of that kind is needed here.

  In Xerabora mode the request is a broadcast unless an RA companion IP
  is set; Caduceus first asks its session bridge (CADQ2, UDP 18197) on the
  companion or SMB host, then the engine on that same PC. While a UDPFS
  chain owns the NIC the same exchange runs through the ministack's
  mstack: device instead of a socket.

  Protocol (xeRAbora v0.1.0-alpha.15 protocol v1 with pointer chain extension):
    console -> "RAP1 <own-ip> <own-port>"                    (discovery)
    PC      -> "RAO1 OK <client>/<version>"
    console -> "RAQ1 <hash32> <serial> <own-ip> <own-port>"
    PC      -> "RAA1 OK <bytes> <chunks> [<achievements> <title>]",
               "RAA1 WAIT" or "RAA1 NO"
    console -> "RAG1 <hash32> <chunk-index> <own-ip> <own-port>"
    PC      -> "RAC1 <index> <length> " + binary data

  The console's own address and port are part of the request because
  the PC client may run inside Docker, where NAT rewrites the source
  address. A reply sent back to the apparent source goes to the Docker
  gateway and dies. The socket is therefore bound to the fixed port
  18196 and the PC replies to that address directly.

  The list travels in chunks: it can approach two kilobytes, and IP
  fragment reassembly on this stack is nothing to rely on.

  Why the receive path looks the way it does. recv/recvfrom are not
  local calls here: libc forwards them over SIF RPC to ps2ips.irx and
  the data comes back by a separate DMA. That layer has two delivery
  paths:

  - a packet LARGER than 64 bytes into a 64-byte-aligned buffer: the
    middle goes by direct SIF DMA into our buffer, the same reliable
    mechanism everything else in OPL uses;
  - a packet of 64 bytes or LESS (or unaligned edges): the bytes go
    into a side structure, rests_pkt, and the recv_intr callback copies
    them out at the end of the RPC. On real hardware this path produced
    "recvfrom returned 14, buffer full of zeros".

  The rules this file follows:
  1) rx_ is 64-byte aligned, so the "middle" starts at byte zero and
     arrives by direct DMA;
  2) the PC pads EVERY reply to a multiple of 64 and to at least 128
     bytes, so there is neither head nor tail for recv_intr and the
     small-packet path is never used;
  3) one reply is at most 960 bytes: the IOP side has 1024 bytes per
     call (BUFF_SIZE in ps2ips.c), receives at offset 64 for an aligned
     buffer, and a datagram longer than 992 would overflow the receive
     buffer. A list chunk is therefore 896 bytes:
     896 + header + padding = 960;
  4) from/fromlen are always passed even though unused: the EE recvfrom
     wrapper memcpys into from unconditionally, and NULL means a write
     to address zero.

  Upstream design and implementation: hacan359. Port notes for this fork:
  the menu's stack is ps2sdk lwIP behind the vendored ps2ips, so
  MSG_DONTWAIT is lwIP's 0x08 here -- the 0x40 in
  modules/network/common/smstcpip-common.h belongs to SMSTCPIP, the
  IN-GAME stack, which this file never talks to.

  This file compiles to nothing unless RETROACHIEVEMENTS=1.
*/

#ifdef RETROACHIEVEMENTS

#include "include/host.h"
#include "include/opl.h"
#include "include/util.h"
#include "include/ioman.h"
#include "include/ranet.h"
#include "include/lang.h"
#include "include/supportbase.h"  /* raHashStep: crumbs to the log on the share;
                                    LOG output is invisible in the menu on hardware */
#include "include/ethsupport.h"   /* ethGetNetConfig: own IP for the request */
#include "include/bdmsupport.h"   /* bdmIsUDPBDLoaded: the NIC interlock */
#include "include/udpfssupport.h" /* udpfsGetModulesLoaded: the NIC interlock */
#include "include/rawatch.h"      /* SetWatchList: list straight into memory */
#include "include/rahash.h"       /* raVcdWatchGuardStore: a VCD list is bound to its content */
#include "include/gui.h"          /* guiWarning: the untracked-launch notice */

#include <ps2ips.h>
#include <netinet/in.h> /* sockaddr_in, htons/htonl: ps2ips.h no longer pulls these in */
#include <lwip/def.h>   /* htons/htonl/ntohl */
#include <errno.h>
#include <kernel.h>
#include <delaythread.h> /* DelayThread */

/* Non-blocking mode goes through fcntl: this port does not declare
   ioctl FIONBIO, and a blocking recvfrom with no reply would hang the
   thread for good. */
#include <fcntl.h>
#include <string.h>
#include <time.h> /* clock(): round trip of the link test */

#include "modules/network/common/ra_client.h"
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h> /* fileXioDevctl: the mstack: link */

constexpr int RA_PORT = 18194;
constexpr int RA_MY_PORT = 18196; /* own port: the PC replies here directly, past NAT */
constexpr int RA_CHUNK = 896;     /* agreed with the PC; rule 3 in the header */
constexpr int RA_RECV_MAX = 992;  /* cap for one recvfrom; rule 3 */
constexpr int RA_POLL_MS = 25;    /* pause between polls for a reply */
/* How many times to ask again while the PC answers WAIT. Identifying
   the image is a live request to the RetroAchievements server and
   takes seconds. WAIT replies can be immediate, so pause between up to 20
   rounds rather than exhausting them before a large set is ready.
   We wait only while the PC keeps answering: if it goes silent, ask()
   gives up within its own three seconds, so "client not running" stays
   a fast failure. */
constexpr int RA_WAIT_ROUNDS = 20;
constexpr int RA_WAIT_PAUSE_MS = 500;

/* Per-call non-blocking receive. The socket is also set non-blocking via
   fcntl (see Ranet::openPcSocket), but on the menu's netman lwIP stack that
   flag does not take effect: recvfrom blocks forever when no reply comes.
   In raAskPC the very first recvfrom is the drain loop that runs BEFORE
   the request is sent, so a blocking socket means the request never goes
   out at all. lwIP honours MSG_DONTWAIT per call regardless of the
   socket flag, so every receive here passes it.

   The value MUST be lwIP's 0x08, not the EE <sys/socket.h> MSG_DONTWAIT
   (0x80): the flag is forwarded verbatim over SIF RPC to lwip_recvfrom
   on the IOP, whose headers define it as 0x08. Sending 0x80 would arrive
   as an unknown flag and block. (The in-game SMSTCPIP stack defines 0x40;
   it serves ee_core's raudp, not this menu path.) */
constexpr int RA_MSG_DONTWAIT = 0x08;
constexpr int RA_MSG_PEEK = 0x01; /* lwIP's value, forwarded the same way */

/* The link to the PC is a socket, or -- while a UDPFS chain owns the NIC
   and the menu's lwIP stack cannot run -- the ministack's mstack: device,
   marked by this sentinel. It is >= 0 so every "sock < 0" check still means
   "no link", and no EE socket descriptor gets anywhere near it. */
constexpr int RA_LINK_MSTACK = 0x10000;
constexpr int RA_LINK_BUSY = -8; /* NIC reserved and no mstack: (UDPBD, or UDPFS not loaded yet) */

Ranet gRanet;

int raAskPC(const char *hash, const char *serial, const char *savepath,
            char *info, int infosz, char *info2, int info2sz)
{
    return gRanet.askPc(hash, serial, savepath, info, infosz, info2, info2sz);
}

void raLaunchNetworkUp(void)
{
    gRanet.launchNetworkUp();
}

int raNetTestLink(char *line1, int sz1, char *line2, int sz2)
{
    return gRanet.netTestLink(line1, sz1, line2, sz2);
}

void raNetQuickFirstContact(int on)
{
    gRanet.quickFirstContact(on);
}

int raNetNicBusy(void)
{
    return Ranet::nicBusy();
}

int raNetNicReserved(void)
{
    return Ranet::nicReserved();
}

int raNetUdpfsNotLoaded(void)
{
    return Ranet::udpfsNotLoaded();
}

unsigned int raNetPeerIP(void)
{
    return gRanet.peerIP();
}

void raNetForgetPeer(void)
{
    gRanet.forgetPeer();
}

int raCaduceusPage(const char *request, unsigned int serial, char *out, int size)
{
    return gRanet.caduceusPage(request, serial, out, size);
}

int raNetXeraboraIdentify(const char *hash, const char *serial, unsigned int *host, char *title, int titlesz, int *count)
{
    return gRanet.xeraboraIdentify(hash, serial, host, title, titlesz, count);
}

int raNetHttpGet(unsigned int host, int port, const char *path, char *buf, int max, int *length)
{
    return gRanet.httpGet(host, port, path, buf, max, length);
}

int raNetTcpRecv(int sock, void *aligned, int max)
{
    return Ranet::tcpRecv(sock, aligned, max);
}

/* The NIC settlement (design doc, decided 2026-09-03): RA's menu check is a
   fourth claimant on the one SMAP NIC. When a UDP transport (UDPBD, or the
   UDPFS filesystem chain) owns it, RA must NOT raise the SMB stack on top --
   ethLoadModules() would refuse anyway, but the caller deserves a distinct
   "network busy" answer rather than a generic socket failure. The in-game
   half is unaffected: raudp speaks over whatever SMAP the launch loaded. */
int Ranet::nicBusy()
{
    return bdmIsUDPBDLoaded() || udpfsGetModulesLoaded();
}

/* The menu-side form. gNetworkProtocol is the user's intent: a UDP transport
   that simply has not loaded yet (its page not visited this boot) still owns
   the NIC. Raising SMB for an RA menu action first would claim it, and the
   UDP loader refuses on top of SMB until the next boot. The launch path keeps
   nicBusy: the game's IOP reset frees the NIC either way. */
int Ranet::nicReserved()
{
    return nicBusy() || gNetworkProtocol == NET_PROTO_UDPFS || gNetworkProtocol == NET_PROTO_UDPFSBD ||
           gNetworkProtocol == NET_PROTO_UDPBD;
}

/* A UDPFS transport is selected but its chain has not loaded yet (its page not
   opened this boot), so there is no mstack: link to ask through. */
int Ranet::udpfsNotLoaded()
{
    return (gNetworkProtocol == NET_PROTO_UDPFS || gNetworkProtocol == NET_PROTO_UDPFSBD) && !nicBusy();
}

int Ranet::hostIsExplicit()
{
    return (gRAHostIp[0] | gRAHostIp[1] | gRAHostIp[2] | gRAHostIp[3]) != 0;
}

/* The last UDP responder (replyIP_) is transient and may not have passed
   protocol validation. Only a completed watch-list exchange or a verified
   link check selects a passive game's RA destination, and only for the RA
   mode and hosts it was verified under. */
void Ranet::forgetPeer()
{
    verifiedIP_ = 0;
    verifiedMode_ = -1;
}

void Ranet::rememberPeer(unsigned int ip)
{
    if (ip == 0 || ip == htonl(INADDR_BROADCAST))
        return;
    verifiedIP_ = ip;
    verifiedMode_ = gRAMode;
    memcpy(verifiedSMBHost_, pc_ip, sizeof(verifiedSMBHost_));
    memcpy(verifiedCompanionHost_, gRAHostIp, sizeof(verifiedCompanionHost_));
}

unsigned int Ranet::peerIP() const
{
    if (verifiedMode_ != gRAMode || memcmp(verifiedSMBHost_, pc_ip, sizeof(verifiedSMBHost_)) != 0 ||
        memcmp(verifiedCompanionHost_, gRAHostIp, sizeof(verifiedCompanionHost_)) != 0)
        return 0;
    return verifiedIP_;
}

/* ask() over mstack:. One devctl per attempt sends the request and waits up
   to the same 10 polls the socket path waits; the retries, the reply-source
   check and replyIP_ are the socket path's. */
int Ranet::askMstack(struct sockaddr_in *to, const char *req, char *out, int asklen, int attempts)
{
    int len = strlen(req);

    if (len <= 0 || len > MSTACK_XCHG_REQ_MAX)
        return -1;
    xreq_.dst_ip = to->sin_addr.s_addr;
    xreq_.dst_port = ntohs(to->sin_port);
    xreq_.timeout_ms = 10 * RA_POLL_MS;
    xreq_.len = len;
    memcpy(xreq_.data, req, len);

    for (int t = 0; t < attempts; t++) {
        int got = fileXioDevctl(MSTACK_XCHG_DEVICE, MSTACK_DEVCTL_UDP_XCHG, &xreq_, MSTACK_XCHG_REQ_HDR + len,
                                &xreply_, sizeof(xreply_));
        if (got <= 0)
            continue;
        if (to->sin_addr.s_addr != htonl(INADDR_BROADCAST) && xreply_.src_ip != to->sin_addr.s_addr)
            continue;
        if (got > asklen)
            got = asklen;
        memcpy(out, xreply_.data, got);
        out[got] = '\0';
        replyIP_ = xreply_.src_ip;
        return got;
    }

    return -1;
}

/* Waits for a reply, resending the request. Returns the length or -1.
   The socket is non-blocking; SO_RCVTIMEO is not to be trusted on this
   stack. A unicast request accepts its reply only from that host. */
int Ranet::ask(int sock, struct sockaddr_in *to, const char *req, char *out, int outmax, int attempts)
{
    int t;

    int asklen = outmax - 1;

    /* Never let the IOP side read past its own buffer: rule 3. */
    if (asklen > RA_RECV_MAX)
        asklen = RA_RECV_MAX;

    if (quickTries_ > 0 && attempts == RA_TRY)
        attempts = quickTries_;
    if (sock == RA_LINK_MSTACK) {
        int got = askMstack(to, req, out, asklen, attempts);
        if (got > 0)
            quickTries_ = 0; // the PC is there: later asks get their full wait
        return got;
    }

    for (t = 0; t < attempts; t++) {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        int got, w;

        /* Drop replies to earlier retransmits: a late one would pass for
           the answer to this request. */
        while (recvfrom(sock, out, asklen, RA_MSG_DONTWAIT, (struct sockaddr *)&from, &fromlen) > 0)
            fromlen = sizeof(from);

        sendto(sock, req, strlen(req), 0, (struct sockaddr *)to, sizeof(*to));

        /* The reply may take a moment, so poll several times before
           resending. */
        for (w = 0; w < 10; w++) {
            got = recvfrom(sock, out, asklen, RA_MSG_DONTWAIT, (struct sockaddr *)&from, &fromlen);

            if (got > 0) {
                if (to->sin_addr.s_addr != htonl(INADDR_BROADCAST) && from.sin_addr.s_addr != to->sin_addr.s_addr)
                    continue;
                out[got] = '\0';
                replyIP_ = from.sin_addr.s_addr;
                quickTries_ = 0; // the PC is there: later asks get their full wait
                return got;
            }

            DelayThread(RA_POLL_MS * 1000);
        }
    }

    return -1;
}

/* Opens the UDP socket for talking to the PC: brings the network up if
   it is down, allows broadcast, makes the socket non-blocking and binds
   it to RA_MY_PORT. Fills myaddr with " <ip> <port>" for the request
   text (empty when the console has no address yet) and ip with the own
   address. Returns the socket or -1.

   The network may be down: OPL loads the network modules only when the
   ETH device is enabled, and one can play from USB alone. Bringing it
   up here is allowed because this runs on the I/O thread, like the
   network settings dialog -- and only when no UDP transport owns the
   NIC (Ranet::openPcLink; ethLoadModules() is the runtime backstop).
   Non-blocking is mandatory: the first unanswered recvfrom would
   otherwise hang the thread for good. The fixed own port is the return
   path past Docker NAT (see the header). */
int Ranet::openPcSocket(char *myaddr, int sz, unsigned char ip[4])
{
    struct sockaddr_in me;
    u8 mask[4], gw[4];
    int sock, on = 1, fl;

    myaddr[0] = '\0';

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        /* Record errno and what the module load answered, then retry
           regardless of that answer: a network that is already up may
           report "not zero", and giving up on that would skip a socket
           that would in fact succeed. */
        int e1 = errno, r;
        char crumb[64];

        LOG("RA: no socket (errno %d), bringing up the network\n", e1);
        snprintf(crumb, sizeof(crumb), "6b-bringing-up-network-errno-%d", e1);
        raHashStep(crumb);

        r = ethLoadInitModules();
        sock = socket(AF_INET, SOCK_DGRAM, 0);

        if (sock < 0) {
            snprintf(crumb, sizeof(crumb), "6x-no-socket-errno-%d-ethinit-%d", errno, r);
            LOG("RA: socket creation failed, errno %d, ethLoadInitModules %d\n", errno, r);
            raHashStep(crumb);
            return -1;
        }
    }

    /* Which descriptor we got, for the log. errno on a socket() failure
       is not a diagnosis here: libcglue's __ps2ipcSocketHelper reports
       ENFILE (23) for ANY failure of the IOP-side socket(). The real
       exhaustion to watch for is the UDP PCB pool (DHCP, DNS, NBNS and
       ours) -- kept from leaking by the vendored ps2ips fix. */
    {
        char crumb[32];

        snprintf(crumb, sizeof(crumb), "6c-sock-%d", sock);
        raHashStep(crumb);
    }

    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));

    fl = fcntl(sock, F_GETFL, 0);
    if (fl >= 0)
        fcntl(sock, F_SETFL, fl | O_NONBLOCK);

    memset(&me, 0, sizeof(me));
    me.sin_family = AF_INET;
    me.sin_port = htons(RA_MY_PORT);
    me.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&me, sizeof(me)) < 0) {
        LOG("RA: bind failed\n");
        raHashStep("6x-bind-failed");
        disconnect(sock);
        return -1; /* Replies must reach the fixed RA_MY_PORT, not an ephemeral port. */
    }

    memset(ip, 0, 4);
    if (ethGetNetConfig(ip, mask, gw) >= 0 && (ip[0] | ip[1] | ip[2] | ip[3]))
        snprintf(myaddr, sz, " %d.%d.%d.%d %d", ip[0], ip[1], ip[2], ip[3], RA_MY_PORT);

    return sock;
}

/* The menu-side link: a socket while the NIC is free; the mstack: device
   while a UDPFS chain owns it (own address = the ministack's static IP);
   RA_LINK_BUSY when the NIC is reserved and there is no mstack: (UDPBD, or a
   UDPFS chain not loaded yet). */
int Ranet::openPcLink(char *myaddr, int sz, unsigned char ip[4])
{
    static u8 own[64] __attribute__((aligned(64)));

    if (!nicReserved())
        return openPcSocket(myaddr, sz, ip);

    myaddr[0] = '\0';
    if (fileXioDevctl(MSTACK_XCHG_DEVICE, MSTACK_DEVCTL_VERSION, NULL, 0, NULL, 0) != MSTACK_XCHG_VERSION)
        return RA_LINK_BUSY;
    /* The address the ministack was loaded with: the chain loads once per boot, so a PS2 IP
       edited since then is not the one the PC must reply to. An older driver falls back. */
    for (int i = 0; i < 4; i++)
        ip[i] = fileXioDevctl(MSTACK_XCHG_DEVICE, MSTACK_DEVCTL_GET_IP, NULL, 0, own, 4) == 4 ? own[i] : (u8)ps2_ip[i];
    snprintf(myaddr, sz, " %d.%d.%d.%d %d", ip[0], ip[1], ip[2], ip[3], RA_MY_PORT);
    return RA_LINK_MSTACK;
}

void Ranet::closePcLink(int sock)
{
    if (sock != RA_LINK_MSTACK)
        disconnect(sock);
}

void Ranet::broadcastTarget(struct sockaddr_in *to)
{
    memset(to, 0, sizeof(*to));
    to->sin_family = AF_INET;
    to->sin_port = htons(RA_PORT);
    to->sin_addr.s_addr = htonl(INADDR_BROADCAST);
}

/* A dedicated RA host is optional. Without it, retain the Caduceus-to-SMB
   host binding and Xerabora's broadcast discovery. An override is a unicast
   destination for either mode and never changes the SMB/HTTP game server or
   allows an arbitrary responder to replace it. */
int Ranet::configuredTarget(struct sockaddr_in *to, int port)
{
    const int *host = hostIsExplicit() ? gRAHostIp : pc_ip;

    if (!(host[0] | host[1] | host[2] | host[3]) ||
        (host[0] == 255 && host[1] == 255 && host[2] == 255 && host[3] == 255))
        return 0;
    for (int i = 0; i < 4; i++)
        if (host[i] < 0 || host[i] > 255)
            return 0;

    memset(to, 0, sizeof(*to));
    to->sin_family = AF_INET;
    to->sin_port = htons(port);
    to->sin_addr.s_addr = ((u32)host[0]) | ((u32)host[1] << 8) | ((u32)host[2] << 16) | ((u32)host[3] << 24);
    return 1;
}

/* Caduceus: the bridge at the configured host must report a ready session
   before the engine on that same PC is asked anything. On success the target
   moves to the bridge's engine port (ask() accepted the reply only from that
   host). 0, -9 (offline) or another negative. */
int Ranet::caduceusSession(int sock, struct sockaddr_in *to, const char *hash, char *rx, int rxsize)
{
    char req[48];

    if (!configuredTarget(to, RA_CADUCEUS_PORT))
        return -2;
    snprintf(req, sizeof(req), "CADQ2 %s", hash);
    int got = ask(sock, to, req, rx, rxsize);
    int session = got > 0 ? raCaduceusSessionReply(rx, hash) : -2;
    if (session == 0)
        to->sin_port = htons(RA_PORT);
    return session;
}

/* Xerabora broadcasts unless an explicit companion IP is set; Caduceus goes
   through its session bridge. 0 when `to` is ready, else the askPc result. */
int Ranet::pcTarget(int sock, struct sockaddr_in *to, const char *hash, char *rx, int rxsize)
{
    broadcastTarget(to);
    if (gRAMode == RA_MODE_CADUCEUS)
        return caduceusSession(sock, to, hash, rx, rxsize);
    if (hostIsExplicit() && !configuredTarget(to, RA_PORT))
        return -2;
    return 0;
}

// The payload is binary: consume exactly the protocol's one separator, never scanf whitespace.
static int raParseChunkHeader(const char *reply, int got, int *idx, int *len, int *hdr)
{
    int end = 0;

    *hdr = 0;
    if (sscanf(reply, "RAC1 %d %d%n", idx, len, &end) != 2 || end <= 0 || end >= got || reply[end] != ' ')
        return 0;
    *hdr = end + 1;
    return 1;
}

// The protocol uses fixed offsets, so a matching byte sum alone cannot prove
// coverage: a short intermediate chunk would leave stale bytes in wl_.
static int raExpectedChunkLength(int total, int chunks, int index)
{
    if (total <= 0 || total > RA_MAX_BYTES || chunks != (total + RA_CHUNK - 1) / RA_CHUNK ||
        index < 0 || index >= chunks)
        return -1;
    int remaining = total - index * RA_CHUNK;
    return remaining > RA_CHUNK ? RA_CHUNK : remaining;
}

int Ranet::askPc(const char *hash, const char *serial, const char *savepath,
                 char *info, int infosz, char *info2, int info2sz)
{
    struct sockaddr_in to;
    char req[80];
    char myaddr[32];
    unsigned char ip[4];
    int sock, got;
    int total = 0, chunks = 0, received = 0, i;

    /* A failed or mismatched new check must not reuse an old peer. */
    forgetPeer();
    if (info != NULL && infosz > 0)
        info[0] = '\0';
    if (info2 != NULL && info2sz > 0)
        info2[0] = '\0';

    sock = openPcLink(myaddr, sizeof(myaddr), ip);
    if (sock == RA_LINK_BUSY) {
        LOG("RA: NIC is owned by a UDP transport without mstack:, not asking the PC\n");
        raHashStep("6-nic-busy-udp-transport");
        return -8;
    }
    if (sock < 0)
        return -1;
    if (sock == RA_LINK_MSTACK)
        raHashStep("6-mstack-link");

    got = pcTarget(sock, &to, hash, rx_, sizeof(rx_));
    if (got != 0) {
        closePcLink(sock);
        return got;
    }

    snprintf(req, sizeof(req), "RAQ1 %s %s%s", hash, serial, myaddr);
    LOG("RA: asking the PC about %s\n", hash);

    got = raqAsk(sock, &to, req);
    if (got == -2 || got == -7) {
        LOG("RA: %s\n", got == -2 ? "no reply from the PC" : "PC is still identifying the image");
        closePcLink(sock);
        return got;
    }

    if (got != 0) {
        LOG("RA: PC does not know this image: %s\n", rx_);
        /* "RAA1 NO <reason>": a newer PC says why (unknown game, set too
           big, server down). The sentence goes to the screen in place of
           the console's own guess. An older PC sends bare NO. */
        if (strncmp(rx_, "RAA1 NO ", 8) == 0 && info != NULL && infosz > 0) {
            char *why = rx_ + 8;

            while (*why == ' ')
                why++;
            int len = (int)strlen(why);
            while (len > 0 && why[len - 1] == ' ')
                why[--len] = '\0';
            if (len > 0)
                snprintf(info, infosz, "%s", why);
        }
        closePcLink(sock);
        return 1; /* not an error: the game is unsupported */
    }

    /* Reply body after "RAA1 OK ": "<bytes> <chunks>" then the
       achievement counts and the title. Newer clients send three counts
       (total, unlocked, unsupported); older ones send only the total.
       The title may itself begin with digits ("2 Fast 2 Furious"), so
       the five-number form is accepted only when all five parse, and the
       older three-number form is re-read otherwise -- that also puts the
       title back together correctly. */
    {
        int bytes = 0, ch = 0;
        int nach = 0, nunlocked = -1, nunsupported = -1, pos = 0;
        char *title;
        int len;

        if (sscanf(rx_ + 8, "%d %d %d %d %d %n",
                   &bytes, &ch, &nach, &nunlocked, &nunsupported, &pos) != 5) {
            nunlocked = nunsupported = -1; /* unknown: older client */
            pos = 0;
            sscanf(rx_ + 8, "%d %d %d %n", &bytes, &ch, &nach, &pos);
        }

        title = rx_ + 8 + pos;
        len = (int)strlen(title);
        while (len > 0 && title[len - 1] == ' ')
            title[--len] = '\0';

        if (info != NULL && infosz > 0)
            snprintf(info, infosz, "%s", len > 0 ? title : _l(_STR_RA_SUPPORTED));

        if (info2 != NULL && info2sz > 0) {
            if (nunlocked >= 0)
                snprintf(info2, info2sz, _l(_STR_RA_ACHIEVEMENTS_FULL),
                         nach, nunlocked, nunsupported);
            else
                snprintf(info2, info2sz, _l(_STR_RA_ACHIEVEMENTS), nach);
        }
    }

    if (sscanf(rx_ + 8, "%d %d", &total, &chunks) != 2 ||
        raExpectedChunkLength(total, chunks, 0) < 0) {
        LOG("RA: malformed reply: %s\n", rx_);
        closePcLink(sock);
        return -3;
    }

    LOG("RA: list is %d bytes in %d chunks\n", total, chunks);

    const int chunkAttempts = gRAMode == RA_MODE_XERABORA ? RA_TRY_CHUNK : RA_TRY;
    for (i = 0; i < chunks; i++) {
        int idx = -1, len = -1, hdr = 0;
        bool matched = false;

        snprintf(req, sizeof(req), "RAG1 %s %d%s", hash, i, myaddr);

        /* A reply carrying another chunk's index is a leftover from an
           earlier request: ask again rather than fail. */
        for (int attempt = 0; attempt < 3; attempt++) {
            got = ask(sock, &to, req, rx_, sizeof(rx_), chunkAttempts);
            if (got <= 0) {
                LOG("RA: chunk %d did not arrive\n", i);
                closePcLink(sock);
                return -4;
            }
            if (raParseChunkHeader(rx_, got, &idx, &len, &hdr) && idx == i) {
                matched = true;
                break;
            }
        }

        if (!matched) {
            LOG("RA: chunk %d is malformed\n", i);
            closePcLink(sock);
            return -5;
        }

        if (len != raExpectedChunkLength(total, chunks, i) || len > got - hdr) {
            LOG("RA: chunk %d does not fit\n", i);
            closePcLink(sock);
            return -6;
        }

        memcpy(&wl_[i * RA_CHUNK], &rx_[hdr], len);
        received += len;
    }

    closePcLink(sock);

    if (received != total) {
        LOG("RA: chunks add up to %d bytes, expected %d\n", received, total);
        return -6;
    }

    /* Into loader memory first. Launching the game then does not depend
       on whether the file reached the medium: on a USB stick writes sit
       in the USB driver's cache and the file may appear later, or never
       if the power goes off. */
    if (SetWatchList(wl_, total, serial) <= 0) {
        raHashStep("8x-list-not-parsed");
        return -6; // Never persist or confirm a list the real parser refused.
    }
    rememberPeer(replyIP_);
    raHashStep("8-list-in-memory");

    /* And as a file, for future launches and so the game gets its badge
       in the list. Two copies: next to the game (the loader reads from
       there) and on the share, if there is one, where writes are
       reliable and the loader looks second (sbLoadWatchList). Failing
       either copy is not an error: the list is already in memory. */
    {
        const char *where[2];
        char dir[128], file[192];
        FILE *f;
        int w, saved = 0;

        where[0] = savepath;
        where[1] = strncmp(savepath, "smb0:", 5) != 0 ? "smb0:" : NULL;

        for (w = 0; w < 2; w++) {
            if (where[w] == NULL)
                continue;

            snprintf(dir, sizeof(dir), "%sRA", where[w]);
            mkdir(dir, 0777);
            snprintf(file, sizeof(file), "%sRA/%s.wl", where[w], serial);

            f = fopen(file, "wb");
            if (f == NULL) {
                LOG("RA: could not write %s\n", file);
                continue;
            }

            size_t written = fwrite(wl_, 1, (size_t)total, f);
            if (fclose(f) != 0 || written != (size_t)total) {
                LOG("RA: short/failed watch-list write: %s\n", file);
                unlink(file); // a truncated list must never pass for a confirmed one
                continue;
            }
            saved++;
            /* A VCD's list is keyed by its path, so bind it to this exact content hash, and only
               once the list itself is on disk: a refused update must not authorize an older list. */
            if (serial[0] == 'P' && strlen(serial) == 15 && raVcdWatchGuardStore(where[w], serial, hash) != 0)
                LOG("RA: VCD guard was not saved: %s\n", file);
            LOG("RA: list saved: %s (%d bytes)\n", file, total);
        }

        /* A launch reads the file, not this session's memory: say so instead
           of "start the game to track it" (a read-only UDPFS share, a full
           card). The result stays 0 -- the PC did confirm the game. */
        if (saved == 0) {
            raHashStep("9x-file-not-written");
            if (info2 != NULL && info2sz > 0)
                snprintf(info2, info2sz, "%s", _l(_STR_RA_WL_NOT_SAVED));
        }
    }

    return 0;
}

/* A telemetry launch needs the network adapter already powered and configured when the game
   takes over. The in-game DEV9 driver (inside cdvdman, modules/iopcore/cdvdman/dev9.c) does not
   power the adapter up -- it expects the menu to have done it ("The DEV9 interface and SPEED
   should be already initialized"). That holds for a share, where the menu network is always up,
   but not for a USB or MMCE launch straight from the list: with no RA check or PC link test first
   the adapter stays off in the game, SMAP never sends, and nothing reaches the PC. Bringing the
   network up here is what the link test did by accident. It also settles the console's own
   address, DHCP included, which sysLaunchLoaderElf reads next (netGetConfig only knows it once
   the stack has run). Called by sbLoadWatchList once a launch has a list; a no-op otherwise.

   Without a usable network -- no cable, no adapter, no address or DHCP lease -- the launch goes
   ahead untracked instead.
   Once the adapter is powered, the in-game SMAP retries auto-negotiation with no limit while it
   loads (InitPHY, modules/network/smap-ingame/smap.c), so a game launched with the network modules
   and no cable would never start. Dropping the list keeps those modules out of the launch, exactly
   as for a game with no watch list. */
void Ranet::launchNetworkUp()
{
    int sock, linkUp;
    u8 ip[4], mask[4], gateway[4];

    if (!gRATelemetry || GetWatchCount() <= 0)
        return;

    /* UDPBD/UDPFS own the NIC. A saved watch list may already be loaded,
       but leaving it armed would embed RA telemetry in a network-backed
       launch despite refusing the menu network handoff. Fail open instead. */
    if (nicBusy()) {
        forgetPeer();
        ClearWatchList();
        return;
    }

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0) {
        /* A resident stack can still have no link after a cable pull. */
        disconnect(sock);
        linkUp = NetManIoctl(NETMAN_NETIF_IOCTL_GET_LINK_STATUS, NULL, 0, NULL, 0) == NETMAN_NETIF_ETH_LINK_STATE_UP;
    } else
        linkUp = ethLoadInitModules() == 0;

    /* A socket and physical link do not mean DHCP succeeded. A failed menu-side
       attempt leaves PS2IP resident, so socket() can work while the console has
       no address. Keep the watch list only if the address handed to the in-game
       stack is usable; a pending DHCP lease is not ready either. */
    if (linkUp)
        linkUp = ethGetNetConfig(ip, mask, gateway) >= 0 &&
                 (ip[0] | ip[1] | ip[2] | ip[3]) != 0 &&
                 (!ps2_ip_use_dhcp || ethGetDHCPStatus() > 0);

    if (!linkUp) {
        forgetPeer();
        LOG("RA: network unavailable, launching without telemetry\n");
        ClearWatchList();
        host::ui().warning(_l(_STR_RA_NO_LINK_UNTRACKED), 6);
        return;
    }

    /* Every tracked Caduceus launch rechecks its session: a bridge that is
       offline or gone means an ordinary launch without telemetry. */
    if (gRAMode == RA_MODE_CADUCEUS) {
        struct sockaddr_in to;
        char address[32];
        int session = -1;

        sock = openPcSocket(address, sizeof(address), ip);
        if (sock >= 0) {
            session = caduceusSession(sock, &to, RA_PROBE_HASH, rx_, sizeof(rx_));
            disconnect(sock);
        }
        if (session != 0) {
            forgetPeer();
            ClearWatchList();
            host::ui().warning(_l(_STR_CAD_UNTRACKED), 6);
            return;
        }
        rememberPeer(replyIP_);
    }
}

/* Link test for the menu: asks for up to three seconds. The reply's source
   address and the version in its text tell what answered; the round trip is
   measured with clock(). */
int Ranet::netTestLink(char *line1, int sz1, char *line2, int sz2)
{
    struct sockaddr_in to;
    char myaddr[32], req[64];
    /* Rules 1 and 3 from the file header apply here too: a 64-byte
       aligned buffer received in a length that is a multiple of 64, so
       the reply arrives entirely by direct DMA and the ps2ips "rests"
       path (with its EE-side buffer limit) is never used. */
    static char rx[256] __attribute__((aligned(64)));
    u8 ip[4];
    int sock, got = 0, found = 0;
    clock_t t0, deadline;
    unsigned int ms;

    forgetPeer();
    sock = openPcLink(myaddr, sizeof(myaddr), ip);
    if (sock == RA_LINK_BUSY) {
        snprintf(line1, sz1, "%s", _l(_STR_RA_NET_BUSY));
        snprintf(line2, sz2, "%s", _l(udpfsNotLoaded() ? _STR_RA_UDPFS_LOAD_FIRST : _STR_RA_NET_BUSY2));
        return 0;
    }
    if (sock < 0) {
        /* Either the network is really down or the IOP refused a socket
           (pool exhausted). Name both, rather than blaming the cable. */
        snprintf(line1, sz1, "%s", _l(_STR_RA_NO_SOCKET));
        snprintf(line2, sz2, "%s", _l(_STR_RA_NO_SOCKET2));
        return 0;
    }

    if (myaddr[0] == '\0') {
        closePcLink(sock);
        snprintf(line1, sz1, "%s", _l(_STR_RA_NO_IP));
        snprintf(line2, sz2, "%s", _l(_STR_RA_NO_IP2));
        return 0;
    }

    int session = pcTarget(sock, &to, RA_PROBE_HASH, rx, sizeof(rx));
    if (session != 0) {
        closePcLink(sock);
        snprintf(line1, sz1, "%s", _l(session == -9 ? _STR_RA_CADUCEUS_OFFLINE : _STR_RA_TEST_NO_ANSWER));
        if (gRAMode == RA_MODE_CADUCEUS)
            snprintf(line2, sz2, "%s", _l(_STR_RA_CADUCEUS_SIGN_IN));
        else
            snprintf(line2, sz2, _l(_STR_RA_TEST_NO_ANSWER2), myaddr);
        return 0;
    }
    snprintf(req, sizeof(req), "RAP1%s", myaddr);

    t0 = clock();
    deadline = t0 + 3 * CLOCKS_PER_SEC;

    /* ask() resends and filters the source on either link; a reply that is
       not "RAO1 OK" (another service on the port) asks again until the
       deadline. 193: ask() reads at most 192 bytes, a multiple of 64
       (rule 3). */
    while (!found && clock() < deadline) {
        got = ask(sock, &to, req, rx, 193);
        if (got <= 0)
            break;
        if (got >= 8 && strncmp(rx, "RAO1 OK ", 8) == 0)
            found = 1;
    }

    ms = (unsigned int)((clock() - t0) * 1000 / CLOCKS_PER_SEC);
    closePcLink(sock);

    if (!found) {
        snprintf(line1, sz1, "%s", _l(_STR_RA_TEST_NO_ANSWER));
        snprintf(line2, sz2, _l(_STR_RA_TEST_NO_ANSWER2), myaddr);
        return 0;
    }

    rememberPeer(replyIP_);
    {
        unsigned int a = ntohl(replyIP_);
        char peer[24] = "";

        if (a != 0)
            snprintf(peer, sizeof(peer), "%u.%u.%u.%u", (a >> 24) & 255, (a >> 16) & 255, (a >> 8) & 255, a & 255);

        snprintf(line1, sz1, _l(_STR_RA_TEST_FOUND), peer[0] ? " at " : "", peer, rx + 8);
        snprintf(line2, sz2, _l(_STR_RA_TEST_FOUND2), ms, myaddr);
    }

    return 1;
}

/* RAQ1 with the WAIT rounds: WAIT means the PC hears us and is asking the RA server about
   this image, so keep asking while it answers that way. The reply stays in rx_.
   0 = "RAA1 OK", 1 = anything else (NO), -2 = silent, -7 = still identifying. */
int Ranet::raqAsk(int sock, struct sockaddr_in *to, const char *req)
{
    int got = ask(sock, to, req, rx_, sizeof(rx_));

    for (int w = 0; got > 0 && w < RA_WAIT_ROUNDS && strncmp(rx_, "RAA1 WAIT", 9) == 0; w++) {
        DelayThread(RA_WAIT_PAUSE_MS * 1000);
        got = ask(sock, to, req, rx_, sizeof(rx_));
    }
    if (got <= 0)
        return -2;
    if (strncmp(rx_, "RAA1 WAIT", 9) == 0)
        return -7;
    return strncmp(rx_, "RAA1 OK ", 8) == 0 ? 0 : 1;
}

/* Caduceus browser: the account and game pages come from the same bridge that tracks games,
   on its account port, and WAIT retries stay pinned to it. */
int Ranet::caduceusPage(const char *request, unsigned int serial, char *out, int size)
{
    struct sockaddr_in to;
    char myaddr[32], expected[32];
    unsigned char ip[4];

    if (gRAMode != RA_MODE_CADUCEUS || !out || size < 1)
        return -1;
    out[0] = '\0';
    int sock = openPcLink(myaddr, sizeof(myaddr), ip);
    if (sock < 0)
        return -1;
    int session = caduceusSession(sock, &to, RA_PROBE_HASH, rx_, sizeof(rx_));
    if (session != 0) {
        closePcLink(sock);
        if (session != -9)
            return -2;
        snprintf(out, size, "OFFLINE");
        return 0;
    }
    to.sin_port = htons(RA_CADUCEUS_ACCOUNT_PORT);
    snprintf(expected, sizeof(expected), "CADB1 %u ", serial);
    const int skip = (int)strlen(expected);
    for (int attempt = 0; attempt < 40; attempt++) {
        if (ask(sock, &to, request, rx_, sizeof(rx_)) <= 0)
            break;
        if (strncmp(rx_, expected, skip))
            continue; // an answer to an earlier page
        if (!strcmp(rx_ + skip, "WAIT")) {
            DelayThread(250000);
            continue;
        }
        snprintf(out, size, "%s", rx_ + skip);
        closePcLink(sock);
        return 0;
    }
    closePcLink(sock);
    return -2;
}

/* The PC that answers xeRAbora requests: the explicit RA companion IP, the verified peer,
   else whoever answers a RAP1 broadcast with "RAO1 OK". Network byte order; 0 = none. */
unsigned int Ranet::xeraboraHost(int sock, const char *myaddr)
{
    struct sockaddr_in to;
    char req[48];
    unsigned int ip = peerIP();

    if (hostIsExplicit())
        return configuredTarget(&to, RA_PORT) ? to.sin_addr.s_addr : 0;
    if (ip != 0)
        return ip;
    broadcastTarget(&to);
    snprintf(req, sizeof(req), "RAP1%s", myaddr);
    if (ask(sock, &to, req, rx_, 193) < 8 || strncmp(rx_, "RAO1 OK ", 8) != 0)
        return 0;
    rememberPeer(replyIP_);
    return replyIP_;
}

/* The list itself travels over TCP from the xeRAbora PC's HTTP interface, so this needs the
   menu's own stack: never over mstack:. */
int Ranet::xeraboraIdentify(const char *hash, const char *serial, unsigned int *host, char *title, int titlesz, int *count)
{
    struct sockaddr_in to;
    char req[96], myaddr[32];
    unsigned char ip[4];

    *host = 0;
    title[0] = '\0';
    *count = 0;
    if (gRAMode != RA_MODE_XERABORA || nicReserved())
        return -8;
    int sock = openPcSocket(myaddr, sizeof(myaddr), ip);
    if (sock < 0)
        return -1;
    if (myaddr[0] == '\0' || (*host = xeraboraHost(sock, myaddr)) == 0) {
        disconnect(sock);
        return -2;
    }
    if (hash == NULL) { // host only: the account library needs no image
        disconnect(sock);
        return 0;
    }
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(RA_PORT);
    to.sin_addr.s_addr = *host;
    snprintf(req, sizeof(req), "RAQ1 %s %s%s", hash, serial, myaddr);
    int result = raqAsk(sock, &to, req);
    disconnect(sock);
    if (result != 0)
        return result;
    int bytes = 0, chunks = 0, unlocked, unsupported, pos = 0;
    // The two reply shapes askPc accepts: five numbers (newer clients) or three, then the title.
    if (sscanf(rx_ + 8, "%d %d %d %d %d %n", &bytes, &chunks, count, &unlocked, &unsupported, &pos) != 5)
        sscanf(rx_ + 8, "%d %d %d %n", &bytes, &chunks, count, &pos); // %n was not reached: pos is still 0
    char *name = rx_ + 8 + pos;
    int len = (int)strlen(name);
    while (len > 0 && name[len - 1] == ' ')
        name[--len] = '\0';
    snprintf(title, titlesz, "%s", name);
    return 0;
}

/* Reads use lwIP's per-call MSG_DONTWAIT into the aligned rx_ (the rules above), and the
   whole exchange is bounded so a stalled PC cannot hold the I/O thread. clock() is a 32-bit
   microsecond counter: time the elapsed span, never an absolute deadline that breaks at the wrap. */
/* A read longer than 64 bytes returns its unaligned tail through ps2ips' side buffer, which holds
   48 bytes (RESTS_EBUF_MAX, modules/network/ps2ips/ps2ips.c); a longer tail is cut, after lwIP has
   already consumed it. The padded UDP replies never have a tail, but a TCP stream would lose bytes.
   So peek at what is waiting and shorten the read until its tail fits those 48 bytes; the rest comes
   on the next call. A read over 64 bytes still lands mostly by direct DMA, and 64 bytes or less
   travels whole in the head buffer. */
int Ranet::tcpRecv(int sock, void *aligned, int max)
{
    int got = recv(sock, aligned, max, RA_MSG_DONTWAIT | RA_MSG_PEEK);
    if (got <= 0)
        return got;
    if (got > 64 && got % 64 > 48)
        got -= got % 64 - 48;
    return recv(sock, aligned, got, RA_MSG_DONTWAIT);
}

int Ranet::httpGet(unsigned int host, int port, const char *path, char *buf, int max, int *length)
{
    constexpr int RA_HTTP_MS = 8000;
    struct sockaddr_in to;
    char req[160];
    int len = 0, closed = 0;

    *length = 0;
    if (host == 0 || max < 64 || nicReserved())
        return -1;
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return -1;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = host;
    if (connect(sock, (struct sockaddr *)&to, sizeof(to)) < 0) {
        disconnect(sock);
        return -2;
    }
    snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: %u.%u.%u.%u\r\nConnection: close\r\n\r\n", path,
             host & 0xff, (host >> 8) & 0xff, (host >> 16) & 0xff, (host >> 24) & 0xff);
    if (send(sock, req, strlen(req), 0) != (int)strlen(req)) {
        disconnect(sock);
        return -3;
    }
    const clock_t start = clock();
    while (len < max - 1 && (clock_t)(clock() - start) < (clock_t)RA_HTTP_MS * (CLOCKS_PER_SEC / 1000)) {
        int room = max - 1 - len;
        int got = tcpRecv(sock, rx_, room < RA_RECV_MAX ? room : RA_RECV_MAX);
        if (got == 0) {
            closed = 1; // the PC closed: complete
            break;
        }
        if (got < 0) {
            DelayThread(RA_POLL_MS * 1000);
            continue;
        }
        memcpy(buf + len, rx_, got);
        len += got;
    }
    disconnect(sock);
    buf[len] = '\0';
    if (len >= max - 1 || !closed)
        return -4; // larger than the buffer, or cut off: never parse a partial document
    int status;
    char *body = strstr(buf, "\r\n\r\n");
    if (sscanf(buf, "HTTP/%*d.%*d %d", &status) != 1 || status != 200 || body == NULL)
        return -5;
    *length = len - (int)(body + 4 - buf);
    return (int)(body + 4 - buf);
}

#endif /* RETROACHIEVEMENTS */
