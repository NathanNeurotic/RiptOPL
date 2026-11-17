"""Execute the RetroGEM disc-serial extractor and the Ember launch barcode on real image files.

The extractor reads SYSTEM.CNF from the ISO9660 filesystem of a POPStarter VCD (1 MiB header), a raw
2352-byte BIN, a cooked 2048-byte ISO and a CUE sheet's data-track FILE. It never invents an ID from a
folder or file name. The Ember barcode reports disc 1: the first discs.txt entry when the folder has
one (not whichever CUE readdir lists first), else the image Ember itself would pick.
"""
from pathlib import Path
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
retrogem = (ROOT / 'src/sources/retrogem.cpp').read_text()
cue = (ROOT / 'src/sources/cuesupport.cpp').read_text()
header = (ROOT / 'include/cuesupport.h').read_text()


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]


extractor = retrogem[retrogem.index('#define PS1_SECTOR_DATA_SIZE'):retrogem.index('static u8 retrogemCalculateCRC(')]
cue_functions = '\n\n'.join(function(cue, signature) for signature in (
    'static char cueSep(', 'static const char *cueEmberFolder(', 'static void cueBuildGamesDir(',
    'int cueNameLaunchable(', 'int cueBuildDiscArgument(', 'int cueBuildDiscPath(',
    'static int cueAppendDiscLine(', 'static int cueConfirmDiscListAbsent(', 'int cueReadDiscList(',
    'int cueResolveGameImage(', 'int cueResolveFirstDiscImage(', 'void cuePrepareRetroGemBarcode(',
))
exts = re.search(r'static const char \*const cueDiscExts\[\] = \{[^}]*\};', cue).group()
constants = '\n'.join(re.findall(r'^#define (?:CUE_(?:NAME_MAX|NAME_LAUNCH_MAX|MAX_DISCS)|EMBER_(?:FOLDER_DEFAULT|GAMES_FOLDER))\s+\S+', header, re.M))
disc_type = re.search(r'typedef struct cue_disc_list\s*\{.*?\} cue_disc_list_t;', header, re.S).group()

HARNESS = r'''
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
typedef unsigned char u8;
constexpr int RETROGEM_GAMEID_MAX = 13;
#define LOG(...) ((void)0)
@CONSTANTS@
@DISC_TYPE@
@EXTS@
static int gPopstarterRetroGemGameID = 1;
static char shown[32];
static int shownCount;
static void displayRetroGemGameID(const char *id, int frames) { assert(frames == 2); snprintf(shown, sizeof(shown), "%s", id); shownCount++; }
@EXTRACTOR@
@CUE@
static const char *dir;
static char path[1024];
static const char *at(const char *name) { snprintf(path, sizeof(path), "%s/%s", dir, name); return path; }
static void expect(const char *name, const char *id)
{
    char out[RETROGEM_GAMEID_MAX];
    int ok = retrogemGetPs1ImageGameID(at(name), out, sizeof(out));
    if (id == NULL) { assert(!ok); return; }
    assert(ok && !strcmp(out, id));
}
int main(int argc, char **argv)
{
    char out[RETROGEM_GAMEID_MAX], prefix[1024];
    assert(argc == 2);
    dir = argv[1];
    expect("game.vcd", "SLUS_012.34");
    assert(retrogemGetVcdGameID(at("game.vcd"), out, sizeof(out)) && !strcmp(out, "SLUS_012.34"));
    expect("raw.bin", "SCES_000.01");
    expect("cooked.iso", "SLPS_999.99");
    expect("track.cue", "SCUS_944.26");          /* follows FILE to the MODE2 data track */
    expect("subdir.bin", "SCUS_946.06");         /* BOOT in a folder (Wild Arms): the file name is the id */
    expect("lightspan.bin", "LSP99016.101");     /* Lightspan: LSP + 5 digits + 3 (CosmicScale) */
    expect("SLUS_000.01.bin", NULL);             /* no filesystem: never the file name */
    expect("game.exe", NULL);
    expect("missing.bin", NULL);
    assert(!retrogemGetPs1ImageGameID(at("raw.bin"), out, 8)); /* undersized output refused */
    /* Serial normalization (CosmicScale's PSBBN rules). */
    assert(retrogemNormalizeGameID("slusp-01234", out, sizeof(out)) && !strcmp(out, "SLUS_012.34"));
    assert(retrogemNormalizeGameID("lsp12345.67", out, sizeof(out)) && !strcmp(out, "LSP12345.67"));
    assert(retrogemNormalizeGameID("LSP99016.101", out, sizeof(out)) && !strcmp(out, "LSP99016.101"));
    assert(!retrogemNormalizeGameID("LSP123456789", out, sizeof(out)));
    assert(!retrogemNormalizeGameID("LSP99016.101", out, 12)); /* no room for the NUL: refused */
    assert(!retrogemNormalizeGameID("SLUS_012.345", out, sizeof(out)));
    assert(!retrogemNormalizeGameID("Final Fantasy", out, sizeof(out)));

    /* Ember barcode: discs.txt's first entry wins over readdir order; no manifest uses Ember's pick. */
    snprintf(prefix, sizeof(prefix), "%s/", dir);
    cuePrepareRetroGemBarcode(prefix, "Multi");
    assert(shownCount == 1 && !strcmp(shown, "SCES_000.02"));
    cuePrepareRetroGemBarcode(prefix, "Single");
    assert(shownCount == 2 && !strcmp(shown, "SCUS_944.26"));
    cuePrepareRetroGemBarcode(prefix, "Broken");  /* malformed discs.txt: no barcode, no crash */
    cuePrepareRetroGemBarcode(prefix, "Absent");  /* unreadable folder: best-effort miss */
    gPopstarterRetroGemGameID = 0;
    cuePrepareRetroGemBarcode(prefix, "Single");
    assert(shownCount == 2);
    assert(cueResolveGameImage(prefix, "Single", path, sizeof(path)) == 1 && strstr(path, "Disc.cue"));
    assert(cueResolveGameImage(prefix, "Absent", path, sizeof(path)) == -1);
    puts("PASS: VCD/BIN/ISO/CUE serials, no name fallback, normalization, Ember disc-1 barcode");
    return 0;
}
'''


def iso(serial, sector_size=2048):
    """ISO9660 sectors 0..21 holding a PVD, a root directory and SYSTEM.CNF."""
    sectors = [bytearray(2048) for _ in range(22)]
    pvd = sectors[16]
    pvd[0] = 1
    pvd[1:6] = b'CD001'
    pvd[156] = 34
    struct.pack_into('<I', pvd, 156 + 2, 20)
    struct.pack_into('<I', pvd, 156 + 10, 2048)
    sectors[17][0] = 255
    sectors[17][1:6] = b'CD001'
    cnf = ('BOOT = cdrom:\\%s;1\r\nTCB = 4\r\n' % serial).encode()
    name = b'SYSTEM.CNF;1'
    record = bytearray(33 + len(name) + 1)
    record[0] = len(record)
    struct.pack_into('<I', record, 2, 21)
    struct.pack_into('<I', record, 10, len(cnf))
    record[32] = len(name)
    record[33:33 + len(name)] = name
    sectors[20][:len(record)] = record
    sectors[21][:len(cnf)] = cnf
    if sector_size == 2048:
        return b''.join(sectors)
    raw = bytearray()
    for data in sectors:
        sector = bytearray(2352)
        sector[1:11] = b'\xff' * 10
        sector[15] = 2      # mode 2
        sector[18] = 0x08   # form 1 data
        sector[24:24 + 2048] = data
        raw += sector
    return bytes(raw)


with tempfile.TemporaryDirectory() as directory:
    folder = Path(directory)
    (folder / 'game.vcd').write_bytes(b'\0' * 0x100000 + iso('SLUS_012.34', 2352))
    (folder / 'raw.bin').write_bytes(iso('SCES_000.01', 2352))
    (folder / 'cooked.iso').write_bytes(iso('SLPS_999.99'))
    (folder / 'subdir.bin').write_bytes(iso('EXE\\SCUS_946.06', 2352))
    (folder / 'lightspan.bin').write_bytes(iso('LSP99016.101', 2352))
    (folder / 'data.bin').write_bytes(iso('SCUS_944.26', 2352))
    (folder / 'track.cue').write_text('FILE "audio.bin" BINARY\n  TRACK 01 AUDIO\nFILE "data.bin" BINARY\n  TRACK 02 MODE2/2352\n')
    (folder / 'SLUS_000.01.bin').write_bytes(b'\0' * 4096)
    (folder / 'game.exe').write_bytes(b'PS-X EXE' + b'\0' * 2040)
    games = folder / 'EMBER/games'
    for name in ('Multi', 'Single', 'Broken'):
        (games / name).mkdir(parents=True)
    multi = games / 'Multi'
    (multi / 'Disc A.bin').write_bytes(iso('SCES_000.01', 2352))
    (multi / 'Disc B.bin').write_bytes(iso('SCES_000.02', 2352))
    (multi / 'Disc 1.cue').write_text('FILE "Disc A.bin" BINARY\n  TRACK 01 MODE2/2352\n')
    (multi / 'Disc 2.cue').write_text('FILE "Disc B.bin" BINARY\n  TRACK 01 MODE2/2352\n')
    (multi / 'discs.txt').write_text('Disc 2.cue\nDisc 1.cue\n')  # order is the file's, not readdir's
    single = games / 'Single'
    (single / 'data.bin').write_bytes(iso('SCUS_944.26', 2352))
    (single / 'Disc.cue').write_text('FILE "data.bin" BINARY\n  TRACK 01 MODE2/2352\n')
    (games / 'Broken' / 'discs.txt').write_text('../escape.cue\n')
    (games / 'Broken' / 'x.cue').write_text('FILE "data.bin" BINARY\n')
    source = HARNESS.replace('@CONSTANTS@', constants).replace('@DISC_TYPE@', disc_type).replace('@EXTS@', exts)
    source = source.replace('@EXTRACTOR@', extractor).replace('@CUE@', cue_functions)
    cfile = folder / 'retrogem.cpp'
    binary = folder / 'retrogem'
    cfile.write_text(source)
    subprocess.run(['g++', '-std=gnu++17', '-Wall', '-Wextra', '-Werror', '-Wno-sign-compare',
                    str(cfile), '-o', str(binary)], check=True)
    subprocess.run([str(binary), str(folder)], check=True, timeout=15)

# Every Ember leg emits the barcode after the preflight and image check, before teardown.
for path, signature, prefix in (
    ('src/sources/bdmlaunch.cpp', 'void BdmLauncher::launchCue(', 'ps1Prefix, cueName'),
    ('src/sources/mmcesupport.cpp', 'void MmceSource::launchCue(', 'ps1Root, cueName'),
    ('src/sources/ethsupport.cpp', 'void EthSource::launchCue(', 'gEthSupport.prefix(), cueName'),
    ('src/sources/udpfssupport.cpp', 'void UdpfsSource::launchCue(', 'udpfsPrefix, cueName'),
    ('src/sources/hddsupport.cpp', 'static void hddDoLaunchEmber(', '"pfs0:/", name'),
):
    body = function((ROOT / path).read_text(), signature)
    at = body.index('cuePrepareRetroGemBarcode(%s);' % prefix)
    assert body.index('cuePrepareLaunch(') < at and body.index('cueGameHasImage(') < at, path
    assert at < body.index('host::app().deinit('), path
barcode = function(cue, 'int cueResolveFirstDiscImage(')
assert barcode.index('cueReadDiscList(') < barcode.index('cueResolveGameImage('), 'discs.txt decides disc 1'
print('PASS: five Ember legs emit the RetroGEM barcode before teardown')
