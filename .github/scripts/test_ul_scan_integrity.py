"""Execute the actual C++ UL scanner with its real record layout under ASan/UBSan.

Malformed lengths, failed size queries, EOF/errors mid-record, close failures and
oversized allocations must retain the previous list/count/size. Short successful
reads are accumulated, and valid UL/ISO/folder rows still publish together.
Use --baseline <supportbase.cpp> to demonstrate the pre-fix memory overwrite.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = Path(sys.argv[2]) if len(sys.argv) == 3 and sys.argv[1] == '--baseline' else ROOT / 'src/sources/supportbase.cpp'
text = source.read_text(encoding='utf-8')
start = text.index('int sbReadList(')
body = text[start:text.index('\n}', start) + 2]
header = (ROOT / 'include/supportbase.h').read_text(encoding='utf-8')
defines = '\n'.join(re.findall(r'^#define (?:UL_GAME_NAME_MAX|ISO_GAME_NAME_MAX|ISO_GAME_EXTENSION_MAX|GAME_STARTUP_MAX)\s+\d+', header, re.M))
types = '\n'.join(re.findall(r'typedef struct\s*\{[^}]*\}\s*(?:base_game_info_t|USBExtreme_game_entry_t);', header))
enums = header[header.index('enum GAME_FORMAT'):header.index('typedef struct')]
code = r'''
#include <cassert>
#include <climits>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <vector>
#include <algorithm>
using u8 = unsigned char; using u32 = unsigned int;
enum { SCECdPS2CD, SCECdPS2DVD, O_RDONLY };
@DEFINES@
@ENUMS@
@TYPES@
struct game_list_t { base_game_info_t gameinfo; game_list_t *next; };
static int sbReadListErrno, fileSize, offset, maxRead, failAt, zeroAt, reads, closes;
static int isoCount, folderCount;
static bool gEnableFolderNav = true, failClose, failAllocation;
static int sbListCacheTrust; // Cache Game List off: every scan reads the folders
static std::vector<unsigned char> contents;
static void sbSetBrowseSub(const char*) {}
static int scanForISO(const char*, int type, game_list_t **head, game_list_t **folders) {
    if (type != SCECdPS2CD) return 0;
    if (isoCount) {
        auto *node = static_cast<game_list_t*>(calloc(1, sizeof(game_list_t)));
        strcpy(node->gameinfo.name, "ISO"); node->gameinfo.format = GAME_FORMAT_ISO;
        node->next = *head; *head = node;
    }
    if (folderCount && folders) {
        auto *node = static_cast<game_list_t*>(calloc(1, sizeof(game_list_t)));
        strcpy(node->gameinfo.name, "folder"); node->gameinfo.format = GAME_FORMAT_FOLDER;
        node->next = *folders; *folders = node;
    }
    return isoCount;
}
static int sbDedupFolderList(game_list_t**) { return folderCount; }
static int openFile(const char *path, int) { assert(strstr(path, "ul.cfg")); return 7; }
static int getFileSize(int fd) { assert(fd == 7); return fileSize; }
static int read(int fd, void *out, int requested) {
    assert(fd == 7); ++reads;
    if (offset >= failAt) return -1;
    if (offset >= zeroAt) return 0;
    int size = std::min({requested, maxRead, int(contents.size()) - offset, failAt - offset, zeroAt - offset});
    memcpy(out, contents.data() + offset, size); offset += size; return size;
}
static int close(int fd) { assert(fd == 7); ++closes; return failClose ? -1 : 0; }
static void *allocate(size_t size) { return failAllocation ? nullptr : malloc(size); }
#define malloc allocate
@BODY@
#undef malloc
static void reset() {
    static_assert(sizeof(USBExtreme_game_entry_t) == 64, "UL disk layout changed");
    USBExtreme_game_entry_t record{};
    memset(record.name, 'N', sizeof(record.name));
    memset(record.startup, 'S', sizeof(record.startup)); record.parts = 2; record.media = 0x14;
    contents.resize(2 * sizeof(record));
    memcpy(contents.data(), &record, sizeof(record)); memcpy(contents.data() + sizeof(record), &record, sizeof(record));
    fileSize = contents.size(); offset = reads = closes = 0;
    maxRead = 7; failAt = zeroAt = INT_MAX; failClose = failAllocation = false;
    isoCount = folderCount = 0;
}
static void preserves(int expectedError) {
    auto *old = static_cast<base_game_info_t*>(calloc(1, sizeof(base_game_info_t)));
    strcpy(old->name, "last-good"); base_game_info_t *list = old; int count = 1, size = 123;
    assert(sbReadList(&list, "mass0:/", "", &size, &count) == 1);
    assert(list == old && count == 1 && size == 123 && !strcmp(list[0].name, "last-good"));
    if (expectedError) assert(sbReadListErrno == expectedError);
    assert(closes == 1); free(list);
}
int main() {
    // Before the fix, the extra byte causes a second output row in a one-row array.
    reset(); contents.resize(65); fileSize = 65; preserves(EINVAL); assert(reads == 0);
    reset(); fileSize = 1; preserves(EINVAL); assert(reads == 0);
    reset(); fileSize = -1; preserves(EIO); assert(reads == 0);
    reset(); fileSize = INT_MAX - INT_MAX % 64; preserves(EOVERFLOW); assert(reads == 0);
    reset(); failAt = 75; preserves(EIO);
    reset(); zeroAt = 75; preserves(EIO);
    reset(); failClose = true; preserves(EIO);
    reset(); failAllocation = true; preserves(0); assert(reads == 0);
    reset(); isoCount = folderCount = 1;
    base_game_info_t *list = nullptr; int count = 0, size = -2;
    assert(sbReadList(&list, "mass0:/", "", &size, &count) == 4 && count == 4 && size == 128);
    assert(reads > 2 && closes == 1);
    for (int i = 0; i < 2; ++i) {
        assert(list[i].format == GAME_FORMAT_USBLD && list[i].parts == 2 && list[i].media == 0x14);
        assert(strlen(list[i].name) == 32 && strlen(list[i].startup) == 12 && !list[i].extension[0]);
    }
    assert(!strcmp(list[2].name, "ISO") && list[2].format == GAME_FORMAT_ISO);
    assert(!strcmp(list[3].name, "folder") && list[3].format == GAME_FORMAT_FOLDER);
    reset(); contents.clear(); fileSize = 0;
    assert(sbReadList(&list, "mass0:/", "", &size, &count) == 0 && !list && count == 0 && size == 0);
}
'''
for name, value in {'DEFINES': defines, 'ENUMS': enums, 'TYPES': types, 'BODY': body}.items():
    code = code.replace('@' + name + '@', value)
directory = ROOT / 'build/test-tmp'
directory.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=directory) as tmp:
    src, exe = Path(tmp) / 'ul.cpp', Path(tmp) / 'ul'
    src.write_text(code, encoding='utf-8')
    compiler = shlex.split(os.environ.get('CXX', 'c++'))
    subprocess.run([*compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-g', str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('UL scan: record bounds, short reads, IO/allocation failure retention and mixed library passed under ASan/UBSan')
