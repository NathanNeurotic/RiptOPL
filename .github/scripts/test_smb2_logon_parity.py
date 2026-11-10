"""The in-game SMB2 reader logs on with the same user name, domain and password as the menu.

A server recomputes NTOWFv2 from the user name and the domain the client sends (MS-NLMP 3.3.2). If
the game's logon sends another user name or domain than the menu's, or hashes another one, a
password that works in the menu can fail at game start, and only on some servers: Samba, which the
live harness uses, accepts several domains for one account.

This builds both clients on the host and runs them against one fake SMB2 server, on one port, that
sends both the same CHALLENGE, then compares the AUTHENTICATE messages they send: UserName,
DomainName and the user, domain and password their NTLMv2 proofs are computed from. The CHALLENGE's
TargetName, ParityDom, is in mixed case and differs from its MsvAvNbDomainName AV pair, so a logon
that changes the domain's case or takes it from the AV pair fails.
Both clients are linked with connect() wrapped: every IPv4 connect goes to the fake server, and the
address and port it was meant for are printed, and checked. So the clients can be told any server:
the fake server's own port; port 445; port 1111, a custom port; port 0, which both take as 445;
and 192.168.100.200 on port 65535.
The AV pairs of the game's NTLMv2 blob are checked byte for byte: the CHALLENGE's TargetInfo with an
MsvAvTargetName "cifs/<ip>" ("cifs/<ip>:<port>" off port 445) in front of the MsvAvEOL, which a
server set to check it (on Windows, "Server SPN target name validation level") wants. TargetInfo
lists that break MS-NLMP's rules must come back as that rewrite, lists that leave nothing to send
back must make the game refuse the logon, and lists of many EOLs stop at smb2.c's SMB2_TI_MAX. The
menu (usmb2) sends the TargetInfo back with "cifs/<TargetName>" instead; its AV pairs are not
compared, but every list must leave it without a sanitizer report, a hang or a server error.
The game's LM response and NTLMv2 timestamp are checked on their own: Z(24) and the server's
timestamp when the AV pairs it kept have an MsvAvTimestamp, else LMv2 and no timestamp (the IOP has
no clock).
- The game: tests/smb2-live/ingame.c around the real modules/iopcore/cdvdman/smb2.c.
- The menu: the real modules/network/smb2man/smb2man.c over its vendored usmb2, built for the host
  with tests/smb2-live/inc-browse and driven through its LOGON and OPENSHARE devctls as the EE does.
This is a host check, not a console test. tests/smb2-live/run.sh runs both against a real Samba.
"""
from pathlib import Path
import concurrent.futures
import hmac
import os
import re
import socket
import struct
import subprocess
import sys
import tempfile
import threading

root = Path(__file__).resolve().parents[2]
live = root / 'tests/smb2-live'
cdvdman = root / 'modules/iopcore/cdvdman'
smb2man_dir = root / 'modules/network/smb2man'
failures = []


def check(cond, what):
    if not cond:
        failures.append(what)


# The most TargetInfo bytes smb2.c keeps, AV pairs and names; its walk ends at a pair past it.
_ti_max = re.search(r'^#define\s+SMB2_TI_MAX\s+(\d+)\b', (cdvdman / 'smb2.c').read_text(encoding='utf-8'), re.M)
check(_ti_max is not None, 'SMB2_TI_MAX is not defined in modules/iopcore/cdvdman/smb2.c: update this check')
TI_MAX = int(_ti_max.group(1)) if _ti_max else 512


def strip_comments(src, keep_literals=False):
    """C source with each comment replaced by a space (its newlines kept) and, unless keep_literals,
    the contents of each string and character literal dropped, so neither can pass for code. Lines
    that end in a backslash are joined first, as C joins them before it removes comments."""
    src = src.replace('\\\n', '')
    out, i, n = [], 0, len(src)
    while i < n:
        if src.startswith('/*', i):
            j = src.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(' ' + '\n' * src.count('\n', i, j))
            i = j
        elif src.startswith('//', i):
            j = src.find('\n', i)
            out.append(' ')
            i = n if j < 0 else j
        elif src[i] in '"\'':
            j = i + 1
            while j < n and src[j] != src[i]:
                j += 2 if src[j] == '\\' else 1
            out.append(src[i:j + 1] if keep_literals else src[i] * 2)
            i = j + 1
        else:
            out.append(src[i])
            i += 1
    return ''.join(out)


def function_body(src, name):
    m = re.search(r'^\w[\w \*]*\b%s\([^)]*\)\n\{\n' % name, src, re.M)
    if not m:
        return ''
    return src[m.end():src.find('\n}\n', m.end())]


# ---- MD4 (RFC 1320): hashlib has none under OpenSSL 3 ---------------------------------------------
def _rol(x, n):
    x &= 0xFFFFFFFF
    return ((x << n) | (x >> (32 - n))) & 0xFFFFFFFF


def md4(msg):
    msg = bytearray(msg)
    bits = len(msg) * 8
    msg.append(0x80)
    while len(msg) % 64 != 56:
        msg.append(0)
    msg += struct.pack('<Q', bits)
    h = [0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476]
    for i in range(0, len(msg), 64):
        x = struct.unpack('<16I', msg[i:i + 64])
        a, b, c, d = h
        for k in range(16):  # round 1
            f = (b & c) | (~b & d)
            a, b, c, d = d, _rol(a + f + x[k], (3, 7, 11, 19)[k % 4]), b, c
        for k in range(16):  # round 2
            g = (b & c) | (b & d) | (c & d)
            a, b, c, d = d, _rol(a + g + x[(k % 4) * 4 + k // 4] + 0x5A827999, (3, 5, 9, 13)[k % 4]), b, c
        for k in range(16):  # round 3
            j = (0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15)[k]
            a, b, c, d = d, _rol(a + (b ^ c ^ d) + x[j] + 0x6ED9EBA1, (3, 9, 11, 15)[k % 4]), b, c
        h = [(v + w) & 0xFFFFFFFF for v, w in zip(h, (a, b, c, d))]
    return struct.pack('<4I', *h)


for _msg, _want in [(b'', '31d6cfe0d16ae931b73c59d7e0c089c0'), (b'abc', 'a448017aaf21d8525fc10ae87aa6729d'),
                    (b'1234567890' * 8, 'e33b4ddc9c38f2199c3e7b164fcc0536')]:
    assert md4(_msg).hex() == _want, 'the MD4 in this script is wrong (RFC 1320 A.5)'


def hmac_md5(key, data):
    return hmac.new(key, data, 'md5').digest()


def u16(s):
    return s.encode('utf-16-le')


# ---- AV pairs (MS-NLMP 2.2.2.1) --------------------------------------------------------------------
MSV_AV_EOL, MSV_AV_TIMESTAMP, MSV_AV_TARGET_NAME = 0, 7, 9


def av(code, value):
    return struct.pack('<HH', code, len(value)) + value


def with_target_name(ti, spn, cap=None):
    """The AV pairs libsmb2 sends for a CHALLENGE's TargetInfo (lib/ntlmssp.c,
    ntlm_decode_challenge_message()): each pair copied, and in front of each MsvAvEOL an
    MsvAvTargetName that names the server by spn, in UTF-16LE. An EOL is copied without a value, and
    what it claims as one is read as pairs; the walk ends at a pair that overruns the list, which is
    dropped with whatever follows. (libsmb2 also stops at the end of its buffer, the CHALLENGE's
    length + 400 bytes, which the lists compared with it are far from.) With cap, the walk also ends
    at a pair that would end more than cap bytes in, names included, as smb2.c's ends at SMB2_TI_MAX."""
    out, off = b'', 0
    while off + 4 <= len(ti):
        code, length = struct.unpack_from('<HH', ti, off)
        if off + 4 + length > len(ti) or (cap is not None and len(out) + 4 + length > cap):
            break
        if code == MSV_AV_EOL:
            out += av(MSV_AV_TARGET_NAME, u16(spn)) + av(MSV_AV_EOL, b'')
            off += 4
        else:
            out += ti[off:off + 4 + length]
            off += 4 + length
    return out


def pairs(avs):
    """AV pairs as AvId=value, names decoded from UTF-16LE, then any bytes left over."""
    out, off = [], 0
    while off + 4 <= len(avs):
        code, length = struct.unpack_from('<HH', avs, off)
        value = avs[off + 4:off + 4 + length]
        out.append('%d=%s' % (code, ascii(value.decode('utf-16-le', 'replace')) if code in (1, 2, 3, 4, 5, 9) else
                              value.hex()))
        off += 4 + length
    if off < len(avs):
        out.append('then ' + avs[off:].hex())
    return '[' + ', '.join(out) + ']'


# ---- the fake server -------------------------------------------------------------------------------
TARGET_NAME = 'ParityDom'  # mixed case: a logon that changes the domain's case must fail
SERVER_CHALLENGE = bytes.fromhex('0123456789abcdef')
TIMESTAMP = struct.pack('<Q', 133000000000000000)
STATUS_SUCCESS, STATUS_MORE_PROCESSING_REQUIRED = 0, 0xC0000016
STATUS_ACCESS_DENIED, STATUS_NOT_SUPPORTED = 0xC0000022, 0xC00000BB
NTLMSSP_NEGOTIATE_ANONYMOUS = 0x00000800

# MsvAvNbDomainName (2) differs from TargetName on purpose: both logons must take the domain from
# TargetName, and a logon that read it from this AV pair would otherwise pass.
PAIRS = (av(2, u16('PARITYNB')) + av(1, u16('FAKESRV')) + av(4, u16('paritydom.test')) +
         av(3, u16('fakesrv.paritydom.test')) + av(MSV_AV_TIMESTAMP, TIMESTAMP))
EOL = av(MSV_AV_EOL, b'')
TARGET_INFO = PAIRS + EOL  # as servers send it
# TargetInfo lists that break MS-NLMP 2.2.2.1. Both logons must send them back the same way.
ODD_TARGET_INFO = [
    ('without an EOL', PAIRS),
    ('of only an EOL', EOL),
    ('with a pair after the EOL', TARGET_INFO + av(5, u16('paritydom.test'))),
    ('with two EOLs', av(2, u16('PARITYNB')) + EOL + av(1, u16('FAKESRV')) + av(MSV_AV_TIMESTAMP, TIMESTAMP) + EOL),
    ('with an EOL that claims a value', PAIRS + struct.pack('<HH', MSV_AV_EOL, 8) + av(1, u16('XY'))),
    ('with a pair that overruns the list and the EOL', PAIRS + struct.pack('<HH', 1, 50) + u16('short') + EOL),
    ('with a pair that overruns the list after the EOL', TARGET_INFO + struct.pack('<HH', 1, 50) + u16('short')),
    ('with a stray byte after the EOL', TARGET_INFO + b'\x01'),
    ('with an EOL that claims more than the list holds', PAIRS + struct.pack('<HH', MSV_AV_EOL, 50)),
    # As long as smb2.c takes, with its EOL at the end: the name goes in past SMB2_TI_MAX bytes.
    ('of SMB2_TI_MAX bytes, its EOL at the end', PAIRS + av(5, u16('t' * ((TI_MAX - len(PAIRS) - 8) // 2))) + EOL),
]
assert len(ODD_TARGET_INFO[-1][1]) == TI_MAX
# The same with the CHALLENGE's TargetName after its TargetInfo (as Windows lays them out), so a
# pair that overruns the list ends in TargetName, not at the end of the message. Even lengths only:
# an odd one puts TargetName at an odd offset, where the vendored libsmb2 reads it misaligned.
TN_LAST_TARGET_INFO = [
    ('with a pair that overruns it into TargetName', TARGET_INFO + struct.pack('<HH', 1, 12) + u16('ab')),
    ('empty, its offset inside the CHALLENGE', b''),
]
# Lists that leave libsmb2 no AV pairs to send back, so that it refuses the logon (lib/ntlmssp.c,
# encode_ntlm_auth()): the game must refuse it too.
REFUSED_TARGET_INFO = [
    ('empty, its offset at the end of the CHALLENGE', b''),
    ('of a stray 3 bytes', b'\x01\x00\x00'),
    ('whose first pair overruns it', struct.pack('<HH', 1, 50) + u16('short')),
]
# Lists that the two stop at different limits by design (SMB2_TI_MAX in the game, the CHALLENGE's
# length + 400 in libsmb2): run on the game alone, against with_target_name() capped as smb2.c caps.
CAPPED_TARGET_INFO = [
    ('of 20 EOLs', EOL * 20),
    ('of SMB2_TI_MAX bytes of EOLs', EOL * (TI_MAX // 4)),
]


def recv_exact(conn, n):
    buf = b''
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            raise EOFError
        buf += chunk
    return buf


def recv_msg(conn):
    while True:  # skip NetBIOS session messages other than data (type 0)
        hdr = recv_exact(conn, 4)
        body = recv_exact(conn, (hdr[1] << 16) | (hdr[2] << 8) | hdr[3])
        if hdr[0] == 0:
            return body


def send_msg(conn, msg):
    conn.sendall(struct.pack('>I', len(msg)) + msg)


def reply(req, status, body, session_id=0):
    h = bytearray(64)
    h[0:4] = b'\xfeSMB'
    struct.pack_into('<H', h, 4, 64)
    struct.pack_into('<I', h, 8, status)
    h[12:14] = req[12:14]  # command
    struct.pack_into('<H', h, 14, 64)  # credits granted
    struct.pack_into('<I', h, 16, 1)  # SMB2_FLAGS_SERVER_TO_REDIR
    h[24:32] = req[24:32]  # message id
    struct.pack_into('<Q', h, 40, session_id)
    return bytes(h) + bytes(body)


def error_body():
    return struct.pack('<H', 9) + b'\0' * 7


def challenge_message(ti, tn_last=False):
    tn = u16(TARGET_NAME)
    tn_off, ti_off = (56 + len(ti), 56) if tn_last else (56, 56 + len(tn))
    # UNICODE, REQUEST_TARGET, SIGN, NTLM, ALWAYS_SIGN, TARGET_TYPE_DOMAIN, EXTENDED_SESSIONSECURITY,
    # TARGET_INFO, VERSION, 128, 56
    flags = 0x00000001 | 0x00000004 | 0x00000010 | 0x00000200 | 0x00008000 | 0x00010000 | 0x00080000 | \
        0x00800000 | 0x02000000 | 0x20000000 | 0x80000000
    m = b'NTLMSSP\0' + struct.pack('<I', 2)
    m += struct.pack('<HHI', len(tn), len(tn), tn_off)
    m += struct.pack('<I', flags) + SERVER_CHALLENGE + b'\0' * 8
    m += struct.pack('<HHI', len(ti), len(ti), ti_off)
    m += bytes([6, 1]) + struct.pack('<H', 7600) + b'\0\0\0\x0f'
    assert len(m) == 56
    return m + (ti + tn if tn_last else tn + ti)


def serve_one(sock, ti, tn_last, record, sign=False):
    """Answer one client: NEGOTIATE (2.1 when offered), the two SESSION_SETUP legs, with this
    TargetInfo in the CHALLENGE (before TargetName with tn_last), and a TREE_CONNECT that is refused,
    which ends both clients. Records the AUTHENTICATE_MESSAGE's fields."""
    try:
        sock.settimeout(60)
        conn, _ = sock.accept()
    except OSError as e:
        record['error'] = 'no client connected (%s)' % e
        return
    conn.settimeout(60)
    session_id = 0x0000400000000001
    try:
        while True:
            req = recv_msg(conn)
            if req[0:4] != b'\xfeSMB':
                record['error'] = 'not an SMB2 message: %s' % req[:4].hex()
                return
            cmd = struct.unpack_from('<H', req, 12)[0]
            body = req[64:]
            if cmd == 0:  # NEGOTIATE
                count = struct.unpack_from('<H', body, 2)[0]
                dialects = [struct.unpack_from('<H', body, 36 + 2 * i)[0] for i in range(count)]
                r = bytearray(65)
                # SecurityMode: signing enabled, and required with sign
                struct.pack_into('<HHH', r, 0, 65, 0x03 if sign else 0x01, 0x0210 if 0x0210 in dialects else 0x0202)
                r[8:24] = b'FAKESMB2SERVER!!'
                struct.pack_into('<IIIQ', r, 28, 65536, 65536, 65536, 133000000000000000)
                struct.pack_into('<HH', r, 56, 128, 0)
                send_msg(conn, reply(req, STATUS_SUCCESS, r))
            elif cmd == 1:  # SESSION_SETUP
                secoff, seclen = struct.unpack_from('<HH', body, 12)
                tok = req[secoff:secoff + seclen]
                i = tok.find(b'NTLMSSP\0')
                if i < 0:
                    record['error'] = 'a SESSION_SETUP without an NTLMSSP token'
                    return
                tok = tok[i:]
                mtype = struct.unpack_from('<I', tok, 8)[0]
                if mtype == 1:
                    ch = challenge_message(ti, tn_last)
                    send_msg(conn, reply(req, STATUS_MORE_PROCESSING_REQUIRED, struct.pack('<HHHH', 9, 0, 72, len(ch)) + ch,
                                         session_id))
                elif mtype == 3:
                    def field(off):
                        ln, _, at = struct.unpack_from('<HHI', tok, off)
                        return tok[at:at + ln]
                    record.update(lm=field(12), nt=field(20), domain=field(28), user=field(36),
                                  flags=struct.unpack_from('<I', tok, 60)[0])
                    send_msg(conn, reply(req, STATUS_SUCCESS, struct.pack('<HHHH', 9, 0, 72, 0), session_id))
            elif cmd == 3:  # TREE_CONNECT
                record['tree_connect'] = req
                send_msg(conn, reply(req, STATUS_ACCESS_DENIED, error_body(), session_id))
                return
            else:
                send_msg(conn, reply(req, STATUS_NOT_SUPPORTED, error_body(), session_id))
    except (EOFError, OSError):
        pass
    finally:
        conn.close()


def proof_inputs(rec, password, user_upper, domain):
    """Whether the client's NTProofStr was computed from this password, upper-cased user and domain."""
    nt = rec.get('nt', b'')
    if len(nt) < 16 + 28:
        return False
    ntowf = hmac_md5(md4(u16(password)), u16(user_upper) + domain)
    return hmac_md5(ntowf, SERVER_CHALLENGE + nt[16:]) == nt[:16]


def av_pairs(rec):
    """The AV pairs of the client's NTLMv2 blob: NtChallengeResponse is NTProofStr (16 bytes), the
    blob's 28-byte header, the AV pairs and Z(4)."""
    return rec['nt'][16 + 28:len(rec['nt']) - 4]


# NTOWFv2 hashes UPPER(user), upper-cased as Windows and Samba do it: within Latin-1 (all the menu
# accepts) every letter that has a single upper case, y-diaeresis to U+0178; micro sign and sharp s
# stay. Both logons must do exactly that (an ASCII-only upper case was refused for "José").
def latin1_upper(s):
    return ''.join(c if c in '\u00b5\u00df' else c.upper() if c <= '\u00ff' else c for c in s)


UPPER = [('as Windows', latin1_upper)]


MENU = r'''
/* The menu's logon: the real smb2man.c, driven as the EE drives it. GETPASSWORDHASHES first (smb2man
   refuses it, so the password goes as PLAINTEXT), then LOGON and OPENSHARE. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iomanX.h>
#include <ps2smb.h>

static iop_device_t *dev;
int AddDrv(iop_device_t *d)
{
    dev = d;
    return 0;
}
int DelDrv(const char *name)
{
    (void)name;
    return 0;
}
int smb2man_start(int argc, char *argv[]);

int main(int argc, char **argv)
{
    /* argv: ip port share user password */
    smbLogOn_in_t l;
    smbOpenShare_in_t o;
    smbGetPasswordHashes_in_t in;
    smbGetPasswordHashes_out_t out;
    int rc;

    if (argc < 6 || smb2man_start(0, NULL) != 0)
        return 2;
    memset(&l, 0, sizeof(l));
    strcpy(l.serverIP, argv[1]);
    l.serverPort = atoi(argv[2]);
    strcpy(l.User, argv[4]);
    strcpy(in.password, argv[5]);
    if (dev->ops->devctl(NULL, "", SMB_DEVCTL_GETPASSWORDHASHES, &in, sizeof(in), &out, sizeof(out)) == 0)
        return 3;
    l.PasswordType = PLAINTEXT_PASSWORD;
    strcpy(l.Password, argv[5]);
    rc = dev->ops->devctl(NULL, "", SMB_DEVCTL_LOGON, &l, sizeof(l), NULL, 0);
    if (rc == 0) {
        memset(&o, 0, sizeof(o));
        strcpy(o.ShareName, argv[3]);
        o.PasswordType = NO_PASSWORD;
        rc = dev->ops->devctl(NULL, "", SMB_DEVCTL_OPENSHARE, &o, sizeof(o), NULL, 0);
    }
    printf("connect %d\n", rc);
    dev->ops->devctl(NULL, "", SMB_DEVCTL_LOGOFF, NULL, 0, NULL, 0);
    return 0;
}
'''

# Linked into both clients with -Wl,--wrap=connect: every IPv4 connect goes to the fake server
# (127.0.0.1, port CONNECT_TO) instead, and prints the address and port it was meant for. So a client
# can be told any address and port, 445 included, without anything listening there.
WRAP = r'''
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

int __real_connect(int fd, const struct sockaddr *sa, socklen_t len);

int __wrap_connect(int fd, const struct sockaddr *sa, socklen_t len)
{
    const char *to = getenv("CONNECT_TO");

    if (to != NULL && sa->sa_family == AF_INET) {
        struct sockaddr_in a = *(const struct sockaddr_in *)sa;
        char ip[INET_ADDRSTRLEN];

        inet_ntop(AF_INET, &a.sin_addr, ip, sizeof(ip));
        fprintf(stderr, "wrapped connect to %s:%u\n", ip, (unsigned)ntohs(a.sin_port));
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons((unsigned short)atoi(to));
        return __real_connect(fd, (const struct sockaddr *)&a, sizeof(a));
    }
    return __real_connect(fd, sa, len);
}
'''

SAN = ['-fsanitize=address,undefined', '-fno-sanitize-recover=undefined', '-fno-omit-frame-pointer', '-g', '-O1']
# smb2man's build (ps2.yaml), as tests/smb2-live/run.sh builds it for the host.
FEATURES = ['-DUSMB2_FEATURE_' + f for f in 'WRITE NTLM UNICODE STAT MKDIR UNLINK RENAME'.split()]
ALLOC = ['-Dmalloc=iop_malloc', '-Dfree=iop_free', '-Dcalloc=iop_calloc']


def cc(args, what):
    p = subprocess.run(['cc'] + args, capture_output=True, text=True)
    if p.returncode != 0:
        print(p.stdout + p.stderr)
        sys.exit('%s does not build on the host' % what)


with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp / 'wrap.c').write_text(WRAP)
    wrap = [str(tmp / 'wrap.c'), '-Wl,--wrap=connect']
    game = tmp / 'ingame'
    cc(SAN + ['-Wall', '-Wno-unused-parameter', '-I', str(live / 'inc-ingame'), '-I', str(cdvdman), '-I',
              str(root / 'modules/iopcore/common'), str(live / 'ingame.c'), str(cdvdman / 'smb2.c'),
              str(cdvdman / 'smb2auth.c')] + wrap + ['-o', str(game)], 'the in-game reader (smb2.c)')
    # usmb2 stores 64-bit header fields at 4-aligned offsets, which the IOP takes and UBSan's
    # alignment check does not (as in run.sh).
    drv = SAN + ['-fno-sanitize=alignment']
    inc = ['-I', str(live / 'inc-browse'), '-I', str(live / 'inc-sdk'), '-I', str(smb2man_dir), '-I',
           str(cdvdman)]
    objs = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 2) as pool:
        jobs = []
        for src in [smb2man_dir / 'usmb2' / (n + '.c') for n in ('ntlm', 'md4c', 'hmac-md5', 'md5', 'unicode')] + \
                [cdvdman / 'smb2auth.c']:
            obj = tmp / ('lib-' + src.stem + '.o')
            objs.append(str(obj))
            jobs.append(pool.submit(cc, drv + ['-w'] + inc + FEATURES + ALLOC + ['-c', str(src), '-o', str(obj)],
                                    'smb2man ' + src.name))
        obj = tmp / 'smb2man.o'
        objs.append(str(obj))
        jobs.append(pool.submit(cc, drv + ['-Wall', '-Wno-unused-function'] + inc + FEATURES + ALLOC +
                                ['-D_start=smb2man_start', '-c', str(smb2man_dir / 'smb2man.c'), '-o', str(obj)],
                                'smb2man.c'))
        for job in jobs:
            job.result()
    (tmp / 'menu.c').write_text(MENU)
    menu = tmp / 'menu'
    cc(SAN + ['-Wall'] + inc + [str(tmp / 'menu.c')] + objs + wrap + ['-o', str(menu)], "the menu's logon (smb2man)")

    def logon(ti, user, password, ip, port, tn_last, game_only, sign=False):
        """Run the game's logon, then the menu's (unless game_only), against one fake server (one
        port, so both name the same server) that sends both this TargetInfo. Both are told this
        address and port (the fake server's own port if None), and the wrapped connect() takes them
        to the fake server. Returns the port the clients were told and each client's AUTHENTICATE
        fields and output (None for the menu with game_only)."""
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.bind(('127.0.0.1', 0))
        sock.listen(2)
        port = sock.getsockname()[1] if port is None else port
        env = dict(os.environ)
        env['CONNECT_TO'] = str(sock.getsockname()[1])
        u8, p8 = user.encode('utf-8'), password.encode('utf-8')
        records = []
        argvs = [[str(game), ip, str(port), u8, p8, 'PS2SMB', '\\DVD\\TEST.iso', '\\VMC\\card.bin']]
        if not game_only:
            argvs.append([str(menu), ip, str(port), 'PS2SMB', u8, p8])
        for argv in argvs:
            record = {}
            server = threading.Thread(target=serve_one, args=(sock, ti, tn_last, record, sign), daemon=True)
            server.start()
            try:
                p = subprocess.run(argv, capture_output=True, text=True, timeout=60, env=env)
                record['out'], record['rc'] = p.stdout + p.stderr, p.returncode
            except subprocess.TimeoutExpired:
                record['out'], record['rc'] = 'timed out', None
            server.join(10)
            records.append(record)
        sock.close()
        return port, records[0], None if game_only else records[1]

    def name_of(field):
        return ascii(field.decode('utf-16-le', 'replace'))

    def spn_of(ip, port):
        """The server as the game names it in the NTLMv2 blob."""
        return 'cifs/%s:%d' % (ip, port) if port > 0 and port != 445 else 'cifs/%s' % ip

    def common_checks(label, ip, port, clients):
        """No sanitizer report, no server error, and every connect to the address and port told
        (port 0 is 445)."""
        want = '%s:%d' % (ip, port if port > 0 else 445)
        for name, rec in clients:
            check('error' not in rec, '%s, %s logon: %s' % (label, name, rec.get('error')))
            check('Sanitizer' not in rec['out'] and 'runtime error' not in rec['out'],
                  '%s, %s logon: a sanitizer report:\n%s' % (label, name, rec['out']))
            connects = re.findall(r'^wrapped connect to (\S+)$', rec['out'], re.M)
            check(connects and all(c == want for c in connects), '%s, %s logon: connected to %s, not only to %s' %
                  (label, name, ', '.join(connects) or 'nothing', want))

    def lm_and_timestamp(label, rec, password, user_upper):
        """The game's LM response and NTLMv2 timestamp (MS-NLMP 3.3.2): with a server timestamp
        among the AV pairs it kept, Z(24) and that timestamp; without, LMv2 over the blob's client
        challenge, and no timestamp."""
        kept, off, avs = [], 0, av_pairs(rec)
        while off + 4 <= len(avs):
            code, length = struct.unpack_from('<HH', avs, off)
            if code == MSV_AV_TIMESTAMP and length == 8:
                kept.append(avs[off + 4:off + 12])
            off += 4 + length
        lm, blob = rec['lm'], rec['nt'][16:]
        if kept:
            check(lm == bytes(24) and blob[8:16] == kept[-1],
                  '%s: the in-game logon sends LM %s and timestamp %s; with the server\'s timestamp among the AV pairs '
                  'it should send Z(24) and %s' % (label, lm.hex(), blob[8:16].hex(), kept[-1].hex()))
        else:
            ntowf = hmac_md5(md4(u16(password)), u16(user_upper) + u16(TARGET_NAME))
            check(len(lm) == 24 and lm[16:] == blob[16:24] and lm[:16] == hmac_md5(ntowf, SERVER_CHALLENGE + lm[16:]) and
                  blob[8:16] == bytes(8),
                  '%s: the in-game logon sends LM %s and timestamp %s; without a server timestamp it should send '
                  'LMv2 over the blob\'s client challenge %s, and no timestamp' %
                  (label, lm.hex(), blob[8:16].hex(), blob[16:24].hex()))

    def signed_checks(label, rec, name, password, user_upper):
        """With signing required, the TREE_CONNECT that follows the logon is signed: SMB2_FLAGS_SIGNED and
        HMAC-SHA256 over the message with the signature zeroed, keyed by the session base key
        HMAC_MD5(NTOWFv2, NTProofStr) (no KEY_EXCH), which is what the server checks."""
        tc = rec.get('tree_connect')
        check(tc is not None, '%s: the %s logon sent no TREE_CONNECT' % (label, name))
        if tc is None:
            return
        ntowf = hmac_md5(md4(u16(password)), u16(user_upper) + u16(TARGET_NAME))
        key = hmac_md5(ntowf, rec['nt'][:16])
        msg = bytearray(tc)
        msg[48:64] = bytes(16)
        want = hmac.new(key, bytes(msg), 'sha256').digest()[:16]
        flags = struct.unpack_from('<I', tc, 16)[0]
        check(flags & 0x08 and tc[48:64] == want,
              '%s: the %s TREE_CONNECT is not signed with the session base key (flags 0x%x, signature %s, want %s)' %
              (label, name, flags, tc[48:64].hex(), want.hex()))

    def run(label, user='ps2', password='secret', ti=TARGET_INFO, ip='127.0.0.1', port=None, tn_last=False,
            sign=False):
        port, g, m = logon(ti, user, password, ip, port, tn_last, False, sign)
        common_checks(label, ip, port, [('in-game', g), ('menu', m)])
        for name, rec in [('in-game', g), ('menu', m)]:
            check('nt' in rec, '%s, %s logon: no AUTHENTICATE_MESSAGE reached the server (exit %s):\n%s' %
                  (label, name, rec['rc'], rec['out']))
        if 'nt' not in g or 'nt' not in m:
            return
        want_user, domain = user or 'Guest', u16(TARGET_NAME)
        check(m['user'] == u16(want_user) and m['domain'] == domain,
              '%s: the menu (smb2man) sends UserName %s and DomainName %s; this check expects %s and %s: '
              'usmb2 or smb2man changed, update this check and smb2.c together' %
              (label, name_of(m['user']), name_of(m['domain']), ascii(want_user), ascii(TARGET_NAME)))
        check(g['user'] == m['user'], '%s: the in-game logon sends UserName %s, the menu sends %s' %
              (label, name_of(g['user']), name_of(m['user'])))
        check(g['domain'] == m['domain'], '%s: the in-game logon sends DomainName %s, the menu sends %s' %
              (label, name_of(g['domain']), name_of(m['domain'])))
        for name, rec in [('in-game', g), ('menu', m)]:
            check(not rec['flags'] & NTLMSSP_NEGOTIATE_ANONYMOUS, '%s: the %s logon is anonymous' % (label, name))
            # Not vacuous: the proof must not also match an empty domain (what a server that ignores
            # the domain would accept).
            check(not any(proof_inputs(rec, password, up(want_user), b'') for _, up in UPPER),
                  '%s: the %s proof matches an empty domain too' % (label, name))
        menu_upper = [how for how, up in UPPER if proof_inputs(m, password, up(want_user), domain)]
        game_upper = [how for how, up in UPPER if proof_inputs(g, password, up(want_user), domain)]
        check(menu_upper, "%s: the menu's NTLMv2 proof is not HMAC(NTOWFv2(password, UPPER(%s), %s), ...)" %
              (label, ascii(want_user), ascii(TARGET_NAME)))
        check(game_upper == menu_upper,
              '%s: the in-game NTLMv2 proof is computed from other inputs than the menu\'s: the menu\'s matches '
              'NTOWFv2(password, UPPER(%s), %s) upper-cased %s, the game\'s %s' %
              (label, ascii(want_user), ascii(TARGET_NAME), ' or '.join(menu_upper) or 'no way',
               ' or '.join(game_upper) or 'no way'))
        # The game's AV pairs, byte for byte: the CHALLENGE's TargetInfo with the server named.
        check(len(g['nt']) >= 16 + 28 + 4 and g['nt'][-4:] == bytes(4),
              '%s: the in-game NTLMv2 blob does not end in Z(4)' % label)
        want = with_target_name(ti, spn_of(ip, port))
        check(av_pairs(g) == want, '%s: the in-game logon sends the AV pairs %s; this check expects %s' %
              (label, pairs(av_pairs(g)), pairs(want)))
        check('connect 0' in m['out'] or 'connect -' in m['out'], '%s: the menu did not finish:\n%s' % (label, m['out']))
        if game_upper:
            lm_and_timestamp(label, g, password, dict(UPPER)[game_upper[0]](want_user))
        if sign and game_upper and menu_upper:
            signed_checks(label, g, 'in-game', password, dict(UPPER)[game_upper[0]](want_user))
            signed_checks(label, m, 'menu', password, dict(UPPER)[menu_upper[0]](want_user))
        check('session setup 1' in g['out'], '%s: the in-game reader did not finish the logon:\n%s' % (label, g['out']))

    def refused(label, ti, tn_last=False):
        """The game refuses the logon (sends no AUTHENTICATE_MESSAGE); the menu finishes cleanly."""
        port, g, m = logon(ti, 'ps2', 'secret', '127.0.0.1', None, tn_last, False)
        common_checks(label, '127.0.0.1', port, [('in-game', g), ('menu', m)])
        check(re.search(r'^connect -?\d+$', m['out'], re.M) is not None, '%s: the menu did not finish:\n%s' %
              (label, m['out']))
        check('session setup -1000' in g['out'] and 'nt' not in g,
              '%s: the in-game logon does not refuse:\n%s' % (label, g['out']))

    def capped(label, ti, ip, port):
        """The game alone: its AV pairs are the rewrite stopped at SMB2_TI_MAX, without a sanitizer report."""
        port, g, _ = logon(ti, 'ps2', 'secret', ip, port, False, True)
        common_checks(label, ip, port, [('in-game', g)])
        check('nt' in g, '%s, in-game logon: no AUTHENTICATE_MESSAGE reached the server (exit %s):\n%s' %
              (label, g['rc'], g['out']))
        if 'nt' not in g:
            return
        want = with_target_name(ti, spn_of(ip, port), TI_MAX)
        check(av_pairs(g) == want, '%s: the in-game logon sends the AV pairs %s; with SMB2_TI_MAX %d it should send %s' %
              (label, pairs(av_pairs(g)), TI_MAX, pairs(want)))
        check('session setup 1' in g['out'], '%s: the in-game reader did not finish the logon:\n%s' % (label, g['out']))

    # (user, password) as configured on the console. Mixed case: the proof hashes UPPER(user), the
    # message sends it as typed. Blank: both log on as "Guest" (with the password, or an empty one).
    for user, password in [('ps2', 'secret'), ('MixedCase', 'Pass word'), ('José', 'Café'), ('zo\u00eb\u00ff', 'x'), ('', 'secret'),
                           ('', '')]:
        run('user %s, password %s' % (ascii(user), ascii(password)), user, password)
    # The server as the EE can give it: on port 445 and port 0 (which both take as 445) both name it
    # without the port; 1111 is a non-default port; 192.168.100.200:65535 is the longest name.
    run('port 445', port=445)
    run('port 0', port=0)
    run('port 1111', port=1111)
    # Signing required: both sign the TREE_CONNECT with the key the server derives.
    run('signing required', sign=True)
    run('signing required, user %s, password %s' % (ascii('Jos\u00e9'), ascii('Caf\u00e9')), 'Jos\u00e9', 'Caf\u00e9',
        sign=True)
    run('192.168.100.200 on port 65535', ip='192.168.100.200', port=65535)
    for what, ti in ODD_TARGET_INFO:
        run('TargetInfo ' + what, ti=ti)
    run('TargetInfo of SMB2_TI_MAX bytes, 192.168.100.200 on port 65535', ti=ODD_TARGET_INFO[-1][1],
        ip='192.168.100.200', port=65535)
    for what, ti in TN_LAST_TARGET_INFO:
        run('TargetInfo before TargetName, ' + what, ti=ti, tn_last=True)
    for what, ti in REFUSED_TARGET_INFO:
        refused('TargetInfo ' + what, ti)
    for what, ti in CAPPED_TARGET_INFO:
        capped('TargetInfo ' + what + ', 192.168.100.200 on port 65535', ti, '192.168.100.200', 65535)

if failures:
    print('SMB2 logon parity checks FAILED:')
    for failure in failures:
        print('  ' + failure)
    sys.exit(1)
print('smb2 logon parity: the in-game reader and the menu connect to the same server, send the same UserName '
      'and DomainName and compute their NTLMv2 proofs from the same user, domain and password (named, mixed-case, '
      'non-ASCII and blank users); both sign as a server requiring it checks; the game names the server in its AV pairs, keeps within SMB2_TI_MAX and sends '
      'its LM response and timestamp as MS-NLMP asks; the menu survives every TargetInfo list cleanly')
