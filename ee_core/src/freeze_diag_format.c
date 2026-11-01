/*
  In-game freeze report formatter (INGAMEDIAG flavour only). ee_core has no printf, so this
  builds the text by hand. Plain C with no PS2 dependencies: tested on the host by
  ee_core/tests/test_freeze_diag_format.c.
*/

#include "freeze_diag_report.h"

typedef struct
{
    char *out;
    int cap;
    int len;
} fd_buf_t;

static void put_char(fd_buf_t *b, char c)
{
    if (b->len < b->cap - 1)
        b->out[b->len++] = c;
}

static void put_str(fd_buf_t *b, const char *s)
{
    while (*s != '\0')
        put_char(b, *s++);
}

static void put_dec(fd_buf_t *b, unsigned int v)
{
    char digits[10];
    int n = 0;

    do {
        digits[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v != 0);
    while (n > 0)
        put_char(b, digits[--n]);
}

static void put_hex(fd_buf_t *b, unsigned int v)
{
    static const char hex[] = "0123456789ABCDEF";
    int shift;

    put_str(b, "0x");
    for (shift = 28; shift >= 0; shift -= 4)
        put_char(b, hex[(v >> shift) & 0xF]);
}

static void put_field(fd_buf_t *b, const char *name, unsigned int v)
{
    put_char(b, ' ');
    put_str(b, name);
    put_char(b, '=');
    put_dec(b, v);
}

// Milliseconds from start to now, both IOP clock values split into hi/lo words.
static unsigned int elapsed_ms(unsigned int start_hi, unsigned int start_lo, unsigned int now_hi, unsigned int now_lo)
{
    // 32-bit only: this also runs in the IOP report writer, which has no 64-bit division.
    // One wrap of the low word (2^32 ticks) is 116508 ms.
    unsigned int hi, lo;

    if (now_hi < start_hi || (now_hi == start_hi && now_lo <= start_lo))
        return 0;
    lo = now_lo - start_lo;
    hi = now_hi - start_hi - (now_lo < start_lo ? 1 : 0);
    // 2^32 = 116508 * 36864 + 16384, so carry each wrap's 16384 leftover ticks exactly.
    return hi * 116508u + lo / FREEZE_DIAG_IOP_TICKS_PER_MS + (lo % FREEZE_DIAG_IOP_TICKS_PER_MS + hi * 16384u) / FREEZE_DIAG_IOP_TICKS_PER_MS;
}

static const char *thread_status_name(unsigned int status)
{
    switch (status) {
        case 0x01:
            return "RUN";
        case 0x02:
            return "READY";
        case 0x04:
            return "WAIT";
        case 0x08:
            return "SUSPEND";
        case 0x0C:
            return "WAITSUSPEND";
        case 0x10:
            return "DORMANT";
        default:
            return 0;
    }
}

static const char *wait_type_name(unsigned int type)
{
    switch (type) {
        case 0:
            return "-";
        case 1:
            return "SLEEP";
        case 2:
            return "SEMA";
        default:
            return 0;
    }
}

static const char *iop_status_name(unsigned int status)
{
    switch (status) {
        case FREEZE_DIAG_IOP_ANSWERED:
            return "answered";
        case FREEZE_DIAG_IOP_NO_BIND:
            return "no-bind";
        case FREEZE_DIAG_IOP_TIMEOUT:
            return "timeout";
        case FREEZE_DIAG_IOP_CALL_FAILED:
            return "call-failed";
        default:
            return "bad-record";
    }
}

static const char *shutdown_status_name(unsigned int status)
{
    switch (status) {
        case FREEZE_DIAG_SHUTDOWN_DONE:
            return "done";
        case FREEZE_DIAG_SHUTDOWN_TIMEOUT:
            return "timeout";
        default:
            return "skipped";
    }
}

static const char *vmc_op_name(unsigned int op)
{
    return op == FREEZE_DIAG_VMC_WRITE ? "write" : "read";
}

// One history entry. ms_ago assumes the entry is less than one low-word wrap (116 s) old.
static void put_event(fd_buf_t *b, const freeze_diag_event_t *e, unsigned int now_lo)
{
    unsigned int code = e->code_sectors >> 24, sectors = e->code_sectors & 0xFFFFFF;

    put_str(b, "ev -");
    put_dec(b, (now_lo - e->tick_lo) / FREEZE_DIAG_IOP_TICKS_PER_MS);
    put_str(b, "ms ");
    if (code >= 'A' && code <= 'Z')
        put_char(b, (char)code);
    else
        put_char(b, '?');
    switch (code) {
        case FREEZE_DIAG_EV_READ:
            put_str(b, " read");
            put_field(b, "lsn", e->lsn);
            put_field(b, "sectors", sectors);
            if (e->arg & 1)
                put_str(b, " from-interrupt");
            if (e->arg & 2)
                put_str(b, " sync");
            break;
        case FREEZE_DIAG_EV_BUSY:
            put_str(b, " read-refused-drive-busy");
            put_field(b, "lsn", e->lsn);
            put_field(b, "sectors", sectors);
            put_field(b, "times", e->arg);
            break;
        case FREEZE_DIAG_EV_DONE_CB:
        case FREEZE_DIAG_EV_DONE_NOCB:
        case FREEZE_DIAG_EV_DONE_STREAM:
            put_str(b, code == FREEZE_DIAG_EV_DONE_CB ? " done game-callback" : code == FREEZE_DIAG_EV_DONE_NOCB ? " done no-callback" :
                                                                                                                   " done stream-callback-instead-of-game");
            put_field(b, "lsn", e->lsn);
            put_field(b, "sectors", sectors);
            put_field(b, "ms", e->arg);
            break;
        case FREEZE_DIAG_EV_ERROR:
            put_str(b, " read-error");
            put_field(b, "lsn", e->lsn);
            put_field(b, "sectors", sectors);
            put_str(b, " err=");
            put_hex(b, e->arg);
            break;
        case FREEZE_DIAG_EV_ST_START:
            put_str(b, " stream-start");
            put_field(b, "lsn", e->lsn);
            put_field(b, "buf", sectors);
            put_field(b, "bank", e->arg);
            break;
        case FREEZE_DIAG_EV_ST_STOP:
        case FREEZE_DIAG_EV_ST_PAUSE:
        case FREEZE_DIAG_EV_ST_RESUME:
            put_str(b, code == FREEZE_DIAG_EV_ST_STOP ? " stream-stop" : code == FREEZE_DIAG_EV_ST_PAUSE ? " stream-pause" :
                                                                                                           " stream-resume");
            put_field(b, "lsn", e->lsn);
            put_field(b, "was_started", e->arg);
            break;
        case FREEZE_DIAG_EV_ST_RUN:
            put_str(b, " stream-run");
            put_field(b, "lsn", e->lsn);
            put_field(b, "fills", sectors & 0xFFF);
            put_field(b, "fills_refused", sectors >> 12);
            put_field(b, "st_reads", e->arg >> 16);
            put_field(b, "short", e->arg & 0xFFFF);
            break;
        default:
            put_field(b, "lsn", e->lsn);
            put_field(b, "sectors", sectors);
            put_field(b, "arg", e->arg);
            break;
    }
    put_char(b, '\n');
}

int freeze_diag_format(const freeze_diag_report_t *r, const char *build, char *out, int cap)
{
    fd_buf_t b;
    unsigned int i, iop_status, busy_ms = 0, vmc_busy_ms = 0, st_wait_ms = 0;
    const freeze_diag_iop_t *iop = &r->iop;

    if (out == 0 || cap <= 0)
        return 0;
    b.out = out;
    b.cap = cap;
    b.len = 0;

    iop_status = r->iop_status;
    if (iop_status == FREEZE_DIAG_IOP_ANSWERED && (iop->magic != FREEZE_DIAG_MAGIC || iop->version != FREEZE_DIAG_VERSION))
        iop_status = FREEZE_DIAG_IOP_BAD_RECORD;

    put_str(&b, "RiptOPL in-game freeze report v3\n");
    put_str(&b, "build=");
    put_str(&b, build != 0 ? build : "?");
    put_char(&b, '\n');

    put_str(&b, "game=");
    for (i = 0; i < sizeof(r->game_id) && r->game_id[i] != '\0'; i++)
        put_char(&b, r->game_id[i]);
    put_char(&b, '\n');

    put_str(&b, "trigger=");
    put_str(&b, r->trigger == FREEZE_DIAG_TRIGGER_STALL ? "stall" : r->trigger == FREEZE_DIAG_TRIGGER_POWER ? "power" :
                                                                                                              "combo");
    put_char(&b, '\n');

    put_str(&b, "mode=");
    put_dec(&b, r->game_mode);
    put_str(&b, " compat=");
    put_hex(&b, r->compat);
    put_field(&b, "cheats", r->cheats);
    put_field(&b, "gsm", r->gsm);
    put_field(&b, "pademu", r->pademu);
    put_char(&b, '\n');

    put_str(&b, "iop_resets=");
    put_dec(&b, r->iop_resets);
    put_field(&b, "failed_resets", r->iop_reset_failures);
    put_field(&b, "igr_polls", r->igr_polls);
    put_field(&b, "padopen_hooked", r->padopen_hooked);
    put_field(&b, "libpad", r->libpad);
    put_field(&b, "pad_state", r->pad_state);
    put_field(&b, "pad_frame", r->pad_frame);
    put_char(&b, '\n');

    put_str(&b, "iop=");
    put_str(&b, iop_status_name(iop_status));
    put_str(&b, " shutdown=");
    put_str(&b, shutdown_status_name(r->shutdown_status));
    put_char(&b, '\n');

    if (iop_status == FREEZE_DIAG_IOP_ANSWERED) {
        if (iop->busy)
            busy_ms = elapsed_ms(iop->busy_start_hi, iop->busy_start_lo, iop->now_hi, iop->now_lo);
        if (iop->vmc_busy)
            vmc_busy_ms = elapsed_ms(iop->vmc_busy_start_hi, iop->vmc_busy_start_lo, iop->now_hi, iop->now_lo);
        if (iop->st_read_waiting)
            st_wait_ms = elapsed_ms(iop->st_wait_start_hi, iop->st_wait_start_lo, iop->now_hi, iop->now_lo);

        put_str(&b, "disc_reads=");
        put_dec(&b, iop->reads);
        put_field(&b, "read_errors", iop->read_errors);
        put_str(&b, " last_err=");
        put_hex(&b, iop->last_err);
        put_field(&b, "eom_hits", iop->eom_hits);
        put_field(&b, "last_lsn", iop->last_lsn);
        put_field(&b, "last_sectors", iop->last_sectors);
        put_field(&b, "media_lsn", iop->media_lsn_count);
        put_char(&b, '\n');

        put_str(&b, "read_busy=");
        put_dec(&b, iop->busy);
        if (iop->busy) {
            put_field(&b, "busy_lsn", iop->busy_lsn);
            put_field(&b, "busy_sectors", iop->busy_sectors);
            put_field(&b, "busy_ms", busy_ms);
        }
        put_field(&b, "max_read_ms", iop->max_read_ms);
        put_field(&b, "slow_reads", iop->slow_reads);
        put_char(&b, '\n');

        put_str(&b, "vmc_ops=");
        put_dec(&b, iop->vmc_ops);
        if (iop->vmc_ops != 0) {
            put_str(&b, " vmc_last_op=");
            put_str(&b, vmc_op_name(iop->vmc_last_op));
            put_field(&b, "vmc_last_lba", iop->vmc_last_lba);
        }
        put_char(&b, '\n');
        put_str(&b, "vmc_busy=");
        put_dec(&b, iop->vmc_busy);
        if (iop->vmc_busy) {
            put_str(&b, " vmc_busy_op=");
            put_str(&b, vmc_op_name(iop->vmc_busy_op));
            put_field(&b, "vmc_busy_lba", iop->vmc_busy_lba);
            put_field(&b, "vmc_busy_ms", vmc_busy_ms);
        }
        put_char(&b, '\n');

        put_str(&b, "drive_held=");
        put_dec(&b, iop->sync_flag);
        put_field(&b, "cd_status", iop->cd_status);
        put_str(&b, " game_cb=");
        put_hex(&b, iop->user_cb);
        put_field(&b, "sync_waiting", iop->sync_waiting);
        put_field(&b, "busy_refusals", iop->busy_refusals);
        put_field(&b, "cb_runs", iop->cb_runs);
        put_field(&b, "cb_to_stream", iop->cb_to_stream);
        put_char(&b, '\n');

        put_str(&b, "stream=");
        put_dec(&b, iop->st_stat);
        put_field(&b, "stm0_cb", iop->st_stm0_cb);
        put_field(&b, "filling", iop->st_is_reading);
        put_field(&b, "lsn", iop->st_lsn);
        put_field(&b, "buf", iop->st_bufmax);
        put_field(&b, "bank", iop->st_banksize);
        put_field(&b, "buffered", iop->st_streamed);
        put_field(&b, "wr", iop->st_wr);
        put_field(&b, "rd", iop->st_rd);
        put_char(&b, '\n');
        put_str(&b, "stream_starts=");
        put_dec(&b, iop->st_starts);
        put_field(&b, "stops", iop->st_stops);
        put_field(&b, "pauses", iop->st_pauses);
        put_field(&b, "resumes", iop->st_resumes);
        put_field(&b, "fills", iop->st_fills);
        put_field(&b, "fills_refused", iop->st_fill_refused);
        put_field(&b, "st_reads", iop->st_reads);
        put_field(&b, "st_reads_ee", iop->st_reads_ee);
        put_field(&b, "short_reads", iop->st_short_reads);
        put_char(&b, '\n');
        put_str(&b, "stream_read_waiting=");
        put_dec(&b, iop->st_read_waiting);
        if (iop->st_read_waiting)
            put_field(&b, "wait_ms", st_wait_ms);
        put_char(&b, '\n');
    }

    // One line a person can act on. Most specific first.
    put_str(&b, "hint=");
    if (iop_status != FREEZE_DIAG_IOP_ANSWERED) {
        put_str(&b, "the IOP did not answer: the I/O processor itself is stuck");
    } else if (iop->busy) {
        put_str(&b, "a disc read started ");
        put_dec(&b, busy_ms);
        put_str(&b, " ms ago and never returned (the storage device stalled)");
    } else if (iop->vmc_busy) {
        put_str(&b, "a memory card (VMC) ");
        put_str(&b, vmc_op_name(iop->vmc_busy_op));
        put_str(&b, " started ");
        put_dec(&b, vmc_busy_ms);
        put_str(&b, " ms ago and never returned");
    } else if (r->shutdown_status == FREEZE_DIAG_SHUTDOWN_TIMEOUT) {
        put_str(&b, "the IOP answered, but OPL's shutdown on it never finished (a device or lock is held)");
    } else if (iop->st_read_waiting && st_wait_ms >= 1000) {
        put_str(&b, "a stream read (sceCdStRead) has waited ");
        put_dec(&b, st_wait_ms);
        put_str(&b, " ms for stream data");
    } else if (iop->sync_flag) {
        put_str(&b, "the drive is still held with no read running: a finished read never released it");
    } else if (iop->cb_to_stream != 0) {
        put_dec(&b, iop->cb_to_stream);
        put_str(&b, " game reads finished while streaming was on, and the game's read callback did not run for them");
    } else if (iop->read_errors != 0) {
        put_str(&b, "disc reads failed");
    } else if (iop->eom_hits != 0) {
        put_str(&b, "the game read past the measured end of the image");
    } else {
        put_str(&b, "disc and VMC idle: the game is stuck on the EE side (see threads)");
    }
    put_char(&b, '\n');

    if (iop_status == FREEZE_DIAG_IOP_ANSWERED && iop->trace_count != 0) {
        unsigned int n = iop->trace_count < FREEZE_DIAG_TRACE ? iop->trace_count : FREEZE_DIAG_TRACE;

        put_str(&b, "history (oldest first, ms before the report):\n");
        for (i = iop->trace_count - n; i < iop->trace_count; i++) {
            const freeze_diag_event_t *e = &iop->trace[i % FREEZE_DIAG_TRACE];

            if (e->code_sectors != 0)
                put_event(&b, e, iop->now_lo);
        }
    }

    for (i = 0; i < r->thread_count && i < FREEZE_DIAG_MAX_THREADS; i++) {
        const freeze_diag_thread_t *t = &r->threads[i];
        const char *status = thread_status_name(t->status);
        const char *wait = wait_type_name(t->wait_type);

        put_str(&b, "thread id=");
        put_dec(&b, t->id);
        put_str(&b, " status=");
        if (status != 0)
            put_str(&b, status);
        else
            put_hex(&b, t->status);
        put_field(&b, "prio", t->prio);
        put_str(&b, " wait=");
        if (wait != 0)
            put_str(&b, wait);
        else
            put_dec(&b, t->wait_type);
        put_field(&b, "wait_id", t->wait_id);
        put_field(&b, "wakeup", t->wakeup);
        put_str(&b, " entry=");
        put_hex(&b, t->entry);
        put_char(&b, '\n');
    }
    if (r->threads_total > r->thread_count) {
        put_str(&b, "threads_not_listed=");
        put_dec(&b, r->threads_total - r->thread_count);
        put_char(&b, '\n');
    }

    out[b.len] = '\0';
    return b.len;
}

// The module whose image holds addr, as name+offset, else the bare address.
static void put_where(fd_buf_t *b, const heap_diag_iop_t *h, unsigned int addr)
{
    unsigned int i, n = h->module_count < HEAP_DIAG_MODULES ? h->module_count : HEAP_DIAG_MODULES;
    int c;

    for (i = 0; i < n; i++) {
        const heap_diag_module_t *m = &h->modules[i];

        if (m->text_start != 0 && addr >= m->text_start && addr - m->text_start < m->size) {
            for (c = 0; c < 8 && m->name[c] != '\0'; c++)
                put_char(b, m->name[c]);
            put_char(b, '+');
            put_hex(b, addr - m->text_start);
            return;
        }
    }
    put_hex(b, addr);
}

// A run this long right after a block is past what a free area or the next block plausibly
// starts with by chance: the game wrote past the end of the block.
#define HEAP_DIAG_OVERRUN_MIN 64

int freeze_diag_format_heap(const heap_diag_snapshot_t *h, char *out, int cap)
{
    fd_buf_t b;
    const heap_diag_iop_t *iop;
    unsigned char order[HEAP_DIAG_BLOCKS];
    unsigned int i, j, n, worst = HEAP_DIAG_BLOCKS, overruns = 0;

    if (out == 0 || cap <= 0)
        return 0;
    b.out = out;
    b.cap = cap;
    b.len = 0;

    if (h == 0 || !h->found || h->iop.version != HEAP_DIAG_VERSION) {
        put_str(&b, "heap=none (the IOP did not send its heap record)\n");
        out[b.len] = '\0';
        return b.len;
    }
    iop = &h->iop;

    put_str(&b, "heap=found at=");
    put_hex(&b, h->found_at);
    put_field(&b, "allocs", iop->allocs);
    put_field(&b, "frees", iop->frees);
    put_field(&b, "failed", iop->fails);
    put_field(&b, "unknown_frees", iop->unknown_frees);
    put_field(&b, "evicted", iop->evicted);
    put_char(&b, '\n');
    put_str(&b, "heap_free now=");
    put_dec(&b, iop->free_now);
    put_field(&b, "lowest", iop->free_min);
    put_field(&b, "largest_block_lowest", iop->largest_min);
    put_char(&b, '\n');

    n = iop->module_count < HEAP_DIAG_MODULES ? iop->module_count : HEAP_DIAG_MODULES;
    for (i = 0; i < n; i++) {
        const heap_diag_module_t *m = &iop->modules[i];
        int c;

        put_str(&b, "module name=");
        for (c = 0; c < 8 && m->name[c] != '\0'; c++)
            put_char(&b, m->name[c]);
        put_str(&b, " text=");
        put_hex(&b, m->text_start);
        put_field(&b, "size", m->size);
        put_char(&b, '\n');
    }
    if (iop->module_count > n) {
        put_str(&b, "modules_not_listed=");
        put_dec(&b, iop->module_count - n);
        put_char(&b, '\n');
    }

    // Live blocks by address, so neighbours sit next to each other.
    n = 0;
    for (i = 0; i < HEAP_DIAG_BLOCKS; i++) {
        if (iop->blocks[i].addr == 0)
            continue;
        for (j = n; j > 0 && iop->blocks[order[j - 1]].addr > iop->blocks[i].addr; j--)
            order[j] = order[j - 1];
        order[j] = (unsigned char)i;
        n++;
    }
    for (j = 0; j < n; j++) {
        const heap_diag_block_t *blk = &iop->blocks[order[j]];
        unsigned int ff = h->tail_ff[order[j]];

        put_str(&b, "block addr=");
        put_hex(&b, blk->addr);
        put_field(&b, "size", blk->size);
        put_str(&b, " by=");
        put_where(&b, iop, blk->ra);
        put_field(&b, "seq", blk->seq);
        put_field(&b, "ff_after", ff);
        if (ff >= HEAP_DIAG_OVERRUN_MIN) {
            put_str(&b, " OVERRUN?");
            overruns++;
            if (worst == HEAP_DIAG_BLOCKS || ff > h->tail_ff[worst])
                worst = order[j];
        }
        put_char(&b, '\n');
    }

    for (i = 0; i < HEAP_DIAG_FAILS; i++) {
        const heap_diag_block_t *f = &iop->failed[i];

        if (f->size == 0 && f->ra == 0)
            continue;
        put_str(&b, "failed_alloc size=");
        put_dec(&b, f->size);
        put_field(&b, "mode", f->seq);
        put_str(&b, " by=");
        put_where(&b, iop, f->ra);
        put_char(&b, '\n');
    }

    put_str(&b, "heap_hint=");
    if (iop->fails != 0) {
        put_dec(&b, iop->fails);
        put_str(&b, " IOP allocation(s) failed: see failed_alloc for the mode and size (IOP RAM running out is one cause)");
    } else if (overruns != 0) {
        put_str(&b, "the ");
        put_dec(&b, iop->blocks[worst].size);
        put_str(&b, "-byte block from ");
        put_where(&b, iop, iop->blocks[worst].ra);
        put_str(&b, " is followed by ");
        put_dec(&b, h->tail_ff[worst]);
        put_str(&b, " bytes of 0xFF: likely written past its end (");
        put_dec(&b, overruns);
        put_str(&b, " block(s) marked OVERRUN?)");
    } else {
        put_str(&b, "no failed allocation and no 0xFF run after any tracked block");
    }
    put_char(&b, '\n');

    out[b.len] = '\0';
    return b.len;
}
