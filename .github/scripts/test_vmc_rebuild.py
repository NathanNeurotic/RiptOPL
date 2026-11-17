"""Rebuild a fragmented native VMC (#986) through the real code, with a fault at every step.

genvmc's real request, worker, copy/verify and abort code and the EE's real start/finish/swap
(src/sources/vmcrebuild.cpp, plus bdmlaunch.cpp's real native layout check, compiled as C) run over one in-memory filesystem
that can refuse a reservation, write short, read back different bytes, fail the layout check, fail or
lie about the card's removal and the rename, and take an abort mid-copy or mid-verify. No ".bak" is
ever made. Whatever happens, the card's bytes are complete under its own name or -- after a half-done
swap on FatFs -- under "<name>.new": the card is removed only once the copy is verified and passes
the layout check, and a copy is removed only while the card is in place. A ".new" left beside the
card by an interrupted rebuild is cleared at the start; a ".bak" left by an older build is never
touched and never in the way.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
genvmc = (root / 'modules/vmc/genvmc/genvmc.c').read_text(encoding='utf-8')
rebuild = (root / 'src/sources/vmcrebuild.cpp').read_text(encoding='utf-8')
bdm = (root / 'src/sources/bdmlaunch.cpp').read_text(encoding='utf-8')


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}\n', start) + 3]


start = genvmc.index('// SONY superblock magic')
iop = genvmc[start:genvmc.index('static int genvmc_io_sema', start)]
start = genvmc.index('static int genvmc_io_sema')
iop += genvmc[start:genvmc.index('\n//----', start)] + '\n'
for signature in ('static int mc_writecluster(', 'static int vmc_open_contiguous(', 'static int vmc_reserve_pfs(',
                  'static int vmc_rebuild(char *filename'):
    iop += function(genvmc, signature)
iop += r'''
static int vmc_mcformat(char *filename, int size_kb, int blocksize, int *progress, char *msg)
{ (void)filename; (void)size_kb; (void)blocksize; (void)progress; (void)msg; assert(!"a rebuild never formats"); return -1; }
static int vmc_mccopy(char *filename, int slot, int *progress, char *msg)
{ (void)filename; (void)slot; (void)progress; (void)msg; assert(!"a rebuild never reads a memory card"); return -1; }
'''
iop += genvmc[genvmc.index('static int genvmc_init(iop_device_t *dev)\n{'):genvmc.index('int _start(')]
ee = rebuild[rebuild.index('static struct\n{'):]
layout = function(bdm, 'int bdmCheckNativeVmcLayout(int iop_fd')

program = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "genvmc.h"
#include "vmc_alloc.h"
#include "nativevmc.h"
#include "vmcrebuild.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef uint64_t u64;
#define O_RDONLY 0
#define O_RDWR 2
#define O_CREAT 0x200
#define O_TRUNC 0x400
#define FIO_O_EXCL 0x800
#define SEEK_SET 0
#define SEEK_END 2
#define PIOCALLOC 0x7001
#define DPRINTF(...) ((void)0)
#define LOG(...) ((void)0)
#define MODNAME "genvmc"
#define TH_C 0x02000000
#define IOP_MUTEX_UNLOCKED 1
#define CARD (8 * 1048576)

typedef struct iop_device { int unused; } iop_device_t;
typedef struct iop_file { int unused; } iop_file_t;
typedef struct { unsigned int attr, option; void *thread; unsigned int stacksize, priority; } iop_thread_t;
typedef struct { unsigned int mode; char name[256]; } iox_dirent_t;

static int failures;
#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL [%s]: %s\n", scenario, what); failures++; } } while (0)
static const char *scenario = "";

/* ---- one in-memory filesystem, both sides of the RPC ---- */
typedef struct { char name[64]; unsigned char *data; int size, exists, fragments, reserved; } vfile;
static vfile files[6];
static struct { int file, pos, used; } fds[16];
static int pfs;                                   /* PFS rename replaces its target; FatFs refuses */
static int readsOfCard, readFailAt, writes, shortWriteAt, abortAtWrite, abortAtVerifyRead, corruptCopy;
static int capsResult, reserveResult, verifyResult, piocallocResult, closeFailCopy, unreservedFragments;
static int renames, renameFail[4], renameLie[4], dirFail, dirOpens, syncs, syncResult, mapOverride, cardRemoved, backupRemoved, workerRuns, unlinkFail, unlinkLie;
static unsigned char original[CARD];

static vfile *lookup(const char *name)
{
    for (int i = 0; i < 6; i++)
        if (files[i].exists && !strcmp(files[i].name, name))
            return &files[i];
    return NULL;
}
static vfile *make(const char *name)
{
    for (int i = 0; i < 6; i++)
        if (!files[i].exists) {
            vfile *f = &files[i];
            memset(f, 0, sizeof(*f));
            snprintf(f->name, sizeof(f->name), "%s", name);
            f->data = calloc(1, 64 * 1048576);
            f->exists = 1;
            return f;
        }
    assert(!"filesystem full");
    return NULL;
}
static void drop(vfile *f)
{
    size_t len = strlen(f->name);
    if (len > 4 && !strcmp(f->name + len - 4, ".bin"))
        cardRemoved++; /* only once the copy is verified */
    if (len > 4 && !strcmp(f->name + len - 4, ".bak"))
        backupRemoved++; /* never allowed */
    free(f->data);
    memset(f, 0, sizeof(*f));
}
static int newfd(vfile *f) { for (int i = 0; i < 16; i++) if (!fds[i].used) { fds[i].file = f - files; fds[i].pos = 0; fds[i].used = 1; return i; } return -EMFILE; }
static vfile *fdfile(int fd) { assert(fd >= 0 && fd < 16 && fds[fd].used); return &files[fds[fd].file]; }
static int isCopy(vfile *f) { size_t n = strlen(f->name); return n > 4 && !strcmp(f->name + n - 4, ".new"); }

/* ---- IOP iomanX ---- */
static int genvmc_devctl(iop_file_t *f, const char *name, int cmd, void *args, unsigned int arglen, void *buf, unsigned int buflen);
static int iop_open(const char *name, int flags)
{
    vfile *f = lookup(name);
    if (f && (flags & O_CREAT) && (flags & FIO_O_EXCL))
        return -EEXIST;
    if (!f) {
        if (!(flags & O_CREAT))
            return -ENOENT;
        f = make(name);
        f->fragments = unreservedFragments;
    }
    if (flags & O_TRUNC)
        f->size = 0;
    return newfd(f);
}
static int iop_close(int fd)
{
    vfile *f = fdfile(fd);
    fds[fd].used = 0;
    return isCopy(f) && closeFailCopy ? -EIO : 0;
}
static int iop_lseek(int fd, int offset, int whence)
{
    vfile *f = fdfile(fd);
    fds[fd].pos = whence == SEEK_END ? f->size + offset : offset;
    return fds[fd].pos;
}
static int iop_read(int fd, void *buf, int n)
{
    vfile *f = fdfile(fd);
    if (!isCopy(f) && ++readsOfCard == readFailAt)
        return -EIO;
    if (isCopy(f) && abortAtVerifyRead && fds[fd].pos == abortAtVerifyRead)
        genvmc_devctl(NULL, "genvmc:", GENVMC_DEVCTL_ABORT, NULL, 0, NULL, 0);
    if (n > f->size - fds[fd].pos)
        n = f->size - fds[fd].pos;
    memcpy(buf, f->data + fds[fd].pos, n);
    if (isCopy(f) && corruptCopy && fds[fd].pos <= corruptCopy && corruptCopy < fds[fd].pos + n)
        ((unsigned char *)buf)[corruptCopy - fds[fd].pos] ^= 0x40;
    fds[fd].pos += n;
    return n;
}
static int iop_write(int fd, const void *buf, int n)
{
    vfile *f = fdfile(fd);
    if (++writes == abortAtWrite)
        genvmc_devctl(NULL, "genvmc:", GENVMC_DEVCTL_ABORT, NULL, 0, NULL, 0);
    if (writes == shortWriteAt)
        n--;
    memcpy(f->data + fds[fd].pos, buf, n);
    fds[fd].pos += n;
    if (fds[fd].pos > f->size)
        f->size = fds[fd].pos;
    return n;
}
static int iop_remove(const char *name)
{
    vfile *f = lookup(name);
    if (!f)
        return -ENOENT;
    drop(f);
    return 0;
}
static int iop_devctl(const char *name, unsigned int cmd, void *in, int insize, void *out, int outsize)
{
    (void)name; (void)in; (void)out;
    assert(cmd == VMC_ALLOC_CAPS && insize == 0 && outsize == 0);
    return capsResult;
}
static int iop_ioctl2(int fd, unsigned int cmd, void *in, unsigned int insize, void *out, int outsize)
{
    vfile *f = fdfile(fd);
    assert(out == NULL && outsize == 0);
    if (cmd == PIOCALLOC) {
        assert(insize == sizeof(int) && *(int *)in == CARD && f->size == 0);
        if (piocallocResult >= 0)
            f->fragments = 1, f->reserved = 1;
        return piocallocResult;
    }
    vmc_alloc_request_t *request = in;
    assert(insize == sizeof(*request) && request->version == VMC_ALLOC_V1 && request->bytes == CARD);
    if (cmd == VMC_ALLOC_RESERVE) {
        if (reserveResult != VMC_ALLOC_V1)
            return reserveResult;
        f->size = request->bytes, f->fragments = 1, f->reserved = 1;
        return VMC_ALLOC_V1;
    }
    assert(cmd == VMC_ALLOC_VERIFY);
    if (verifyResult != VMC_ALLOC_V1)
        return verifyResult;
    return f->size == (int)request->bytes && f->fragments == 1 ? VMC_ALLOC_V1 : -EOVERFLOW;
}

/* ---- IOP kernel: semaphores and a worker the test schedules ---- */
static int semTokens[8], semCount;
static int CreateMutex(int tokens) { semTokens[semCount] = tokens; return semCount++; }
static int DeleteSema(int id) { (void)id; return 0; }
static int WaitSema(int id) { assert(semTokens[id] > 0 && "a wait no one can satisfy"); semTokens[id]--; return 0; }
static int SignalSema(int id) { semTokens[id]++; return 0; }
static void (*pending)(void *);
static int CreateThread(iop_thread_t *t) { pending = (void (*)(void *))t->thread; return 7; }
static int StartThread(int thid, void *arg) { (void)thid; (void)arg; return 0; }
static int DeleteThread(int thid) { (void)thid; pending = NULL; return 0; }
static void ExitDeleteThread(void) {}
static void runWorker(void) { void (*entry)(void *) = pending; pending = NULL; if (entry) { workerRuns++; entry(NULL); } }

#define open iop_open
#define close iop_close
#define lseek iop_lseek
#define read iop_read
#define write iop_write
#define remove iop_remove
#define devctl iop_devctl
#define ioctl2 iop_ioctl2
@IOP@
#undef open
#undef close
#undef lseek
#undef read
#undef write
#undef remove
#undef devctl
#undef ioctl2

/* ---- EE: fileXio over the same filesystem ---- */
#define USBMASS_IOCTL_GET_LBA 1
#define USBMASS_IOCTL_GET_FRAGLIST 2
typedef struct { u64 sector; u32 count; } bd_fragment_t;
static int ee_open(const char *name, int flags) { assert(flags == O_RDONLY); vfile *f = lookup(name); return f ? newfd(f) : -ENOENT; }
static int ee_close(int fd) { fdfile(fd); fds[fd].used = 0; return 0; }
static int ps2sdk_get_iop_fd(int fd) { return fd; }
static int fileXioIoctl2(int fd, int cmd, void *in, unsigned int insize, void *out, unsigned int outsize)
{
    vfile *f = fdfile(fd);
    (void)in; (void)insize;
    if (cmd == USBMASS_IOCTL_GET_LBA) {
        assert(outsize == sizeof(u64));
        *(u64 *)out = 0x800;
        return 0;
    }
    assert(cmd == USBMASS_IOCTL_GET_FRAGLIST);
    if (f->fragments == 1 && outsize >= sizeof(bd_fragment_t)) {
        ((bd_fragment_t *)out)->sector = 0x800;
        ((bd_fragment_t *)out)->count = f->size / 512;
    }
    return f->fragments;
}
@LAYOUT@
static int hddVMCMapFits(const char *path)
{
    vfile *f = lookup(path);
    if (mapOverride)
        return mapOverride;
    return f == NULL ? -1 : f->fragments <= 1;
}
static int ee_rename(const char *from, const char *to)
{
    int step = renames++;
    vfile *f = lookup(from), *target = lookup(to);
    if (renameFail[step] || !f)
        return -1;
    if (target) {
        if (!pfs)
            return -1; /* FatFs: FR_EXIST */
        drop(target);  /* PFS replaces the target */
    }
    snprintf(f->name, sizeof(f->name), "%s", to);
    return renameLie[step] ? -1 : 0;
}
static int ee_unlink(const char *name)
{
    size_t len = strlen(name);
    if (len > 4 && !strcmp(name + len - 4, ".bin")) {
        if (unlinkFail)
            return -1;
        if (unlinkLie) {
            iop_remove(name);
            return -1;
        }
    }
    return iop_remove(name);
}
static int dirPos;
static char dirName[300];
static int fileXioDopen(const char *folder) { dirOpens++; if (dirFail) return -EIO; snprintf(dirName, sizeof(dirName), "%s/", folder); dirPos = 0; return 3; }
static int fileXioDread(int dir, iox_dirent_t *entry)
{
    assert(dir == 3);
    while (dirPos < 6) {
        vfile *f = &files[dirPos++];
        if (f->exists && !strncmp(f->name, dirName, strlen(dirName))) {
            /* FatFs without LFN answers in upper case: the match must not depend on it. */
            snprintf(entry->name, sizeof(entry->name), "%s", f->name + strlen(dirName));
            for (char *c = entry->name; *c; c++)
                if (*c >= 'a' && *c <= 'z') *c -= 32;
            return 1;
        }
    }
    return 0;
}
static int fileXioDclose(int dir) { assert(dir == 3); return 0; }
static int fileXioSync(const char *device, int flag) { assert(!strcmp(device, "pfs0:") && flag == 0); syncs++; return syncResult; }
static int fileXioDevctl(const char *name, int cmd, void *in, unsigned int insize, void *out, unsigned int outsize)
{
    assert(!strcmp(name, "genvmc:"));
    return genvmc_devctl(NULL, name, cmd, in, insize, out, outsize);
}
#define open ee_open
#define close ee_close
#define rename ee_rename
#define unlink ee_unlink
@EE@
#undef open
#undef close
#undef rename
#undef unlink

/* ---- scenarios ---- */
static char card[64], copyName[64], backupName[64];
static void reset(const char *name, int onPfs)
{
    for (int i = 0; i < 6; i++)
        if (files[i].exists) { free(files[i].data); memset(&files[i], 0, sizeof(files[i])); }
    memset(fds, 0, sizeof(fds));
    memset(&gVMCRebuild, 0, sizeof(gVMCRebuild));
    memset(&genvmc_stats, 0, sizeof(genvmc_stats));
    memset(renameFail, 0, sizeof(renameFail));
    memset(renameLie, 0, sizeof(renameLie));
    scenario = name;
    pfs = onPfs;
    readsOfCard = readFailAt = writes = shortWriteAt = abortAtWrite = abortAtVerifyRead = corruptCopy = 0;
    capsResult = reserveResult = verifyResult = VMC_ALLOC_V1;
    piocallocResult = closeFailCopy = renames = dirFail = dirOpens = syncs = syncResult = mapOverride = cardRemoved = backupRemoved = workerRuns = unlinkFail = unlinkLie = 0;
    unreservedFragments = 7; /* written without a reservation, a file lands in pieces */
    semCount = 0;
    pending = NULL;
    genvmc_init(NULL);
    snprintf(card, sizeof(card), "%s:VMC/card.bin", onPfs ? "pfs0" : "mass0");
    snprintf(copyName, sizeof(copyName), "%s:VMC/card.new", onPfs ? "pfs0" : "mass0");
    snprintf(backupName, sizeof(backupName), "%s:VMC/card.bak", onPfs ? "pfs0" : "mass0");
    vfile *f = make(card);
    for (int i = 0; i < CARD; i++)
        original[i] = (unsigned char)(i * 7 + (i >> 9));
    memcpy(f->data, original, CARD);
    f->size = CARD;
    f->fragments = 5;
}
static int same(const char *name)
{
    vfile *f = lookup(name);
    return f != NULL && f->size == CARD && memcmp(f->data, original, CARD) == 0;
}
static int status(void)
{
    statusVMCparam_t s;
    memset(&s, 0xff, sizeof(s));
    assert(genvmc_devctl(NULL, "genvmc:", GENVMC_DEVCTL_STATUS, NULL, 0, &s, sizeof(s)) == 0);
    assert(s.VMC_status == GENVMC_STAT_AVAIL);
    return s.VMC_error;
}
static int run(int sizeMb)
{
    int started = vmcRebuildStart(card, sizeMb);
    if (started != 0)
        return started;
    runWorker();
    return vmcRebuildFinish(status());
}
/* Nothing changed: the card is where it was, with every byte, and nothing else is left over. */
static void unchanged(void)
{
    CHECK(same(card), "the card keeps every byte under its own name");
    CHECK(lookup(card)->fragments == 5, "the card is the original file");
    CHECK(lookup(copyName) == NULL, "no copy is left behind");
    CHECK(lookup(backupName) == NULL, "no backup is made");
    CHECK(cardRemoved == 0 && backupRemoved == 0, "no .bin or .bak was ever removed");
}

int main(void)
{
    for (int onPfs = 0; onPfs < 2; onPfs++) {
        reset(onPfs ? "pfs_success" : "bdm_success", onPfs);
        CHECK(vmcRebuildLayout(card, 8) == 0, "the fragmented card is offered for a rebuild");
        CHECK(run(8) == VMC_REBUILD_DONE, "the rebuild completes");
        CHECK(same(card) && lookup(card)->fragments == 1 && lookup(card)->reserved, "the card is now the reserved one-piece copy, byte for byte");
        CHECK(vmcRebuildLayout(card, 8) == 1, "and the launch check maps it");
        CHECK(lookup(backupName) == NULL && backupRemoved == 0, "no .bak is ever made");
        CHECK(lookup(copyName) == NULL && cardRemoved == 1 && renames == 1, "no .new remains; the old card went once, after the checks");
        CHECK(onPfs ? syncs > 0 : syncs == 0, "the PFS rename is committed with a sync");

        reset("reservation_refused", onPfs);
        if (onPfs) piocallocResult = -ENOSPC; else reserveResult = -ENOSPC;
        CHECK(run(8) == VMC_REBUILD_NO_ROOM, "no unbroken space is reported as such");
        CHECK(genvmc_stats.VMC_error == -112 && writes == 0, "the job stops at the refusal, before copying anything");
        unchanged();

        /* Someone else's .new turns up after the start's check: genvmc never truncates or removes it. */
        reset("foreign_copy_appears", onPfs);
        CHECK(vmcRebuildStart(card, 8) == 0, "started");
        make(copyName)->size = 33;
        runWorker();
        CHECK(vmcRebuildFinish(status()) == VMC_REBUILD_FAILED && genvmc_stats.VMC_error == -101, "exclusive creation fails");
        CHECK(same(card) && lookup(copyName) != NULL && lookup(copyName)->size == 33 && cardRemoved == 0, "the foreign file is left exactly as it was");
        reset("foreign_copy_and_size_mismatch", onPfs);
        CHECK(vmcRebuildStart(card, 16) == 0, "started");
        make(copyName)->size = 33;
        runWorker();
        CHECK(vmcRebuildFinish(status()) == VMC_REBUILD_FAILED && genvmc_stats.VMC_error == -121, "a failure before the copy is created");
        CHECK(same(card) && lookup(copyName) != NULL && lookup(copyName)->size == 33, "removes nothing it did not create");

        reset("copy_short_write", onPfs);
        shortWriteAt = 3;
        CHECK(run(8) == VMC_REBUILD_FAILED && genvmc_stats.VMC_error == -125, "a short write fails the copy");
        unchanged();

        reset("card_read_error", onPfs);
        readFailAt = 2;
        CHECK(run(8) == VMC_REBUILD_FAILED && genvmc_stats.VMC_error == -122, "a card read error fails the copy");
        unchanged();

        reset("verify_mismatch", onPfs);
        corruptCopy = 5 * 1048576 + 123;
        CHECK(run(8) == VMC_REBUILD_FAILED && genvmc_stats.VMC_error == -123, "a copy that reads back different is refused");
        unchanged();

        reset("copy_close_fails", onPfs);
        closeFailCopy = 1;
        CHECK(run(8) == VMC_REBUILD_FAILED && genvmc_stats.VMC_error == -114, "a copy that cannot be closed is refused");
        unchanged();

        reset("abort_during_copy", onPfs);
        abortAtWrite = 4;
        CHECK(run(8) == VMC_REBUILD_ABORTED && genvmc_stats.VMC_error == -1000, "abort during the copy");
        unchanged();

        reset("abort_during_verify", onPfs);
        abortAtVerifyRead = 3 * 1048576;
        CHECK(run(8) == VMC_REBUILD_ABORTED && genvmc_stats.VMC_error == -1000, "abort during the verify");
        unchanged();

        reset("size_mismatch", onPfs);
        CHECK(run(16) == VMC_REBUILD_FAILED && genvmc_stats.VMC_error == -121, "a card of another size is not copied");
        unchanged();

        reset("copy_layout_fails", onPfs);
        if (onPfs) {
            /* PFS took the reservation but still split the copy: its map is too long for mcemu. */
            CHECK(vmcRebuildStart(card, 8) == 0, "started");
            runWorker();
            lookup(copyName)->fragments = 40;
            CHECK(vmcRebuildFinish(status()) == VMC_REBUILD_NO_ROOM, "a copy whose PFS map is too long is not swapped in");
        } else {
            verifyResult = -EOVERFLOW;
            CHECK(run(8) == VMC_REBUILD_FAILED && genvmc_stats.VMC_error == -113, "a copy that fails the driver's one-extent check is refused");
        }
        unchanged();

        reset("copy_layout_unknown", onPfs);
        CHECK(vmcRebuildStart(card, 8) == 0, "started");
        runWorker();
        mapOverride = -1;
        if (!onPfs)
            lookup(copyName)->fragments = -5; /* the fragment query itself fails */
        CHECK(vmcRebuildFinish(status()) == VMC_REBUILD_FAILED, "a copy whose layout cannot be checked is not swapped in");
        unchanged();

        reset("rename_fails", onPfs);
        renameFail[0] = 1;
        if (onPfs) {
            CHECK(run(8) == VMC_REBUILD_FAILED, "the copy cannot replace the card");
            unchanged();
        } else {
            CHECK(run(8) == VMC_REBUILD_STUCK, "the card is gone and the copy could not take its name");
            CHECK(lookup(card) == NULL && same(copyName) && lookup(backupName) == NULL, "every save is in the .new copy, which is kept");
        }

        reset("rename_lies", onPfs);
        renameLie[0] = 1;
        CHECK(run(8) == VMC_REBUILD_DONE, "a rename that happened is recognised");
        CHECK(same(card) && lookup(card)->fragments == 1 && lookup(backupName) == NULL && lookup(copyName) == NULL && cardRemoved == 1,
              "and the swap stands, with nothing left over");

        reset("backup_already_there", onPfs);
        make(backupName)->size = 99;
        CHECK(run(8) == VMC_REBUILD_DONE, "an old .bak beside the card does not block the rebuild");
        CHECK(same(card) && lookup(card)->fragments == 1 && lookup(backupName)->size == 99 && backupRemoved == 0 && lookup(copyName) == NULL,
              "and is left exactly as it was");

        reset("copy_already_there", onPfs);
        make(copyName)->size = 77;
        CHECK(run(8) == VMC_REBUILD_DONE, "a .new left by an interrupted rebuild does not block the next one");
        CHECK(same(card) && lookup(card)->fragments == 1 && lookup(copyName) == NULL && lookup(backupName) == NULL, "nothing but the card remains");

        reset("card_missing", onPfs);
        make(copyName)->size = 77;
        drop(lookup(card));
        CHECK(vmcRebuildStart(card, 8) == VMC_REBUILD_FAILED && workerRuns == 0, "without the card nothing is cleared or started");
        CHECK(lookup(copyName)->size == 77, "a lone .new is never removed");

        reset("folder_unreadable", onPfs);
        dirFail = 1;
        CHECK(run(8) == VMC_REBUILD_FAILED && workerRuns == 0, "a folder that cannot be read stops the rebuild");
        unchanged();
    }

    reset("fat_card_removal_fails", 0);
    unlinkFail = 1;
    CHECK(run(8) == VMC_REBUILD_FAILED && renames == 0, "a card that cannot be removed is left as it is");
    unchanged();
    reset("fat_card_removal_lies", 0);
    unlinkLie = 1;
    CHECK(run(8) == VMC_REBUILD_STUCK && renames == 0, "a removal that reported failure but happened is not trusted");
    CHECK(lookup(card) == NULL && same(copyName), "and the verified copy is kept");

    reset("pfs_swap_not_committed", 1);
    syncResult = -EIO;
    CHECK(run(8) == VMC_REBUILD_UNSYNCED, "a swap PFS could not commit is not reported as rebuilt");
    CHECK(same(card) && lookup(copyName) == NULL && lookup(backupName) == NULL, "the card holds every byte");
    reset("pfs_late_rename_not_committed", 1);
    renameLie[0] = 1;
    syncResult = -EIO;
    CHECK(run(8) == VMC_REBUILD_UNSYNCED, "nor is a rename that reported failure but happened");
    CHECK(same(card) && lookup(backupName) == NULL, "the card holds every byte");

    reset("driver_without_safe_allocation", 0);
    capsResult = 0;
    CHECK(run(8) == VMC_REBUILD_FAILED && genvmc_stats.VMC_error == -111, "an old driver is refused before anything is created");
    unchanged();

    reset("one_rebuild_at_a_time", 0);
    CHECK(vmcRebuildStart(card, 8) == 0, "started");
    CHECK(vmcRebuildStart(card, 8) == VMC_REBUILD_FAILED, "a second rebuild is refused while one is pending");
    runWorker();
    CHECK(vmcRebuildFinish(status()) == VMC_REBUILD_DONE, "the first completes");
    CHECK(vmcRebuildFinish(0) == VMC_REBUILD_FAILED, "finishing twice does nothing");

    reset("genvmc_refuses_unsafe_requests", 0);
    {
        createVMCparam_t p;
        memset(&p, 0, sizeof(p));
        p.VMC_size_mb = 8; p.VMC_blocksize = 16; p.VMC_thread_priority = 0x0f; p.VMC_card_slot = -1;
        strcpy(p.VMC_filename, "mass0:VMC/card.bin");
        p.VMC_flags = VMC_CREATE_REBUILD | VMC_CREATE_CONTIGUOUS;
        CHECK(genvmc_devctl(NULL, "genvmc:", GENVMC_DEVCTL_CREATE_VMC, &p, sizeof(p), NULL, 0) < 0, "a rebuild can never write a .bin");
        strcpy(p.VMC_filename, "mass0:VMC/card.new");
        p.VMC_flags = VMC_CREATE_REBUILD;
        CHECK(genvmc_devctl(NULL, "genvmc:", GENVMC_DEVCTL_CREATE_VMC, &p, sizeof(p), NULL, 0) < 0, "a rebuild without a reservation is refused");
        p.VMC_flags = VMC_CREATE_REBUILD | VMC_CREATE_CONTIGUOUS | VMC_CREATE_PFS_RESERVE;
        CHECK(genvmc_devctl(NULL, "genvmc:", GENVMC_DEVCTL_CREATE_VMC, &p, sizeof(p), NULL, 0) < 0, "and with both");
        p.VMC_flags = VMC_CREATE_REBUILD | VMC_CREATE_CONTIGUOUS;
        p.VMC_card_slot = 0;
        CHECK(genvmc_devctl(NULL, "genvmc:", GENVMC_DEVCTL_CREATE_VMC, &p, sizeof(p), NULL, 0) < 0, "and from a memory card slot");
        p.VMC_card_slot = -1; p.VMC_size_mb = 12;
        CHECK(genvmc_devctl(NULL, "genvmc:", GENVMC_DEVCTL_CREATE_VMC, &p, sizeof(p), NULL, 0) < 0, "and of an odd size");
        CHECK(pending == NULL && lookup(copyName) == NULL, "none of them starts a job");
        unchanged();
    }

    if (failures)
        return 1;
    puts("VMC rebuild: verified copy, layout-checked swap, every fault and abort leaves the card intact (BDM + PFS)");
    return 0;
}
'''.replace('@IOP@', iop).replace('@EE@', ee).replace('@LAYOUT@', layout)

with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    cfile, exe = temp / 'rebuild.c', temp / 'rebuild.exe'
    cfile.write_text(program, encoding='utf-8')
    subprocess.run(['gcc', '-std=gnu99', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-parameter',
                    '-Wno-sign-compare', '-Wno-unused-const-variable', '-I', str(root / 'include'), '-I', str(root / 'modules/vmc/genvmc'),
                    str(cfile), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=120)

# The dialog (src/ui/guigame.cpp): Rebuild is offered only for a valid native-core card on BDM or the
# HDD with a definite "does not map" verdict, never while validating, and Delete waits for any job.
ui = (root / 'src/ui/guigame.cpp').read_text(encoding='utf-8')
offer = function(ui, 'static void guiGameRefreshVMCRebuild(')
for clause in ('sizeMb == 8 || sizeMb == 16 || sizeMb == 32 || sizeMb == 64', 'gEnableWrite',
               'guiGameResolveEffectiveCore(configSet, support, id) != 1', 'sourceMode == HDD_MODE',
               'sourceMode >= BDM_MODE && sourceMode <= BDM_MODE_LAST', 'vmcRebuildLayout(path, sizeMb) == 0'):
    assert clause in offer, clause
assert 'guiGameRefreshVMCRebuild(id, support, configSet, vmc, validate ? 0 : size)' in ui
assert 'result == VMC_BUTTON_DELETE && vmc_operation == OPERATION_CREATE' in ui
assert 'vmcRebuildFinish(vmc_status.VMC_error)' in ui
print('VMC rebuild dialog: offered only for a mappable-by-copy native card; Delete waits for the job')
