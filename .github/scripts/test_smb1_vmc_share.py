"""SMBv1 opens must not lock a VMC away from the next session (issue #418).

A console switched off or reset mid-game never closes its VMC, so the server keeps that handle
until it drops the dead session. Meanwhile the menu's check of the card failed at random with
"Error accessing VMC ...": ps2sdk's smbman opens a file to read sharing READ only, which no server
grants while another handle can write, and the in-game driver opened in compatibility mode, which
Windows maps to a share that denies other readers (MS-CIFS appendix A). SMB2 shares read and write
on both sides and never showed it.

Checks, from the sources:
  1. The in-game SMBv1 OPEN_ANDX asks sharing mode 4 (deny none, 0x40) for reads and writes.
  2. smbman-ra's NT_CREATE_ANDX shares read and write (0x03) when it opens to read, and keeps
     read-only sharing (0x01) for a write.
  3. Only a read-only NT sharing-violation fallback requests deny-none in legacy OPEN_ANDX;
     servers without NT SMB commands and write-capable opens retain their prior behavior.
  4. Every edition embeds smbman-ra, not ps2sdk's prebuilt copy.
"""
import re
import sys

failures = []


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


ingame = read("modules/iopcore/cdvdman/smb.c")
body = re.search(r"static int smb_OpenAndXOnce\(.*?\n}\n", ingame, re.S)
if not body:
    failures.append("in-game smb.c: smb_OpenAndXOnce not found")
elif "OR->AccessMask = (Write ? 2 : 0) | 0x40;" not in body.group(0):
    failures.append("in-game smb.c: OPEN_ANDX must ask deny-none sharing (| 0x40)")

menu = read("modules/network/smbman-ra/smb.c")
body = re.search(r"int smb_NTCreateAndX\(.*?\n}\n", menu, re.S)
want = "NTCR->ShareAccess    = ((mode & O_RDWR) == O_RDWR || (mode & O_WRONLY)) ? 0x01 : 0x03;"
if not body:
    failures.append("smbman-ra smb.c: smb_NTCreateAndX not found")
elif want not in body.group(0):
    failures.append("smbman-ra smb.c: a read must share read|write (0x03), a write read (0x01)")

# An NT sharing violation may retry through SMB_COM_OPEN_ANDX. That specific read-only
# retry needs deny-none, while old NAS servers without NT SMB support retain compatibility
# mode, and writes must never take the held-file retry.
legacy = re.search(r"static int smb_LegacyOpenAndX\(.*?\n}\n", menu, re.S)
open_wrapper = re.search(r"int smb_OpenAndX\(.*?\n}\n", menu, re.S)
if not legacy or not open_wrapper:
    failures.append("smbman-ra smb.c: legacy open or its wrapper not found")
else:
    legacy_src, wrapper_src = legacy.group(0), open_wrapper.group(0)
    if not re.search(r"OR->AccessMask\s*=.*?\|\s*\(deny_none\s*\?\s*0x40\s*:\s*0\);", legacy_src):
        failures.append("smbman-ra smb.c: legacy open must add deny-none only when requested")
    nt_retry = "r = smb_LegacyOpenAndX(UID, TID, filename, filesize, mode, 1);"
    write_guard = "if (((mode & O_RDWR) == O_RDWR) || (mode & O_WRONLY) || (mode & (O_CREAT | O_TRUNC)))"
    guard_patterns = (
        ("legacy-only server", re.escape("if (!(server_specs.Capabilities & SERVER_CAP_NT_SMBS))")
         + r"\s*return\s+" + re.escape("smb_LegacyOpenAndX(UID, TID, filename, filesize, mode, 0);")),
        ("sharing violation", re.escape("if (r != -EBUSY)") + r"\s*return\s+r\s*;"),
        ("write-capable open", re.escape(write_guard) + r"\s*return\s+-EBUSY\s*;"),
    )

    def guards_prevent_retry(source):
        matches = [re.search(pattern, source) for _, pattern in guard_patterns]
        return (all(matches)
                and nt_retry in source
                and matches[0].end() < source.find("smb_NTCreateAndX(")
                < matches[1].start() < matches[1].end()
                < matches[2].start() < matches[2].end() < source.index(nt_retry))

    if not guards_prevent_retry(wrapper_src):
        failures.append("smbman-ra smb.c: only NT sharing-violation reads may retry deny-none")
    else:
        # Mutation check: a syntactically valid empty guard body must fail this test.
        for label, pattern in guard_patterns:
            mutant, count = re.subn(
                pattern,
                lambda match: re.sub(r"\breturn\b[^;]*;", ";", match.group(0), count=1),
                wrapper_src,
                count=1,
            )
            if count != 1 or guards_prevent_retry(mutant):
                failures.append("smbman-ra smb.c: missing-return mutation escaped %s guard" % label)

make = read("Makefile")
assignments = []
depth = 0
for lineno, line in enumerate(make.splitlines(), 1):
    line = line.lstrip()
    if not line or line.startswith("#"):
        continue
    if re.match(r"^(?:ifeq|ifneq|ifdef|ifndef)\b", line):
        depth += 1
    elif re.match(r"^endif\b", line):
        depth -= 1
    else:
        # Reject *any* competing assignment, including :=, ?=, += and conditional variants.
        match = re.match(r"^(?:(?:override|export|private)\s+)*SMBMAN_IRX\s*(?:::?=|\?=|\+=|!=|=)\s*(\S+)", line)
        if match:
            assignments.append((lineno, depth, match.group(1)))
        elif re.match(r"^(?:(?:override|export|private)\s+)*define\s+SMBMAN_IRX\b", line):
            assignments.append((lineno, depth, "<define>"))

# One unconditional assignment means the same fork-built module is used by every flavour.
if len(assignments) != 1 or assignments[0][1:] != (0, "modules/network/smbman-ra/smbman.irx"):
    failures.append("Makefile: SMBMAN_IRX must be set unconditionally, exactly once, to smbman-ra; found %r" % assignments)

if failures:
    print("\n".join(failures))
    sys.exit(1)
print("SMBv1: in-game opens deny none, the menu reads share read|write, every edition embeds smbman-ra")
