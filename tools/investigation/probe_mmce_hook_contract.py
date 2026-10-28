"""Execute the vendored SIO2 hook with synthetic export tables; optionally inspect installed IRXs.

This checks hook selection and balanced calls, not real semaphore scheduling.
"""

from pathlib import Path
import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import tempfile

from probe_navigation_input_gaps import ROOT, source


PIN = "b4c23bdac8f983a4b4e3456d4036b8645cf95842"
PREAMBLE = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned int u32;
typedef struct library {
    unsigned short version;
    char name[9];
    void *exports[64];
} iop_library_t;
typedef struct { unsigned int regdata[16]; } sio2_transfer_data_t;
typedef struct {
    struct {int (*handler)(void *); void *userdata;} interrupt_handler_table[64];
} intrman_internals_t;
static intrman_internals_t intrman;
static iop_library_t libraries[2];
static int library_count, lock_calls, unlock_calls, intr_depth;
#define IRX_VER(major, minor) (((major)<<8) | (minor))
#define IOP_IRQ_SIO2 17
#define DPRINTF(...) ((void)0)
#define SIO2MAN_HOOK_SUPPORT_SET_INTR_HANDLER 1
static void CpuSuspendIntr(int *state) { *state=intr_depth++; }
static void CpuResumeIntr(int state) { intr_depth=state; }
static intrman_internals_t *GetIntrmanInternalData(void) {return &intrman;}
static int ioplib_iterateByName(const char *name, int (*cb)(iop_library_t *,void *),void *data) {
    int count=0;
    for (int i=0; i<library_count; ++i) {
        if (strcmp(name,libraries[i].name)) continue;
        ++count;
        if (cb(&libraries[i],data)) break;
    }
    return count;
}
static void *ioplib_hookSameExportEntries(iop_library_t *lib,unsigned int n,void *fn) {
    void *old=lib->exports[n];
    for (unsigned int i=0; i<64; ++i) if (lib->exports[i]==old) lib->exports[i]=fn;
    return old;
}
static void ioplib_relinkExports(iop_library_t *lib) {(void)lib;}
static void native_lock(void) {++lock_calls;}
static void native_unlock(void) {++unlock_calls;}
static int original_irq(void *p) {(void)p; return 1;}
static int mmce_irq(void *p) {(void)p; return 1;}
'''

MAIN = r'''
int main(void) {
    // The installed SDK names its tables sio2man 2.7 and sio2man1 1.2.
    // The actual iterator matches all eight name bytes, so only the first matches.
    for (int one_version=0; one_version<2; ++one_version) {
        memset(g_hook_data,0,sizeof(g_hook_data));
        memset(libraries,0,sizeof(libraries));
        library_count=one_version ? 1 : 2;
        libraries[0].version=one_version ? IRX_VER(1,2) : IRX_VER(2,7);
        libraries[1].version=IRX_VER(1,2);
        for (int i=0; i<library_count; ++i) {
            strcpy(libraries[i].name,i==1 ? "sio2man1" : "sio2man");
            libraries[i].exports[23]=(void *)native_lock;
            libraries[i].exports[26]=(void *)native_unlock;
        }
        intrman.interrupt_handler_table[IOP_IRQ_SIO2].handler=original_irq;
        intrman.interrupt_handler_table[IOP_IRQ_SIO2].userdata=&lock_calls;
        lock_calls=unlock_calls=0;
        assert(sio2man_hook_init()==0);
        sio2man_hook_sio2_lock();
        sio2man_hook_sio2_set_intr_handler(mmce_irq,&unlock_calls);
        assert(intrman.interrupt_handler_table[IOP_IRQ_SIO2].handler==mmce_irq);
        assert(intrman.interrupt_handler_table[IOP_IRQ_SIO2].userdata==&unlock_calls);
        sio2man_hook_sio2_set_intr_handler(NULL,NULL);
        assert(intrman.interrupt_handler_table[IOP_IRQ_SIO2].handler==original_irq);
        assert(intrman.interrupt_handler_table[IOP_IRQ_SIO2].userdata==&lock_calls);
        sio2man_hook_sio2_unlock();
        assert(lock_calls==!one_version && unlock_calls==!one_version && intr_depth==0);
        printf("{\"case\":\"%s\",\"native_lock_calls\":%d,\"native_unlock_calls\":%d,\"irq_restored\":true}\n",
            one_version ? "synthetic_sio2man_1_2_only" : "sdk_named_export_tables",lock_calls,unlock_calls);
        sio2man_hook_deinit();
    }
}
'''


def exports(path):
    data = path.read_bytes()
    tables = []
    for offset in range(0, len(data) - 20, 4):
        if data[offset:offset + 4] != b"\x00\x00\xc0\x41":
            continue
        name = data[offset + 12:offset + 20].split(b"\0")[0]
        if not name.startswith(b"sio2man"):
            continue
        version = struct.unpack_from("<H", data, offset + 8)[0]
        tables.append({"name": name.decode("ascii"), "version": f"{version >> 8}.{version & 255}",
                       "export_23": struct.unpack_from("<I", data, offset + 20 + 23 * 4)[0],
                       "export_26": struct.unpack_from("<I", data, offset + 20 + 26 * 4)[0]})
    assert tables, f"No sio2man export tables in {path.name}"
    return {"file": path.name, "sha256": hashlib.sha256(data).hexdigest(), "exports": tables}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ref", default=PIN)
    parser.add_argument("--compiler", default=shutil.which("gcc") or shutil.which("cc"))
    parser.add_argument("--sdk-root", type=Path)
    args = parser.parse_args()
    if not args.compiler:
        parser.error("A host C compiler is required")
    hook = source(args.ref, "modules/mmce/mmceman/src/sio2man_hook.c")
    hook = "\n".join(line for line in hook.splitlines() if not line.startswith("#include"))
    with tempfile.TemporaryDirectory(prefix="navigation-mmce-") as temp:
        folder = Path(temp)
        c = folder / "hook.c"
        exe = folder / ("hook.exe" if __import__("os").name == "nt" else "hook")
        c.write_text(PREAMBLE + hook + MAIN, encoding="utf-8")
        subprocess.run([args.compiler, "-std=gnu99", "-O2", "-Wall", "-Wextra",
                        "-Wno-unused-parameter", str(c), "-o", str(exe)], check=True)
        output = subprocess.check_output([str(exe)], text=True, timeout=10)
        for line in output.splitlines():
            print(json.dumps({"ref": args.ref, **json.loads(line)}, sort_keys=True))
    if args.sdk_root:
        for path in sorted((args.sdk_root / "packages/core/sio2man/bin").glob("*.irx")):
            print(json.dumps({"case": "installed_irx", **exports(path)}, sort_keys=True))


if __name__ == "__main__":
    main()
