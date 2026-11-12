/* Check for Updates: the portable half (src/platform/selfupdate.c, host-tested) and the menu entry
   (src/ui/selfupdate.cpp). Built only into standard builds that have a release channel
   (RIPTOPL_UPDATER, tools/build_edition.py update_channel). */
#ifndef __SELFUPDATE_H__
#define __SELFUPDATE_H__

#ifdef __cplusplus
extern "C" {
#endif

#define SU_HOST_MAX 64

/* The build number in a version string: 4256 in "v1.2.0-Beta-4256-6faabe3-PS2BUILD-Korium-GCC" and in
   "v1.2.0-4256", the first all-digit part after the dotted version. -1 when there is none. */
int suRevision(const char *version);

/* Reads a release's SHA256SUMS.txt: elf's digest (exact name, "<64 hex>  <name>" lines) and the
   release's build number, taken from its versioned archive names (...-v1.2.0-Beta-4256.zip, every one
   the same). 0, or -1 when elf is not listed, a digest is malformed or the archives disagree. */
int suParseSums(const char *text, int len, const char *elf, unsigned char sha[32], int *revision);

/* Splits an https:// URL whose host is one of GitHub's release hosts. Returns the path (starting
   with '/') inside url with the host copied to host, or NULL for any other URL. */
const char *suSplitUrl(const char *url, char *host, int host_size);

/* dir + "/" + name + suffix (dir "mc0:" or "mass0:" gets no extra slash). 0, or -1 if it does not fit. */
int suJoin(char *out, int size, const char *dir, const char *name, const char *suffix);

/* 1 when the first bytes are a 32-bit little-endian MIPS executable ELF header. */
int suLooksLikeElf(const unsigned char *head, int len);

#ifdef __cplusplus
}

/* The main menu's Check for Updates (GUI thread). */
void selfUpdateMenu(void);
#endif

#endif
