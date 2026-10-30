"""Xerabora-mode achievement browser (ORBIT parity) against the production code.

src/sources/achievements.cpp is compiled with stubbed network calls. The documents mirror
xeRAbora's own JSON builders (client/src/webui.c build_library / build_game):
nested "base"/"subsets" members, an "achievements" member that is a number
inside subsets, escaped quotes/backslashes, \\u escapes and raw UTF-8.
Ranet::httpGet and Ranet::xeraboraIdentify are compiled separately against stubbed sockets, and
the HTTPS game-ID lookup (ORBIT's path to a never-played game's list) against a stubbed raHttpsGet.
"""
import json
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]


def c_str(text):
    out = []
    for b in text.encode("utf-8"):
        ch = chr(b)
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif 32 <= b < 127:
            out.append(ch)
        else:
            out.append("\\%03o" % b)
    return '"' + "".join(out) + '"'


library = json.dumps({"games": [
    {"id": 19040, "title": "Need for Speed: Underground 2", "console": "PlayStation 2", "icon": "/i.png",
     "award": "", "played": "2026-10-01 10:00:00", "total": 76, "awarded": 3, "hardcore": 1, "mastered": False},
    {"id": 11, "title": "Café \"Quoted\"", "console": "PlayStation", "icon": "", "award": "",
     "played": "", "total": 10, "awarded": 0, "hardcore": 0, "mastered": False},
]}, ensure_ascii=True)  # é and \" escapes, as xeRAbora's json_field emits
achievements = [
    {"id": i, "title": t, "description": d, "badge": "123", "type": "", "earned": e,
     "points": 5, "order": i, "players": 10, "median": 0}
    for i, t, d, e in [
        (1, 'First "Win"', "Win a race \\ any track", "2026-10-01 12:34:56"),
        (2, "Niño", "Raw UTF-8 日本", ""),
        (3, "Third", "Locked", ""),
        (4, "Fourth", "Earned", "2026-10-02 08:00:00"),
        (5, "Fifth", "Locked", ""),
    ]]
game = json.dumps({"id": 19040, "title": "Need for Speed: Underground 2", "console": "PlayStation 2",
                   "icon": "/x.png", "total": 5, "awarded": 2,
                   "base": {"id": 19040, "title": "Need for Speed: Underground 2"},
                   "subsets": [{"id": 5, "title": "Sub", "achievements": 3, "points": 10}],
                   "achievements": achievements}, ensure_ascii=False)

source = (root / "src/sources/achievements.cpp").read_text(encoding="utf-8")
source = re.sub(r"^#include.*\n", "", source, flags=re.M)
prefix = r"""
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include "include/achievements.h"
#include "include/ratls.h"
#include "src/sources/rahunt.cpp"
#include <sys/stat.h>
extern "C" int unlink(const char *);
extern "C" int rmdir(const char *);
#define RETROACHIEVEMENTS 1
#define LOG(...) ((void)0)
#define IO_OK 0
#define IO_CUSTOM_SIMPLEACTION 1
#define O_RDONLY 0
#define RA_MODE_XERABORA 0
#define RA_MODE_CADUCEUS 1
static int gRAMode = RA_MODE_XERABORA;
static void (*worker)(void);
static int idRc, idCount = 40, httpFail, libraryGets, gameGets, hashed;
static char idTitle[64] = "Need for Speed: Underground 2";
static const char *libraryDoc = @LIBRARY@, *gameDoc = @GAME@;
static const char *nextDoc;
static int sbHashGameBusy(void) { return 0; }
static int discCheckBusy(void) { return 0; }
static int raNetNicReserved(void) { return 0; }
static int ethEnsureSMBShareConnected(void) { assert(!"Xerabora mode never needs the SMB share"); return 0; }
enum { NET_PROTO_OFF, NET_PROTO_SMB, NET_PROTO_UDPFS };
static int gNetworkProtocol = NET_PROTO_OFF;
static int udpfsGetModulesLoaded(void) { assert(!"Xerabora mode never needs the UDPFS share"); return 0; }
static const char *ethGetSMBPrefix(void) { return ""; }
static int open(const char *p, int f) { (void)p; (void)f; assert(!"no CADUCEUS.KEY in Xerabora mode"); return -1; }
static int read(int fd, void *o, int n) { (void)fd; (void)o; (void)n; return -1; }
static int close(int fd) { (void)fd; return 0; }
static int ioPutRequest(int type, void *fn) { (void)type; worker = reinterpret_cast<void (*)(void)>(fn); return 0; }
static const char *hashOverride;
static int vcdHashed;
static int raHashVcd(const char *p, char *boot, int n, char *out)
{
    (void)p; vcdHashed++; snprintf(boot, n, "SLUS_012.15");
    memset(out, 'c', 32); out[32] = 0; return 0;
}
static int raHashIsoDirect(const char *p, const char *s, char *out)
{
    (void)p; (void)s; hashed++;
    if (hashOverride) { strcpy(out, hashOverride); return 0; }
    memset(out, 'b', 32); out[32] = 0; return 0;
}
/* retroachievements.org over HTTPS: fails (no internet) unless a test scripts a document. */
static int httpsRc = RATLS_ERR_NET, httpsCalls;
static const char *httpsDoc;
static char httpsPath[128];
int raHttpsGet(const char *host, const char *path, char *buf, int max, int *length)
{
    static const char head[] = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n";
    int h = (int)strlen(head), n;
    httpsCalls++;
    assert(!strcmp(host, "retroachievements.org"));
    snprintf(httpsPath, sizeof(httpsPath), "%s", path);
    if (httpsRc < 0)
        return httpsRc;
    n = (int)strlen(httpsDoc);
    assert(h + n < max);
    memcpy(buf, head, h);
    memcpy(buf + h, httpsDoc, n + 1);
    *length = h + n;
    return h;
}
static int raCaduceusPage(const char *r, unsigned s, char *o, int n) { (void)r; (void)s; (void)o; (void)n; assert(!"no bridge in Xerabora mode"); return -1; }
static char identifiedSerial[16];
static int raNetXeraboraIdentify(const char *hash, const char *serial, unsigned int *host, char *title, int tsz, int *count)
{
    if (hash != NULL)
        snprintf(identifiedSerial, sizeof(identifiedSerial), "%s", serial ? serial : "");
    *host = 0x0201a8c0;
    /* "RAA1 NO" (1) and "RAA1 WAIT" (-7) answer a hash query; the host-only query (hash NULL)
       never gets them. */
    if (idRc != 0 && (hash != NULL || (idRc != 1 && idRc != -7)))
        return idRc;
    snprintf(title, tsz, "%s", hash ? idTitle : "");
    *count = hash ? idCount : 0;
    return 0;
}
static int raNetHttpGet(unsigned int host, int port, const char *path, char *buf, int max, int *length)
{
    assert(host == 0x0201a8c0 && port == 18280);
    if (httpFail)
        return -2;
    if (!strcmp(path, "/library")) { libraryGets++; nextDoc = libraryDoc; }
    else { assert(!strcmp(path, "/game?id=19040")); gameGets++; nextDoc = gameDoc; }
    *length = (int)strlen(nextDoc);
    assert(*length < max);
    memcpy(buf, nextDoc, *length);
    return 0;
}
"""
tests = r"""
static achievement_page_t run(char kind, int page, int filter, const char *target)
{
    achievement_page_t p;
    assert(achievementsRequest(kind, page, filter, target));
    worker();
    achievementsSnapshot(&p);
    return p;
}
int main(void)
{
    achievement_page_t p;
    /* Account library: games with progress, escapes and \u decoding. */
    p = run('G', 0, 0, "0");
    assert(p.state == ACH_READY && p.kind == 'G' && p.total == 2 && p.count == 2);
    assert(p.entries[0].id == 19040 && p.entries[0].total == 76 && p.entries[0].earned == 3 && p.entries[0].hardcore == 1);
    assert(!strcmp(p.entries[1].title, "Caf\303\251 \"Quoted\""));
    /* One game's set: nested base/subsets are skipped, earned dates kept. */
    p = run('A', 0, 0, "19040");
    assert(p.state == ACH_READY && p.kind == 'A' && p.gameId == 19040 && p.total == 5 && p.count == 3);
    assert(p.earned == 2 && p.maximum == 5 && !strcmp(p.title, "Need for Speed: Underground 2"));
    assert(!strcmp(p.entries[0].title, "First \"Win\"") && !strcmp(p.entries[0].description, "Win a race \\ any track"));
    assert(p.entries[0].earned && !strcmp(p.entries[0].date, "2026-10-01 12:34:56") && p.entries[0].points == 5);
    assert(!strcmp(p.entries[1].title, "Ni\303\261o") && !p.entries[1].earned && !p.entries[1].date[0]);
    assert(!strcmp(p.entries[1].description, "Raw UTF-8 \346\227\245\346\234\254"));
    /* Paging and filters come from the cache: no further fetch. */
    p = run('A', 1, 0, "19040");
    assert(p.total == 5 && p.count == 2 && p.entries[0].id == 4);
    p = run('A', 0, 1, "19040"); assert(p.total == 2 && p.entries[1].id == 4);
    p = run('A', 0, 2, "19040"); assert(p.total == 3 && p.entries[0].id == 2);
    p = run('A', 0, 3, "19040"); assert(p.total == 0 && p.count == 0);
    assert(libraryGets == 1 && gameGets == 1);
    /* Select marks the cache stale: the next request refetches. */
    achievementsForget();
    p = run('A', 0, 0, "19040"); assert(p.state == ACH_READY && gameGets == 2);
    /* From a game card: RAQ1 title -> library ID -> the set. */
    /* With the game's device, the set is also saved as RA/<startup>.ach for the in-game menu's LEFT TO HUNT. */
    char dir[] = "/tmp/xera-achXXXXXX", prefixPath[64], achPath[96], text[4096];
    assert(mkdtemp(dir));
    snprintf(prefixPath, sizeof(prefixPath), "%s/", dir);
    assert(achievementsRequestImage("mass0:DVD/NFSU2.iso", "SLUS_210.65", prefixPath)); worker(); achievementsSnapshot(&p);
    assert(hashed == 1 && p.state == ACH_READY && p.gameId == 19040 && p.total == 5);
    {
        snprintf(achPath, sizeof(achPath), "%sRA/SLUS_210.65.ach", prefixPath);
        FILE *f = fopen(achPath, "rb");
        assert(f);
        int n = (int)fread(text, 1, sizeof(text) - 1, f), unlocked = 0;
        fclose(f);
        text[n] = 0;
        static ra_hunt_t hunt[8];
        assert(!strncmp(text, "RIPPS2 ACH 1\n", 13) && raHuntRead(text, n, hunt, 8) == 5);
        for (int i = 0; i < 5; i++)
            unlocked += hunt[i].unlocked;
        assert(unlocked == p.earned && hunt[0].points > 0 && hunt[0].title[0]);
        unlink(achPath);
        snprintf(achPath, sizeof(achPath), "%sRA", prefixPath);
        rmdir(achPath);
        rmdir(dir);
    }
    /* A PS1 VCD: hashed by its boot executable, whose name the PC matches by title. */
    assert(achievementsRequestVcd("mass0:/POPS/Game.VCD")); worker(); achievementsSnapshot(&p);
    assert(vcdHashed == 1 && hashed == 1 && !strcmp(identifiedSerial, "SLUS_012.15") && p.state == ACH_READY);
    /* Known to RetroAchievements but never played: say so, with title and count. */
    strcpy(idTitle, "Never Played Game");
    assert(achievementsRequestImage("mass0:DVD/X.iso", "SLUS_000.01", NULL)); worker(); achievementsSnapshot(&p);
    assert(p.state == ACH_NOT_PLAYED && !strcmp(p.title, "Never Played Game") && p.maximum == 40);
    /* Unknown image, unreachable PC, unreadable document. */
    idRc = 1; assert(achievementsRequestImage("mass0:DVD/X.iso", "SLUS_000.01", NULL)); worker(); achievementsSnapshot(&p);
    assert(p.state == ACH_UNSUPPORTED);
    idRc = -2; p = run('G', 0, 0, "0"); assert(p.state == ACH_OFFLINE);
    /* The PC answers but is still identifying the image: a retry, not "turn on the network". */
    idRc = -7; assert(achievementsRequestImage("mass0:DVD/X.iso", "SLUS_000.01", NULL)); worker(); achievementsSnapshot(&p);
    assert(p.state == ACH_ERROR); idRc = 0;
    achievementsForget(); httpFail = 1; p = run('G', 0, 0, "0"); assert(p.state == ACH_OFFLINE); httpFail = 0;
    /* gameid_parse: retroachievements.org's {"Success":true,"GameID":N}. */
    assert(achievementsParseGameId("{\"Success\":true,\"GameID\":2791}", (int)strlen("{\"Success\":true,\"GameID\":2791}")) == 2791);
    assert(achievementsParseGameId("{\"Success\":true,\"GameID\":0}", (int)strlen("{\"Success\":true,\"GameID\":0}")) == 0);
    assert(achievementsParseGameId("{\"Success\":false}", (int)strlen("{\"Success\":false}")) == -1);
    assert(achievementsParseGameId("{\"GameID\":5}", (int)strlen("{\"GameID\":5}")) == -1);
    assert(achievementsParseGameId("{\"Success\":true,\"GameID\":\"x\"}", (int)strlen("{\"Success\":true,\"GameID\":\"x\"}")) == -1);
    assert(achievementsParseGameId("{\"Success\":tr", (int)strlen("{\"Success\":tr")) == -1);
    assert(achievementsParseGameId("{\"Success\":true,\"GameID\":-3}", (int)strlen("{\"Success\":true,\"GameID\":-3}")) == -1);
    assert(achievementsParseGameId("{\"Success\":false,\"GameID\":5}", (int)strlen("{\"Success\":false,\"GameID\":5}")) == -1);
    assert(achievementsParseGameId("{\"Success\":true,\"GameID\":+5}", (int)strlen("{\"Success\":true,\"GameID\":+5}")) == -1);
    assert(achievementsParseGameId("{\"Success\":true,\"GameID\":100000001}", (int)strlen("{\"Success\":true,\"GameID\":100000001}")) == -1);
    /* never_played_reaches_game: the ORBIT path, no library title match needed. */
    strcpy(idTitle, "Never Played Game");
    httpsRc = 0; httpsDoc = "{\"Success\":true,\"GameID\":19040}";
    achievementsForget();
    {
        int lib = libraryGets, games = gameGets;
        assert(achievementsRequestImage("mass0:DVD/X.iso", "SLUS_000.01", NULL)); worker(); achievementsSnapshot(&p);
        assert(p.state == ACH_READY && p.gameId == 19040 && gameGets == games + 1 && libraryGets == lib);
        assert(!strcmp(httpsPath, "/dorequest.php?r=gameid&m=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
    }
    /* gameid_zero_unsupported */
    httpsDoc = "{\"Success\":true,\"GameID\":0}";
    {
        int games = gameGets;
        assert(achievementsRequestImage("mass0:DVD/X.iso", "SLUS_000.01", NULL)); worker(); achievementsSnapshot(&p);
        assert(p.state == ACH_UNSUPPORTED && gameGets == games);
    }
    /* https_failure_falls_back: the #926 title match, unchanged. */
    httpsRc = RATLS_ERR_NET;
    assert(achievementsRequestImage("mass0:DVD/X.iso", "SLUS_000.01", NULL)); worker(); achievementsSnapshot(&p);
    assert(p.state == ACH_NOT_PLAYED && !strcmp(p.title, "Never Played Game") && !xeraClockNote);
    /* unreachable_once_per_session: no internet costs one attempt, not one per game card ... */
    {
        int calls = httpsCalls;
        assert(achievementsRequestImage("mass0:DVD/Y.iso", "SLUS_000.02", NULL)); worker(); achievementsSnapshot(&p);
        assert(p.state == ACH_NOT_PLAYED && httpsCalls == calls);
        httpsRc = RATLS_ERR_TIMEOUT;
        achievementsForget(); /* ... until Select asks again */
        assert(achievementsRequestImage("mass0:DVD/Y.iso", "SLUS_000.02", NULL)); worker();
        assert(achievementsRequestImage("mass0:DVD/Y.iso", "SLUS_000.02", NULL)); worker();
        assert(httpsCalls == calls + 1); /* a timeout counts as no internet too */
        achievementsForget();
    }
    /* clock_unset_notes: fallback taken, and the browser says to set the clock. */
    httpsRc = RATLS_ERR_CLOCK;
    assert(achievementsRequestImage("mass0:DVD/X.iso", "SLUS_000.01", NULL)); worker(); achievementsSnapshot(&p);
    assert(p.state == ACH_NOT_PLAYED && xeraClockNote == 1 && achievementsClockUnset());
    /* gameid_rejects_bad_hash: nothing is sent for a malformed hash. */
    httpsRc = 0; httpsDoc = "{\"Success\":true,\"GameID\":19040}";
    {
        int calls = httpsCalls;
        hashOverride = "ABC";
        assert(achievementsRequestImage("mass0:DVD/X.iso", "SLUS_000.01", NULL)); worker();
        hashOverride = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbG";
        assert(achievementsRequestImage("mass0:DVD/X.iso", "SLUS_000.01", NULL)); worker();
        hashOverride = NULL;
        assert(httpsCalls == calls && !achievementsClockUnset()); /* each lookup starts with no clock note */
    }
    httpsRc = RATLS_ERR_NET;
    strcpy(idTitle, "Need for Speed: Underground 2");
    libraryDoc = "{\"games\":[{\"id\":1,\"title\":\"unterminated}]}";
    achievementsForget(); p = run('G', 0, 0, "0"); assert(p.state == ACH_ERROR);
    puts("PASS: xeRAbora library/game parsing, escapes and UTF-8, cached paging/filters, image lookup, not-played, failures and the HTTPS game-ID path");
    return 0;
}
"""
harness = prefix.replace("@LIBRARY@", c_str(library)).replace("@GAME@", c_str(game))
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path / "test.cpp").write_text(harness + source + tests, encoding="utf-8")
    subprocess.run(["c++", "-std=gnu++17", "-Wall", "-Wno-unused-function", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=undefined", "-I", str(root), str(path / "test.cpp"), "-o", str(path / "test.exe")], check=True)
    subprocess.run([str(path / "test.exe")], check=True, timeout=30)

network = (root / "src/sources/ranet.cpp").read_text(encoding="utf-8")


def method(signature):
    a = network.index(signature)
    return network[a:network.index("\n}", a) + 2] + "\n"


http_harness = r"""
#include <cassert>
#include <cstdio>
#include <cstring>
typedef unsigned int u32;
#define AF_INET 2
#define SOCK_STREAM 1
#define INADDR_BROADCAST 0xffffffffu
#define CLOCKS_PER_SEC 1000
typedef long clock_t_;
#define clock_t clock_t_
struct sockaddr { int x; };
struct sockaddr_in { int sin_family, sin_port; struct { u32 s_addr; } sin_addr; };
#include "include/ranet.h"
constexpr int RA_PORT = 18194, RA_RECV_MAX = 992, RA_MSG_DONTWAIT = 0x08, RA_MSG_PEEK = 0x01, RA_POLL_MS = 25, RA_WAIT_ROUNDS = 20, RA_WAIT_PAUSE_MS = 500;
enum { RA_MODE_XERABORA, RA_MODE_CADUCEUS };
static int gRAMode, pc_ip[4], gRAHostIp[4];
static int nic, sent, closes, scriptPos, scriptOff, ticks, asks, sockets, peeks, lost;
static const char *script[8];
static const char *udp[8];
static u32 broadcastFrom = 0x0201a8c0;
static int htons(int p) { return p; }
static u32 htonl(u32 p) { return p; }
int Ranet::nicReserved() { return nic; }
int Ranet::openPcSocket(char *myaddr, int sz, unsigned char *) { sockets++; snprintf(myaddr, sz, " 192.168.1.9 18196"); return 5; }
void Ranet::forgetPeer() { verifiedIP_ = 0; verifiedMode_ = -1; }
int Ranet::ask(int, sockaddr_in *to, const char *req, char *out, int max) {
    const char *reply = asks < 8 ? udp[asks] : NULL;
    asks++;
    if (reply == NULL)
        return -1;
    if (!strncmp(req, "RAP1", 4))
        assert(to->sin_addr.s_addr == INADDR_BROADCAST && !strcmp(req, "RAP1 192.168.1.9 18196") && max <= 193);
    else
        assert(to->sin_addr.s_addr == 0x0201a8c0 && to->sin_port == RA_PORT && !strcmp(req, "RAQ1 abc SLUS_000.01 192.168.1.9 18196"));
    replyIP_ = broadcastFrom;
    snprintf(out, max, "%s", reply);
    return strlen(out);
}
static int socket(int, int, int) { return 5; }
static int connect(int, struct sockaddr *, int) { return 0; }
static int send(int, const char *b, int n, int) { assert(!strncmp(b, "GET /library HTTP/1.0\r\nHost: 192.168.1.2\r\n", 41)); sent++; return n; }
static int recv(int, void *buf, int n, int f)
{
    char *b = static_cast<char *>(buf);
    assert((f & ~RA_MSG_PEEK) == RA_MSG_DONTWAIT && ((size_t)b & 63) == 0 && n <= RA_RECV_MAX);
    const char *part = script[scriptPos];
    if (part == NULL)
        return -1;          /* nothing yet */
    if (!*part)
        return 0;           /* closed */
    /* Like a real recv: never more than n; the rest of a part comes next call. */
    int left = (int)strlen(part) - scriptOff, take = left < n ? left : n;
    /* ps2ips: a read over 64 bytes returns its unaligned tail through a 48-byte buffer, and the
       rest of a longer tail is consumed but never delivered (modules/network/ps2ips/ps2ips.c). */
    int got = take > 64 && take % 64 > 48 ? take - (take % 64 - 48) : take;
    memcpy(b, part + scriptOff, got);
    if (f & RA_MSG_PEEK)
        return peeks++, got;
    lost += take - got;
    scriptOff += take;
    if (scriptOff == (int)strlen(part))
        scriptPos++, scriptOff = 0;
    return got;
}
static void disconnect(int) { closes++; }
static void DelayThread(int) { ticks += 25; }
static clock_t clock(void) { return ticks; }
"""
http_tests = r"""
static Ranet net;
static int get(char *buf, int max, int *len)
{
    scriptPos = scriptOff = 0;
    return net.httpGet(0x0201a8c0, 18280, "/library", buf, max, len);
}
static int identify(const char *hash, unsigned *host, char *title, int *count)
{
    asks = 0;
    return net.xeraboraIdentify(hash, "SLUS_000.01", host, title, 64, count);
}
int main(void)
{
    static char buf[256];
    char title[64];
    unsigned host;
    int len, body, count;
    script[0] = "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n\r\n{\"games\":"; script[1] = "[]}"; script[2] = ""; script[3] = NULL;
    body = get(buf, sizeof(buf), &len);
    assert(body > 0 && len == 12 && !strcmp(buf + body, "{\"games\":[]}"));
    script[0] = "HTTP/1.0 404 Not Found\r\n\r\nno"; script[1] = ""; script[2] = NULL;
    assert(get(buf, sizeof(buf), &len) < 0);
    script[0] = "HTTP/1.0 200 OK\r\n\r\n{\"games\":["; script[1] = NULL; /* never closes */
    ticks = 0; assert(get(buf, sizeof(buf), &len) < 0 && ticks >= 8000);
    script[0] = "HTTP/1.0 200 OK\r\n\r\n0123456789012345678901234567890123456789012345678901234567890123456789"; script[1] = NULL;
    assert(get(buf, 64, &len) < 0); /* larger than the buffer */
    nic = 1; assert(get(buf, sizeof(buf), &len) < 0); nic = 0;
    /* 119 bytes waiting: one plain read would end in a 55-byte tail and lose 7 of them. */
    script[0] = "HTTP/1.0 200 OK\r\n\r\n0123456789012345678901234567890123456789012345678901234567890123456789"
                "012345678901234567890123456789"; script[1] = ""; script[2] = NULL;
    body = get(buf, sizeof(buf), &len);
    assert(body == 19 && len == 100 && peeks > 0 && !strcmp(buf + body + 90, "0123456789"));
    assert(closes == 5 && lost == 0);
    puts("PASS: Ranet::httpGet status, body offset, deadline, oversize, NIC guard and whole TCP reads");

    /* Discovery accepts only "RAO1 OK" and remembers that peer; then RAQ1 asks it, in both reply shapes. */
    gRAMode = RA_MODE_CADUCEUS; assert(identify(NULL, &host, title, &count) == -8 && !sockets); gRAMode = RA_MODE_XERABORA;
    nic = 1; assert(identify(NULL, &host, title, &count) == -8 && !sockets); nic = 0;
    udp[0] = "RAO1 NO"; udp[1] = NULL;
    assert(identify(NULL, &host, title, &count) == -2 && !host && !net.peerIP());
    udp[0] = "RAO1 OK xeRAbora"; udp[1] = NULL;
    assert(identify(NULL, &host, title, &count) == 0 && host == 0x0201a8c0 && net.peerIP() == 0x0201a8c0 && asks == 1);
    udp[0] = "RAA1 OK 100 1 76 3 10 Need for Speed: Underground 2  "; udp[1] = NULL;
    assert(identify("abc", &host, title, &count) == 0 && asks == 1 && count == 76 && !strcmp(title, "Need for Speed: Underground 2"));
    udp[0] = "RAA1 OK 100 1 76 2 Fast 2 Furious"; udp[1] = NULL; /* an older PC: three numbers, a title starting with digits */
    assert(identify("abc", &host, title, &count) == 0 && count == 76 && !strcmp(title, "2 Fast 2 Furious"));
    udp[0] = "RAA1 WAIT"; udp[1] = "RAA1 NO unknown"; udp[2] = NULL;
    assert(identify("abc", &host, title, &count) == 1 && asks == 2);
    udp[0] = NULL; assert(identify("abc", &host, title, &count) == -2);
    udp[0] = "RAA1 WAIT"; udp[1] = NULL; assert(identify("abc", &host, title, &count) == -2 && asks == 2); /* silent after WAIT: give up */
    /* An explicit companion IP is asked directly, never broadcast to. */
    net.forgetPeer(); gRAHostIp[0] = 192; gRAHostIp[1] = 168; gRAHostIp[2] = 1; gRAHostIp[3] = 2;
    udp[0] = NULL; assert(identify(NULL, &host, title, &count) == 0 && host == 0x0201a8c0 && !asks);
    puts("PASS: Ranet::xeraboraIdentify strict discovery, both RAQ1 shapes, WAIT/NO/silent and the pinned host");
    return 0;
}
"""
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    program = http_harness + "".join(method(m) for m in (
        "int Ranet::hostIsExplicit()", "void Ranet::rememberPeer(", "unsigned int Ranet::peerIP()", "void Ranet::broadcastTarget(",
        "int Ranet::configuredTarget(", "int Ranet::raqAsk(", "unsigned int Ranet::xeraboraHost(", "int Ranet::xeraboraIdentify(",
        "int Ranet::tcpRecv(", "int Ranet::httpGet(")) + http_tests
    (path / "http.cpp").write_text(program, encoding="utf-8")
    subprocess.run(["c++", "-std=gnu++17", "-Wall", "-Wno-unused-function", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=undefined", "-I", str(root), str(path / "http.cpp"), "-o", str(path / "http.exe")], check=True)
    subprocess.run([str(path / "http.exe")], check=True, timeout=30)
print("PASS: Xerabora achievement browser")

# The browser's messages name an unset clock when the HTTPS lookup could not run.
menu = (root / "src/ui/menusys.cpp").read_text(encoding="utf-8")
assert "achievementsClockUnset() ? _STR_XERA_SET_CLOCK : _STR_XERA_NOT_PLAYED" in menu
unsupported = menu[menu.index("page.state == ACH_UNSUPPORTED)"):menu.index("page.state == ACH_NOT_PLAYED")]
assert "SET_CLOCK" not in unsupported, "an unsupported game is not a clock problem"
base_yml = (root / "lng/tmpl/_base.yml").read_text(encoding="utf-8")
clock_text = base_yml[base_yml.index("- label: XERA_SET_CLOCK"):]
clock_text = clock_text[:clock_text.index("\n\n") if "\n\n" in clock_text else len(clock_text)]
assert "Showing your library" not in clock_text and "library yet" in clock_text and "clock" in clock_text
assert "- label: XERA_SET_CLOCK" in (root / "lng/tmpl/_base.yml").read_text(encoding="utf-8")
print("PASS: HTTPS game-ID messages")

# clock() is a 32-bit microsecond counter on the EE: RA_HTTP_MS * CLOCKS_PER_SEC overflows (8 s
# became ~3.7 s) and an absolute deadline breaks at the wrap. httpGet compares elapsed time.
http_get = method("int Ranet::httpGet(")
assert "RA_HTTP_MS * CLOCKS_PER_SEC /" not in http_get and "clock_t deadline" not in http_get
assert "(clock_t)(clock() - start) < (clock_t)RA_HTTP_MS * (CLOCKS_PER_SEC / 1000)" in http_get
print("PASS: Ranet::httpGet budget is wrap-safe elapsed time")
