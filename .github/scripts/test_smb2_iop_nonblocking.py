"""Exercise libsmb2's IOP nonblocking-socket guard before TCP connect.

The PS2 menu's SMB2 client must not enter connect() in blocking mode:
its synchronous poll timeout cannot interrupt a blocked socket call.
Extract the real production helper so negative fcntl outcomes remain tested.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOCKET_SOURCE = (ROOT / "modules/network/smb2man/libsmb2/lib/socket.c").read_text(encoding="utf-8")


def extract_function(signature: str) -> str:
    """Extract a function's literal C source, with balanced braces."""
    begin = SOCKET_SOURCE.index(signature)
    opening = SOCKET_SOURCE.index("{", begin)
    depth = 0
    for i in range(opening, len(SOCKET_SOURCE)):
        if SOCKET_SOURCE[i] == "{":
            depth += 1
        elif SOCKET_SOURCE[i] == "}":
            depth -= 1
            if depth == 0:
                return SOCKET_SOURCE[begin:i + 1]
    raise AssertionError("Unterminated C function: " + signature)


helper = extract_function("static int\nset_nonblocking(t_socket fd)")
# The platform-specific connect condition has alternative #if arms that each
# contain a '{' token; only one arm compiles, so a raw brace counter cannot
# extract that full function. Inspect its source range without rewriting it.
connector = SOCKET_SOURCE.split("static int\nconnect_async_ai(", 1)[1].split(
    "static int\nsmb2_connect_async_next_addr(", 1
)[0]
guard = 'if (set_nonblocking(fd) < 0) {'
assert guard in connector
failed = connector.split(guard, 1)[1].split("}", 1)[0]
assert "close(fd);" in failed and "return -EIO;" in failed
assert connector.index(guard) < connector.index("if (connect(fd,")

harness = r'''
#include <assert.h>
#include <stdio.h>
#define __PS2__ 1
#define _IOP 1
typedef int t_socket;
#define F_GETFL 3
#define F_SETFL 4
#define O_NONBLOCK 1
static int get_fails, set_fails, get_calls, set_calls, last_fd, last_flags;
static int fake_fcntl(int fd, int cmd, int flags)
{
    assert(fd == 7);
    last_fd = fd;
    if (cmd == F_GETFL) {
        ++get_calls;
        return get_fails ? -1 : 0x20;
    }
    assert(cmd == F_SETFL);
    ++set_calls;
    last_flags = flags;
    return set_fails ? -1 : 0;
}
#define fcntl fake_fcntl
'''
tests = r'''
int main(void)
{
    assert(set_nonblocking(7) == 0);
    assert(get_calls == 1 && set_calls == 1);
    assert(last_fd == 7 && last_flags == (0x20 | O_NONBLOCK));

    get_calls = set_calls = 0;
    get_fails = 1;
    assert(set_nonblocking(7) < 0);
    assert(get_calls == 1 && set_calls == 0);

    get_calls = set_calls = 0;
    get_fails = 0;
    set_fails = 1;
    assert(set_nonblocking(7) < 0);
    assert(get_calls == 1 && set_calls == 1);

    puts("SMB2 IOP nonblocking socket fail-closed guard: PASS");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="riptopl-smb2-nonblocking-") as td:
    source = Path(td) / "test.c"
    binary = Path(td) / "test"
    source.write_text(harness + helper + tests, encoding="utf-8")
    compiler = os.environ.get("CC", "cc")
    result = subprocess.run([
        compiler, "-std=c99", "-O1", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        str(source), "-o", str(binary)
    ], text=True, capture_output=True, check=False)
    if result.returncode:
        raise RuntimeError("IOP nonblocking host harness compile failed: " + result.stderr)
    subprocess.run([str(binary)], check=True, timeout=15)
