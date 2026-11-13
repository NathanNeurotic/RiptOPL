/*
  General & System -> Date and Time -> Set from Internet (Launchstation issue 346 request).

  Reads the time from a web server's HTTP Date header over plain HTTP on port 80. Not NTP: an NTP
  reply is 48 bytes, and ps2ips delivers a packet of 64 bytes or less through its small-packet path,
  which returned zeros on real hardware (ranet.cpp, rule 2). Not HTTPS: the TLS check needs a clock
  that is already right (ratls_ps2.cpp, RATLS_MIN_YEAR). A Date header is good to the second, which
  is all the console clock keeps.

  Runs only when asked, never at boot. Every wait is bounded: link and DHCP 30 s each (netsupport),
  then about 8 s per server.
*/

#include "include/nettime.h"
#include "include/opl.h"          /* gNetworkProtocol */
#include "include/ethsupport.h"   /* ethLoadInitModules, ethGetNetConfig */
#include "include/netsupport.h"   /* netGetModulesLoaded, netApplyConfig */
#include "include/bdmsupport.h"   /* bdmIsUDPBDLoaded */
#include "include/udpfssupport.h" /* udpfsGetModulesLoaded */
#include <ps2ips.h>
#include <netinet/in.h> /* sockaddr_in */
#include <lwip/def.h>   /* htons */
#include <netdb.h>
#include <kernel.h>
#include <delaythread.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <time.h>

/* <netdb.h> only declares struct hostent; this is lwIP's layout, as in ratls_ps2.cpp. */
struct hostent
{
    char *h_name;
    char **h_aliases;
    int h_addrtype;
    int h_length;
    char **h_addr_list;
};

constexpr int NETTIME_DEADLINE_MS = 8000; /* DNS, connect and reply, per server */
constexpr int NETTIME_POLL_MS = 20;

int netTimeParseDate(const char *headers, int t[6])
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *p;
    char mon[4];
    int sec = 0, end = 0;

    /* The header name is case-insensitive; it starts a line. */
    for (p = headers; (p = strchr(p, '\n')) != NULL; p++)
        if (strncasecmp(p + 1, "date:", 5) == 0)
            break;
    if (p == NULL)
        return 0;
    /* RFC 9110 IMF-fixdate: "Fri, 13 Nov 2026 03:37:42 GMT" */
    /* A whole field only: two seconds digits, then GMT, then the end of the line. */
    const char *q = p + 6;
    if (sscanf(q, " %*[^,], %d %3s %d %d:%d:%2d%n GMT%n", &t[2], mon, &t[0], &t[3], &t[4], &t[5], &sec, &end) != 6 || end == 0 ||
        q[sec - 3] != ':' || (q[end] != '\r' && q[end] != '\n'))
        return 0;
    const char *m = strstr(months, mon);
    if (strlen(mon) != 3 || m == NULL || (m - months) % 3)
        return 0;
    t[1] = (int)(m - months) / 3 + 1;
    static const char mdays[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int days = (t[1] == 2 && t[0] % 4) ? 28 : mdays[t[1] - 1]; /* 2000-2099: every fourth year is a leap year */
    return t[0] >= 2000 && t[0] <= 2099 && t[2] >= 1 && t[2] <= days && t[3] >= 0 && t[3] < 24 && t[4] >= 0 &&
           t[4] < 60 && t[5] >= 0 && t[5] < 61;
}

/* clock() is a 32-bit microsecond counter: compare elapsed time, which survives the wrap. */
static int netTimeExpired(clock_t start)
{
    return (clock_t)(clock() - start) >= (clock_t)NETTIME_DEADLINE_MS * (CLOCKS_PER_SEC / 1000);
}

/* One HEAD request. The header block is read through a 64-byte-aligned buffer, peeking first and
   taking a length ps2ips copies whole, as ratls_ps2.cpp does. */
static int netTimeAsk(const char *host, int t[6])
{
    static char rx[960] __attribute__((aligned(64)));
    char headers[1024], req[128];
    struct sockaddr_in to;
    clock_t start = clock();
    int sock, have = 0, ok = 0;

    struct hostent *he = gethostbyname(host);
    if (he == NULL || he->h_addr_list == NULL || he->h_addr_list[0] == NULL)
        return 0;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(80);
    memcpy(&to.sin_addr, he->h_addr_list[0], sizeof(to.sin_addr));

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return 0;
    if (connect(sock, reinterpret_cast<struct sockaddr *>(&to), sizeof(to)) < 0) {
        disconnect(sock);
        return 0;
    }
    int len = snprintf(req, sizeof(req), "HEAD / HTTP/1.1\r\nHost: %s\r\nUser-Agent: RiptOPL\r\nConnection: close\r\n\r\n", host);
    if (send(sock, req, len, 0) != len) {
        disconnect(sock);
        return 0;
    }
    headers[0] = '\0';
    while (!netTimeExpired(start) && have < (int)sizeof(headers) - 1) {
        int want = (int)sizeof(headers) - 1 - have;
        int got = recv(sock, rx, want < (int)sizeof(rx) ? want : (int)sizeof(rx), 0x08 | 0x01); /* MSG_DONTWAIT | MSG_PEEK, lwIP's values */
        if (got == 0)
            break; /* closed */
        if (got < 0) {
            DelayThread(NETTIME_POLL_MS * 1000);
            continue;
        }
        if (got > 64 && got % 64 > 48)
            got -= got % 64 - 48;
        if ((got = recv(sock, rx, got, 0x08)) <= 0)
            continue;
        memcpy(headers + have, rx, got);
        have += got;
        headers[have] = '\0';
        if (strstr(headers, "\r\n\r\n") != NULL)
            break;
    }
    ok = netTimeParseDate(headers, t);
    disconnect(sock);
    return ok;
}

int netTimeFetch(int t[6])
{
    static const char *const hosts[] = {"github.com", "www.google.com"};
    u8 ip[4], mask[4], gw[4];
    int i;

    if (bdmIsUDPBDLoaded() || udpfsGetModulesLoaded() || gNetworkProtocol == NET_PROTO_UDPFS ||
        gNetworkProtocol == NET_PROTO_UDPFSBD || gNetworkProtocol == NET_PROTO_UDPBD)
        return NETTIME_ERR_NETBUSY;
    /* Network modules are loaded on demand: bring them up once, with the saved settings. */
    if (!netGetModulesLoaded()) {
        if (ethLoadInitModules() != 0)
            return NETTIME_ERR_NETWORK;
    } else if (ethGetNetConfig(ip, mask, gw) < 0 || (ip[0] | ip[1] | ip[2] | ip[3]) == 0) {
        /* Loaded earlier with no cable: the saved settings were never applied, so apply them now. */
        if (netApplyConfig() != 0)
            return NETTIME_ERR_NETWORK;
    }
    for (i = 0; i < 40; i++) { /* a DHCP lease can take a few seconds */
        if (ethGetNetConfig(ip, mask, gw) >= 0 && (ip[0] | ip[1] | ip[2] | ip[3]) != 0)
            break;
        DelayThread(250 * 1000);
    }
    if (i == 40)
        return NETTIME_ERR_NETWORK;
    for (i = 0; i < (int)(sizeof(hosts) / sizeof(hosts[0])); i++)
        if (netTimeAsk(hosts[i], t))
            return NETTIME_OK;
    return NETTIME_ERR_SERVER;
}
