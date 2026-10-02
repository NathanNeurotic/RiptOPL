/*
  Copyright 2026, RiptOPL
  Licenced under Academic Free License version 3.0
  Review OpenPS2Loader README & LICENSE files for further details.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <kernel.h>

#include "include/opl.h"
#include "include/ioman.h" // LOG
#include "include/artindex.h"
#include "include/artindexint.h"

// See artindex.h for why this exists. Everything below is in service of one rule: an uncertain answer
// is always "go and look".

ArtIndex gArtIndex;

void artIndexSetOwnerThread(int threadId)
{
    gArtIndex.setOwnerThread(threadId);
}

int artIndexMayExist(const char *fullPath)
{
    return gArtIndex.mayExist(fullPath);
}

void artIndexInvalidate(void)
{
    gArtIndex.invalidate();
}

void artIndexDebug(int *dirsHeld, unsigned int *absentAnswered, unsigned int *sweepsFailed)
{
    gArtIndex.debug(dirsHeld, absentAnswered, sweepsFailed);
}

// FNV-1a over the UPPERCASED name. Case folding is not cosmetic: FAT and exFAT are
// case-insensitive-preserving, so readdir can hand back "Slus_123.45_COV.PNG" for a path we build as
// "SLUS_123.45_COV.png". Hashing them differently would make the index answer "absent" for a file
// that exists -- the one failure this must never have.
unsigned int ArtIndex::hashName(const char *s)
{
    unsigned int h = 2166136261u;

    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c >= 'a' && c <= 'z')
            c = (unsigned char)(c - 'a' + 'A');
        h ^= c;
        h *= 16777619u;
    }

    return h;
}

int ArtIndex::hashCmp(const void *a, const void *b)
{
    unsigned int x = *(const unsigned int *)a;
    unsigned int y = *(const unsigned int *)b;

    if (x < y)
        return -1;
    return (x > y) ? 1 : 0;
}

// Split a full path into its directory and its basename. Handles both "mass0:/ART/x.png" and the
// colon-only form "mass0:x.png", where the device token IS the directory.
int ArtIndex::splitPath(const char *fullPath, char *dirOut, int dirMax, const char **baseOut)
{
    const char *slash = strrchr(fullPath, '/');
    const char *cut;

    if (slash != NULL) {
        cut = slash;
    } else {
        const char *colon = strchr(fullPath, ':');
        if (colon == NULL)
            return 0;
        cut = colon + 1; // keep the colon on the directory side
    }

    int dirLen = (int)(cut - fullPath);
    if (dirLen <= 0 || dirLen >= dirMax)
        return 0;

    memcpy(dirOut, fullPath, dirLen);
    dirOut[dirLen] = '\0';

    *baseOut = (slash != NULL) ? slash + 1 : cut;
    if ((*baseOut)[0] == '\0')
        return 0;

    return 1;
}

// Sweep one directory into a fresh slot. Returns the slot, which may be marked invalid -- an invalid
// slot is still worth keeping, because it stops us re-sweeping a directory that cannot be indexed on
// every single probe.
ArtIndex::Dir *ArtIndex::build(const char *dir)
{
    Dir *slot = &slots_[next_];
    next_ = (next_ + 1) % kSlots;

    // Safe to free here and ONLY here: this runs on the owner thread, which is the only thread that
    // ever reads these buffers. invalidate() deliberately does not free for exactly this reason.
    if (slot->hashes != NULL) {
        unsigned int *old = slot->hashes;
        slot->hashes = NULL;
        slot->count = 0;
        slot->valid = 0;
        free(old);
    }

    snprintf(slot->dir, sizeof(slot->dir), "%s", dir);
    slot->valid = 0;
    slot->count = 0;

    // TWO PATH FORMS, because OPL builds art paths without a slash after the device token and not
    // every driver accepts that for a DIRECTORY. bdmDeviceRoot is "mass0:" (bdmsupport.c, no trailing
    // slash) and bdmGetImage appends "ART/<name>_COV", so the directory here is "mass0:ART" -- which
    // opendir rejected, silently leaving this whole module inert. Hardware read IX0/0 on a build where
    // a missing cover still cost 4395 ms in open() alone: the index was never built, for any
    // directory, and nothing said so.
    //
    // Try the path as given, then the "mass0:/ART" form. Costs one extra failed opendir once per
    // directory, only when the first form fails.
    DIR *d = opendir(dir);
    if (d == NULL) {
        const char *colon = strchr(dir, ':');
        if (colon != NULL && colon[1] != '\0' && colon[1] != '/') {
            char alt[kDirMax + 1];
            int head = (int)(colon - dir) + 1;
            if (snprintf(alt, sizeof(alt), "%.*s/%s", head, dir, colon + 1) < (int)sizeof(alt)) {
                d = opendir(alt);
                if (d != NULL)
                    LOG("ARTINDEX: '%s' listed as '%s'\n", dir, alt);
            }
        }
    }

    if (d == NULL) {
        // Could not list it -- a missing ART folder, or a device that went away. Not an error: the
        // slot stays invalid and every probe for this directory takes the ordinary path.
        sweepFailed_++;
        LOG("ARTINDEX: cannot list '%s' -- probes will use the device\n", dir);
        return slot;
    }

    unsigned int *hashes = (unsigned int *)malloc(kMaxEntries * sizeof(unsigned int));
    if (hashes == NULL) {
        closedir(d);
        return slot;
    }

    int n = 0;
    int overflowed = 0;
    struct dirent *e;

    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '\0' || e->d_name[0] == '.')
            continue;

        if (n >= kMaxEntries) {
            overflowed = 1;
            break;
        }

        hashes[n++] = hashName(e->d_name);
    }

    closedir(d);

    if (overflowed) {
        // A partial listing is the one thing that could make this answer "absent" about a file that
        // exists. Throw it away rather than hold a truthful-looking half-index.
        LOG("ARTINDEX: '%s' has more than %d entries -- not indexing\n", dir, kMaxEntries);
        free(hashes);
        return slot;
    }

    qsort(hashes, n, sizeof(unsigned int), &hashCmp);

    // Publish order matters even on one thread: valid LAST, so the slot is never readable with a
    // count that does not match its buffer.
    slot->hashes = hashes;
    slot->count = n;
    slot->valid = 1;

    LOG("ARTINDEX: '%s' indexed, %d entries\n", dir, n);
    return slot;
}

ArtIndex::Dir *ArtIndex::slotFor(const char *dir)
{
    int i;

    for (i = 0; i < kSlots; i++) {
        if (slots_[i].dir[0] != '\0' && strcmp(slots_[i].dir, dir) == 0)
            return &slots_[i];
    }

    return build(dir);
}

int ArtIndex::mayExist(const char *fullPath)
{
    char dir[kDirMax];
    const char *base = NULL;

    if (fullPath == NULL || fullPath[0] == '\0')
        return 1;

    // Reject SMB / backslash paths: splitPath expects '/' directory separators.
    // Returning 1 ("uncertain; perform real open") guarantees SMB art is never falsely declared absent.
    if (strchr(fullPath, '\\') != NULL || !strncmp(fullPath, "smb", 3))
        return 1;

    // Not the owner thread: never touch the slots at all. See ownerThread_.
    if (ownerThread_ < 0 || GetThreadId() != ownerThread_) {
        wrongThread_++; // HUD: if this climbs, confinement is rejecting the art worker itself
        return 1;
    }

    if (!splitPath(fullPath, dir, sizeof(dir), &base))
        return 1; // cannot even name the directory: go and look

    unsigned int genBefore = gen_;

    Dir *slot = slotFor(dir);
    if (slot == NULL || !slot->valid || slot->hashes == NULL)
        return 1;

    unsigned int want = hashName(base);
    int lo = 0, hi = slot->count - 1;

    while (lo <= hi) {
        int mid = lo + ((hi - lo) >> 1);
        unsigned int got = slot->hashes[mid];

        if (got == want)
            return 1; // a hash match is only a MAYBE -- collisions exist, so confirm on the device
        if (got < want)
            lo = mid + 1;
        else
            hi = mid - 1;
    }

    // Someone invalidated while we were searching, so this listing may describe a device that has
    // since changed. Downgrade to "go and look" rather than report an absence we are no longer sure
    // of -- a wrong "absent" hides art that exists, which is the one outcome this must never produce.
    if (gen_ != genBefore)
        return 1;

    absentAnswered_++;
    return 0; // not in the listing, and the listing was complete: genuinely not there
}

void ArtIndex::invalidate()
{
    int i;

    // Callable from ANY thread, so it deliberately does NOT free: the owner thread may be searching a
    // buffer right now, and releasing it underneath would be a use-after-free. Marking the slots
    // unusable is enough -- the next build on the owner thread reclaims the memory, and the bounded
    // three slots cap what can be held meanwhile.
    gen_++;

    for (i = 0; i < kSlots; i++) {
        slots_[i].valid = 0;
        slots_[i].dir[0] = '\0';
    }
}

void ArtIndex::debug(int *dirsHeld, unsigned int *absentAnswered, unsigned int *sweepsFailed)
{
    if (sweepsFailed)
        *sweepsFailed = sweepFailed_ + wrongThread_;

    if (dirsHeld) {
        int i, n = 0;
        for (i = 0; i < kSlots; i++) {
            if (slots_[i].valid)
                n++;
        }
        *dirsHeld = n;
    }

    if (absentAnswered)
        *absentAnswered = absentAnswered_;
}
