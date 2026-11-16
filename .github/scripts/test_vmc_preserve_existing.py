"""Execute sysCheckVMC() with a fake existing card: resizing must not delete saves."""
from pathlib import Path
import subprocess
import tempfile
import sys

root = Path(__file__).resolve().parents[2]
source = Path(sys.argv[1]).read_text(encoding="utf-8") if len(sys.argv)>1 else (root / "src/platform/sysvmc.cpp").read_text(encoding="utf-8")
start = source.index("static int sysVMCAdoptRebuilt(")
end = start
for _ in range(3):  # the rebuilt-copy helper, sysCheckVMCEx, then the sysCheckVMC wrapper after it
    end = source.index("{", end)
    depth = 0
    while end < len(source):
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
        if depth == 0:
            break
    assert depth == 0
function = source[start:end]
assert "int sysCheckVMC(" in function
ui = (root / "src/ui/guigame.cpp").read_text(encoding="utf-8")
assert "createResult == SYS_VMC_REPLACE_REQUIRES_DELETE" in ui
assert "_STR_VMC_REPLACE_REQUIRES_DELETE" in ui

program = r'''
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <initializer_list>
#define O_RDONLY 1
#define SEEK_END 2
#define SEEK_SET 0
#define S_IRUSR 0
#define S_IWUSR 0
#define S_IXUSR 0
#define S_IRGRP 0
#define S_IWGRP 0
#define S_IXGRP 0
#define S_IROTH 0
#define S_IWOTH 0
#define S_IXOTH 0
#define SYS_VMC_REPLACE_REQUIRES_DELETE (-2)
#define SYS_VMC_CREATE_FAILED (-3)
#define SYS_VMC_PROBE_FORMAT (-4)
#define LOG(...) ((void)0)
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
@SUPERBLOCK@
static vmc_superblock_t header;
static int readPosition, readLimit, readErrorAt=-1, readEOFAt=-1, closeError, seekError;
typedef struct {
    char VMC_filename[1024];
    int VMC_size_mb, VMC_blocksize, VMC_thread_priority, VMC_card_slot, VMC_flags;
} createVMCparam_t;
static char gVMCCreatePath[256] = {0};
static int exists, length, opened, erased, jobs, returnsError;
static int openError;
static int dirOpenError, dirReadError, dirPosition;
typedef struct {char name[256];} iox_dirent_t;
static int fileXioDopen(const char *folder) {
    assert(!strcmp(folder,"mass0:/VMC"));dirPosition=0;
    return dirOpenError ? -1 : 4;
}
static int fileXioDread(int fd, iox_dirent_t *entry) {
    (void)fd;
    if (dirReadError) return -1;
    if (dirPosition++==0) {strcpy(entry->name,"unrelated.bin");return 1;}
    if (exists && dirPosition==2) {strcpy(entry->name,"save_0.BIN");return 1;}
    return 0;
}
static int fileXioDclose(int fd) {(void)fd;return 0;}
static int fake_open(const char *path, int flags, ...) {
    (void)path; (void)flags; opened++;
    if (openError) {errno=openError;return -1;}
    if (exists) {readPosition=0;return 3;}
    errno=ENOENT;return -1;
}
static int fake_lseek(int fd, int offset, int whence) {
    (void)fd; (void)offset;
    if(seekError==1 && whence==SEEK_END)return -1;
    if(seekError==2 && whence==SEEK_SET)return -1;
    return whence == SEEK_END ? length : 0;
}
static int fake_read(int fd, void *buf, int count) {
    (void)fd;
    if(readErrorAt>=0 && readPosition>=readErrorAt)return -1;
    if(readEOFAt>=0 && readPosition>=readEOFAt)return 0;
    int available=sizeof(header)-readPosition;
    if(count>available)count=available;
    if(readLimit && count>readLimit)count=readLimit;
    memcpy(buf, (char*)&header+readPosition, count);readPosition+=count;return count;
}
static int fake_close(int fd) { (void)fd; return closeError ? -1 : 0; }
static int fake_unlink(const char *path) { (void)path; erased++; return 0; }
static int fake_devctl(const char *path, int command, void *args, int alen, void *buf, int blen) {
    (void)path; (void)command; (void)args; (void)alen; (void)buf; (void)blen;
    jobs++; return returnsError ? -1 : 0;
}
static int sysVMCCardState(const char *path) { (void)path; return -1; } // cannot tell: never adopt here
#define open fake_open
#define lseek fake_lseek
#define read fake_read
#define close fake_close
#define unlink fake_unlink
#define fileXioDevctl fake_devctl
@FUNCTION@
int main(void) {
    char name[] = "SAVE_0";
    exists = 1; length = 8 * 1048576;
    // An idle/restarted formatter has no trustworthy error history. The full
    // erase pass leaves this exact size but no final header: preserve, refuse.
    for(int erasedByte : {0,255}) {
        memset(&header,erasedByte,sizeof(header));
        assert(sysCheckVMC("mass0:/", "/", name, SYS_VMC_PROBE_FORMAT, NULL)==SYS_VMC_CREATE_FAILED);
        assert(sysCheckVMC("mass0:/", "/", name, 0, NULL)==SYS_VMC_CREATE_FAILED);
        assert(sysCheckVMC("mass0:/", "/", name, 8, NULL)==SYS_VMC_REPLACE_REQUIRES_DELETE);
        assert(!erased && !jobs);
    }
    memset(&header,0,sizeof(header));
    memcpy(header.magic,"Sony PS2 Memory Card Format ",28);
    header.mc_type=2;header.page_size=512;header.pages_per_cluster=2;
    header.pages_per_block=16;header.clusters_per_card=8192;
    const vmc_superblock_t validHeader=header;
    for(int mb : {4,8,16,32,64,128}) {
        header.clusters_per_card=mb*1024;length=mb*1048576;
        assert(sysCheckVMC("mass0:/", "/", name, SYS_VMC_PROBE_FORMAT, NULL)==mb);
    }
    header=validHeader;length=8*1048576;
    assert(sysCheckVMC("mass0:/", "/", name, SYS_VMC_PROBE_FORMAT, NULL)==8);
    readLimit=7; // positive short reads must accumulate
    assert(sysCheckVMC("mass0:/", "/", name, SYS_VMC_PROBE_FORMAT, NULL)==8);
    readErrorAt=14;
    assert(sysCheckVMC("mass0:/", "/", name, SYS_VMC_PROBE_FORMAT, NULL)==SYS_VMC_CREATE_FAILED);
    readErrorAt=-1;readLimit=0;
    readLimit=7;readEOFAt=14;
    assert(sysCheckVMC("mass0:/", "/", name, SYS_VMC_PROBE_FORMAT, NULL)==SYS_VMC_CREATE_FAILED);
    assert(sysCheckVMC("mass0:/", "/", name, 0, NULL)==SYS_VMC_CREATE_FAILED);
    assert(sysCheckVMC("mass0:/", "/", name, 8, NULL)==SYS_VMC_REPLACE_REQUIRES_DELETE);
    readEOFAt=-1;readLimit=0;
    for(int fault : {1,2,3}) {
        seekError=fault<3 ? fault : 0;closeError=fault==3;
        assert(sysCheckVMC("mass0:/", "/", name, SYS_VMC_PROBE_FORMAT, NULL)==SYS_VMC_CREATE_FAILED);
        assert(!erased && !jobs);
    }
    seekError=closeError=0;
    for(int fault=0;fault<7;fault++) {
        header=validHeader;
        if(fault==0)header.magic[0]='x';
        if(fault==1)header.mc_type=1;
        if(fault==2)header.clusters_per_card=0;
        if(fault==3)header.page_size=0;
        if(fault==4)header.pages_per_cluster=0;
        if(fault==5)header.pages_per_block=0;
        if(fault==6){header.clusters_per_card=0xffffffff;header.pages_per_cluster=0xffff;header.page_size=0xffff;}
        assert(sysCheckVMC("mass0:/", "/", name, SYS_VMC_PROBE_FORMAT, NULL)==SYS_VMC_CREATE_FAILED);
    }
    header=validHeader;header.clusters_per_card=8191;length=8191*1024;
    assert(sysCheckVMC("mass0:/", "/", name, SYS_VMC_PROBE_FORMAT, NULL)==0); // formatted odd size for Neutrino
    length=8*1048576;
    assert(sysCheckVMC("mass0:/", "/", name, SYS_VMC_PROBE_FORMAT, NULL)==SYS_VMC_CREATE_FAILED);
    header=validHeader;
    char prefix[243];
    memset(prefix, 'p', 241); prefix[241] = 0;
    assert(sysCheckVMC(prefix, "/", name, 0, NULL) == 8); // 255 characters fit
    int priorOpened = opened;
    prefix[241] = 'p'; prefix[242] = 0;
    strcpy(gVMCCreatePath, "stale");
    assert(sysCheckVMC(prefix, "/", name, 8, NULL) == SYS_VMC_CREATE_FAILED);
    assert(!gVMCCreatePath[0]);
    assert(sysCheckVMC(prefix, "/", name, 0, NULL) == SYS_VMC_CREATE_FAILED);
    assert(sysCheckVMC(prefix, "/", name, -1, NULL) == SYS_VMC_CREATE_FAILED);
    assert(opened == priorOpened && erased == 0 && jobs == 0);
    assert(sysCheckVMC("mass0:/", "/", name, 16, NULL) == SYS_VMC_REPLACE_REQUIRES_DELETE);
    assert(erased == 0 && jobs == 0);
    const int errors[] = {EACCES, EIO, ENOTDIR};
    for (unsigned i=0;i<sizeof(errors)/sizeof(errors[0]);i++) {
        openError=errors[i];
        assert(sysCheckVMC("mass0:/", "/", name, 8, NULL) == SYS_VMC_CREATE_FAILED);
        assert(erased==0 && jobs==0 && !gVMCCreatePath[0]);
    }
    openError=0;
    assert(sysCheckVMC("mass0:/", "/", name, 8, NULL) == 8 && jobs == 0);
    length = 123; // corrupted or unrecognized size is still someone's data
    assert(sysCheckVMC("mass0:/", "/", name, 8, NULL) == SYS_VMC_REPLACE_REQUIRES_DELETE);
    assert(erased == 0 && jobs == 0);
    exists = 0;
    dirOpenError=1;
    assert(sysCheckVMC("mass0:/", "/", name, 8, NULL) == SYS_VMC_CREATE_FAILED);
    dirOpenError=0;dirReadError=1;
    assert(sysCheckVMC("mass0:/", "/", name, 8, NULL) == SYS_VMC_CREATE_FAILED);
    assert(jobs==0 && erased==0);
    dirReadError=0;openError=4; // FatFs FR_NO_FILE, not POSIX ENOENT
    assert(sysCheckVMC("mass0:/", "/", name, 8, NULL) == -1 && jobs==1);
    openError=0;jobs=0;
    assert(sysCheckVMC("mass0:/", "/", name, 8, NULL) == -1 && jobs == 1);
    assert(erased == 0);
    returnsError=1;
    assert(sysCheckVMC("mass0:/", "/", name, 8, NULL) == SYS_VMC_CREATE_FAILED);
    assert(jobs==2 && erased==0 && !gVMCCreatePath[0]);
    returnsError=0;
    assert(sysCheckVMC("mass0:/", "/", name, -1, NULL) == -1 && erased == 2) /* the card and its ".new" sibling */;
    puts("PASS: VMC format/IO checks refuse unfinished cards, preserve odd formatted cards and never replace saves");
    return 0;
}
'''.replace("@FUNCTION@", function).replace("@SUPERBLOCK@", (root / "include/mcemu.h").read_text(encoding="utf-8"))
with tempfile.TemporaryDirectory() as d:
    src = Path(d) / "test.cpp"
    out = Path(d) / "test.exe"
    src.write_text(program, encoding="utf-8")
    legacy_flags = ["-Wno-error=sign-compare"] if len(sys.argv)>1 else []
    san_flags = ["-fsanitize=address,undefined", "-fno-sanitize-recover=undefined"] if sys.platform != "win32" else []
    subprocess.run(["g++", "-std=c++17", "-Wno-write-strings", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Werror",
                    *san_flags, *legacy_flags,
                    str(src), "-o", str(out)], check=True)
    subprocess.run([str(out)], check=True, timeout=10)
