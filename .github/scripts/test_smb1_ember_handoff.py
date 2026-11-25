"""Exercise the real SMB1 directory-reader EOF/handle code and pin Ember's SMB handoff.

Issue #463: Ember enumerates nested SMB folders and rejects a directory scan that ends
with an error instead of normal EOF. No console/network server is needed for the mock
SMB1 response cases; actual hardware validation remains necessary.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2]


fio = read("modules/network/smbman-ra/smb_fio.c")
proto = read("modules/network/smbman-ra/smb.c")
eth = read("src/sources/ethsupport.cpp")
net = read("src/sources/netsupport.cpp")
system = read("src/platform/system.cpp")
cue = read("src/sources/cuesupport.cpp")
opl = read("src/app/opl.cpp")

# One ordinary EOF, whether carried on a final entry, on an empty result, or as Windows
# SMB1's STATUS_NO_MORE_FILES / STATUS_NO_SUCH_FILE, is not a scanner error. Only
# nonterminal/transport failures propagate as errors.
assert "unsigned char dir_eos;" in fio
assert "fh->dir_eos  = 0;" in function(fio, "int smb_dopen(")
find_fn = function(proto, "int smb_FindFirstNext2(")
qpi_fn = function(proto, "int smb_QueryPathInformation(")
assert "return FFNRspParam->EndOfSearch ? 1 : -EIO;" in find_fn
assert "case STATUS_NO_MORE_FILES:" in find_fn
assert "case STATUS_NO_SUCH_FILE:" in find_fn
assert "case DOSERR_NO_MORE_FILES:" in find_fn
assert "case STATUS_OBJECT_NAME_NOT_FOUND:" in qpi_fn
assert "case STATUS_OBJECT_PATH_NOT_FOUND:" in qpi_fn
assert "return -ENOENT;" in qpi_fn
assert "fh->dir_eos  = (info->EOS != 0);" in function(fio, "int smb_dread(")
assert "fh->mode != O_DIROPEN &&" in function(fio, "int smb_close(")
assert "smb_FindClose2(UID, TID, fh->smb_fid);" in function(fio, "int smb_close(")

# The selected game device is spared by deinit; SMB also needs the IOP TCP/IP
# modules and fileXio-backed driver kept alive after the UI and RPC wrappers end.
clean = function(eth, "void EthSource::cleanUp(")
launch = function(eth, "void EthSource::launchCue(")
assert "if ((exception & UNMOUNT_EXCEPTION) == 0)\n            gEthSupport.smbDisconnect();" in clean
assert "if ((exception & UNMOUNT_EXCEPTION) == 0)\n        ethDeinitModules();" in clean
assert "NetManDeinit();" in function(net, "void NetSupport::deinitModules(")
assert "ps2ip_deinit();" in function(net, "void NetSupport::deinitModules(")
assert "host::app().deinit(UNMOUNT_EXCEPTION, itemList->mode);" in launch
assert launch.index("cuePrepareLaunch(") < launch.index("host::app().deinit(")
assert "sysLaunchEmber(emberElf, launchName);" in launch
handoff = function(system, "void sysLaunchEmber(")
assert "argv[argc++] = (char *)emberElf;" in handoff
assert "argv[argc++] = (char *)gameFolder;" in handoff
assert 'sysLoadELFKeepIOP(emberElf, "", argc, argv)' in handoff
assert "deinitAllSupport(exception, modeSelected);" in function(opl, "static void deinitWithTransition(")
preflight = function(cue, "int cuePrepareLaunch(")
assert preflight.index("2 * strlen(emberElf) + strlen(argument) + 3 > 256") < preflight.index("if (count == 0)\n        return 1;")

program = r"""
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#define SMB_NAME_MAX 256
#define O_DIROPEN 0x1000
#define TRANS2_FIND_FIRST2 1
#define TRANS2_FIND_NEXT2 2
typedef struct { int size; } PathInformation_t;
typedef struct { int size; } iox_stat_t;
typedef struct { int SID, EOS; PathInformation_t fileInfo; char FileName[256]; } SearchInfo_t;
typedef struct { iox_stat_t stat; char name[256]; } iox_dirent_t;
typedef struct iop_file { void *privdata; } iop_file_t;
typedef struct {
    iop_file_t *f;
    int smb_fid;
    long long filesize, position;
    unsigned int mode;
    unsigned char dir_eos, dir_stale, dir_buf; /* dir_buf 0: one entry per request, the path under test */
    unsigned short dir_left, dir_off;
    char name[SMB_NAME_MAX];
} FHANDLE;
#define MAX_FDHANDLES 4
static FHANDLE smbman_fdhandles[MAX_FDHANDLES];
static int UID = 1, TID = 1, queries, closing, find_closing, test_case;
static char SearchBuf[sizeof(SearchInfo_t)];
typedef unsigned char u8;
#define SMB_FIND_BATCH_SIZE 2048
static u8 DirBuf[1][SMB_FIND_BATCH_SIZE], DirBufUsed[1];
static int smb_FindEntry(const u8 *entry, SearchInfo_t *info) { (void)entry; (void)info; assert(!"no batch buffer in this test"); return 0; }
static void smb_io_lock(void) {}
static void smb_io_unlock(void) {}
static void smb_statFiller(const PathInformation_t *src, iox_stat_t *dst)
{ dst->size = src->size; }
static int smb_Close(int a, int b, int fid)
{ (void)a; (void)b; (void)fid; closing++; return 0; }
static int smb_FindClose2(int a, int b, int sid)
{ (void)a; (void)b; (void)sid; find_closing++; return test_case == 6 ? -ETIMEDOUT : 0; }
static int aborts;
static int smb_AbortConnection(void) { aborts++; return 0; }
static int smb_FindFirstNext2(int a, int b, char *path, int op, SearchInfo_t *info, u8 *batch, int size, int *count)
{
    (void)a; (void)b; (void)path; (void)size; (void)count;
    assert(batch == NULL);
    assert(op == (queries == 0 ? TRANS2_FIND_FIRST2 : TRANS2_FIND_NEXT2));
    queries++;
    if (test_case == 4) return -EIO;
    if (test_case == 3) return 1; /* empty successful search */
    if (test_case == 5) {
        /* Windows SMB1 srv.sys: last entry has EOS=0, next FIND_NEXT2 returns STATUS_NO_MORE_FILES (1). */
        if (queries == 2) { info->EOS = 1; return 1; }
        info->SID = 42;
        info->EOS = 0;
        info->fileInfo.size = 1;
        strcpy(info->FileName, "Chrono Trigger");
        return 0;
    }
    if (queries > (test_case == 2 ? 2 : 1)) assert(!"request after EOS");
    info->SID = 42;
    info->EOS = (test_case == 1 || queries == 2);
    info->fileInfo.size = queries;
    strcpy(info->FileName, queries == 1 ? "Game A" : "Game B");
    return 0;
}
@CLOSE@
@DREAD@
static void setup(FHANDLE *handle, iop_file_t *file, int which)
{
    memset(handle, 0, sizeof(*handle));
    file->privdata = handle;
    handle->f = file;
    handle->mode = O_DIROPEN;
    handle->smb_fid = -1;
    strcpy(handle->name, "\\EMBER\\games\\*");
    test_case = which;
    queries = closing = find_closing = 0;
}
int main(void)
{
    FHANDLE h;
    iop_file_t f;
    iox_dirent_t de;

    setup(&h, &f, 1);
    assert(smb_dread(&f, &de) == 1 && strcmp(de.name, "Game A") == 0);
    assert(smb_dread(&f, &de) == 0 && queries == 1);
    assert(smb_close(&f) == 0 && h.f == NULL && closing == 0 && find_closing == 0);

    setup(&h, &f, 2);
    assert(smb_dread(&f, &de) == 1 && strcmp(de.name, "Game A") == 0);
    assert(smb_dread(&f, &de) == 1 && strcmp(de.name, "Game B") == 0);
    assert(smb_dread(&f, &de) == 0 && queries == 2);
    assert(smb_close(&f) == 0 && closing == 0 && find_closing == 0);

    setup(&h, &f, 3);
    assert(smb_dread(&f, &de) == 0 && queries == 1);
    assert(smb_dread(&f, &de) == 0 && queries == 1);
    assert(smb_close(&f) == 0 && closing == 0 && find_closing == 0);

    setup(&h, &f, 4);
    assert(smb_dread(&f, &de) == -EIO && queries == 1);
    assert(smb_close(&f) == 0 && find_closing == 0);

    setup(&h, &f, 5);
    assert(smb_dread(&f, &de) == 1 && strcmp(de.name, "Chrono Trigger") == 0);
    assert(smb_dread(&f, &de) == 0 && queries == 2);
    assert(smb_dread(&f, &de) == 0 && queries == 2);
    assert(smb_close(&f) == 0 && closing == 0 && find_closing == 0);

    setup(&h, &f, 5);
    assert(smb_dread(&f, &de) == 1 && strcmp(de.name, "Chrono Trigger") == 0);
    assert(smb_close(&f) == 0 && closing == 0 && find_closing == 1); /* early close before EOS */

    setup(&h, &f, 1);
    assert(smb_close(&f) == 0 && h.f == NULL && find_closing == 0); /* closed without dread */
    iop_file_t parent, unopened, completed;
    setup(&smbman_fdhandles[0], &parent, 6);
    setup(&smbman_fdhandles[1], &unopened, 6);
    setup(&smbman_fdhandles[2], &completed, 6);
    smbman_fdhandles[0].smb_fid = 100;
    smbman_fdhandles[2].smb_fid = 101;
    smbman_fdhandles[2].dir_eos = 1;
    setup(&h, &f, 6);
    h.smb_fid = 102;
    assert(smb_close(&f) == -ETIMEDOUT && aborts == 1);
    assert(smb_dread(&parent, &de) == -EIO && queries == 0);
    assert(smb_dread(&completed, &de) == 0 && queries == 0);
    assert(smb_dread(&unopened, &de) == 1 && queries == 1);
    TID = -1;
    assert(smb_close(&parent) == 0 && !smbman_fdhandles[0].f && find_closing == 1);
    puts("PASS: SMB1 one/two/zero entries, Windows NO_MORE_FILES EOF, early FindClose2, transport error and handle cleanup");
    return 0;
}
"""
program = program.replace("@CLOSE@", function(fio, "int smb_close("))
program = program.replace("@DREAD@", function(fio, "int smb_dread("))
with tempfile.TemporaryDirectory() as work:
    src = Path(work) / "ember_smb1.c"
    exe = Path(work) / "ember_smb1"
    src.write_text(program, encoding="utf-8")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=10)
print("PASS: SMB Ember launch preserves selected network driver, no-reset ELF argv and arg limits")
