"""Compile the real SMB1 DOS-status discriminator and verify FIND_FIRST2 EOF behavior."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "modules/network/smbman-ra/smb.c").read_text()
header = (root / "modules/network/smbman-ra/smb.h").read_text()
start = source.index("static int smbFindFirstEmptyDosStatus(")
end = source.index("\n}\n", start) + 2
helper = source[start:end]
find = source[source.index("int smb_FindFirstNext2("):]
assert "if (smbFindFirstEmptyDosStatus(cmd," in find
assert "info->EOS = 1;" in find
macros = ["TRANS2_FIND_FIRST2", "TRANS2_FIND_NEXT2", "DOSERR_FILE_NOT_FOUND",
          "DOSERR_PATH_NOT_FOUND", "DOSERR_NO_MORE_FILES", "STATUS_NO_SUCH_FILE",
          "STATUS_OBJECT_PATH_NOT_FOUND"]
defines = []
for key in macros:
    m = re.search(r"^#define\s+" + key + r"\s+0x[0-9a-fA-F]+", header, re.M)
    assert m, key
    defines.append(m.group())
code = "#include <assert.h>\n#include <stdint.h>\ntypedef uint32_t u32;\n" + "\n".join(defines) + "\n" + helper + r"""
int main(void)
{
    assert(smbFindFirstEmptyDosStatus(TRANS2_FIND_FIRST2, DOSERR_FILE_NOT_FOUND));
    assert(!smbFindFirstEmptyDosStatus(TRANS2_FIND_NEXT2, DOSERR_FILE_NOT_FOUND));
    assert(!smbFindFirstEmptyDosStatus(TRANS2_FIND_FIRST2, DOSERR_PATH_NOT_FOUND));
    assert(!smbFindFirstEmptyDosStatus(TRANS2_FIND_FIRST2, STATUS_OBJECT_PATH_NOT_FOUND));
    assert(!smbFindFirstEmptyDosStatus(TRANS2_FIND_FIRST2, STATUS_NO_SUCH_FILE));
    assert(!smbFindFirstEmptyDosStatus(TRANS2_FIND_FIRST2, DOSERR_NO_MORE_FILES));
    return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="smb1-empty-search-") as tmp:
    cfile = Path(tmp) / "search.c"
    exe = Path(tmp) / "search"
    cfile.write_text(code)
    subprocess.run(["gcc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", str(cfile), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: SMB1 empty first-directory DOS code is not confused with path failures or FIND_NEXT")
