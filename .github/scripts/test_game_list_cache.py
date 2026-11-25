"""Cache Game List (game_list_cache) on the devices that keep CD/games.bin and DVD/games.bin.

With a device's bit on, its first PS2 list of the session must come from those files alone, without
opening the CD or DVD folder, and must be the list a scan of the unchanged folders gives. This compiles
the real span of src/sources/supportbase.cpp (sbListCacheTrust through sbReadList) on the host against a
scripted device and checks:

- with the bit off, or once the page has had its one cached list, sbReadList opens both folders;
- with the bit on, the first sbReadList opens neither folder and returns the same rows, in the same
  order, as the scan that wrote the games.bin files;
- each page (mode) gets one cached list, even when two pages share a device bit;
- folder browsing (a non-NULL sub with folder browsing on) always reads the folders;
- a CD folder with no games.bin is read, while DVD still comes from its games.bin;
- a folder read as empty deletes its games.bin, so an emptied folder never comes back from it.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
support = (root / 'src/sources/supportbase.cpp').read_text(encoding='utf-8').replace('\r\n', '\n')
failures = []

start = support.find('// Cache Game List: set by sbListCacheFirstRead')
update_start = support.find('static int updateISOGameList(')
end_match = re.search(r'^int sbReadList\([^;{]*\)\s*\{', support, re.M)
if start < 0 or update_start < 0 or end_match is None:
    print('FAIL: supportbase.cpp spans not found')
    sys.exit(1)
first_read = support[start:support.index('\n}', start) + 3]
update_fn = support[update_start:support.index('\n}', update_start) + 3]
span_start = support.find('static int sbReadListErrno;')
scan_span = support[span_start:support.index('\n}', end_match.start())] + '\n}\n'

HARNESS = r'''
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define LOG(...) ((void)0)
#define O_RDONLY 0
#define FOLDER_SUB_MAX 128
#define ISO_GAME_NAME_MAX 160
#define ISO_GAME_EXTENSION_MAX 4
#define UL_GAME_NAME_MAX 32
#define GAME_STARTUP_MAX 12
#define GAME_FORMAT_USBLD 0
#define GAME_FORMAT_OLD_ISO 1
#define GAME_FORMAT_ISO 2
#define GAME_FORMAT_FOLDER 3
#define SCECdPS2CD 0x12
#define SCECdPS2DVD 0x14
#define FIO_MT_RDONLY 0

typedef struct
{
    char name[ISO_GAME_NAME_MAX + 1];
    char startup[GAME_STARTUP_MAX + 1];
    char extension[ISO_GAME_EXTENSION_MAX + 1];
    unsigned char parts;
    unsigned char media;
    unsigned char format;
    unsigned int sizeMB;
} base_game_info_t;

typedef struct
{
    char name[UL_GAME_NAME_MAX];
    char startup[GAME_STARTUP_MAX];
    unsigned char parts;
    unsigned char media;
} USBExtreme_game_entry_t;

struct game_list_t
{
    base_game_info_t gameinfo;
    struct game_list_t *next;
};

struct game_cache_list
{
    unsigned int count;
    base_game_info_t *games;
};

static int gGameListCache;
static int gEnableFolderNav;
static int sbListCacheReadOnly;

/* Scripted device: two folders of ISO names, and the games.bin each folder holds (written by the
   real updateISOGameList through the FILE stubs below). */
#define MAXF 16
static const char *cdNames[MAXF], *dvdNames[MAXF];
static int cdCount, dvdCount, opens, dirPos;
static const char **dirNames;
static int dirCount;
static base_game_info_t binCd[64], binDvd[64];
static int binCdCount = -1, binDvdCount = -1; /* -1: no games.bin */
static int removes;

static struct dirent fakeEnt;
static DIR *stub_opendir(const char *path)
{
    if (strchr(path, '/') != NULL)
        return NULL; /* a folder-browsing probe of a file: not a folder, and not counted */
    opens++;
    if (strstr(path, "DVD")) { dirNames = dvdNames; dirCount = dvdCount; }
    else if (strstr(path, "CD")) { dirNames = cdNames; dirCount = cdCount; }
    else return NULL;
    dirPos = 0;
    return (DIR *)&fakeEnt;
}
static struct dirent *stub_readdir(DIR *d)
{
    (void)d;
    if (dirPos >= dirCount)
        return NULL;
    snprintf(fakeEnt.d_name, sizeof(fakeEnt.d_name), "%s", dirNames[dirPos++]);
    return &fakeEnt;
}
static int stub_closedir(DIR *d) { (void)d; return 0; }
#define opendir stub_opendir
#define readdir stub_readdir
#define closedir stub_closedir

static int loadISOGameListCache(const char *path, struct game_cache_list *c)
{
    int dvd = strstr(path, "DVD") != NULL;
    int n = dvd ? binDvdCount : binCdCount;
    if (n <= 0)
        return ENOENT;
    c->games = (base_game_info_t *)malloc(n * sizeof(base_game_info_t));
    memcpy(c->games, dvd ? binDvd : binCd, n * sizeof(base_game_info_t));
    c->count = n;
    return 0;
}
static void freeISOGameListCache(struct game_cache_list *c)
{
    free(c->games);
    c->games = NULL;
    c->count = 0;
}
static int queryISOGameListCache(const struct game_cache_list *c, base_game_info_t *g, const char *n, int *h)
{
    (void)h;
    for (unsigned int i = 0; i < c->count; i++) {
        char full[200];
        snprintf(full, sizeof(full), "%s%s", c->games[i].name, c->games[i].extension);
        if (!strcmp(full, n)) {
            *g = c->games[i];
            return 0;
        }
    }
    return ENOENT;
}

/* The real updateISOGameList writes through these: capture what lands in each games.bin. */
static int curDvd;
static FILE *fake_fopen(const char *p, const char *m) { (void)m; curDvd = strstr(p, "DVD") != NULL; return (FILE *)&curDvd; }
static size_t fake_fwrite(const void *b, size_t sz, size_t n, FILE *f)
{
    (void)f;
    memcpy(curDvd ? binDvd : binCd, b, sz * n);
    *(curDvd ? &binDvdCount : &binCdCount) = (int)n;
    return n;
}
static int fake_fclose(FILE *f) { (void)f; return 0; }
static int fake_remove(const char *p) { removes++; *(strstr(p, "DVD") ? &binDvdCount : &binCdCount) = -1; return 0; }
#define fopen fake_fopen
#define fwrite fake_fwrite
#define fclose fake_fclose
#define remove fake_remove

static int isValidIsoName(char *name, int *len)
{
    const char *dot = strrchr(name, '.');
    if (dot == NULL || strcasecmp(dot, ".iso"))
        return 0;
    *len = (int)(dot - name);
    return GAME_FORMAT_ISO;
}
static int fileXioMount(const char *m, const char *p, int f) { (void)m; (void)p; (void)f; return 0; }
static int fileXioUmount(const char *m) { (void)m; return 0; }
static int GetStartupExecName(const char *p, char *o, int n) { (void)p; snprintf(o, n, "SLUS_000.01"); return 0; }
static int openFile(const char *p, int f) { (void)p; (void)f; return -1; } /* no ul.cfg */
static int getFileSize(int fd) { (void)fd; return 0; }
static int stub_read(int fd, void *b, int n) { (void)fd; (void)b; (void)n; return 0; }
static int stub_close(int fd) { (void)fd; return 0; }
#define read stub_read
#define close stub_close

@FIRST_READ@

@UPDATE@

@SPAN@

static int failed;
static void check(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL %s (folders opened: %d)\n", what, opens);
        failed = 1;
    }
}

static int sameRows(const base_game_info_t *a, int na, const base_game_info_t *b, int nb)
{
    if (na != nb)
        return 0;
    for (int i = 0; i < na; i++)
        if (strcmp(a[i].name, b[i].name) || strcmp(a[i].extension, b[i].extension) || strcmp(a[i].startup, b[i].startup) || a[i].media != b[i].media)
            return 0;
    return 1;
}

int main(void)
{
    static const char *cd[] = {"Zeta.iso", "Alpha.iso", "readme.txt"};
    static const char *dvd[] = {"Bravo.iso", "Echo.iso", "Delta.iso", "Charlie.iso"};
    memcpy(cdNames, cd, sizeof(cd));
    cdCount = 3;
    memcpy(dvdNames, dvd, sizeof(dvd));
    dvdCount = 4;

    base_game_info_t *list = NULL, *scanned = NULL;
    int fsize = -1, count = 0, scannedCount = 0;

    /* A plain scan (bit off) reads both folders and writes both games.bin files. */
    sbListCacheFirstRead(0, 0);
    opens = 0;
    sbReadList(&list, "mass0:", "", &fsize, &count);
    check(opens == 2 && count == 6, "bit off: the first list reads both folders");
    check(binCdCount == 2 && binDvdCount == 4, "the scan wrote both games.bin files");
    scanned = (base_game_info_t *)malloc(count * sizeof(base_game_info_t));
    memcpy(scanned, list, count * sizeof(base_game_info_t));
    scannedCount = count;

    /* Bit on, a page that has not listed yet: no folder read, the same rows in the same order. */
    gGameListCache = 1 << 0;
    sbListCacheFirstRead(1, 0);
    opens = 0;
    sbReadList(&list, "mass1:", "", &fsize, &count);
    check(opens == 0, "bit on: the first list opens no folder");
    check(sameRows(list, count, scanned, scannedCount), "bit on: the cached list equals the scanned list, in order");

    /* The same page again (Refresh): the folders are read. */
    sbListCacheFirstRead(1, 0);
    opens = 0;
    sbReadList(&list, "mass1:", "", &fsize, &count);
    check(opens == 2 && sameRows(list, count, scanned, scannedCount), "bit on: the second list of that page reads the folders");

    /* A second page with the same bit gets its own one cached list; a page whose bit is off never does. */
    sbListCacheFirstRead(2, 0);
    opens = 0;
    sbReadList(&list, "mass2:", "", &fsize, &count);
    check(opens == 0, "a second page on the same bit gets its own cached list");
    sbListCacheFirstRead(3, 1);
    opens = 0;
    sbReadList(&list, "mass3:", "", &fsize, &count);
    check(opens == 2, "a page whose bit is off reads the folders");

    /* sbReadList without sbListCacheFirstRead first (another caller) never uses the cache. */
    opens = 0;
    sbReadList(&list, "mass2:", "", &fsize, &count);
    check(opens == 2, "the cached list is used only right after sbListCacheFirstRead");

    /* Folder browsing on: the folders are always read (games.bin holds no folder rows). */
    gEnableFolderNav = 1;
    sbListCacheFirstRead(4, 0);
    opens = 0;
    sbReadList(&list, "mass4:", "", &fsize, &count);
    check(opens == 2, "folder browsing on: the folders are read");
    gEnableFolderNav = 0;

    /* No CD games.bin: CD is read, DVD still comes from its games.bin. */
    binCdCount = -1;
    sbListCacheFirstRead(5, 0);
    opens = 0;
    sbReadList(&list, "mass5:", "", &fsize, &count);
    check(opens == 1 && sameRows(list, count, scanned, scannedCount), "no CD games.bin: only CD is read, the rows are unchanged");

    /* Every CD game removed: the folder is read as empty and its games.bin is deleted. */
    cdCount = 1;
    cdNames[0] = "readme.txt";
    removes = 0;
    sbListCacheFirstRead(6, 1);
    sbReadList(&list, "mass6:", "", &fsize, &count);
    check(removes == 1 && binCdCount == -1 && count == 4, "an emptied CD folder deletes its games.bin");

    free(list);
    free(scanned);
    if (!failed)
        printf("game list cache: cached first list matches the scan; later lists, folder browsing and empty folders read the device\n");
    return failed;
}
'''

program = HARNESS.replace('@FIRST_READ@', first_read).replace('@UPDATE@', update_fn).replace('@SPAN@', scan_span)
with tempfile.TemporaryDirectory() as tmp:
    c_file = Path(tmp) / 'glc.cpp'
    exe = Path(tmp) / 'glc'
    c_file.write_text(program, encoding='utf-8')
    build = subprocess.run(['g++', '-std=gnu++17', '-Wall', '-Wno-unused-function', '-fsanitize=address,undefined', '-o', str(exe), str(c_file)],
                           capture_output=True, text=True)
    if build.returncode != 0:
        print('FAIL: harness did not compile:\n' + build.stdout + build.stderr)
        sys.exit(1)
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    sys.stdout.write(run.stdout + run.stderr)
    sys.exit(run.returncode)
