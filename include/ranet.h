/* RetroAchievements: talking to the PC client from the OPL menu. See src/sources/ranet.cpp */
#ifndef __RANET_H__
#define __RANET_H__

struct sockaddr_in;

// Ceiling for the watch list one ask can bring in (src/sources/ranet.cpp).
constexpr int RA_MAX_BYTES = (16 * 1024);

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
       ask was refused without touching the network, another negative value when
       the PC did not answer or the transfer broke.
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

private:
    /* 1 when a UDP transport (UDPBD, or the UDPFS filesystem chain) owns the
       SMAP NIC. RA must not raise the SMB stack on top of it; the menu actions
       refuse with a notice instead. */
    static int nicBusy();
    /* Waits for a reply, resending the request. Returns the length or -1. */
    int ask(int sock, struct sockaddr_in *to, const char *req, char *out, int outmax);
    /* Opens the UDP socket: network up if needed, broadcast allowed,
       non-blocking, bound to the fixed own port. Fills myaddr with
       " <ip> <port>" (empty when the console has no address yet). */
    int openPcSocket(char *myaddr, int sz, unsigned char ip[4]);
    static void broadcastTarget(struct sockaddr_in *to);

    /* Alignment is mandatory: DMA delivery rule 1 in ranet.cpp. */
    char rx_[2048] __attribute__((aligned(64)));
    unsigned char wl_[RA_MAX_BYTES];
};

extern Ranet gRanet;

/* The free API other translation units call; all forward to gRanet. */
int raAskPC(const char *hash, const char *serial, const char *savepath,
            char *info, int infosz, char *info2, int info2sz);
void raLaunchNetworkUp(void);
int raNetTestLink(char *line1, int sz1, char *line2, int sz2);

#endif
