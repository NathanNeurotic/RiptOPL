#!/usr/bin/env python3
"""In-game freeze report (INGAMEDIAG, the ingamediag configuration) structural checks.

The report must not exist in any other build, must not be able to hang IGR on a stuck IOP, and
must capture thread waits and priorities after the handler suspends the game. The text formatter is
covered by ee_core/tests/test_freeze_diag_format.c, which this compiles and runs.

Ported from the original's test of the same name. RiptOPL has no Makefiles: the build checks read
tools/build_configuration.py's ingamediag configuration instead.
"""

import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
FAILURES = []


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8").replace("\r\n", "\n")


def check(cond, what):
    if not cond:
        FAILURES.append(what)


TOKEN = re.compile(r"freeze_diag|FreezeDiag|FREEZE_DIAG|CDSC_GET_FREEZE_DIAG")


def positive_diag_guard(line):
    return re.fullmatch(r"#ifdef\s+INGAMEDIAG|#(?:if|elif)\s+defined\s*(?:\(\s*INGAMEDIAG\s*\)|INGAMEDIAG)", line) is not None


assert positive_diag_guard("#ifdef INGAMEDIAG")
assert positive_diag_guard("#elif defined(INGAMEDIAG)")
assert not positive_diag_guard("#ifndef INGAMEDIAG")
assert not positive_diag_guard("#if !defined(INGAMEDIAG)")
assert not positive_diag_guard("#if defined(INGAMEDIAG) || OTHER")


def unguarded_lines(rel):
    """Lines that mention the report outside any #if/#ifdef naming INGAMEDIAG."""
    stack = []
    bad = []
    for number, line in enumerate(read(rel).split("\n"), 1):
        s = line.strip()
        if s.startswith("#if"):
            stack.append(positive_diag_guard(s))
            continue
        if s.startswith("#elif"):
            if stack:
                stack[-1] = positive_diag_guard(s)
            continue
        if s.startswith("#else"):
            if stack:
                stack[-1] = False
            continue
        if s.startswith("#endif"):
            if stack:
                stack.pop()
            continue
        if TOKEN.search(line) and not any(stack):
            bad.append(f"{rel}:{number}: {s}")
    return bad


# 1. Nothing of the report outside INGAMEDIAG in the shared sources.
for rel in [
    "ee_core/src/padhook.c",
    "ee_core/src/iopmgr.c",
    "modules/iopcore/cdvdman/cdvdman.c",
    "modules/iopcore/cdvdman/internal.h",
    "modules/iopcore/cdvdman/device-bdm.c",
    "modules/iopcore/cdvdman/device-mmce.c",
    "modules/iopcore/cdvdfsv/cdvdfsv.c",
]:
    for line in unguarded_lines(rel):
        FAILURES.append("report code outside #ifdef INGAMEDIAG: " + line)

import build_edition as editions  # noqa: E402

for edition in ("korium", "incebtion"):
    plain = {t["name"]: t for t in editions.definition(edition)["targets"]}
    diag = {t["name"]: t for t in editions.definition(edition, "ingamediag")["targets"]}
    check(all("INGAMEDIAG" not in t.get("defines", []) for t in plain.values()) and "freezediag" not in plain,
          f"{edition}: the standard build carries nothing of the report")
    for name, target in diag.items():
        wanted = name in ("riptopl", "eecore", "cdvdfsv") or name.endswith("_cdvdman")
        check(("INGAMEDIAG" in target.get("defines", [])) == wanted, f"{edition}: {name} defines INGAMEDIAG only when it must")
    check("ee_core/src/freeze_diag.c" in diag["eecore"]["sources"] and "ee_core/src/freeze_diag.c" not in plain["eecore"]["sources"],
          "ee_core builds freeze_diag.c only under INGAMEDIAG (formatting and file I/O live in freezediag.irx)")
    check("freezediag" in diag["riptopl"]["embed_irx"], "the menu embeds freezediag.irx only in the configuration")
    check(diag["freezediag"]["sources"] == ["modules/iopcore/freezediag/freezediag.c", "ee_core/src/freeze_diag_format.c"],
          "the IRX uses ee_core's host-tested formatter")
    check("FREEZEDIAG_BUILD=OPL_VERSION" in diag["freezediag"]["defines"], "the report names the build it came from")
try:
    editions.definition("ra", "ingamediag")
    FAILURES.append("ingamediag must refuse RetroAchievements (RA runs IGR from VBLANK)")
except ValueError:
    pass
check("{edition: korium, configuration: ingamediag}" in read(".github/workflows/configurations.yml"),
      "CI builds the ingamediag configuration")

# 2. IGR ordering in padhook.c. The VBLANK_END handler stops the game; the worker reports.
pad = read("ee_core/src/padhook.c")
handler = pad[pad.index("static int IGR_Intc_Handler(int cause)"):]
handler = handler[:handler.index("\n}\n")]
check("g_freeze_diag_polls++;" in handler[:handler.index("IGR_CheckInputs();")], "each VBLANK input sample is counted")
check(handler.index("iSuspendThread(i);") < handler.index("iWakeupThread(IGR_Thread_ID);"),
      "the handler suspends the game's threads before waking the worker")
worker = pad[pad.index("static void IGR_Thread(void *arg)"):]
i_wait = worker.index("SleepThread();")
i_begin = worker.index("FreezeDiag_Begin(")
i_fetch = worker.index("FreezeDiag_FetchAndShutdown();")
check(i_wait < i_begin < i_fetch, "threads are captured once the worker wakes, before the IOP shutdown")
check("if (Pad_Data.combo_type == IGR_COMBO_START_SELECT) {" in worker[i_wait:i_begin],
      "only the reset combo arms a report (not IGS or power-off)")
check("SuspendThread(i);" not in worker[:i_fetch],
      "the worker does not repeat suspension; reports include the handler's suspension")

thread = pad[pad.index("static void IGR_Thread(void *arg)"):]
gate = re.search(r"#ifdef INGAMEDIAG\n(?:\s*//[^\n]*\n)*\s*FreezeDiag_FetchAndShutdown\(\);\n#else\n#ifdef RETROACHIEVEMENTS\n(?:\s*//[^\n]*\n)*\s*if \(config->GameMode != DISC_MODE\)\n#endif\n\s*oplIGRShutdown\(0\);\n#endif\n", thread)
check(gate is not None, "the flavour replaces the blocking shutdown RPC; other builds keep theirs unchanged")
i_lmb = thread.index("sbv_patch_enable_lmb();")
i_write = thread.index("FreezeDiag_Write();")
i_spu = thread.index("LoadOPLModule(OPL_MODULE_ID_RESETSPU")
check(i_lmb < i_write < i_spu, "the report is written after the IGR IOP reboot, before RESETSPU")

# 3. freeze_diag.c: bounded waits, non-blocking bind, the reboot after writing.
core = read("ee_core/src/freeze_diag.c")
check("SifBindRpc(&igr_cd, IGR_RPC_SERVER, SIF_RPC_M_NOWAIT)" in core, "the bind does not block on a stuck IOP")
check(re.search(r"SifCallRpc\([^;]*SIF_RPC_M_NOWAIT", core) is not None, "the call does not block on a stuck IOP")
for match in re.finditer(r"while \(SifCheckStatRpc\(&(?:igr_cd|shutdown_cd)\)\) \{(.*?)\n    \}", core, re.S):
    check("budget_spent(start)" in match.group(1), "every RPC wait has a deadline")
check(len(re.findall(r"while \(SifCheckStatRpc", core)) == 3, "three RPC waits (bind, record, shutdown), all bounded")
fas = core[core.index("void FreezeDiag_FetchAndShutdown(void)"):]
fas = fas[:fas.index("\n}\n")]
check(re.search(r"if \(!report_armed\) \{\n\s*oplIGRShutdown\(0\);\n\s*return;", fas) is not None,
      "without a report to take (IGS, power-off) the shutdown is exactly oplIGRShutdown(0)")
check(fas.index("fetch_iop()") < fas.index("shutdown_with_deadline()"), "the record is fetched before the shutdown changes the IOP")
check("FREEZE_DIAG_SHUTDOWN_SKIPPED" in fas, "bind failure skips shutdown")
sd = core[core.index("static void shutdown_with_deadline(void)"):]
sd = sd[:sd.index("\n}\n")]
check(re.search(r"SifCallRpc\(&shutdown_cd, 1, SIF_RPC_M_NOWAIT", sd) is not None, "the shutdown is RPC function 1, sent without blocking")
check("= 0; // 0 = shut down, do not power off" in sd, "the shutdown asks for the same thing as oplIGRShutdown(0)")
check(core.count("budget_spent(start)") >= 3, "the bind retry loop also has a deadline")
w = core[core.index("void FreezeDiag_Write(void)"):]
check(w.index("LoadModule(\"rom0:MCMAN\"") < w.index("FlushCache(0)") < w.index("CopyToIop(") < w.index("LoadOPLModule(OPL_MODULE_ID_FREEZEDIAG") < w.index("Reset_Iop(\"\", 0)"),
      "ROM MCMAN, cache flushed before the DMA, record handed to freezediag.irx, then a clean IOP reboot")
check("fio" not in core and "freeze_diag_format(" not in core, "ee_core carries no file client or formatter (stack budget)")
irx = read("modules/iopcore/freezediag/freezediag.c")
for path in re.findall(r'"(mc[01]:/[^"]+)"', irx):
    name = path.split(":/", 1)[1]
    check("/" not in name and len(name) <= 31, f"report path {path} fits a memory card name")
check("O_WRONLY | O_CREAT | O_TRUNC" in irx, "IOP ioman flags")
check("freeze_diag_format(" in irx, "the IRX uses the host-tested formatter")
fmt = read("ee_core/src/freeze_diag_format.c")
check("long long" not in fmt, "the formatter needs no 64-bit division (IOP has none)")
mods = read("ee_core/include/modules.h")
check(re.search(r"#ifdef INGAMEDIAG\n(?:\s*//[^\n]*\n)*\s*OPL_MODULE_ID_FREEZEDIAG,\n#endif\n\n\s*OPL_MODULE_ID_PCCF = 60,\n(?:[^\n]*\n)*?\s*OPL_MODULE_ID_COUNT", mods) is not None,
      "the diagnostic ID is guarded, and the CF ID is pinned after it, so every other ID is unchanged")
modmgr = read("ee_core/src/modmgr.c")
check(re.search(r"result == -400\n(?:#if[^\n]*\n[^\n]*\n#endif\n)*#ifdef INGAMEDIAG\n\s*&& id != OPL_MODULE_ID_FREEZEDIAG\n#endif", modmgr) is not None,
      "a failed report writer never traps IGR in the out-of-IOP-memory loop")
sysc = read("src/platform/system.cpp")
check(re.search(r"#ifdef INGAMEDIAG\n(?:\s*//[^\n]*\n)*\s*irxptr_tab\[modcount\]\.info = size_freezediag_irx", sysc) is not None,
      "the menu registers freezediag.irx only in the flavour")

# 4. Shared constants.
opl_h = read("modules/iopcore/common/cdvdman_opl.h")
codes = re.findall(r"#define (CDSC_\w+)\s+(0x[0-9A-Fa-f]+)", opl_h)
values = [int(v, 16) for _, v in codes]
check(len(values) == len(set(values)), "CDSC codes are unique")
check(("CDSC_GET_FREEZE_DIAG", "0xFFFFFFEF") in codes, "freeze report CDSC code defined")
shared = read("modules/iopcore/common/freeze_diag.h")
check(re.search(r"#define FREEZE_DIAG_RPC_FNO\s+2\b", shared) is not None, "RPC function 2 (1 is shutdown)")

# 5. The formatter's host test.
with tempfile.TemporaryDirectory() as tmp:
    exe = Path(tmp) / "test_freeze_diag_format"
    built = subprocess.run(["cc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-Iee_core/include",
                            "ee_core/tests/test_freeze_diag_format.c", "ee_core/src/freeze_diag_format.c", "-o", str(exe)],
                           cwd=ROOT, capture_output=True, text=True)
    check(built.returncode == 0, "the formatter host test builds: " + built.stderr.strip()[-400:])
    if built.returncode == 0:
        ran = subprocess.run([str(exe)], capture_output=True, text=True)
        check(ran.returncode == 0, "the formatter host test passes: " + ran.stdout.strip()[-400:])

if FAILURES:
    for f in FAILURES:
        print("FAIL:", f)
    sys.exit(1)
print("test_ingame_freeze_diag: all checks passed")
