"""Exercise smbman's batched directory reads against a scripted SMB1 server."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
smb = (root / "modules/network/smbman-ra/smb.c").read_text()
fio = (root / "modules/network/smbman-ra/smb_fio.c").read_text()
header = (root / "modules/network/smbman-ra/smb.h").read_text()


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


code = r"""
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t s64;
typedef struct { void *privdata; } iop_file_t;
typedef struct { int mode; } iox_stat_t;
typedef struct { iox_stat_t stat; char name[256]; } iox_dirent_t;
typedef struct { char ShareName[32]; } ShareEntry_t;
typedef struct { int unused; } smbQueryDiskInfo_out_t;
#define O_DIROPEN 0x100
#define ZERO_PKT_ALIGNED(p, n) memset((p), 0, (n))
@HEADER@
static server_specs_t server_specs;
static struct {
    u32 sessionHeader;
    union {
        u8 u8buff[MAX_SMB_BUF + MAX_SMB_BUF_HDR];
        FindFirstNext2Request_t findFirstNext2Request;
        FindFirstNext2Response_t findFirstNext2Response;
        FindClose2Request_t findClose2Request;
        CloseResponse_t closeResponse;
    } smb;
} __attribute__((packed)) SMB_buf;

// ---- the scripted server: directories of DIRS x ENTRIES names, answering at most server_max
// entries per reply (1 = a server that ignores SearchCount) within the client's MaxDataCount.
#define ENTRIES 30
static int server_max, requests, closes, corrupt;
static int cursor[16], next_sid;
static void nb_SetSessionMessage(u32 size) { SMB_buf.sessionHeader = size; }
static void entry_name(char *out, int dir, int i) {
    if (i == 0) strcpy(out, ".");
    else if (i == 1) strcpy(out, "..");
    else sprintf(out, "dir%d game %03d with a long enough name (USA).cue", dir, i);
}
static int GetSMBServerReply(int a, void *b, int c) {
    FindFirstNext2Request_t *rq = &SMB_buf.smb.findFirstNext2Request;
    u8 *prm = &SMB_buf.smb.u8buff[rq->smbTrans.ParamOffset];
    int first, sid, want, maxdata, dir, n = 0, len = 0, last = 0, eos;
    static u8 data[8192];
    (void)a; (void)b; (void)c;
    requests++;
    if (rq->smbH.Cmd == SMB_COM_FIND_CLOSE2) {
        closes++;
        memset(&SMB_buf.smb, 0, sizeof(CloseResponse_t));
        SMB_buf.smb.closeResponse.smbH.Magic = SMB_MAGIC;
        return 1;
    }
    assert(rq->smbH.Cmd == SMB_COM_TRANSACTION2);
    first   = rq->SubCommand == TRANS2_FIND_FIRST2;
    maxdata = rq->smbTrans.MaxDataCount;
    if (first) {
        FindFirst2RequestParam_t *p = (FindFirst2RequestParam_t *)prm;
        want = p->SearchCount;
        sid  = next_sid++;
        assert(sid < 16);
        cursor[sid] = 0;
        dir = p->SearchPattern[4] - '0'; // "dirN\*", ASCII (Capabilities has no unicode bit)
    } else {
        FindNext2RequestParam_t *p = (FindNext2RequestParam_t *)prm;
        want = p->SearchCount;
        sid  = p->SearchID;
    }
    dir = first ? dir : sid; // one search per directory in every case below
    while (n < want && n < server_max && cursor[sid] < ENTRIES) {
        char name[128];
        FindFirst2ResponseData_t e;
        int start = (len + 7) & ~7, nl;
        entry_name(name, dir, cursor[sid]);
        nl = strlen(name);
        if (n && start + (int)sizeof(e) + nl > maxdata)
            break;
        memset(&e, 0, sizeof(e));
        e.FileNameLen    = nl;
        e.EndOfFile      = 1000 + cursor[sid];
        e.FileAttributes = cursor[sid] < 2 ? EXT_ATTR_DIRECTORY : 0;
        if (n) {
            u32 off = start - last;
            memset(&data[len], 0, start - len);
            memcpy(&data[last], &off, 4);
        }
        memcpy(&data[start], &e, sizeof(e));
        memcpy(&data[start + sizeof(e)], name, nl);
        last = start;
        len  = start + sizeof(e) + nl;
        n++;
        cursor[sid]++;
    }
    eos = cursor[sid] >= ENTRIES;
    if (corrupt == 1) { u32 bad = 0x7fffffff; memcpy(&data[60], &bad, 4); } // FileNameLen
    if (corrupt == 2 && n > 1) { u32 bad = 0x4000; memcpy(&data[0], &bad, 4); } // NextEntryOffset
    {
        FindFirstNext2Response_t *rs = &SMB_buf.smb.findFirstNext2Response;
        int poff = (sizeof(FindFirstNext2Response_t) + 3) & ~3, plen = first ? 10 : 8, doff;
        u16 params[5] = {(u16)sid, (u16)n, (u16)eos, 0, 0};
        memset(rs, 0, sizeof(*rs));
        rs->smbH.Magic = SMB_MAGIC;
        memcpy(&SMB_buf.smb.u8buff[poff], first ? &params[0] : &params[1], plen);
        doff = (poff + plen + 3) & ~3;
        rs->smbTrans.ParamOffset = poff;
        rs->smbTrans.ParamCount = rs->smbTrans.TotalParamCount = plen;
        rs->smbTrans.DataOffset = doff;
        rs->smbTrans.DataCount = rs->smbTrans.TotalDataCount = len;
        memcpy(&SMB_buf.smb.u8buff[doff], data, len);
    }
    return 1;
}
static void smb_io_lock(void) {}
static void smb_io_unlock(void) {}
int smb_Close(int uid, int tid, int fid) { (void)uid; (void)tid; (void)fid; return 0; }
int smb_AbortConnection(void) { return 0; }
static void smb_statFiller(const PathInformation_t *info, iox_stat_t *stat) { stat->mode = (int)info->EndOfFile; }
static int UID = 1, TID = 1;
#define SMB_NAME_MAX 256
#define SMB_SEARCH_BUF_MAX 4096
"""
code = code.replace("@HEADER@", "\n".join(l for l in header.splitlines() if not l.startswith("#include")))
start = fio.index("typedef struct", fio.index("#define SMB_NAME_MAX"))
code += fio[start:fio.index("} FHANDLE;", start) + len("} FHANDLE;")] + "\n"
code += "#define MAX_FDHANDLES 8\nstatic FHANDLE smbman_fdhandles[MAX_FDHANDLES];\n"
code += "static u8 SearchBuf[SMB_SEARCH_BUF_MAX];\n"
start = fio.index("#define SMB_DIR_BUFS")
code += fio[start:fio.index("static u8 DirBufUsed[SMB_DIR_BUFS];") + len("static u8 DirBufUsed[SMB_DIR_BUFS];")] + "\n"
for name in ("static int asciiToUtf16(", "static int utf16ToAscii(", "static int setStringField(", "static int getStringField(",
             "static int smbFindFirstEmptyDosStatus(", "int smb_FindFirstNext2(", "int smb_FindEntry(", "int smb_FindClose2("):
    code += function(smb, name)
code += function(fio, "int smb_close(")
code += function(fio, "int smb_dread(")
# smb_dopen minus its existence check: the handle setup is what is under test.
dopen = function(fio, "int smb_dopen(")
start = dopen.index("    // test if the dir exists")
dopen = dopen[:start] + dopen[dopen.index("    fh = smbman_getfilefreeslot();"):]
dopen = dopen.replace("char *path = prepare_path(dirname, smb_curpath, SMB_PATH_MAX);", "const char *path = dirname;")
dopen = dopen.replace("    PathInformation_t info;\n", "")
code += function(fio, "static FHANDLE *smbman_getfilefreeslot(") + dopen
code += r"""
static iop_file_t files[8];
static void reset(int max) {
    memset(smbman_fdhandles, 0, sizeof(smbman_fdhandles));
    memset(DirBufUsed, 0, sizeof(DirBufUsed));
    memset(cursor, 0, sizeof(cursor));
    server_max = max;
    requests = closes = corrupt = next_sid = 0;
}
static void open_dir(int d) {
    char path[16];
    sprintf(path, "\\dir%d", d);
    assert(smb_dopen(&files[d], path) == 0);
}
// Reads entry i of directory d and checks it is the right one, in order, with its own size.
static void expect(int d, int i) {
    iox_dirent_t de;
    char name[128];
    assert(smb_dread(&files[d], &de) == 1);
    entry_name(name, d, i);
    assert(strcmp(de.name, name) == 0);
    assert(de.stat.mode == 1000 + i);
}
static void expect_end(int d) {
    iox_dirent_t de;
    assert(smb_dread(&files[d], &de) == 0);
    assert(smb_dread(&files[d], &de) == 0);
}
int main(void) {
    int i, d;
    iox_dirent_t de;

    // A server that answers one entry per reply: every entry, one request each, as before.
    reset(1);
    open_dir(0);
    for (i = 0; i < ENTRIES; i++)
        expect(0, i);
    expect_end(0);
    assert(requests == ENTRIES);
    assert(smb_close(&files[0]) == 0 && closes == 0); // the server closed the search at its end

    // A batching server: the same entries in a handful of requests, none bigger than the buffer.
    reset(64);
    open_dir(0);
    for (i = 0; i < ENTRIES; i++)
        expect(0, i);
    expect_end(0);
    assert(requests >= 2 && requests <= 4);
    assert(smb_close(&files[0]) == 0 && closes == 0);

    // The Ember scan: an outer listing kept open while each inner folder is opened, read as far
    // as its first names and closed early. Neither search may disturb the other's place.
    reset(64);
    open_dir(0);
    for (i = 0; i < ENTRIES; i++) {
        expect(0, i);
        open_dir(1 + i % 3);
        cursor[1 + i % 3] = 0;
        next_sid = 1 + i % 3; // the scripted server names a search after its directory
        expect(1 + i % 3, 0);
        expect(1 + i % 3, 1);
        expect(1 + i % 3, 2);
        assert(smb_close(&files[1 + i % 3]) == 0);
        next_sid = 4;
    }
    expect_end(0);
    assert(closes == ENTRIES); // each early close released its search on the server
    assert(smb_close(&files[0]) == 0);
    for (i = 0; i < SMB_DIR_BUFS; i++)
        assert(!DirBufUsed[i]); // every close returned its buffer

    // More directories open at once than buffers: the extra ones read one entry per request.
    reset(64);
    for (d = 0; d < SMB_DIR_BUFS + 2; d++)
        open_dir(d);
    for (i = 0; i < ENTRIES; i++)
        for (d = 0; d < SMB_DIR_BUFS + 2; d++)
            expect(d, i);
    for (d = 0; d < SMB_DIR_BUFS + 2; d++) {
        expect_end(d);
        assert(smb_close(&files[d]) == 0);
    }
    assert(requests >= 2 * ENTRIES + 2 * SMB_DIR_BUFS && requests <= 2 * ENTRIES + 4 * SMB_DIR_BUFS);

    // A reply whose lengths or offsets point outside it is an error, never a read past the buffer.
    for (i = 1; i <= 2; i++) {
        reset(64);
        corrupt = i;
        open_dir(0);
        assert(smb_dread(&files[0], &de) == -EIO);
        assert(smb_close(&files[0]) == 0);
    }
    puts("PASS: smbman batched directory reads keep every entry in order, fall back, and reject bad replies");
    return 0;
}
"""

with tempfile.TemporaryDirectory(prefix="smb-findbatch-") as tmp:
    source = Path(tmp) / "test.c"
    exe = Path(tmp) / "test"
    source.write_text(code)
    compiler = shutil.which("gcc") or shutil.which("clang")
    assert compiler, "A C host compiler is required"
    subprocess.run([compiler, "-std=gnu11", "-g", "-fsanitize=address,undefined", "-fno-sanitize=alignment", "-Wall", "-Wextra",
                    "-Werror", "-Wno-unused-function", "-Wno-unused-label", "-Wno-sign-compare", "-Wno-address-of-packed-member", "-Wno-unused-parameter", str(source), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
