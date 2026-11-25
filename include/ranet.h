/* RetroAchievements: talking to the PC client from the OPL menu. See src/sources/ranet.cpp */
#ifndef __RANET_H__
#define __RANET_H__

struct sockaddr_in;
#include "modules/network/udpfs_ministack/include/mstack_xchg.h" /* the mstack: link */

// Ceiling for the watch list one ask can bring in (src/sources/ranet.cpp).
constexpr int RA_MAX_BYTES = (24 * 1024); // 4,096 direct entries, 512 PS2 nodes, headers and hash

// The menu-side PC client exchange (src/sources/ranet.cpp): discovery, the
// hash -> watch-list ask, the telemetry network bring-up and the link test.
// Owns the aligned receive buffer and the incoming watch-list buffer. One
// instance; the free functions below are thin forwarders.
class Ranet
{
public:
    /* Hashes are known; ask the PC for the watch list and store it next to
       the game. Returns 0 when the list was received, 1 when RetroAchievements
       does not know the image, -7 when the PC was still identifying it after
       the waiting rounds, -8 when the NIC is owned by a UDP transport and the
       ask was refused without touching the network, -9 when the selected
       Caduceus bridge answered but its RA session is offline, another negative
       value when the PC did not answer or the transfer broke.
       info (may be NULL) receives the game title, info2 (may be NULL) the
       achievement counts as the PC reported them, e.g. info "Need for Speed:
       Underground 2" and info2 "76 achievements: 3 unlocked, 10 unsupported".
       Older PC clients report only the total, so info2 is then just
       "76 achievements". */
    int askPc(const char *hash, const char *serial, const char *savepath,
              char *info, int infosz, char *info2, int info2sz);

    /* Brings the menu network up for a telemetry launch. */
    void launchNetworkUp();

    /* Broadcasts a discovery request and reports the outcome as two lines
       of text for the notice popup. Returns 1 when a PC client answered. */
    int netTestLink(char *line1, int sz1, char *line2, int sz2);

    /* 1 when a UDP transport (UDPBD, or the UDPFS filesystem chain) owns the
       SMAP NIC. RA must not raise the SMB stack on top of it; the launch drops
       its telemetry instead. */
    static int nicBusy();
    // Selected UDP transports reserve the NIC before their modules load. Menu actions only.
    static int nicReserved();
    // A UDPFS transport is selected but its chain (and with it mstack:) is not loaded yet.
    static int udpfsNotLoaded();

    /* The peer a completed watch-list exchange or link test verified, for the
       RA mode, SMB host and companion IP it was verified under; else 0. */
    unsigned int peerIP() const;
    void forgetPeer();

    /* Caduceus browser: after the session check, one CADA1 request to the bridge's account
       port. 0 with out filled ("OFFLINE" when the session is signed out), else negative. */
    int caduceusPage(const char *request, unsigned int serial, char *out, int size);
    /* Xerabora browser: the xeRAbora PC in *host (network order). With a hash, also RAQ1's
       title and achievement count: 0 known, 1 unknown image, -7 still identifying, -8 NIC
       reserved, another negative when no PC answered. */
    int xeraboraIdentify(const char *hash, const char *serial, unsigned int *host, char *title, int titlesz, int *count);
    /* HTTP/1.0 GET of path from host:port (network order): the body offset in buf (its size
       in *length), or negative. 8 s for the whole exchange. */
    int httpGet(unsigned int host, int port, const char *path, char *buf, int max, int *length);
    /* One non-blocking TCP read of at most max (<= 992) bytes into a 64-byte-aligned buffer:
       the count, 0 when the peer closed, negative when nothing is waiting. */
    static int tcpRecv(int sock, void *aligned, int max);
    /* A launch is waiting on the next first contact: until a reply arrives, an ask that would
       wait RA_TRY rounds (3 s) waits RA_TRY_LAUNCH (1.5 s). A PC on the LAN answers in well under
       that, so only an absent PC client costs less. Off again with 0. */
    void quickFirstContact(int on) { quickTries_ = on ? RA_TRY_LAUNCH : 0; }

private:
    static constexpr int RA_TRY = 12;
    static constexpr int RA_TRY_LAUNCH = 6;
    static constexpr int RA_TRY_CHUNK = 60; // xeRAbora allows a longer wait once transfer begins
    static int hostIsExplicit();
    void rememberPeer(unsigned int ip);
    /* Waits for a reply, resending the request. Returns the length or -1. */
    int ask(int sock, struct sockaddr_in *to, const char *req, char *out, int outmax, int attempts = RA_TRY);
    int askMstack(struct sockaddr_in *to, const char *req, char *out, int asklen, int attempts);
    /* Opens the UDP socket: network up if needed, broadcast allowed,
       non-blocking, bound to the fixed own port. Fills myaddr with
       " <ip> <port>" (empty when the console has no address yet). */
    int openPcSocket(char *myaddr, int sz, unsigned char ip[4]);
    // A socket, or the mstack: device while UDPFS owns the NIC; -8 when neither can be used.
    int openPcLink(char *myaddr, int sz, unsigned char ip[4]);
    static void closePcLink(int sock);
    static void broadcastTarget(struct sockaddr_in *to);
    static int configuredTarget(struct sockaddr_in *to, int port);
    int caduceusSession(int sock, struct sockaddr_in *to, const char *hash, char *rx, int rxsize);
    int pcTarget(int sock, struct sockaddr_in *to, const char *hash, char *rx, int rxsize);
    int raqAsk(int sock, struct sockaddr_in *to, const char *req);
    unsigned int xeraboraHost(int sock, const char *myaddr);

    /* Alignment is mandatory: DMA delivery rule 1 in ranet.cpp. */
    char rx_[2048] __attribute__((aligned(64)));
    unsigned char wl_[RA_MAX_BYTES];
    mstack_xchg_req_t xreq_ __attribute__((aligned(64)));
    mstack_xchg_reply_t xreply_ __attribute__((aligned(64)));
    unsigned int replyIP_ = 0; // last responder, network order; not yet verified
    int quickTries_ = 0;       // quickFirstContact: the attempts for RA_TRY asks until a reply
    unsigned int verifiedIP_ = 0;
    int verifiedMode_ = -1;
    int verifiedSMBHost_[4] = {};
    int verifiedCompanionHost_[4] = {};
};

extern Ranet gRanet;

/* The free API other translation units call; all forward to gRanet. */
int raAskPC(const char *hash, const char *serial, const char *savepath,
            char *info, int infosz, char *info2, int info2sz);
void raLaunchNetworkUp(void);
int raNetTestLink(char *line1, int sz1, char *line2, int sz2);
void raNetQuickFirstContact(int on);
int raNetNicBusy(void);
int raNetNicReserved(void);
int raNetUdpfsNotLoaded(void);
unsigned int raNetPeerIP(void);
void raNetForgetPeer(void);
int raCaduceusPage(const char *request, unsigned int serial, char *out, int size);
int raNetXeraboraIdentify(const char *hash, const char *serial, unsigned int *host, char *title, int titlesz, int *count);
int raNetHttpGet(unsigned int host, int port, const char *path, char *buf, int max, int *length);
int raNetTcpRecv(int sock, void *aligned, int max);

#endif
