/*
 * Host test for the shared UDPRDMA transport (udpfs_shared/src/udprdma.c).
 *
 * The real transport is compiled against stubbed IOP event flags, alarms and a stubbed
 * ministack. A script feeds it what the network and the alarm would deliver, and every
 * packet it transmits is recorded. Checks:
 *   - a stale ACK does not cost a retry: the request is sent once and the covering ACK
 *     that follows completes it;
 *   - a reconnect after a dead session starts again from sequence 0 (TX and RX), which
 *     is what a server that reset its session on DISCOVERY expects.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef UDPRDMA_TEST_SOURCE
#define UDPRDMA_TEST_SOURCE "../src/udprdma.c"
#endif
#include UDPRDMA_TEST_SOURCE

enum {
    EV_TIMEOUT,
    EV_INFORM,
    EV_ACK,
    EV_DATA_FIN,
    EV_DATA,         /* reply data packet without FIN */
    EV_DATA_TIMEOUT, /* a reply data packet and the alarm, both before the transport wakes */
};

typedef struct
{
    int kind;
    int arg;
} script_ev_t;

static script_ev_t script[64];
static int script_len, script_pos;

static uint32_t flags;
static unsigned int (*alarm_cb)(void *);
static void *alarm_arg;
static int alarm_fail; /* SetAlarm refuses, as on an exhausted IOP kernel heap */
static int alarms_set; /* SetAlarm calls that armed an alarm */

static udp_socket_t bound_socket;
static uint8_t frame[64];

typedef struct
{
    int type;
    int seq;
    int seq_nr_ack;
    int payload;
    int flags;
    int event; /* index of the script event that was being delivered, -1 outside a script */
} sent_t;

static sent_t sent[64];
static int sent_len;

static int failures;

static void check(const char *what, int ok)
{
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

static void run_script(int n, const script_ev_t *evs)
{
    if (n > 0)
        memcpy(script, evs, n * sizeof(*evs));
    script_len = n;
    script_pos = 0;
}

/* --- network injection --- */

static void deliver(void)
{
    bound_socket.handler(&bound_socket, bound_socket.handler_arg, frame, 44);
}

static void inject_inform(void)
{
    udprdma_pkt_disc_t *p = (udprdma_pkt_disc_t *)frame;

    memset(frame, 0, sizeof(frame));
    p->ip.addr_src.addr[0] = 192;
    p->ip.addr_src.addr[1] = 168;
    p->ip.addr_src.addr[2] = 1;
    p->ip.addr_src.addr[3] = 2;
    p->udp.port_src = htons(57996); /* PS2-Servers' separate data port */
    p->hdr.packet_type = UDPRDMA_PT_INFORM;
    p->hdr.seq_nr = 1;
    p->disc.service_id = UDPRDMA_SVC_UDPFS;
    deliver();
}

static void inject_data(int seq, int ack, int flags_, int bytes)
{
    udprdma_pkt_data_t *p = (udprdma_pkt_data_t *)frame;

    memset(frame, 0, sizeof(frame));
    p->hdr.packet_type = UDPRDMA_PT_DATA;
    p->hdr.seq_nr = seq;
    p->data.seq_nr_ack = ack;
    p->data.flags = flags_;
    p->data.data_byte_count = bytes;
    deliver();
}

static void step(void)
{
    script_ev_t ev;

    if (script_pos >= script_len) {
        fprintf(stderr, "FAIL: transport waited past the end of the script\n");
        exit(1);
    }
    ev = script[script_pos++];
    switch (ev.kind) {
        case EV_TIMEOUT:
            if (alarm_cb == NULL) {
                fprintf(stderr, "FAIL: timeout scripted with no alarm armed\n");
                exit(1);
            }
            alarm_cb(alarm_arg);
            break;
        case EV_INFORM:
            inject_inform();
            break;
        case EV_ACK:
            inject_data(0, ev.arg, UDPRDMA_DF_ACK, 0);
            break;
        case EV_DATA_FIN:
            inject_data(ev.arg, 0, UDPRDMA_DF_FIN, 4);
            break;
        case EV_DATA:
            inject_data(ev.arg, 0, 0, 4);
            break;
        case EV_DATA_TIMEOUT:
            inject_data(ev.arg, 0, 0, 4);
            if (alarm_cb == NULL) {
                fprintf(stderr, "FAIL: timeout scripted with no alarm armed\n");
                exit(1);
            }
            alarm_cb(alarm_arg);
            break;
    }
}

/* --- IOP stubs --- */

int CreateEventFlag(iop_event_t *event)
{
    flags = event->bits;
    return 1;
}
int DeleteEventFlag(int ef)
{
    (void)ef;
    return 0;
}
int SetEventFlag(int ef, uint32_t bits)
{
    (void)ef;
    flags |= bits;
    return 0;
}
int iSetEventFlag(int ef, uint32_t bits)
{
    return SetEventFlag(ef, bits);
}
int ClearEventFlag(int ef, uint32_t bits)
{
    (void)ef;
    flags &= bits;
    return 0;
}
int WaitEventFlag(int ef, uint32_t bits, int mode, uint32_t *resbits)
{
    (void)ef;
    while (!(flags & bits))
        step();
    *resbits = flags;
    if (mode & WEF_CLEAR)
        flags = 0;
    return 0;
}
void USec2SysClock(uint32_t usec, iop_sys_clock_t *clock)
{
    clock->lo = usec;
    clock->hi = 0;
}
int SetAlarm(iop_sys_clock_t *clock, unsigned int (*cb)(void *), void *arg)
{
    (void)clock;
    if (alarm_fail)
        return -400; /* KE_NO_MEMORY */
    alarm_cb = cb;
    alarm_arg = arg;
    alarms_set++;
    return 0;
}
int CancelAlarm(unsigned int (*cb)(void *), void *arg)
{
    (void)cb;
    (void)arg;
    alarm_cb = NULL;
    return 0;
}
int DelayThread(int usec)
{
    (void)usec;
    return 0;
}

void smap_fifo_read(uint16_t offset, void *dst, uint32_t bytes)
{
    memcpy(dst, frame + offset, bytes);
}

udp_socket_t *udp_bind(uint16_t port_src, udp_port_handler handler, void *handler_arg)
{
    bound_socket.port_src = port_src;
    bound_socket.handler = handler;
    bound_socket.handler_arg = handler_arg;
    return &bound_socket;
}
void udp_packet_init(udp_packet_t *pkt, uint32_t ip_dst, uint16_t port_dst)
{
    (void)ip_dst;
    pkt->udp.port_dst = htons(port_dst);
}
int udp_packet_send_ll(udp_socket_t *socket, udp_packet_t *pkt, uint16_t pktdatasize, const void *data, uint16_t datasize)
{
    udprdma_pkt_data_t *p = (udprdma_pkt_data_t *)pkt;

    (void)socket;
    (void)pktdatasize;
    (void)data;
    if (sent_len < (int)(sizeof(sent) / sizeof(sent[0]))) {
        sent[sent_len].type = p->hdr.packet_type;
        sent[sent_len].seq = p->hdr.seq_nr;
        sent[sent_len].seq_nr_ack = p->hdr.packet_type == UDPRDMA_PT_DATA ? (int)p->data.seq_nr_ack : -1;
        sent[sent_len].payload = datasize;
        sent[sent_len].flags = p->hdr.packet_type == UDPRDMA_PT_DATA ? (int)p->data.flags : -1;
        sent[sent_len].event = script_pos - 1;
        sent_len++;
    }
    return 0;
}

static int count_requests(int from)
{
    int i, n = 0;
    for (i = from; i < sent_len; i++)
        if (sent[i].type == UDPRDMA_PT_DATA && sent[i].payload > 0)
            n++;
    return n;
}

/* An ACK or NACK sent while script events first..last were delivered that tells the
 * server every packet up to and including `last_seq` arrived. */
static int answered_up_to(int from, int first, int last, int last_seq)
{
    int i;
    for (i = from; i < sent_len; i++) {
        if (sent[i].type != UDPRDMA_PT_DATA || sent[i].payload != 0)
            continue;
        if (sent[i].event < first || sent[i].event > last)
            continue;
        if ((sent[i].flags & UDPRDMA_DF_ACK) ? sent[i].seq_nr_ack == last_seq :
                                               sent[i].seq_nr_ack == ((last_seq + 1) & 0xFFF))
            return 1;
    }
    return 0;
}

static int first_request(int from)
{
    int i;
    for (i = from; i < sent_len; i++)
        if (sent[i].type == UDPRDMA_PT_DATA && sent[i].payload > 0)
            return i;
    return -1;
}

int main(void)
{
    static const script_ev_t connect[] = {{EV_INFORM, 0}};
    static const script_ev_t stale_then_ack1[] = {{EV_ACK, 0}, {EV_ACK, 1}};
    static const script_ev_t reply_seq0[] = {{EV_DATA_FIN, 0}};
    static const script_ev_t ack2[] = {{EV_ACK, 2}};
    static const script_ev_t dead[] = {{EV_TIMEOUT, 0}, {EV_TIMEOUT, 0}, {EV_TIMEOUT, 0}, {EV_TIMEOUT, 0}};
    static const script_ev_t ack0[] = {{EV_ACK, 0}};
    static const script_ev_t timeout_then_ack0[] = {{EV_TIMEOUT, 0}, {EV_ACK, 0}};
    script_ev_t stream[40];
    uint8_t req[16] = {0}, reply[16], bulk[64];
    int n, burst;
    udprdma_socket_t *s;
    int mark, idx;

    s = udprdma_create(0, UDPRDMA_SVC_UDPFS);
    check("socket created", s != NULL);

    /* A fresh socket must not treat the first request as already acknowledged. */
    run_script(1, connect);
    check("first discovery connects", udprdma_discover(s, 2000) == UDPRDMA_OK);
    mark = sent_len;
    run_script(2, timeout_then_ack0);
    check("first request survives one timeout", udprdma_send(s, req, sizeof(req)) == UDPRDMA_OK);
    check("unacknowledged first request is resent once", count_requests(mark) == 2);

    /* Stale ACK: keep waiting for the covering ACK, do not resend. */
    mark = sent_len;
    run_script(2, stale_then_ack1); /* ACK 0 is stale for request 1 */
    check("request with a stale ACK first completes", udprdma_send(s, req, sizeof(req)) == UDPRDMA_OK);
    check("a stale ACK does not cost a retry", count_requests(mark) == 1);
    check("stale ACK leaves the script fully consumed", script_pos == script_len);

    /* Receive one reply so RX state is non-zero too. */
    udprdma_set_rx_buffer(s, reply, sizeof(reply));
    run_script(1, reply_seq0);
    check("reply received", udprdma_recv(s, reply, sizeof(reply), 1000) == 4);

    run_script(1, ack2);
    check("second request completes", udprdma_send(s, req, sizeof(req)) == UDPRDMA_OK);

    /* The session dies: four timeouts disconnect it. */
    mark = sent_len;
    run_script(4, dead);
    check("dead session returns an error", udprdma_send(s, req, sizeof(req)) == UDPRDMA_ERR_NACK);
    check("dead session sent four tries", count_requests(mark) == 4);
    check("dead session disconnects", !udprdma_is_connected(s));

    /* Reconnect: the server reset its session on DISCOVERY, so start again from zero. */
    run_script(1, connect);
    check("rediscovery connects", udprdma_discover(s, 2000) == UDPRDMA_OK);
    mark = sent_len;
    run_script(1, ack0);
    check("first request after reconnect completes", udprdma_send(s, req, sizeof(req)) == UDPRDMA_OK);
    idx = first_request(mark);
    check("a request was sent after reconnect", idx >= 0);
    if (idx >= 0) {
        check("reconnect restarts TX at sequence 0", sent[idx].seq == 0);
        check("reconnect restarts RX expectation at 0", sent[idx].seq_nr_ack == 0xFFF);
    }

    /* The split-header send path must consume stale ACKs without retransmitting too. */
    mark = sent_len;
    run_script(2, stale_then_ack1);
    check("split send with stale ACK completes", udprdma_send_ll(s, req, 4, req, sizeof(req)) == UDPRDMA_OK);
    check("split send does not retry on stale ACK", count_requests(mark) == 1);
    check("split send consumes covering ACK", script_pos == script_len);

    /* A lost window ACK. The server has sent its whole window (8 packets) and they all
     * arrived, but the ACK sent after the sixth was lost, so the server times out and sends
     * the window again. The receiver must answer every such copy: when its first answer is
     * lost too, nothing else can move the transfer. The server retransmits stale copies for
     * 20 s and aborts, and the console sits in recv for 30 s, then reconnects. */
    n = 0;
    for (idx = 0; idx < 8; idx++)
        stream[n++] = (script_ev_t) {EV_DATA, idx}; /* 0..7; the ACK after 5 is lost */
    for (burst = 0; burst < 2; burst++)
        for (idx = 0; idx < 8; idx++)
            stream[n++] = (script_ev_t) {EV_DATA, idx}; /* server timeout: 0..7 again */
    for (idx = 8; idx < 15; idx++)
        stream[n++] = (script_ev_t) {EV_DATA, idx};
    stream[n++] = (script_ev_t) {EV_DATA_FIN, 15};
    mark = sent_len;
    udprdma_set_rx_buffer(s, bulk, sizeof(bulk));
    run_script(n, stream);
    check("stream with a lost window ACK completes", udprdma_recv(s, bulk, sizeof(bulk), 1000) == (int)sizeof(bulk));
    check("stream consumes the whole script", script_pos == script_len);
    check("first resent window is answered", answered_up_to(mark, 8, 15, 7));
    check("second resent window is answered after the first answer was lost", answered_up_to(mark, 16, 23, 7));

    /* SetAlarm fails: every wait must end as a timeout, never block. An empty script makes a
     * wait that is not ended by the transport itself fail the test instead of hanging. */
    alarm_fail = 1;
    mark = sent_len;
    run_script(0, NULL);
    check("split send with no alarm gives up", udprdma_send_ll(s, req, 4, req, sizeof(req)) == UDPRDMA_ERR_NACK);
    check("split send with no alarm still makes four tries", count_requests(mark) == 4);

    alarm_fail = 0;
    run_script(1, connect);
    check("rediscovery after split send connects", udprdma_discover(s, 2000) == UDPRDMA_OK);
    alarm_fail = 1;
    run_script(0, NULL);
    check("receive with no alarm times out", udprdma_recv(s, reply, sizeof(reply), 1000) == UDPRDMA_ERR_TIMEOUT);

    alarm_fail = 0;
    run_script(1, connect);
    check("rediscovery after receive connects", udprdma_discover(s, 2000) == UDPRDMA_OK);
    alarm_fail = 1;
    mark = sent_len;
    run_script(0, NULL);
    check("send with no alarm gives up", udprdma_send(s, req, sizeof(req)) == UDPRDMA_ERR_NACK);
    check("send with no alarm still makes four tries", count_requests(mark) == 4);
    check("discovery with no alarm times out", udprdma_discover(s, 2000) == UDPRDMA_ERR_TIMEOUT);
    alarm_fail = 0;

    /* A transfer that stops making progress must still time out. Resent copies of packets already
     * received wake recv to ACK them; they must not restart its timeout, or a server that keeps
     * resending would hold recv, and every read behind it, forever. */
    run_script(1, connect);
    check("rediscovery before the stalled stream connects", udprdma_discover(s, 2000) == UDPRDMA_OK);
    n = 0;
    for (idx = 0; idx < 6; idx++)
        stream[n++] = (script_ev_t) {EV_DATA, idx}; /* progress: the timeout restarts once */
    for (burst = 0; burst < 3; burst++)
        for (idx = 0; idx < 6; idx++)
            stream[n++] = (script_ev_t) {EV_DATA, idx}; /* copies: no progress */
    stream[n++] = (script_ev_t) {EV_TIMEOUT, 0};
    udprdma_set_rx_buffer(s, bulk, sizeof(bulk));
    run_script(n, stream);
    mark = alarms_set;
    check("stalled stream times out", udprdma_recv(s, bulk, sizeof(bulk), 1000) == UDPRDMA_ERR_TIMEOUT);
    check("stalled stream consumes the whole script", script_pos == script_len);
    check("resent copies do not restart the receive timeout", alarms_set - mark == 2);

    /* The alarm fires in the same wakeup as a resent copy. The ACK for the copy must not hide the
     * timeout: the alarm is spent, so nothing else would end this receive. An empty rest of script
     * makes a wait past this point fail the test instead of hanging. */
    run_script(1, connect);
    check("rediscovery before the coalesced timeout connects", udprdma_discover(s, 2000) == UDPRDMA_OK);
    n = 0;
    for (idx = 0; idx < 6; idx++)
        stream[n++] = (script_ev_t) {EV_DATA, idx};
    stream[n++] = (script_ev_t) {EV_DATA_TIMEOUT, 0}; /* copy of 0 plus the alarm */
    udprdma_set_rx_buffer(s, bulk, sizeof(bulk));
    run_script(n, stream);
    check("timeout coalesced with a resent copy ends the receive", udprdma_recv(s, bulk, sizeof(bulk), 1000) == UDPRDMA_ERR_TIMEOUT);

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("udprdma session: all checks passed\n");
    return 0;
}
