/*
  In-game freeze report (INGAMEDIAG flavour only).

  CDVDMAN keeps this record of its disc reads and VMC sector I/O while a game runs. When the
  player presses the IGR combo after a freeze, ee_core asks CDVDFSV for it over the existing IGR
  RPC (server 0x80000598, function FREEZE_DIAG_RPC_FNO) before the IOP is reset, and writes it
  into the report on the memory card. Shared by CDVDMAN, CDVDFSV and ee_core, so it uses plain
  C types only.

  Times are IOP system-clock ticks (36.864 MHz), split into hi/lo words.

  Version 2 adds the state of the drive lock, the game's read callback and CD streaming, plus a
  short history of the last disc commands (trace[]). It was added for the Tales of the Abyss
  field freeze (RiptOPL #185), where the game streams audio while it loads the field.
*/

#ifndef FREEZE_DIAG_H
#define FREEZE_DIAG_H

#define FREEZE_DIAG_MAGIC   0x5A524644 // "DFRZ"
#define FREEZE_DIAG_VERSION 2

// IGR RPC function numbers: "send the record" and "send the heap snapshot" (1 is the shutdown request).
#define FREEZE_DIAG_RPC_FNO      2
#define FREEZE_DIAG_HEAP_RPC_FNO 3 // answers with a heap_diag_snapshot_t (heap_diag.h)

#define FREEZE_DIAG_IOP_TICKS_PER_MS 36864

// Values for vmc_last_op / vmc_busy_op.
#define FREEZE_DIAG_VMC_READ  1
#define FREEZE_DIAG_VMC_WRITE 2

// trace[] entries: what the disc command was. Each is one letter, so the report can print it.
#define FREEZE_DIAG_EV_READ        'R' // the game's sceCdRead was accepted. arg bit 0: from an interrupt, bit 1: synchronous
#define FREEZE_DIAG_EV_BUSY        'B' // the game's sceCdRead was refused: a read held the drive. arg = refusals in a row
#define FREEZE_DIAG_EV_DONE_CB     'C' // a game read finished and its sceCdCallback was scheduled. arg = ms the read took
#define FREEZE_DIAG_EV_DONE_NOCB   'N' // a game read finished with no callback set. arg = ms
#define FREEZE_DIAG_EV_DONE_STREAM 'M' // a game read finished while streaming was on: the stream's callback ran, the game's did not. arg = ms
#define FREEZE_DIAG_EV_ERROR       'E' // a read failed. arg = error code
#define FREEZE_DIAG_EV_ST_START    'S' // sceCdStStart / sceCdStSeek. sectors = buffer sectors, arg = bank sectors
#define FREEZE_DIAG_EV_ST_STOP     'T' // sceCdStStop
#define FREEZE_DIAG_EV_ST_PAUSE    'P' // sceCdStPause
#define FREEZE_DIAG_EV_ST_RESUME   'U' // sceCdStResume
// A run of streaming work, merged into one entry. sectors = fills (low 12 bits) and fills refused
// because the drive was busy (high 12 bits); arg = sceCdStRead calls (high 16 bits) and calls that
// got fewer sectors than asked (low 16 bits).
#define FREEZE_DIAG_EV_ST_RUN      'F'

#define FREEZE_DIAG_TRACE 16

typedef struct freeze_diag_event
{
    unsigned int tick_lo; // when it happened (low word of the IOP clock; wraps every 116 s)
    unsigned int lsn;
    unsigned int code_sectors; // code << 24 | sector count (24 bits)
    unsigned int arg;
} freeze_diag_event_t;

typedef struct freeze_diag_iop
{
    unsigned int magic;
    unsigned int version;
    unsigned int now_hi, now_lo; // when the record was handed over

    // Disc reads, as the game sees them (cdvdman_read_sectors).
    unsigned int reads;
    unsigned int read_errors;
    unsigned int eom_hits; // reads refused or cut short by the media size
    unsigned int last_err;
    unsigned int last_lsn;
    unsigned int last_sectors;
    unsigned int media_lsn_count;
    unsigned int max_read_ms;
    unsigned int slow_reads; // device reads that took longer than one second

    // A device read that started and has not returned yet.
    unsigned int busy;
    unsigned int busy_lsn;
    unsigned int busy_sectors;
    unsigned int busy_start_hi, busy_start_lo;

    // VMC sector I/O through CDVDMAN's exports (MCEMU on BDM and MMCE).
    unsigned int vmc_ops;
    unsigned int vmc_last_op;
    unsigned int vmc_last_lba;
    unsigned int vmc_busy;
    unsigned int vmc_busy_op;
    unsigned int vmc_busy_lba;
    unsigned int vmc_busy_start_hi, vmc_busy_start_lo;

    // The drive lock and the game's callback (version 2).
    unsigned int sync_flag;     // 1 = a read holds the drive right now
    unsigned int cd_status;     // what sceCdStatus would answer
    unsigned int user_cb;       // address of the game's sceCdCallback, 0 if none
    unsigned int busy_refusals; // game reads refused because the drive was held
    unsigned int cb_runs;       // game reads that finished and scheduled the game's callback
    unsigned int cb_to_stream;  // game reads that finished while streaming was on: the game's callback did not run
    unsigned int sync_waiting;  // threads blocked in sceCdSync(0) right now

    // CD streaming (sceCdSt*), version 2.
    unsigned int st_stat;       // 1 = a stream is started
    unsigned int st_stm0_cb;    // 1 = the stream's read callback is installed
    unsigned int st_is_reading; // 1 = a stream fill read is in flight
    unsigned int st_lsn;        // next sector the stream fills from
    unsigned int st_bufmax;     // stream buffer, sectors
    unsigned int st_banksize;   // sectors per fill
    unsigned int st_streamed;   // sectors buffered and not yet read by the game
    unsigned int st_wr, st_rd;  // buffer positions
    unsigned int st_starts, st_stops, st_pauses, st_resumes;
    unsigned int st_fills, st_fill_refused;
    unsigned int st_reads, st_reads_ee, st_short_reads;
    unsigned int st_read_waiting; // a sceCdStRead is blocked waiting for data
    unsigned int st_wait_start_hi, st_wait_start_lo;

    unsigned int trace_count; // events recorded in total; the newest is trace[(trace_count - 1) % FREEZE_DIAG_TRACE]
    freeze_diag_event_t trace[FREEZE_DIAG_TRACE];

    unsigned int reserved[1]; // keeps the size a multiple of 16 for the SIF DMA
} freeze_diag_iop_t;

// sizeof is checked by the host test; the SIF DMA needs a multiple of 16.
#define FREEZE_DIAG_IOP_SIZE 480

#endif
