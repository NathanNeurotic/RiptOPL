"""Host test for src/platform/selfupdate.c, the portable half of Check for Updates.

Builds selfupdate.c with a small driver and checks it against the real SHA256SUMS.txt of each rolling
channel (copies in fixtures/selfupdate, taken 2026-11-12): the ELF's digest and the release's build
number, the build number in RiptOPL's version strings, the GitHub-only redirect allowlist, path
joining for mc0:/mass0: and the ELF header check. Also checks the console side keeps its safety
order: the digest is compared before the running ELF is touched, and the ELF is overwritten in place.
"""
import os
import re
import struct
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIX = Path(__file__).resolve().parent / "fixtures/selfupdate"
CC = os.environ.get("CC") or shutil.which("cc") or shutil.which("gcc")
assert CC, "needs a C compiler"

DRIVER = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/selfupdate.h"

#define APA_IDMAX 32
@APA_BOOT_PARSER@

static char *slurp(const char *path, int *len)
{
    FILE *f = fopen(path, "rb");
    char *data;
    long size;
    if (f == NULL)
        return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc(size + 1);
    *len = (int)fread(data, 1, size, f);
    fclose(f);
    return data;
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "rev")) {
        printf("%d\n", suRevision(argv[2]));
    } else if (argc == 4 && !strcmp(argv[1], "sums")) {
        unsigned char sha[32];
        int len, rev = -2, i, rc;
        char *text = slurp(argv[2], &len);
        rc = text ? suParseSums(text, len, argv[3], sha, &rev) : -9;
        printf("rc=%d rev=%d sha=", rc, rev);
        for (i = 0; rc == 0 && i < 32; i++)
            printf("%02x", sha[i]);
        printf("\n");
    } else if (argc == 4 && !strcmp(argv[1], "url")) {
        char host[SU_HOST_MAX];
        const char *path = suSplitUrl(argv[2], host, atoi(argv[3]));
        printf(path ? "%s|%s\n" : "NULL\n", host, path);
    } else if (argc == 6 && !strcmp(argv[1], "join")) {
        char out[64];
        int rc = suJoin(out, atoi(argv[5]), argv[2], argv[3], argv[4]);
        printf(rc == 0 ? "%s\n" : "FAIL\n", out);
    } else if (argc == 3 && !strcmp(argv[1], "apa")) {
        char partition[38], directory[256];
        int rc = hddParseBootDirectory(argv[2], partition, sizeof(partition), directory, sizeof(directory));
        printf(rc == 0 ? "%s|%s\n" : "FAIL\n", partition, directory);
    } else if (argc == 3 && (!strcmp(argv[1], "elf") || !strcmp(argv[1], "format"))) {
        int len = 0;
        unsigned char *data = (unsigned char *)slurp(argv[2], &len);
        printf("%d\n", !strcmp(argv[1], "format") ? suElfFormat(data, len) : suLooksLikeElf(data, len));
        free(data);
    } else {
        return 2;
    }
    return 0;
}
"""

VERSION_DRIVER = r"""
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "include/selfupdate.h"
#define SU_FIO_WRONLY 0x0002
#define SU_FIO_CREAT  0x0200
#define SU_FIO_TRUNC  0x0400
static char path[272], contents[32];
static int shortWrite, closes, suRemoteRev = 4321, suStep;
int fileXioOpen(const char *name, int, int) { snprintf(path, sizeof(path), "%s", name); return 1; }
int fileXioWrite(int, const void *data, int len) {
    if (shortWrite) return -5;
    memcpy(contents, data, (size_t)len);
    contents[len] = 0;
    return len;
}
int fileXioClose(int) { closes++; return 0; }
@VERSION_WRITER@
int main() {
    int rc = suWriteVersion("mc0:/APPS/RIPTOPL.ELF");
    assert(rc == 0 && !strcmp(path, "mc0:/APPS/version.txt"));
    assert(!strcmp(contents, "4321\n") && suStep == 6);
    shortWrite = 1;
    assert(suWriteVersion("mc0:/APPS/RIPTOPL.ELF") < 0 && closes == 2);
    puts("version.txt: sibling path, validated decimal revision, and write failure handling ok");
    return 0;
}
"""

APA_DRIVER = r"""
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#define SU_FIO_CREAT 0x0200
#define SU_FIO_TRUNC 0x0400
#define ENOENT 2
#define EEXIST 17
typedef struct { int size; } iox_stat_t;
struct SuFile { unsigned char *data; int written; };
static int pathExists = 1, stagedExists, backupExists, writes, removeCalls, syncCalls, syncFailAt = -1, statError, stagedSize;
static int suStep;
static char stagedData[32];
static int suWriteElf(const char *, int, const SuFile &) { writes++; stagedExists = 1; stagedSize = 4; memcpy(stagedData, "ELF!", 4); return 0; }
int fileXioGetStat(const char *path, iox_stat_t *st) {
    st->size = stagedSize;
    if (strstr(path, ".update")) return stagedExists ? 0 : -ENOENT;
    if (strstr(path, ".bak")) return backupExists ? 0 : -ENOENT;
    return statError ? statError : pathExists ? 0 : -ENOENT;
}
int fileXioOpen(const char *, int, int) { return 1; }
int fileXioRead(int, void *data, int len) { memcpy(data, stagedData, (size_t)len); return len; }
int fileXioClose(int) { return 0; }
int fileXioSync(const char *, int) { return syncCalls++ == syncFailAt ? -1 : 0; }
int fileXioRename(const char *from, const char *to) {
    if (strstr(to, ".bak")) { backupExists = 1; pathExists = 0; }
    else if (strstr(from, ".update")) { pathExists = 1; stagedExists = 0; }
    else { pathExists = 1; backupExists = 0; }
    return 0;
}
int fileXioRemove(const char *path) {
    if (strstr(path, ".update")) { stagedExists = 0; return 0; }
    if (removeCalls++ == 0) return -1;
    backupExists = 0;
    return 0;
}
@APA_INSTALL@
static void resetInstall(void) {
    pathExists = 1; stagedExists = backupExists = 0; stagedSize = 0;
    syncCalls = 0; syncFailAt = -1; statError = 0; stagedData[0] = 0;
}
int main() {
    SuFile file{(unsigned char *)"ELF!", 4};
    int rc = suInstallApa("pfs1:/launcher/RIPTOPL.ELF", file);
    assert(rc == 0 && pathExists && backupExists && writes == 1);
    rc = suInstallApa("pfs1:/launcher/RIPTOPL.ELF", file);
    assert(rc == 0 && !backupExists && writes == 2);
    puts("APA committed swap survives cleanup failure and permits the next update");

    resetInstall(); syncFailAt = 1;
    rc = suInstallApa("pfs1:/launcher/RIPTOPL.ELF", file);
    assert(rc < 0 && backupExists && pathExists && !stagedExists);
    puts("APA post-swap sync failure preserves the backup");

    resetInstall(); backupExists = 1; statError = -95;
    rc = suInstallApa("pfs1:/launcher/RIPTOPL.ELF", file);
    assert(rc == -95 && backupExists && writes == 3);
    puts("APA original-stat error preserves the backup");

    resetInstall(); stagedExists = 1; stagedSize = 2; memcpy(stagedData, "EL", 2);
    rc = suInstallApa("pfs1:/launcher/RIPTOPL.ELF", file);
    assert(rc == 0 && writes == 4);
    puts("APA matching partial stage is cleaned and retried");

    resetInstall(); stagedExists = 1; stagedSize = 2; memcpy(stagedData, "XX", 2);
    rc = suInstallApa("pfs1:/launcher/RIPTOPL.ELF", file);
    assert(rc == -EEXIST && stagedExists && writes == 4);
    puts("APA unrecognized stage is preserved");

    resetInstall(); stagedExists = 1; stagedSize = -2147483648; memcpy(stagedData, "EL", 2);
    rc = suInstallApa("pfs1:/launcher/RIPTOPL.ELF", file);
    assert(rc == -EEXIST && stagedExists && writes == 4);
    puts("APA negative staged size is preserved as a mismatch");
    return 0;
}
"""

failures = []
checks = 0


def check(name, ok, detail=""):
    global checks
    checks += 1
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else f": {detail}"))
    if not ok:
        failures.append(name)


def run(exe, *args):
    return subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True, check=True).stdout.strip()


def sums_line(path, name):
    for line in Path(path).read_text().splitlines():
        digest, _, file = line.partition("  ")
        if file == name:
            return digest
    raise KeyError(name)


def source_checks():
    src = (ROOT / "src/ui/selfupdate.cpp").read_text(encoding="utf-8")
    inst = src[src.index("static void suInstallWorker"):]
    inst = inst[:inst.index("\n}\n")]
    # The digest is compared before anything touches the running ELF, and a mismatch removes the download.
    a = inst.index("suInstallApa(")
    check("verify_before_swap", inst.index("memcmp(") < a and inst.index("suElfFormat(") < a)
    check("overwrite_in_place", "suWriteElf(target.path, SU_FIO_CREAT | SU_FIO_TRUNC, f)" in inst)
    apa = src[src.index("static int suInstallApa("):src.index("static void suInstallWorker(")]
    check("stage_before_apa_swap", apa.index("suWriteElf(staged,") < apa.index("fileXioRename(path, backup)"))
    check("rollback_apa_swap", "fileXioRename(backup, path)" in apa)
    version_call = "suWriteVersion(target.path)"
    version_pos = inst.rindex(version_call)
    success_pos = inst.rindex("suResult = SU_OK")
    check("version_after_install", version_call in inst and
          inst.index("suInstallApa(") < version_pos and
          inst.index("suWriteElf(target.path, SU_FIO_CREAT | SU_FIO_TRUNC, f)") < version_pos and
          version_pos < success_pos,
          "both ELF install paths complete before the validated revision write, which precedes success")
    labels = re.findall(r"^\s*(?:-\s*)?label:\s*(\S+)\s*$", (ROOT / "lng/tmpl/_base.yml").read_text(encoding="utf-8"), re.M)
    check("update_labels_append", labels.index("UPDATE_RELAUNCH") > labels.index("UPDATE_ERR_RESTORE") and
          labels.index("UPDATE_RELAUNCH_FAILED") > labels.index("UPDATE_ERR_RESTORE"),
          "new update labels must append after existing labels to preserve positional language-pack IDs")
    check("boot_version_txt_rejected", 'strcasecmp(gBootElfName, "version.txt") == 0' in src,
          "a boot ELF named version.txt in any case must be rejected before update writes")
    check("metadata_retry_result_checked", "suVersionPending && suResult != SU_OK" in src,
          "a failed metadata retry must report instead of reaching UPDATE_DONE")
    check("apa_stat_error_preserves_backup", "originalStat" in apa and "return originalStat" in apa,
          "an original-stat I/O error must abort before removing the previous backup")
    check("apa_sync_error_preserves_backup", "syncRc" in apa and "syncRc < 0" in apa,
          "a post-swap sync failure must leave the backup in place")
    check("staged_requires_download_match", "suStagedMatches" in apa,
          "an interrupted stage is reused or removed only after comparison with the downloaded bytes")
    check("metadata_retry_is_separate", "suVersionPending" in src and "_STR_UPDATE_ERR_METADATA" in src and
          "suVersionPending = 1" in inst,
          "metadata failure is distinct and records a retry without another ELF install")
    # Redirects are followed only to GitHub's hosts, and only a few times.
    check("redirects_allowlisted", "suSplitUrl(" in src and re.search(r"for \(hop = 0; hop < 4", src) is not None)
    callers = sorted(p.relative_to(ROOT).as_posix() for p in (ROOT / "src").rglob("*.c*")
                     if "selfUpdateMenu" in p.read_text(encoding="utf-8", errors="replace"))
    check("no_auto_check", callers == ["src/ui/menusys.cpp", "src/ui/selfupdate.cpp"],
          f"the check runs only from the menu: {callers}")


def main():
    source_checks()
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        hdd = (ROOT / "src/platform/hdd.cpp").read_text(encoding="utf-8")
        parser = hdd[hdd.index("int hddParseBootDirectory("):].split("\n}\n", 1)[0] + "\n}"
        (tmp / "driver.c").write_text(DRIVER.replace("@APA_BOOT_PARSER@", parser))
        exe = tmp / ("driver.exe" if os.name == "nt" else "driver")
        subprocess.run([CC, "-O1", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT), str(tmp / "driver.c"),
                        str(ROOT / "src/platform/selfupdate.c"), "-o", str(exe)], check=True)

        for version, rev in (("v1.2.0-Beta-4256-6faabe3-PS2BUILD-Korium-GCC", 4256), ("v1.2.0-4256", 4256),
                             ("v1.2.0-Beta-4257-6faabe3-dirty", 4257), ("v1.2.0-Beta-4257-1234567-LLVM", 4257),
                             ("v1.2.0", -1), ("v1.2.0-Beta--dirty", -1), ("", -1), ("Beta-4256", -1),
                             ("v1.2.0-Beta-12a-4256", 4256), ("v1.2.0-Beta-999999999999", -1)):
            got = int(run(exe, "rev", version))
            check(f"revision {version!r}", got == rev, f"got {got}, want {rev}")

        for channel, elf in (("rolling-korium", "RIPTOPL.ELF"), ("rolling-incebtion", "RIPTOPL.ELF"),
                             ("rolling-ra", "RIPTOPL-RA.ELF"), ("rolling-ra", "RIPTOPL-RA-IncebtIon.ELF"),
                             ("rolling-llvm", "RIPTOPL.ELF"), ("rolling-llvm", "RIPTOPL-RA.ELF")):
            path = FIX / f"{channel}.txt"
            text = path.read_text()
            want_rev = int(re.search(r"-v[\d.]+-Beta-(\d+)\.zip", text).group(1))
            out = run(exe, "sums", path, elf)
            check(f"sums {channel} {elf}", out == f"rc=0 rev={want_rev} sha={sums_line(path, elf)}", out)

        base = (FIX / "rolling-korium.txt").read_text()
        cases = {
            "missing_elf": (base, "RIPTOPL-NOPE.ELF"),
            "prefix_name": (base, "RIPTOPL"),  # names match exactly, never by prefix
            "mixed_versions": (base.replace("RIPTOPL-LANGS-v1.2.0-Beta-4257", "RIPTOPL-LANGS-v1.2.0-Beta-4258"), "RIPTOPL.ELF"),
            "no_versioned_zip": ("\n".join(l for l in base.splitlines() if ".zip" not in l) + "\n", "RIPTOPL.ELF"),
            "bad_hex": (base.replace("c813fc83", "c813fcZ3"), "RIPTOPL.ELF"),
        }
        for name, (text, elf) in cases.items():
            (tmp / "sums.txt").write_text(text)
            out = run(exe, "sums", tmp / "sums.txt", elf)
            check(f"sums_refuses_{name}", out.startswith("rc=-1"), out)
        (tmp / "crlf.txt").write_bytes(base.replace("\n", "\r\n").encode())
        out = run(exe, "sums", tmp / "crlf.txt", "RIPTOPL.ELF")
        check("sums_crlf", out == f"rc=0 rev=4257 sha={sums_line(FIX / 'rolling-korium.txt', 'RIPTOPL.ELF')}", out)
        (tmp / "nolf.txt").write_text(base.rstrip("\n"))
        out = run(exe, "sums", tmp / "nolf.txt", "RIPTOPL-BUGREPORT-v1.2.0-Beta-4257.zip")
        check("sums_last_line_without_newline", out.startswith("rc=0 rev=4257"), out)

        for url, want in (
            ("https://github.com/NathanNeurotic/RiptOPL/releases/download/rolling-korium/RIPTOPL.ELF",
             "github.com|/NathanNeurotic/RiptOPL/releases/download/rolling-korium/RIPTOPL.ELF"),
            ("https://release-assets.githubusercontent.com/github-production-release-asset/1?sig=x",
             "release-assets.githubusercontent.com|/github-production-release-asset/1?sig=x"),
            ("https://objects.githubusercontent.com/x", "objects.githubusercontent.com|/x"),
            ("http://github.com/x", "NULL"),
            ("https://github.com.evil.example/x", "NULL"),
            ("https://evil.example/github.com/x", "NULL"),
            ("https://github.com:444/x", "NULL"),
            ("https://user@github.com/x", "NULL"),
            ("https://140.82.112.3/x", "NULL"),
            ("https://github.com", "NULL"),
            ("/relative/path", "NULL"),
        ):
            out = run(exe, "url", url, 64)
            check(f"url {url[:48]}", out == want, out)
        check("url_host_buffer_too_small", run(exe, "url", "https://github.com/x", 10) == "NULL")

        for args, want in ((("mc0:", "RIPTOPL.ELF", ".NEW", 64), "mc0:RIPTOPL.ELF.NEW"),
                           (("mc0:/APPS", "RIPTOPL.ELF", "", 64), "mc0:/APPS/RIPTOPL.ELF"),
                           (("mass0:/", "RIPTOPL.ELF", ".BAK", 64), "mass0:/RIPTOPL.ELF.BAK"),
                           (("mass0:/RIPTOPL", "RIPTOPL.ELF", ".BAK", 64), "mass0:/RIPTOPL/RIPTOPL.ELF.BAK"),
                           (("mass0:/RIPTOPL", "RIPTOPL.ELF", ".BAK", 30), "FAIL"),
                           (("mass0:/RIPTOPL", "RIPTOPL.ELF", ".BAK", 31), "mass0:/RIPTOPL/RIPTOPL.ELF.BAK")):
            out = run(exe, "join", *args)
            check(f"join {args}", out == want, out)

        mips = bytes([0x7F, ord("E"), ord("L"), ord("F"), 1, 1, 1, 0]) + bytes(8) + bytes([2, 0, 8, 0]) + bytes(44)
        for name, data, want in (("mips_exec", mips, "1"), ("html", b"<!DOCTYPE html><html>" + bytes(60), "0"),
                                 ("x86_64", mips[:4] + bytes([2]) + mips[5:18] + bytes([0x3E]) + mips[19:], "0"),
                                 ("big_endian", mips[:5] + bytes([2]) + mips[6:], "0"),
                                 ("relocatable", mips[:16] + bytes([1]) + mips[17:], "0"),
                                 ("short", mips[:19], "0")):
            (tmp / "head.bin").write_bytes(data)
            check(f"elf_{name}", run(exe, "elf", tmp / "head.bin") == want)

        def elf_format(data_offset, address, align, headers=1):
            header = bytearray(mips[:52])
            struct.pack_into("<II", header, 24, address, 52)
            struct.pack_into("<HH", header, 42, 32, headers)
            header.extend(struct.pack("<8I", 1, data_offset, address, address, 16, 16, 7, align))
            header.extend(bytes(32 * (headers - 1)))
            return header

        packed = elf_format(96, 0x01B00000, 16)
        for name, data, want in (
                ("packed", packed, "1"),
                ("gnu_unpacked", elf_format(128, 0x00100000, 128), "2"),
                ("llvm_unpacked", elf_format(256, 0x00100000, 128, 3), "2"),
                ("short_program_headers", packed[:83], "0"),
                ("unknown_layout", elf_format(128, 0x00200000, 128), "0")):
            (tmp / "head.bin").write_bytes(data)
            check(f"format_{name}", run(exe, "format", tmp / "head.bin") == want)

        for directory, want in (
                ("hdd0:__system:pfs:/launcher", "hdd0:__system|/launcher"),
                ("hdd0:/__system/launcher", "hdd0:__system|/launcher"),
                ("hdd0:__system:pfs0:launcher", "hdd0:__system|/launcher"),
                ("hdd0:+OPL:pfs:", "hdd0:+OPL|/"),
                ("pfs0:/launcher", "FAIL"),
                ("hdd0::pfs:/launcher", "FAIL"),
                ("hdd0:__system:other:/launcher", "FAIL")):
            check(f"apa_{directory}", run(exe, "apa", directory) == want)

        updater = (ROOT / "src/ui/selfupdate.cpp").read_text(encoding="utf-8")
        if "static int suWriteVersion(" in updater:
            writer = updater[updater.index("static int suWriteVersion("):]
            writer = writer[:writer.index("\n}\n") + 2]
            (tmp / "version-driver.cpp").write_text(VERSION_DRIVER.replace("@VERSION_WRITER@", writer))
            subprocess.run([CC, "-O1", "-I", str(ROOT), "-c", str(ROOT / "src/platform/selfupdate.c"),
                            "-o", str(tmp / "selfupdate.o")], check=True)
            version_exe = tmp / ("version-driver.exe" if os.name == "nt" else "version-driver")
            subprocess.run([os.environ.get("CXX", "c++"), "-O1", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
                            str(tmp / "version-driver.cpp"), str(tmp / "selfupdate.o"), "-o", str(version_exe)], check=True)
            subprocess.run([str(version_exe)], check=True)
            check("version_metadata_behavior", True)
        else:
            check("version_metadata_behavior", False, "suWriteVersion is missing")

        apa_start = updater.index("static int suStagedMatches(") if "static int suStagedMatches(" in updater else updater.index("static int suInstallApa(")
        apa = updater[apa_start:updater.index("static void suInstallWorker(")]
        (tmp / "apa-driver.cpp").write_text(APA_DRIVER.replace("@APA_INSTALL@", apa))
        apa_exe = tmp / ("apa-driver.exe" if os.name == "nt" else "apa-driver")
        subprocess.run([os.environ.get("CXX", "c++"), "-O1", "-Wall", "-Wextra", "-Werror",
                        str(tmp / "apa-driver.cpp"), "-o", str(apa_exe)], check=True)
        subprocess.run([str(apa_exe)], check=True)
        check("apa_committed_swap_retry", True)

    if failures:
        print(f"{len(failures)} failure(s)")
        sys.exit(1)
    print(f"PASS: selfupdate host tests ({checks} checks)")


if __name__ == "__main__":
    main()
