"""A VMC rebuild interrupted between removing the card and renaming its verified copy leaves only
"<name>.new". sysCheckVMCEx (src/platform/sysvmc.cpp) gives that file the card's name when the card
does not open. The real function runs here over a temporary folder: a lone copy becomes the card
with every byte, a copy beside a card that opens is never looked at, and nothing happens on PFS.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/platform/sysvmc.cpp').read_text(encoding='utf-8')
start = source.index('static int sysVMCAdoptRebuilt(')
helper = source[start:source.index('\n}\n', start) + 3]
check = source[source.index('int sysCheckVMCEx('):]
assert 'if (fd < 0 && sysVMCAdoptRebuilt(path))' in check, 'the card check must adopt a lone copy only when the card does not open'

program = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
@HELPER@
static void put(const char *name, const char *text) { FILE *f = fopen(name, "wb"); assert(f); fputs(text, f); fclose(f); }
static int holds(const char *name, const char *text)
{
    char buffer[32] = {0};
    FILE *f = fopen(name, "rb");
    if (!f)
        return 0;
    fread(buffer, 1, sizeof(buffer) - 1, f);
    fclose(f);
    return !strcmp(buffer, text);
}
static int exists(const char *name) { struct stat s; return stat(name, &s) == 0; }
int main(void)
{
    mkdir("mass0", 0777);
    mkdir("pfs0", 0777);
    put("mass0/card.new", "saves");
    assert(sysVMCAdoptRebuilt("mass0/card.bin") == 1);
    assert(holds("mass0/card.bin", "saves") && !exists("mass0/card.new"));
    assert(sysVMCAdoptRebuilt("mass0/card.bin") == 0); /* no copy: nothing to do */
    assert(holds("mass0/card.bin", "saves"));
    put("pfs0/card.bin", "card");
    put("pfs0/card.new", "half a copy");
    assert(sysVMCAdoptRebuilt("pfs0/card.bin") == 0);
    assert(holds("pfs0/card.bin", "card") && holds("pfs0/card.new", "half a copy"));
    puts("VMC rebuild: a lone verified copy becomes the card again; PFS is never touched");
    return 0;
}
""".replace('@HELPER@', helper)

with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    (temp / 'adopt.c').write_text(program, encoding='utf-8')
    subprocess.run(['gcc', '-std=gnu99', '-Wall', '-Wextra', '-Werror', 'adopt.c', '-o', 'adopt'], check=True, cwd=temp)
    subprocess.run([str(temp / 'adopt')], check=True, cwd=temp, timeout=60)
