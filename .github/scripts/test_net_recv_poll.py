#!/usr/bin/env python3
"""The in-game SMB2 and NFS readers poll a silent socket every 100 us for the first 2 ms, then every 1 ms,
and still give up after their 10 s timeout (#399).

Each reader's receive function is taken from its source and compiled on the host against a socket that
never has data and a DelayThread that only advances a virtual clock."""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))


def function(path, signature):
    src = open(os.path.join(ROOT, path)).read()
    start = src.index(signature)
    brace = src.index('{', start)
    depth = 0
    for i in range(brace, len(src)):
        if src[i] == '{':
            depth += 1
        elif src[i] == '}':
            depth -= 1
            if depth == 0:
                return src[start:i + 1]
    raise RuntimeError('no end for ' + signature)


def define(path, name):
    m = re.search(r'#define\s+' + name + r'\s+(\S+)', open(os.path.join(ROOT, path)).read())
    return '#define %s %s\n' % (name, m.group(1))


PRELUDE = r'''
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
typedef unsigned char u8;
typedef unsigned int u32;
#define MSG_DONTWAIT 0x40
static uint64_t virtual_us;
static int short_delays, long_delays, recv_calls;
static int DelayThread(int us)
{
    assert(us == 100 || us == 1000);
    if (us == 100) {
        assert(long_delays == 0); /* short polls come first */
        short_delays++;
    } else
        long_delays++;
    virtual_us += us;
    return 0;
}
static int silent_recv(int s, void *mem, int len, unsigned int flags)
{
    (void)s; (void)mem; (void)len;
    assert(flags == MSG_DONTWAIT);
    recv_calls++;
    return -1;
}
static int (*plwip_recv)(int s, void *mem, int len, unsigned int flags) = silent_recv;
static void reset(void) { virtual_us = 0; short_delays = long_delays = recv_calls = 0; }
'''

SMB2 = 'modules/iopcore/cdvdman/smb2.c'
NFS = 'modules/iopcore/cdvdman/nfs.c'

code = PRELUDE
code += define(SMB2, 'SMB2_RECV_TIMEOUT_MS') + define(SMB2, 'SMB2_POLL_SHORT_US') + define(SMB2, 'SMB2_POLL_SHORT_COUNT')
code += 'static int NegotiateTimedOut(void) { return 0; }\n'
code += function(SMB2, 'static int RecvData(int sock, void *buf, int size)') + '\n'
code += define(NFS, 'NFS_RECV_TIMEOUT_MS')
code += function(NFS, 'static int nfs_sock_read(int fd, void *buf, int len)') + '\n'
code += r'''
int main(void)
{
    char buf[16];

    reset();
    assert(RecvData(3, buf, sizeof(buf)) == -1);
    assert(short_delays == 20 && long_delays == 9998 && virtual_us == 10000000ULL);

    reset();
    assert(nfs_sock_read(3, buf, sizeof(buf)) == -1);
    assert(short_delays == 20 && long_delays == 9998 && virtual_us == 10000000ULL);
    assert(recv_calls == 20 + 9998);

    reset();
    assert(nfs_sock_read(-1, buf, sizeof(buf)) == -1 && recv_calls == 0);

    puts("in-game SMB2 and NFS receive polls: 20 x 100 us, then 1 ms, 10 s timeout kept");
    return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    c = os.path.join(tmp, 'test.c')
    exe = os.path.join(tmp, 'test')
    open(c, 'w').write(code)
    build = subprocess.run(['gcc', '-std=gnu99', '-Wall', '-Wno-unused-function', '-fsanitize=address,undefined', c, '-o', exe],
                           capture_output=True, text=True)
    if build.returncode != 0:
        sys.exit('host compile failed:\n' + build.stderr)
    sys.exit(subprocess.run([exe]).returncode)
