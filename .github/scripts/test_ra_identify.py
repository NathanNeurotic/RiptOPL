"""Execute raudp's host-argument parser and its xeRAbora 2.1.0 "RAI1" announcement.

The loader hands raudp "<ipv4>[,<32 hex digits>]" in the RA_ARG_HOST field. The hash
counts only whole; with one, raudp tells the PC client every second which image runs,
so a client that has never seen the game loads its set. A list from an older client
carries no hash and must send nothing new, and the Caduceus bridge has no such message.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'modules/network/raudp/raudp.c').read_text(encoding='utf-8')
functions = ''
for name in ('static void ra_identify(void)', 'static void ra_parse_host(const char *tail)'):
    start = source.index(name)
    functions += source[start:source.index('\n}', start) + 2] + '\n'
# The announcement runs once a second, beside the keepalive, and never from the Caduceus path alone.
thread = source[source.index('static void ra_thread(void *arg)'):source.index('static void ra_parse_host(')]
assert 'if (iter % RA_KEEPALIVE_TICKS == 0)\n            ra_identify();' in thread
program = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "modules/network/common/ra_snap.h"
typedef uint32_t u32;
#define RA_CTL_MAX 64
static char ra_game_id[16], ra_game_hash[RA_HASH_LEN + 1], sent[RA_CTL_MAX + 1], seen_host[32];
static int ra_caduceus, sends, sent_len;
static u32 ra_server_ip;
static u32 inet_addr(const char *s) { snprintf(seen_host, sizeof(seen_host), "%s", s); return 0x0501A8C0; }
static void ra_ctl_send(const char *msg, int len) {
    assert(len > 0 && len <= RA_CTL_MAX);
    memcpy(sent, msg, len); sent[len] = '\0'; sent_len = len; sends++;
}
''' + functions + r'''
#define HASH "0123456789abcdef0123456789ABCDEF"
static void parse(const char *tail) {
    memset(ra_game_hash, 'x', RA_HASH_LEN); ra_game_hash[RA_HASH_LEN] = '\0'; /* a stale value must not survive */
    ra_parse_host(tail);
}
int main(void) {
    strcpy(ra_game_id, "SLUS_210.65");

    parse("192.168.1.5"); assert(!strcmp(seen_host, "192.168.1.5") && ra_server_ip == 0x0501A8C0 && !ra_game_hash[0]);
    ra_identify(); assert(!sends);
    parse(""); assert(!seen_host[0] && !ra_game_hash[0]);
    puts("PASS: a host without a hash parses as before and announces nothing");

    parse("192.168.1.5," HASH); assert(!strcmp(seen_host, "192.168.1.5") && !strcmp(ra_game_hash, HASH));
    ra_identify();
    assert(sends == 1 && !strcmp(sent, "RAI1 SLUS_210.65 " HASH) && sent_len == 5 + 11 + 1 + RA_HASH_LEN);
    parse("255.255.255.255," HASH); assert(!strcmp(seen_host, "255.255.255.255") && !strcmp(ra_game_hash, HASH));
    parse("," HASH); assert(!seen_host[0] && !strcmp(ra_game_hash, HASH)); /* discovery by broadcast, no pinned host */
    strcpy(ra_game_id, "SLUS_210.65XXXX"); sends = 0; ra_identify();
    assert(sends == 1 && sent_len == 5 + 15 + 1 + RA_HASH_LEN && sent_len <= RA_CTL_MAX);
    puts("PASS: a whole hash is kept and sent as RAI1 <serial> <hash>, the longest serial included");

    static const char *bad[] = {
        "192.168.1.5,", "192.168.1.5,0123456789abcdef0123456789ABCDE", "192.168.1.5," HASH "0",
        "192.168.1.5,0123456789abcdeg0123456789ABCDEF", "192.168.1.5," HASH ",1", "1234567890123456," HASH,
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        parse(bad[i]); assert(!ra_game_hash[0]);
        sends = 0; ra_identify(); assert(!sends);
    }
    puts("PASS: an empty, short, long, non-hex or misplaced hash is dropped whole");

    parse("192.168.1.5," HASH); sends = 0;
    ra_caduceus = 1; ra_identify(); assert(!sends); ra_caduceus = 0;
    ra_game_id[0] = '\0'; ra_identify(); assert(!sends);
    puts("PASS: nothing goes to the Caduceus bridge, and nothing without a serial");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='ra-identify-') as tmp:
    src = Path(tmp) / 'test.c'
    exe = Path(tmp) / 'test.exe'
    src.write_text(program, encoding='utf-8')
    subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-I', str(root), str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
