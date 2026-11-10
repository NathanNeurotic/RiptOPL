"""Compile the production SMSTCPIP SYN retry path and check its timeline.

The in-game stack is built with ARP_QUEUEING 0, so after every IOP reset the
first SYN is dropped while ARP resolves the server, and the connection waits
for the first retransmission. That retry now comes after TCP_SYN_RTO_FIRST
(3 slow ticks, about 1 s) instead of 9 ticks (about 2.9 s). Everything else
must stay as it was: later SYN retries every 9 ticks, the same give-up rule
(TCP_SYNMAXRTX), passive opens untouched, and data retransmits after the
connection opens starting from (sa >> 3) + sv with the usual backoff.

The harness compiles the real tcp_alloc, tcp_connect and tcp_slowtmr from
tcp.c, tcp_rexmit from tcp_out.c, and the SYN_SENT case of tcp_process from
tcp_in.c, with stubs only for output and memory.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SMS = ROOT / "modules/network/SMSTCPIP"
TCP = (SMS / "tcp.c").read_text(encoding="utf-8")
TCP_OUT = (SMS / "tcp_out.c").read_text(encoding="utf-8")
TCP_IN = (SMS / "tcp_in.c").read_text(encoding="utf-8")
TCP_H = (SMS / "include/lwip/tcp.h").read_text(encoding="utf-8")
OPTS = (SMS / "include/lwipopts.h").read_text(encoding="utf-8")
OPT_H = (SMS / "include/lwip/opt.h").read_text(encoding="utf-8")


def extract(source, pattern):
    match = re.search(pattern, source)
    if not match:
        raise ValueError("not found: " + pattern)
    opening = source.index("{", match.start())
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():index + 1]
    raise ValueError("unterminated: " + pattern)


def define(source, name):
    match = re.search(r"#define\s+" + name + r"\s+(.+?)(?:/\*.*)?$", source, re.M)
    if not match:
        raise ValueError("define not found: " + name)
    return match.group(1).strip()


# The premise of the change: the in-game build does not queue packets during ARP.
assert re.search(r"#define\s+ARP_QUEUEING\s+0", OPTS), "ARP_QUEUEING is no longer 0"
for name in ("TCP_TMR_INTERVAL", "TCP_SLOW_INTERVAL", "TCP_SYN_RTO_FIRST", "TCP_SYNMAXRTX", "TCP_MAXRTX"):
    assert not re.search(r"#define\s+" + name + r"\b", OPTS), name + " is overridden in lwipopts.h; recheck the timeline"

tcp_alloc = extract(TCP, r"struct tcp_pcb \*\s*\ntcp_alloc\(u8_t prio\)")
tcp_connect = extract(TCP, r"err_t tcp_connect\(struct tcp_pcb \*pcb,")
tcp_slowtmr = extract(TCP, r"void tcp_slowtmr\(void\)")
tcp_rexmit = extract(TCP_OUT, r"void tcp_rexmit\(struct tcp_pcb \*pcb\)")
backoff = re.search(r"const u8_t tcp_backoff\[13\] =\s*\{[^}]*\};", TCP).group(0)

start = TCP_IN.index("        case SYN_SENT:")
end = TCP_IN.index("        case SYN_RCVD:", start)
syn_sent_case = TCP_IN[start:end]

harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef unsigned char u8_t;
typedef unsigned short u16_t;
typedef unsigned int u32_t;
typedef signed short s16_t;
typedef int err_t;
#define ERR_OK 0
#define ERR_ABRT -3
#define ERR_VAL -2

#define TCP_TMR_INTERVAL @TMR@
#define TCP_SLOW_INTERVAL @SLOW@
#define TCP_SYN_RTO_FIRST @FIRST@
#define TCP_SYNMAXRTX @SYNMAX@
#define TCP_MAXRTX @MAXRTX@
#define TCP_QUEUE_OOSEQ 0
#define LWIP_CALLBACK_API 1
#define TCP_FIN_WAIT_TIMEOUT 20000
#define TCP_SYN_RCVD_TIMEOUT 20000
#define TCP_MSL 60000
#define TCP_MAXIDLE 0
#define TCP_KEEPINTVL 75000
#define TCP_KEEPDEFAULT 7200000
#define SOF_KEEPALIVE 0x08
#define TCP_PRIO_NORMAL 64
#define TCP_SND_BUF 2048
#define TCP_WND 8192
#define TCP_TTL 255
#define TCP_MSS 1460
#define MEMP_TCP_PCB 1
#define TCP_SYN 0x02U
#define TCP_ACK 0x10U
#define LWIP_DEBUGF(...) ((void)0)
#define LWIP_ASSERT(m, c) assert(c)
#define LWIP_MIN(a, b) ((a) < (b) ? (a) : (b))
#define ip4_addr1(a) 0
#define ip4_addr2(a) 0
#define ip4_addr3(a) 0
#define ip4_addr4(a) 0
#define ntohl(x) (x)
#define htonl(x) (x)
#define mips_memset memset

enum tcp_state { CLOSED, LISTEN, SYN_SENT, SYN_RCVD, ESTABLISHED, FIN_WAIT_1,
                 FIN_WAIT_2, CLOSE_WAIT, CLOSING, LAST_ACK, TIME_WAIT };

struct ip_addr { u32_t addr; };
struct tcp_hdr { u32_t seqno; u16_t wnd; };
struct tcp_seg { struct tcp_seg *next; struct tcp_hdr *tcphdr; struct tcp_hdr hdr; };

struct tcp_pcb {
    struct tcp_pcb *next;
    enum tcp_state state;
    u8_t prio, tos, ttl, so_options;
    void *callback_arg;
    struct ip_addr local_ip, remote_ip;
    u16_t local_port, remote_port;
    u32_t rcv_nxt; u16_t rcv_wnd;
    u32_t tmr; u8_t polltmr, pollinterval;
    u16_t rtime; u16_t mss;
    u16_t rttest; u32_t rtseq;
    s16_t sa, sv;
    u16_t rto; u8_t nrtx;
    u32_t lastack;
    u16_t cwnd, ssthresh;
    u32_t snd_nxt, snd_max, snd_wnd, snd_wl1, snd_wl2, snd_lbb;
    u16_t snd_buf; u8_t snd_queuelen;
    struct tcp_seg *unsent, *unacked;
    u32_t keepalive; u8_t keep_cnt;
    err_t (*recv)(void *, struct tcp_pcb *, void *, err_t);
    err_t (*connected)(void *, struct tcp_pcb *, err_t);
    void (*errf)(void *, err_t);
};

u32_t tcp_ticks;
struct tcp_pcb *tcp_active_pcbs;
struct tcp_pcb *tcp_tw_pcbs;
@BACKOFF@

/* Wire model: the tick of every segment put on the wire. */
static int sent_ticks[32];
static int sent_count;
static struct tcp_pcb storage;
static int pcb_allocated;
static int pcb_freed;
static int connected_calls;
static struct tcp_seg segs[8];
static int seg_used;

static err_t tcp_recv_null(void *a, struct tcp_pcb *p, void *b, err_t e) { (void)a; (void)p; (void)b; return e; }
static void *memp_malloc(int type) { (void)type; if (pcb_allocated) return NULL; pcb_allocated = 1; return &storage; }
static void memp_free(int type, void *p) { (void)type; assert(p == &storage); pcb_freed = 1; }
static void tcp_kill_timewait(void) {}
static void tcp_kill_prio(u8_t prio) { (void)prio; }
static u32_t tcp_next_iss(void) { return 1000; }
static u16_t tcp_new_port(void) { return 4096; }
static void tcp_pcb_purge(struct tcp_pcb *pcb) { pcb->unacked = pcb->unsent = NULL; }
static void tcp_abort(struct tcp_pcb *pcb) { (void)pcb; assert(!"abort"); }
static void tcp_keepalive(struct tcp_pcb *pcb) { (void)pcb; }
static void tcp_seg_free(struct tcp_seg *seg) { (void)seg; }
static void tcp_parseopt(struct tcp_pcb *pcb) { (void)pcb; }
#define tcp_ack(pcb) ((void)0)
#define TCP_REG(pcbs, npcb) do { (npcb)->next = *(pcbs); *(pcbs) = (npcb); } while (0)
void tcp_rexmit(struct tcp_pcb *pcb);
#define TCP_EVENT_ERR(errf, arg, err) ((void)0)
#define TCP_EVENT_POLL(pcb, ret) ((ret) = ERR_OK)
#define TCP_EVENT_CONNECTED(pcb, e, ret) (connected_calls++, (ret) = ERR_OK)

static err_t tcp_enqueue(struct tcp_pcb *pcb, void *arg, u16_t len, u8_t flags,
                         u8_t copy, u8_t *optdata, u8_t optlen)
{
    struct tcp_seg *seg = &segs[seg_used++];
    (void)arg; (void)len; (void)copy; (void)optdata; (void)optlen;
    memset(seg, 0, sizeof(*seg));
    seg->tcphdr = &seg->hdr;
    seg->hdr.seqno = pcb->snd_lbb + 1;
    pcb->snd_lbb += (flags & TCP_SYN) ? 1 : 100;
    pcb->snd_queuelen++;
    seg->next = pcb->unsent;
    pcb->unsent = seg;
    return ERR_OK;
}

/* Same send bookkeeping as tcp_output_segment(): restart rtime, start one RTT sample. */
static err_t tcp_output(struct tcp_pcb *pcb)
{
    while (pcb->unsent != NULL) {
        struct tcp_seg *seg = pcb->unsent;
        pcb->unsent = seg->next;
        seg->next = pcb->unacked;
        pcb->unacked = seg;
        pcb->rtime = 0;
        if (pcb->rttest == 0) {
            pcb->rttest = tcp_ticks;
            pcb->rtseq = seg->hdr.seqno;
        }
        sent_ticks[sent_count++] = (int)tcp_ticks;
    }
    return ERR_OK;
}

@ALLOC@

@CONNECT@

@SLOWTMR@

@REXMIT@

/* Globals tcp_process() reads for the segment being processed. */
static u8_t flags;
static u32_t seqno, ackno;
static struct tcp_hdr *tcphdr;
static struct tcp_hdr synack_hdr = { 5000, 8192 };

static void receive_synack(struct tcp_pcb *pcb)
{
    struct tcp_seg *rseg;
    err_t err = ERR_OK;
    flags = TCP_SYN | TCP_ACK;
    seqno = 5000;
    ackno = pcb->unacked->tcphdr->seqno + 1;
    tcphdr = &synack_hdr;
    switch (pcb->state) {
@SYNSENT@
        default:
            assert(!"unexpected state");
    }
    (void)rseg;
    (void)err;
}

static void reset_world(void)
{
    memset(&storage, 0, sizeof(storage));
    pcb_allocated = pcb_freed = connected_calls = 0;
    seg_used = sent_count = 0;
    tcp_ticks = 0;
    tcp_active_pcbs = tcp_tw_pcbs = NULL;
}

static struct tcp_pcb *open_active(void)
{
    struct ip_addr server = { 0x6401a8c0 };
    struct tcp_pcb *pcb;
    reset_world();
    pcb = tcp_alloc(TCP_PRIO_NORMAL);
    assert(pcb != NULL);
    pcb->pollinterval = 255;
    assert(tcp_connect(pcb, &server, 445, NULL) == ERR_OK);
    assert(pcb->state == SYN_SENT && sent_count == 1 && sent_ticks[0] == 0);
    return pcb;
}

static void tick_until_sent(int want)
{
    int guard = 0;
    while (sent_count < want) {
        tcp_slowtmr();
        assert(++guard < 1000);
    }
}

int main(void)
{
    const int old_rto = 3000 / TCP_SLOW_INTERVAL;
    struct tcp_pcb *pcb;
    int i;

    assert(TCP_SLOW_INTERVAL == 320);
    assert(old_rto == 9);
    assert(TCP_SYN_RTO_FIRST == 3);

    /* A passive or never-connected PCB keeps the old 3 s timers. */
    reset_world();
    pcb = tcp_alloc(TCP_PRIO_NORMAL);
    assert(pcb->rto == old_rto && pcb->sv == old_rto && pcb->sa == 0);

    /* 1. Server never answers: first retry at tick 3, then every 9 ticks, then give up. */
    pcb = open_active();
    for (i = 1; i <= TCP_SYNMAXRTX; i++) {
        tick_until_sent(i + 1);
        assert(sent_ticks[i] == TCP_SYN_RTO_FIRST + (i - 1) * old_rto);
        assert(pcb->nrtx == i && pcb->rto == old_rto);
    }
    while (!pcb_freed) {
        tcp_slowtmr();
        assert(tcp_ticks < 1000);
    }
    printf("give-up: SYN at ticks");
    for (i = 0; i < sent_count; i++)
        printf(" %d", sent_ticks[i]);
    printf(", PCB removed at tick %u (was %d before)\n",
           tcp_ticks, TCP_SYNMAXRTX * old_rto + 1);
    assert(sent_count == TCP_SYNMAXRTX + 1);
    assert(tcp_ticks == TCP_SYN_RTO_FIRST + (TCP_SYNMAXRTX - 1) * old_rto + 1);
    assert(tcp_active_pcbs == NULL);

    /* 2. First SYN lost to ARP, retry answered: open after tick 3, data timers as before. */
    pcb = open_active();
    tick_until_sent(2);
    assert(sent_ticks[1] == TCP_SYN_RTO_FIRST);
    receive_synack(pcb);
    assert(pcb->state == ESTABLISHED && connected_calls == 1);
    assert(pcb->rto == old_rto);
    assert(pcb->nrtx == 1); /* unchanged lwIP behaviour: nrtx clears on the first new ACK */
    /* The first data segment is lost too: same retry time and backoff as before. */
    tcp_enqueue(pcb, NULL, 100, 0, 0, NULL, 0);
    tcp_output(pcb);
    i = (int)tcp_ticks;
    tick_until_sent(4);
    assert(sent_ticks[3] - i == old_rto);
    assert(pcb->rto == (old_rto << tcp_backoff[1]));

    /* 3. First SYN answered at once: the early timeout does not leak into the open connection. */
    pcb = open_active();
    tcp_slowtmr();
    receive_synack(pcb);
    assert(pcb->state == ESTABLISHED && pcb->nrtx == 0 && pcb->rto == old_rto);
    tcp_enqueue(pcb, NULL, 100, 0, 0, NULL, 0);
    tcp_output(pcb);
    i = (int)tcp_ticks;
    tick_until_sent(3);
    assert(sent_ticks[2] - i == old_rto);
    assert(pcb->rto == (old_rto << tcp_backoff[0]));

    printf("SMSTCPIP SYN retry: first retry %d ms (was %d ms), later timers unchanged\n",
           TCP_SYN_RTO_FIRST * TCP_SLOW_INTERVAL, old_rto * TCP_SLOW_INTERVAL);
    return 0;
}
'''

tmr = define(TCP_H, "TCP_TMR_INTERVAL")
slow = define(TCP_H, "TCP_SLOW_INTERVAL")
first = define(TCP_H, "TCP_SYN_RTO_FIRST")
harness = (harness
           .replace("@TMR@", tmr)
           .replace("@SLOW@", slow)
           .replace("@FIRST@", first)
           .replace("@SYNMAX@", define(OPT_H, "TCP_SYNMAXRTX"))
           .replace("@MAXRTX@", define(OPT_H, "TCP_MAXRTX"))
           .replace("@BACKOFF@", backoff)
           .replace("@ALLOC@", tcp_alloc)
           .replace("@CONNECT@", tcp_connect)
           .replace("@SLOWTMR@", tcp_slowtmr)
           .replace("@REXMIT@", tcp_rexmit)
           .replace("@SYNSENT@", syn_sent_case))

with tempfile.TemporaryDirectory() as tmp:
    src = os.path.join(tmp, "syn_retry.c")
    exe = os.path.join(tmp, "syn_retry")
    Path(src).write_text(harness, encoding="utf-8")
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wno-unused-function", "-Werror", src, "-o", exe], check=True)
    subprocess.run([exe], check=True)
