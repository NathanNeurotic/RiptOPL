"""Host test for src/platform/selfupdate.c, the portable half of Check for Updates.

Builds selfupdate.c with a small driver and checks it against the real SHA256SUMS.txt of each rolling
channel (copies in fixtures/selfupdate, taken 2026-11-12): the ELF's digest and the release's build
number, the build number in RiptOPL's version strings, the GitHub-only redirect allowlist, path
joining for mc0:/mass0: and the ELF header check. Also checks the console side keeps its safety
order: the digest is compared before the running ELF is touched, and a failed swap restores it.
"""
import os
import re
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
    } else if (argc == 3 && !strcmp(argv[1], "elf")) {
        unsigned char head[64];
        int len = 0;
        char *data = slurp(argv[2], &len);
        memcpy(head, data, len < 64 ? len : 64);
        printf("%d\n", suLooksLikeElf(head, len < 64 ? len : 64));
    } else {
        return 2;
    }
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
    check("verify_before_swap", inst.index("memcmp(") < inst.index("unlink(cur)"), "digest compare must come first")
    check("mismatch_unlinks", "unlink(fresh)" in inst[inst.index("memcmp("):inst.index("unlink(cur)")])
    # The swap: current removed, new into place.
    a, b = inst.index("unlink(cur)"), inst.index("suRename(fresh, cur,")
    check("swap_order", a < b and "bak" not in inst.lower().replace("break", ""), "cur removed, new->cur, no backup")
    # Redirects are followed only to GitHub's hosts, and only a few times.
    check("redirects_allowlisted", "suSplitUrl(" in src and re.search(r"for \(hop = 0; hop < 4", src) is not None)
    callers = sorted(str(p.relative_to(ROOT)) for p in (ROOT / "src").rglob("*.c*")
                     if "selfUpdateMenu" in p.read_text(encoding="utf-8", errors="replace"))
    check("no_auto_check", callers == ["src/ui/menusys.cpp", "src/ui/selfupdate.cpp"],
          f"the check runs only from the menu: {callers}")


def main():
    source_checks()
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        (tmp / "driver.c").write_text(DRIVER)
        exe = tmp / "driver"
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

    if failures:
        print(f"{len(failures)} failure(s)")
        sys.exit(1)
    print(f"PASS: selfupdate host tests ({checks} checks)")


if __name__ == "__main__":
    main()
