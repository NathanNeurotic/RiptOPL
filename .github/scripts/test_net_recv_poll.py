#!/usr/bin/env python3
"""The in-game SMB2 and NFS readers wait on the socket's SO_RCVTIMEO (100 ms per receive), or, when that cannot
be set, poll a silent socket every 100 us for the first 2 ms, then every 1 ms. Both give up after their 10 s
timeout (#399, #536).

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
static int recv_waits; /* SO_RCVTIMEO set on the socket */
#define smb2_recv_waits recv_waits
#define nfs_recv_waits recv_waits
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
    assert(flags == (recv_waits ? 0 : MSG_DONTWAIT));
    if (recv_waits)
        virtual_us += 100000; /* SO_RCVTIMEO passed */
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
code += define(SMB2, 'SMB2_RECV_WAIT_MS')
code += 'static int NegotiateTimedOut(void) { return 0; }\n'
code += function(SMB2, 'static int RecvData(int sock, void *buf, int size)') + '\n'
code += define(NFS, 'NFS_RECV_TIMEOUT_MS') + define(NFS, 'NFS_RECV_WAIT_MS')
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

    /* With SO_RCVTIMEO each receive waits 100 ms itself: about 100 receives, still about 10 s. */
    recv_waits = 1;
    reset();
    assert(RecvData(3, buf, sizeof(buf)) == -1);
    assert(recv_calls == 101 && virtual_us >= 10000000ULL && virtual_us < 10200000ULL);

    reset();
    assert(nfs_sock_read(3, buf, sizeof(buf)) == -1);
    assert(recv_calls == 100 && virtual_us >= 10000000ULL && virtual_us < 10200000ULL);

    puts("in-game SMB2 and NFS receives: wait 100 ms with SO_RCVTIMEO, else poll 20 x 100 us then 1 ms; 10 s timeout kept");
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
