"""Exercise SMB1 early-directory close errors and transport reset behavior."""
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
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t s64;
typedef struct { void *privdata; } iop_file_t;
@WIRE_TYPES@
#define SMB_MAGIC 0x424D53FFu
#define SMB_COM_FIND_CLOSE2 0x34
#define SMB_FLAGS_CANONICAL_PATHNAMES 1
#define SMB_FLAGS2_KNOWS_LONG_NAMES 2
#define SMB_FLAGS2_32BIT_STATUS 4
#define STATUS_SUCCESS 0
#define O_DIROPEN 0x100
#define SMB_NAME_MAX 256
#define SHUT_WR 1
#define ZERO_PKT_ALIGNED(p, n) memset((p), 0, (n))
static struct {
    u32 sessionHeader;
    union { FindClose2Request_t findClose2Request; CloseResponse_t closeResponse; } smb;
} __attribute__((packed)) SMB_buf;
static int main_socket = -1, reply_result, close_calls, shutdown_calls;
static short reply_status;
static int nb_SetSessionMessage(u32 size) { SMB_buf.sessionHeader = size; return 0; }
static int GetSMBServerReply(int header, void *payload, int payload_header) {
    assert(!header && payload == NULL && !payload_header);
    assert(SMB_buf.sessionHeader == sizeof(FindClose2Request_t));
    assert(SMB_buf.smb.findClose2Request.smbH.Cmd == SMB_COM_FIND_CLOSE2);
    assert(SMB_buf.smb.findClose2Request.SearchID == 77);
    if (reply_result > 0) {
        memset(&SMB_buf.smb, 0, sizeof(SMB_buf.smb));
        SMB_buf.smb.closeResponse.smbH.Magic = SMB_MAGIC;
        SMB_buf.smb.closeResponse.smbH.Ecode = reply_status;
    }
    return reply_result;
}
static int shutdown(int socket, int how) { (void)how; assert(socket == main_socket); shutdown_calls++; return 0; }
static int lwip_close(int socket) { assert(socket == main_socket); close_calls++; return 0; }
static int smb_Close(int uid, int tid, int fid) { (void)uid; (void)tid; (void)fid; return 0; }
static void smb_io_lock(void) {}
static void smb_io_unlock(void) {}
static int UID = 1, TID = 1;
"""
wire_types = []
for typ in ("SMBHeader_t", "FindClose2Request_t", "CloseResponse_t"):
    end = header.index("} __attribute__((packed)) " + typ + ";")
    start = header.rindex("typedef struct", 0, end)
    wire_types.append(header[start:header.index(";", end) + 1])
code = code.replace("@WIRE_TYPES@", "\n".join(wire_types))
start = fio.index("typedef struct", fio.index("#define SMB_NAME_MAX"))
code += fio[start:fio.index("} FHANDLE;", start) + len("} FHANDLE;")] + "\n"
code += "#define MAX_FDHANDLES 4\nstatic FHANDLE smbman_fdhandles[MAX_FDHANDLES];\n"
code += function(smb, "int smb_FindClose2(")
code += function(smb, "int smb_AbortConnection(")
code += function(fio, "int smb_close(")
code += r"""
static void reset_case(int reply) {
    memset(&SMB_buf, 0, sizeof(SMB_buf));
    reply_status = 0;
    reply_result = reply;
    main_socket = 12;
    close_calls = shutdown_calls = 0;
}
static int close_once(FHANDLE *fh, iop_file_t *file) {
    memset(fh, 0, sizeof(*fh));
    fh->mode = O_DIROPEN;
    fh->smb_fid = 77;
    file->privdata = fh;
    return smb_close(file);
}
int main(void) {
    FHANDLE fh;
    iop_file_t file;
    reset_case(32);
    assert(close_once(&fh, &file) == 0 && fh.smb_fid == -1 && !close_calls && main_socket == 12);
    reset_case(32);
    reply_status = 1;
    assert(close_once(&fh, &file) == -EIO && fh.smb_fid == -1 && !close_calls && main_socket == 12);
    reset_case(0);
    assert(close_once(&fh, &file) == -ETIMEDOUT && fh.smb_fid == -1);
    assert(close_calls == 1 && shutdown_calls == 1 && main_socket == -1);
    puts("PASS: SMB FindClose2 releases handles, separates server/transport errors, and aborts timed-out sockets");
    return 0;
}
"""

with tempfile.TemporaryDirectory(prefix="smb-findclose-") as tmp:
    source = Path(tmp) / "test.c"
    exe = Path(tmp) / "test"
    source.write_text(code)
    compiler = shutil.which("gcc") or shutil.which("clang")
    assert compiler, "A C host compiler is required"
    subprocess.run([compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
