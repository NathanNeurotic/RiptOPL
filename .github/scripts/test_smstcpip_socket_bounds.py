"""Compile the production SMSTCPIP socket index lookup under ASan/UBSan.

Valid descriptors are [0, NUM_SOCKETS); descriptor NUM_SOCKETS is the first
out-of-bounds element and must be rejected just like a negative descriptor.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "modules/network/SMSTCPIP/sockets.c").read_text(encoding="utf-8")
MARKER = "static struct lwip_socket *\nget_socket(int s)"
start = SOURCE.index(MARKER)
opening = SOURCE.index("{", start)
depth = 0
end = None
for index in range(opening, len(SOURCE)):
    if SOURCE[index] == "{":
        depth += 1
    elif SOURCE[index] == "}":
        depth -= 1
        if depth == 0:
            end = index + 1
            break
assert end is not None
function = SOURCE[start:end]

harness = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#define NUM_SOCKETS 4
#define SOCKETS_DEBUG 0
#define LWIP_DEBUGF(...) ((void)0)
struct netconn { int marker; };
struct lwip_socket { struct netconn *conn; };
static struct lwip_socket sockets[NUM_SOCKETS];
static int last_errno;
#define set_errno(e) (last_errno = (e))
'''
checks = r'''
int main(void)
{
    struct netconn a = {12};
    assert(get_socket(-1) == NULL && last_errno == EBADF);
    assert(get_socket(NUM_SOCKETS) == NULL && last_errno == EBADF);
    assert(get_socket(NUM_SOCKETS + 1) == NULL && last_errno == EBADF);
    assert(get_socket(0) == NULL && last_errno == EBADF);
    sockets[0].conn = &a;
    sockets[NUM_SOCKETS - 1].conn = &a;
    assert(get_socket(0) == &sockets[0]);
    assert(get_socket(NUM_SOCKETS - 1) == &sockets[NUM_SOCKETS - 1]);
    assert(get_socket(NUM_SOCKETS) == NULL && last_errno == EBADF);
    puts("SMSTCPIP socket descriptor capacity bound: PASS");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="riptopl-socket-bounds-") as temp:
    source = Path(temp) / "test.c"
    binary = Path(temp) / "test"
    source.write_text(harness + function + checks, encoding="utf-8")
    compiler = os.environ.get("CC", "cc")
    result = subprocess.run(
        [compiler, "-std=c99", "-O1", "-Wall", "-Wextra", "-Werror",
         "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
         str(source), "-o", str(binary)],
        capture_output=True, text=True, check=False,
    )
    if result.returncode:
        raise RuntimeError("SMSTCPIP socket-bound test compilation failed:\n" + result.stderr)
    subprocess.run([str(binary)], check=True, timeout=15)
