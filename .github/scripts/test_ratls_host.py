"""Host test for src/platform/ratls.c: verified HTTPS GET over the vendored mbedTLS (TLS 1.3 and 1.2).

The RA edition looks games up on retroachievements.org over HTTPS (ORBIT parity). This builds the
portable TLS + HTTP code (src/platform/ratls.c) with the vendored mbedTLS and RiptOPL's configuration, then
drives it (tests/ratls/host_main.c) against local Python TLS servers using a throwaway test CA:
full GETs on TLS 1.3 and TLS 1.2, chunked and close-delimited bodies (a chunk ending in "0\r\n" is
not the end), refusal of oversize bodies,
wrong host names, expired and untrusted certificates, an unset clock (no connection at all), a
stalled server, a 404, plus the seven real anchors, the RTC conversion and DRBG reseeding.

Check for Updates' streamed download (raTlsDownload) is driven the same way, on the RA configuration
and on the slim TLS 1.2 one the other standard editions build: a sized body streamed to the sink,
redirects handing back Location, and refusal of unsized, chunked, compressed, oversize and short
bodies and of a sink that fails. The slim build must complete TLS 1.2 with ECDSA and RSA chains and
fail cleanly against a TLS 1.3-only server.
"""
import concurrent.futures
import os
import re
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MBED = ROOT / "third_party/mbedtls"
CC = os.environ.get("CC") or shutil.which("cc") or shutil.which("gcc")
BASE_FLAGS = ["-O1", "-I", str(ROOT), "-I", str(MBED / "include"), "-I", str(MBED)]
CONFIGS = {"ra": '-DMBEDTLS_CONFIG_FILE="riptopl_mbedtls_config.h"',
           "slim": '-DMBEDTLS_CONFIG_FILE="riptopl_mbedtls_slim_config.h"'}
LIBS = ["-lws2_32", "-lbcrypt"] if sys.platform == "win32" else []
OPENSSL = shutil.which("openssl")
assert CC and OPENSSL, "needs a C compiler and the openssl CLI"

failures = []

# On the console the clock gate comes before any DNS lookup or connection.
_ps2 = (ROOT / "src/sources/ratls_ps2.cpp").read_text(encoding="utf-8")
_get = _ps2[_ps2.index("int raHttpsGet("):]
assert _get.index("raTlsClockOk(") < _get.index("gethostbyname(") < _get.index("connect("), "clock check must precede DNS"
assert "raNetNicReserved()" in _get[:_get.index("gethostbyname(")]
# clock() is a 32-bit microsecond counter on the EE: the budget is elapsed time from before DNS,
# never an absolute deadline, and never RATLS_DEADLINE_MS * CLOCKS_PER_SEC (8e9 overflows).
assert "(clock_t)(clock() - start) >= (clock_t)ms * (CLOCKS_PER_SEC / 1000)" in _ps2
assert "int ms = RATLS_DEADLINE_MS" in _ps2
assert "> bio->deadline" not in _ps2 and "RATLS_DEADLINE_MS * CLOCKS_PER_SEC" not in _ps2
assert _get.index("clock_t start = clock();") < _get.index("gethostbyname(")


checks = 0


def check(name, ok, detail=""):
    global checks
    checks += 1
    if not ok:
        failures.append(f"{name}: {detail}")
        print(f"FAIL {name}: {detail}")
    else:
        print(f"ok   {name}")


def build(tmp, config):
    flags = BASE_FLAGS + [CONFIGS[config]]
    objs = tmp / f"obj-{config}"
    objs.mkdir()
    sources = sorted((MBED / "library").glob("*.c"))

    def compile_one(src):
        out = objs / (src.stem + ".o")
        subprocess.run([CC, "-c", *flags, str(src), "-o", str(out)], check=True)
        return str(out)

    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        objects = list(pool.map(compile_one, sources))
    exe = tmp / (f"host_main-{config}.exe" if sys.platform == "win32" else f"host_main-{config}")
    subprocess.run([CC, *flags, "-Wall", "-Werror", str(ROOT / "tests/ratls/host_main.c"), str(ROOT / "src/platform/ratls.c"),
                    *objects, "-o", str(exe), *LIBS], check=True)
    return exe


def openssl(*args, cwd):
    subprocess.run([OPENSSL, *args], cwd=cwd, check=True, capture_output=True)


def make_pki(d):
    """A test CA plus leaves, all ECDSA P-256 (needed for the TLS 1.2 ECDHE-ECDSA suite)."""
    ec = ["-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256", "-nodes"]
    openssl("req", "-x509", *ec, "-keyout", "ca.key", "-out", "ca.pem", "-days", "3650",
            "-subj", "/CN=RiptOPL Test CA", "-addext", "basicConstraints=critical,CA:TRUE",
            "-addext", "keyUsage=critical,keyCertSign,cRLSign", cwd=d)
    openssl("x509", "-in", "ca.pem", "-outform", "der", "-out", "ca.der", cwd=d)
    for name, cn in (("leaf", "ra.test"), ("other", "other.test")):
        openssl("req", *ec, "-keyout", f"{name}.key", "-out", f"{name}.csr", "-subj", f"/CN={cn}", cwd=d)
        (d / f"{name}.ext").write_text(f"subjectAltName=DNS:{cn}\nbasicConstraints=CA:FALSE\n"
                                       "keyUsage=critical,digitalSignature\nextendedKeyUsage=serverAuth\n")
        openssl("x509", "-req", "-in", f"{name}.csr", "-CA", "ca.pem", "-CAkey", "ca.key", "-CAcreateserial",
                "-out", f"{name}.pem", "-days", "30", "-extfile", f"{name}.ext", cwd=d)
    # An unrelated CA, for "untrusted root".
    openssl("req", "-x509", *ec, "-keyout", "stranger.key", "-out", "stranger.pem", "-days", "3650",
            "-subj", "/CN=Stranger CA", "-addext", "basicConstraints=critical,CA:TRUE", cwd=d)
    openssl("x509", "-in", "stranger.pem", "-outform", "der", "-out", "stranger.der", cwd=d)
    # An RSA CA and leaf: the release download host chains to ISRG Root X1, an RSA root.
    rsa = ["-newkey", "rsa:2048", "-nodes"]
    openssl("req", "-x509", *rsa, "-keyout", "rsaca.key", "-out", "rsaca.pem", "-days", "3650",
            "-subj", "/CN=RiptOPL Test RSA CA", "-addext", "basicConstraints=critical,CA:TRUE",
            "-addext", "keyUsage=critical,keyCertSign,cRLSign", cwd=d)
    openssl("x509", "-in", "rsaca.pem", "-outform", "der", "-out", "rsaca.der", cwd=d)
    openssl("req", *rsa, "-keyout", "rsaleaf.key", "-out", "rsaleaf.csr", "-subj", "/CN=ra.test", cwd=d)
    (d / "rsaleaf.ext").write_text("subjectAltName=DNS:ra.test\nbasicConstraints=CA:FALSE\n"
                                   "keyUsage=critical,digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\n")
    openssl("x509", "-req", "-in", "rsaleaf.csr", "-CA", "rsaca.pem", "-CAkey", "rsaca.key", "-CAcreateserial",
            "-out", "rsaleaf.pem", "-days", "30", "-extfile", "rsaleaf.ext", cwd=d)


class Server:
    """One-connection TLS (or silent TCP) server answering with a scripted HTTP response."""

    def __init__(self, d, cert="leaf", version=None, response=b"", tls=True, linger=0, ciphers=None):
        """response may be a list of parts, sent 0.3 s apart (separate TLS records); linger keeps
        the connection open that long after the response."""
        self.linger = linger
        self.sock = socket.socket()
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(1)
        self.sock.settimeout(8)
        self.port = self.sock.getsockname()[1]
        self.accepts = 0
        self.request = b""
        self.response = response
        self.tls = tls
        self.ctx = None
        if tls:
            self.ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            self.ctx.load_cert_chain(str(d / f"{cert}.pem"), str(d / f"{cert}.key"))
            if version == "1.3":
                self.ctx.minimum_version = self.ctx.maximum_version = ssl.TLSVersion.TLSv1_3
            elif version == "1.2":
                self.ctx.minimum_version = self.ctx.maximum_version = ssl.TLSVersion.TLSv1_2
                self.ctx.set_ciphers(ciphers or "ECDHE-ECDSA-AES128-GCM-SHA256")
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        try:
            conn, _ = self.sock.accept()
        except OSError:
            return
        self.accepts += 1
        if not self.tls:
            time.sleep(3)  # accept and say nothing
            conn.close()
            return
        try:
            with self.ctx.wrap_socket(conn, server_side=True) as tls:
                tls.settimeout(5)
                while b"\r\n\r\n" not in self.request:
                    chunk = tls.recv(4096)
                    if not chunk:
                        return
                    self.request += chunk
                for i, part in enumerate(self.response if isinstance(self.response, list) else [self.response]):
                    if i:
                        time.sleep(0.3)
                    tls.sendall(part)
                time.sleep(self.linger)
                try:
                    tls.unwrap()
                except (ssl.SSLError, OSError):
                    pass
        except (ssl.SSLError, OSError):
            pass  # the client refused the certificate: expected in the negative cases

    def finish(self):
        self.thread.join(timeout=6)
        self.sock.close()


def get(exe, server_port, d, *, host="ra.test", path="/dorequest.php?r=gameid&m=abc", max_=4096,
        deadline=5000, now="sys", anchors=("ca.der",)):
    args = [str(exe), "get", host, str(server_port), path, str(max_), str(deadline), now] + [str(d / a) for a in anchors]
    out = subprocess.run(args, capture_output=True, timeout=30).stdout
    head, _, body = out.partition(b"--BODY--\n")
    rc = int(re.search(rb"rc=(-?\d+)", head).group(1))
    return rc, body


def download(exe, server_port, d, *, path="/NathanNeurotic/RiptOPL/releases/download/rolling-korium/RIPTOPL.ELF",
             max_=16384, max_body=1 << 20, fail_at=-1, anchors=("ca.der",)):
    args = [str(exe), "download", "ra.test", str(server_port), path, str(max_), str(max_body), str(fail_at)]
    out = subprocess.run(args + [str(d / a) for a in anchors], capture_output=True, timeout=30).stdout
    head, _, body = out.partition(b"--BODY--\n")
    rc = int(re.search(rb"rc=(-?\d+)", head).group(1))
    calls = int(re.search(rb"calls=(\d+)", head).group(1))
    location = re.search(rb"location=([^\n]*)", head).group(1)
    return rc, calls, location, body


def sized(body, extra=b""):
    return b"HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: " + \
        str(len(body)).encode() + b"\r\n" + extra + b"\r\n" + body


def download_checks(exe, d, tag):
    """raTlsDownload against local servers; tag names the configuration in each check."""
    elf = bytes(range(256)) * 1200  # 300 KB: many TLS records, several sink calls
    for version in ("1.3", "1.2") if tag == "ra" else ("1.2",):
        s = Server(d, version=version, response=sized(elf), linger=1)
        rc, calls, _, body = download(exe, s.port, d)
        s.finish()
        check(f"{tag}_download_tls{version.replace('.', '')}", rc == len(elf) and body == elf and calls > 1,
              f"rc={rc} calls={calls} got={len(body)}")
        if version == "1.2":
            check(f"{tag}_download_request", s.request.startswith(
                b"GET /NathanNeurotic/RiptOPL/releases/download/rolling-korium/RIPTOPL.ELF HTTP/1.1\r\n")
                and b"\r\nHost: ra.test\r\n" in s.request and b"\r\nAccept: */*\r\n" in s.request, repr(s.request))

    # GitHub answers the asset URL with a 302 to a signed URL on its download host.
    target = b"https://release-assets.githubusercontent.com/github-production-release-asset/1/2?sig=" + b"a" * 900
    s = Server(d, version="1.2", response=b"HTTP/1.1 302 Found\r\nLocation: " + target + b"\r\nContent-Length: 0\r\n\r\n")
    rc, calls, location, _ = download(exe, s.port, d)
    s.finish()
    check(f"{tag}_download_redirect", rc == -9 and location == target and calls == 0, f"rc={rc} location={location[:60]!r}")

    s = Server(d, version="1.2", response=b"HTTP/1.1 302 Found\r\nContent-Length: 0\r\n\r\n")
    rc, _, _, _ = download(exe, s.port, d)
    s.finish()
    check(f"{tag}_download_redirect_without_location", rc == -5, f"rc={rc}")

    refused = (
        ("unsized", b"HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n" + elf[:1000]),
        ("chunked", b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 4\r\n\r\n4\r\nabcd\r\n0\r\n\r\n"),
        ("gzip", sized(elf[:1000], b"Content-Encoding: gzip\r\n")),
        ("not_found", b"HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nNot Found"),
        ("zero_length", b"HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n"),
        ("bad_length", b"HTTP/1.1 200 OK\r\nContent-Length: 12x\r\n\r\n" + elf[:12]),
        ("huge_length", b"HTTP/1.1 200 OK\r\nContent-Length: 99999999999999999999\r\n\r\n" + elf[:12]),
    )
    for name, response in refused:
        s = Server(d, version="1.2", response=response)
        rc, calls, _, _ = download(exe, s.port, d)
        s.finish()
        check(f"{tag}_download_refuses_{name}", rc == -5 and calls == 0, f"rc={rc} calls={calls}")

    s = Server(d, version="1.2", response=sized(elf))
    rc, calls, _, _ = download(exe, s.port, d, max_body=len(elf) - 1)
    s.finish()
    check(f"{tag}_download_oversize", rc == -5 and calls == 0, f"rc={rc} calls={calls}")

    # Cut short: the server closes before Content-Length is met.
    s = Server(d, version="1.2", response=sized(elf)[:-1000])
    rc, _, _, body = download(exe, s.port, d)
    s.finish()
    check(f"{tag}_download_short", rc < 0 and rc != -9, f"rc={rc} got={len(body)}")

    # Bytes past the declared length are never handed on.
    s = Server(d, version="1.2", response=sized(elf[:5000]) + b"TRAILING")
    rc, _, _, body = download(exe, s.port, d)
    s.finish()
    check(f"{tag}_download_stops_at_length", rc == 5000 and body == elf[:5000], f"rc={rc} got={len(body)}")

    s = Server(d, version="1.2", response=sized(elf))
    rc, _, _, body = download(exe, s.port, d, fail_at=100000)
    s.finish()
    check(f"{tag}_download_sink_failure", rc == -10 and len(body) <= 100000, f"rc={rc} got={len(body)}")

    s = Server(d, cert="other", version="1.2", response=sized(elf))
    rc, calls, _, _ = download(exe, s.port, d)
    s.finish()
    check(f"{tag}_download_wrong_host", rc == -4 and calls == 0, f"rc={rc}")

    s = Server(d, version="1.2", response=sized(elf))
    rc, calls, _, _ = download(exe, s.port, d, anchors=("stranger.der",))
    s.finish()
    check(f"{tag}_download_untrusted_root", rc == -4 and calls == 0, f"rc={rc}")

    s = Server(d, cert="rsaleaf", version="1.2", ciphers="ECDHE-RSA-AES128-GCM-SHA256", response=sized(elf[:4000]))
    rc, _, _, body = download(exe, s.port, d, anchors=("rsaca.der",))
    s.finish()
    check(f"{tag}_download_rsa_chain", rc == 4000 and body == elf[:4000], f"rc={rc}")


def http(body, extra=b""):
    return b"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + str(len(body)).encode() + \
        b"\r\n" + extra + b"Connection: close\r\n\r\n" + body


def main():
    json_body = b'{"Success":true,"GameID":2791}'
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            exe, slim = pool.map(lambda c: build(tmp, c), ("ra", "slim"))
        d = tmp / "pki"
        d.mkdir()
        make_pki(d)

        for version in ("1.3", "1.2"):
            s = Server(d, version=version, response=http(json_body))
            rc, body = get(exe, s.port, d)
            s.finish()
            check(f"tls{version.replace('.', '')}_get", rc > 0 and body == json_body, f"rc={rc} body={body!r}")
            if version == "1.3":
                check("request_line", s.request.startswith(b"GET /dorequest.php?r=gameid&m=abc HTTP/1.1\r\n")
                      and b"\r\nHost: ra.test\r\n" in s.request and b"\r\nUser-Agent: RiptOPL/host-test\r\n" in s.request,
                      repr(s.request))

        chunked = (b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
                   b"a\r\n{\"Success\"\r\n14\r\n:true,\"GameID\":2791}\r\n0\r\n\r\n")
        s = Server(d, version="1.3", response=chunked)
        rc, body = get(exe, s.port, d)
        s.finish()
        check("chunked_body", rc > 0 and body == json_body, f"rc={rc} body={body!r}")

        # The first part ends "0\r\n\r\n" inside a chunk: the original stopped reading there and
        # failed the de-chunk. The response is complete only at the real last chunk.
        s = Server(d, version="1.3", response=[b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n6\r\n1230\r\n\r\n",
                                               b"0\r\n\r\n"])
        rc, body = get(exe, s.port, d)
        s.finish()
        check("chunk_ending_in_zero_crlf", rc > 0 and body == b"1230\r\n", f"rc={rc} body={body!r}")

        # The last chunk is complete only at the blank line after it: a close before that is a
        # truncated response, not an empty trailer.
        s = Server(d, version="1.3", response=b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\n1234\r\n0\r\n")
        rc, body = get(exe, s.port, d)
        s.finish()
        check("chunk_missing_final_crlf", rc < 0, f"rc={rc} body={body!r}")

        # A whole response ends the read at once, without waiting for the server to close.
        empty_chunked = b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n"
        for name, response, expected in (("length", http(json_body), json_body), ("chunked", chunked, json_body),
                                          ("empty_chunked", empty_chunked, b"")):
            s = Server(d, version="1.3", response=response, linger=3)
            t0 = time.monotonic()
            rc, body = get(exe, s.port, d, deadline=2000)
            elapsed = time.monotonic() - t0
            s.finish()
            check(f"complete_without_close_{name}", rc > 0 and body == expected and elapsed < 1.5,
                  f"rc={rc} body={body!r} elapsed={elapsed:.2f}")

        s = Server(d, version="1.3", response=b"HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n" + json_body)
        rc, body = get(exe, s.port, d)
        s.finish()
        check("no_length_until_close", rc > 0 and body == json_body, f"rc={rc} body={body!r}")

        s = Server(d, version="1.3", response=http(b"x" * 3000))
        rc, _ = get(exe, s.port, d, max_=512)
        s.finish()
        check("oversize_refused", rc == -5, f"rc={rc}")

        s = Server(d, cert="other", version="1.3", response=http(json_body))
        rc, _ = get(exe, s.port, d)
        s.finish()
        check("wrong_host", rc == -4, f"rc={rc}")

        s = Server(d, version="1.3", response=http(json_body))
        rc, _ = get(exe, s.port, d, now=str(int(time.time()) + 400 * 86400))
        s.finish()
        check("expired", rc == -4, f"rc={rc}")

        s = Server(d, version="1.3", response=http(json_body))
        rc, _ = get(exe, s.port, d, anchors=("stranger.der",))
        s.finish()
        check("untrusted_root", rc == -4, f"rc={rc}")

        s = Server(d, version="1.3", response=http(json_body))
        rc, _ = get(exe, s.port, d, now="unset")
        time.sleep(0.3)
        accepts = s.accepts
        s.sock.close()
        check("clock_unset", rc == -2 and accepts == 0, f"rc={rc} accepts={accepts}")

        s = Server(d, tls=False)
        t0 = time.monotonic()
        rc, _ = get(exe, s.port, d, deadline=1000)
        elapsed = time.monotonic() - t0
        s.finish()
        check("stall_deadline", rc == -6 and elapsed < 2.5, f"rc={rc} elapsed={elapsed:.2f}")

        s = Server(d, version="1.3", response=b"HTTP/1.1 404 Not Found\r\nContent-Length: 2\r\nConnection: close\r\n\r\nno")
        rc, _ = get(exe, s.port, d)
        s.finish()
        check("http_404", rc == -5, f"rc={rc}")

        roots = sorted((ROOT / "third_party/ca-roots").glob("*.der"))
        out = subprocess.run([str(exe), "anchors", *map(str, roots)], capture_output=True, text=True).stdout
        check("anchors_parse", len(roots) == 7 and "anchors bad=0" in out, out.strip())
        out = subprocess.run([str(slim), "anchors", *map(str, roots)], capture_output=True, text=True).stdout
        check("slim_anchors_parse", "anchors bad=0" in out, out.strip())

        # ps2sdk's ps2time() (ee/rpc/cdvd/src/scmd.c) reads the RTC as JST: UTC = RTC - 9 h.
        out = subprocess.run([str(exe), "rtc", "2026", "10", "12", "9", "0", "0"], capture_output=True, text=True).stdout
        check("rtc_to_utc", out.strip() == "1791763200", out.strip())

        lines = []
        for _ in range(2):
            lines += subprocess.run([str(exe), "drbg"], capture_output=True, text=True).stdout.split()
        check("drbg_distinct", len(lines) == 4 and all(re.fullmatch(r"[0-9a-f]{64}", x) for x in lines)
              and len(set(lines)) == 4, repr(lines))

        download_checks(exe, d, "ra")
        download_checks(slim, d, "slim")

        # The slim build speaks TLS 1.2 only: a TLS 1.3-only server is a clean TLS failure.
        s = Server(d, version="1.3", response=sized(b"x" * 100))
        rc, calls, _, _ = download(slim, s.port, d)
        s.finish()
        check("slim_refuses_tls13_only", rc == -3 and calls == 0, f"rc={rc}")

        # And it still does the RA-style GET over TLS 1.2.
        s = Server(d, version="1.2", response=http(json_body))
        rc, body = get(slim, s.port, d)
        s.finish()
        check("slim_get_tls12", rc > 0 and body == json_body, f"rc={rc} body={body!r}")

    if failures:
        print(f"{len(failures)} failure(s)")
        sys.exit(1)
    print(f"PASS: ratls host tests ({checks} checks)")


if __name__ == "__main__":
    main()
