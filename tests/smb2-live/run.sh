#!/bin/sh
# Live SMB check: runs the loader's two SMB2 clients, and its SMB1 in-game reader, on the host
# against a real Samba server.
#
#   ingame.c  drives modules/iopcore/cdvdman/smb2.c (smb2_cdvdman, the in-game reader) the way
#             device-smb.c and smb_mcemu do: negotiate, NTLMv2 logon, tree connect, a whole 5 MB
#             image read back and compared byte for byte, end-of-file short counts, VMC page
#             writes and read-back, a 100 KB write across the 64 KB chunk, error paths.
#   browse.c  drives modules/network/smb2man/smb2man.c (the menu's browse driver) over usmb2,
#             vendored at modules/network/smb2man/usmb2: LOGON/OPENSHARE devctls (GETSHARELIST is
#             refused: no share list), directory listing, stat, read/write/seek (seeks past 4 GiB in
#             a sparse 7 GiB image too), create/truncate/exclusive opens, mkdir, rename, remove,
#             paths past 127 characters, reconnect, and handles left open across LOGOFF.
#   keepalive.c  includes smb2man.c and runs its keepalive passes by hand: a session lost (the
#             socket cut) under open files and directories, by a failed ECHO, read or write, the
#             reconnect on the next tick, the backoff after a refused reconnect, LOGOFF and
#             CLOSESHARE while lost, and the leak check that a dropped session or a share change
#             frees every handle and directory listing. With signing mandatory: every reply verified
#             but ECHO's, and a reply that comes unsigned costing one drop and one reconnect, not a loop.
#   readreply.c  includes usmb2.c and feeds usmb2_pread hand-built READ replies on a socketpair:
#             data at DataOffset with padding before it, and replies claiming more bytes than they
#             carried, or than were asked for, or a DataOffset outside the frame, all refused; signed,
#             a padded reply verifies and one whose padding or data changed after signing is refused.
#   longname.c  compiles device-smb.c whole, with smb2.c and again with smb.c (SMB1), and launches
#             a game the way the IOP does, from a settings block filled as the EE fills it, for the
#             longest file names the menu lists and the lengths either side of each one the old
#             160-byte field cut, and for an image that is not there, whose read must fail
#             (longname.sh, sourced at the end; the SMB1 cases run against the same server with NT1
#             allowed). launchname.c checks the same names without a server, in CI.
#
# All are built with AddressSanitizer and UBSan; a sanitizer report, a leak included, fails the case
# (it exits 86, so not even a case that expects a refusal takes one for it).
# The server is started on a private port with NTLMv2 only, run once with signing mandatory and once
# with signing off, once capped at SMB 2.0.2, and once SMB 3.0-and-up, where the browse driver must
# connect and report an SMB 3.x dialect (it offers 2.0.2 and 2.1, then 3.0 and 3.0.2). Both readers
# sign SMB 2.x with HMAC-SHA256 when the server requires it; an SMB 3.x server that requires signing
# must be refused cleanly; longname.sh's SMB1 cases restart it with signing off and NT1 and NTLM allowed, as smb.c
# needs. Guest logons (an unknown user, and a blank user name) are checked against a guest-ok share;
# a blank user name is also checked against a server where only a real "Guest" account gets in,
# since it logs on as "Guest" as the menu does, not anonymously.
#
# Needs: a C compiler, Samba (smbd, smbpasswd, smbclient) and the PS2SDK's ps2smb.h (found through
# PS2BUILD_HOME, default ~/.local/share/ps2build). Run as root (smbd wants to own its state) from
# anywhere: sh tests/smb2-live/run.sh
# This is a host test, not a console test: it does not exercise SMSTCPIP, the IOP or real hardware.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
port=${SMB2_LIVE_PORT:-4450}
sdk=${PS2BUILD_HOME:-$HOME/.local/share/ps2build}
ps2smb=$(find "$sdk/packages" -name ps2smb.h -path '*common/include*' 2>/dev/null | head -n 1)
[ -n "$ps2smb" ] || { echo "ps2smb.h not found under $sdk/packages"; exit 2; }
command -v smbd >/dev/null || { echo "smbd not installed (apt-get install samba smbclient)"; exit 2; }
command -v timeout >/dev/null || { echo "timeout not installed (coreutils)"; exit 2; }

work=$(mktemp -d)
cleanup() {
    [ -f "$work/run/smbd.pid" ] && kill "$(cat "$work/run/smbd.pid")" 2>/dev/null || true
    # smbd starts samba-dcerpcd and its rpcd_* workers on demand (for the share lists) and leaves
    # them running when it exits; with $work gone they never exit. Stop them before removing it.
    pkill -f -- "--configfile=$work/smb.conf" 2>/dev/null || true
    i=0
    while pgrep -f -- "--configfile=$work/smb.conf" >/dev/null 2>&1 && [ $i -lt 50 ]; do
        i=$((i + 1))
        sleep 0.1
    done
    rm -rf "$work"
}
# EXIT cleans up; INT/TERM exit (so the script does not resume with $work gone), which runs it.
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# ---- the share ----------------------------------------------------------------------------------
mkdir -p "$work/share/DVD" "$work/share/VMC" "$work/run/ncalrpc" "$work/inc"
python3 - "$work/share" <<'EOF'
import sys
root = sys.argv[1]
open(root + '/DVD/TEST.iso', 'wb').write(bytes((i * 7 + 3) & 0xff for i in range(5 * 1024 * 1024 + 1234)))
open(root + '/VMC/card.bin', 'wb').write(bytes(1024 * 1024 * 2))
# A dual-layer image in one file, sparse (no data on disk): 7 GiB long, a marker where layer 1 starts.
with open(root + '/DVD/DVD9.iso', 'wb') as f:
    f.seek(0xFE830000)
    f.write(b'LAYER1')
    f.truncate(0x1C0001234)
EOF
# A name outside ASCII, as libsmb2 lists it (UTF-8): the in-game reader must request the same name.
u8name=$(printf 'Caf\303\251 \303\274.iso')
dd if="$work/share/DVD/TEST.iso" of="$work/share/DVD/$u8name" bs=4096 count=16 status=none

conf="$work/smb.conf"
write_conf() { # $1 signing, $2 extra global lines, $3 the share's guest ok (default yes)
    cat >"$conf" <<EOF
[global]
   server role = standalone server
   map to guest = bad user
   smb ports = $port
   interfaces = 127.0.0.1
   bind interfaces only = yes
   hosts allow = 127.0.0.1
   server min protocol = SMB2_02
   server signing = $1
   ntlm auth = ntlmv2-only
   $2
   ncalrpc dir = $work/run/ncalrpc
   pid directory = $work/run
   lock directory = $work/run
   state directory = $work/run
   cache directory = $work/run
   private dir = $work/run
   log file = $work/run/log.%m
   disable netbios = yes
   load printers = no
[PS2SMB]
   path = $work/share
   read only = no
   force user = root
   guest ok = ${3:-yes}
EOF
}

start_smbd() {
    # Wait for the previous smbd to be gone: if it still held the port, the readiness probe below
    # would succeed against the OLD configuration and the next cases would test nothing new.
    if [ -f "$work/run/smbd.pid" ]; then
        oldpid=$(cat "$work/run/smbd.pid")
        kill "$oldpid" 2>/dev/null || true
        i=0
        while kill -0 "$oldpid" 2>/dev/null; do
            i=$((i + 1))
            [ $i -lt 50 ] || { echo "previous smbd ($oldpid) did not exit"; exit 1; }
            sleep 0.2
        done
    fi
    smbd -s "$conf" -D || { echo "smbd did not start"; tail -20 "$work/run/log.smbd" 2>/dev/null; exit 1; }
    i=0
    until smbclient -s "$conf" -p "$port" //127.0.0.1/PS2SMB -U ps2%secret -c ls >/dev/null 2>&1; do
        i=$((i + 1))
        [ $i -lt 50 ] || { echo "smbd did not come up"; cat "$work/run/log.smbd" 2>/dev/null | tail -20; exit 1; }
        sleep 0.2
    done
}

# ---- build ----------------------------------------------------------------------------------------
# A sanitizer report exits 86, distinct from every exit code the clients use themselves (1-7, 9, 10).
export ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}exitcode=86"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:+$UBSAN_OPTIONS:}exitcode=86"
cp "$ps2smb" "$work/inc/"
san="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g -O1"
cc $san -Wall -Wno-unused-parameter -I"$here/inc-ingame" -I"$root/modules/iopcore/cdvdman" -I"$root/modules/iopcore/common" \
    "$here/ingame.c" "$root/modules/iopcore/cdvdman/smb2.c" "$root/modules/iopcore/cdvdman/smb2auth.c" -o "$work/ingame"
cc $san -Wall -Wno-unused-parameter -Wno-unused-function -I"$here/inc-ingame" -I"$root/modules/iopcore/cdvdman" \
    -I"$root/modules/iopcore/common" "$here/utf16.c" "$root/modules/iopcore/cdvdman/smb2auth.c" -o "$work/utf16"

u2="$root/modules/network/smb2man/usmb2"
# The menu driver's build: smb2man.c compiles usmb2.c into itself; usmb2's NTLM and UTF-16 helpers
# are separate objects, with the features ps2.yaml turns on.
feat="-DUSMB2_FEATURE_WRITE -DUSMB2_FEATURE_NTLM -DUSMB2_FEATURE_UNICODE -DUSMB2_FEATURE_STAT -DUSMB2_FEATURE_MKDIR \
 -DUSMB2_FEATURE_UNLINK -DUSMB2_FEATURE_RENAME"
alloc="-Dmalloc=iop_malloc -Dfree=iop_free -Dcalloc=iop_calloc"
# usmb2 stores 64-bit header fields at 4-byte-aligned offsets of its buffer, which the IOP (32-bit
# MIPS, two word stores) takes and UBSan's alignment check does not: that one check is off for the
# driver's own code. Every offset is 4-aligned (usmb2_context.buf starts 8-aligned).
drvsan="$san -fno-sanitize=alignment"
mkdir -p "$work/lib"
for f in ntlm md4c hmac-md5 md5 unicode; do
    # shellcheck disable=SC2086
    cc $drvsan -w $feat $alloc -c "$u2/$f.c" -o "$work/lib/$f.o"
done
# Signing: the in-game reader's hashing, as ps2.yaml builds it into smb2man.
cc $drvsan -Wall -I"$here/inc-browse" -c "$root/modules/iopcore/cdvdman/smb2auth.c" -o "$work/lib/smb2auth.o"
# shellcheck disable=SC2086
cc $drvsan -Wall -Wno-unused-function -I"$here/inc-browse" -I"$work/inc" -I"$root/modules/network/smb2man" -I"$root/modules/iopcore/cdvdman" $feat $alloc \
    -D_start=smb2man_start -c "$root/modules/network/smb2man/smb2man.c" -o "$work/smb2man.o"
cc $san -Wall -I"$here/inc-browse" -I"$work/inc" -I"$root/modules/network/smb2man" "$here/browse.c" "$work/smb2man.o" "$work"/lib/*.o -o "$work/browse"
# shellcheck disable=SC2086
cc $drvsan -Wall -Wno-unused-function -I"$here/inc-browse" -I"$work/inc" -I"$root/modules/network/smb2man" -I"$root/modules/iopcore/cdvdman" $feat $alloc \
    -D_start=smb2man_start "$here/keepalive.c" "$work"/lib/*.o -o "$work/keepalive"
# usmb2's READ reply framing, signed and not, against hand-built replies (no server)
# shellcheck disable=SC2086
cc $drvsan -Wall -Wno-unused-function -I"$here/inc-browse" -I"$u2" -I"$root/modules/iopcore/cdvdman" -DUSMB2_FEATURE_WRITE \
    "$here/readreply.c" "$root/modules/iopcore/cdvdman/smb2auth.c" -o "$work/readreply"

id ps2 >/dev/null 2>&1 || useradd -M ps2
fails=0
run() { # $1 label, then the command
    label=$1
    shift
    if (cd "$work" && "$@") >"$work/out.txt" 2>&1; then
        echo "ok   $label"
    else
        echo "FAIL $label"
        # the failed checks and any sanitizer report, wherever they are in the output (an ASan
        # report ends with its shadow-byte legend, which is all the last lines would show)
        grep -E '^FAIL[: ]|ERROR: [A-Za-z]+Sanitizer|runtime error|^SUMMARY: ' "$work/out.txt" || true
        echo "     last lines:"
        tail -15 "$work/out.txt"
        fails=$((fails + 1))
    fi
}
# Each client gets a time limit: the in-game negotiate retries forever by design (a game keeps
# trying), so a stalled case must fail here instead of blocking the run.
ingame() { # $1 user, $2 password
    timeout 120 ./ingame 127.0.0.1 "$port" "$1" "$2" PS2SMB '\DVD\TEST.iso' '\VMC\card.bin' "\\DVD\\$u8name" "share/DVD/$u8name" && cmp readback.bin share/DVD/TEST.iso
}
denied() { # the logon must be refused cleanly: exit 3 after the refusal, no hang, crash or sanitizer report
    rc=0
    timeout 120 ./ingame 127.0.0.1 "$port" ps2 wrong PS2SMB '\DVD\TEST.iso' '\VMC\card.bin' >denied.txt 2>&1 || rc=$?
    cat denied.txt
    [ $rc -eq 3 ] && grep -q 'session setup -1000' denied.txt
}
browse() {
    timeout 120 ./browse 127.0.0.1 "$port" ps2 secret PS2SMB '\DVD\TEST.iso' share/DVD/TEST.iso "$u8name"
}
keepalive() { # $1 the server's signing setting
    timeout 120 ./keepalive 127.0.0.1 "$port" ps2 secret PS2SMB "$1"
}
browse_refused() { # the share must be refused cleanly: no hang, crash or sanitizer report
    rc=0
    timeout 120 ./browse 127.0.0.1 "$port" ps2 secret PS2SMB '\DVD\TEST.iso' share/DVD/TEST.iso >refused.txt 2>&1 || rc=$?
    cat refused.txt
    [ $rc -eq 1 ] && grep -q '^FAIL: openshare PS2SMB' refused.txt && ! grep -q 'Sanitizer' refused.txt
}
browse_smb3only() { # SMB 3.0 and up only: the menu must list and open it, negotiating 3.0 or 3.0.2
    # ("SMB3" alone means 3.1.1 to Samba, which the driver does not offer: it has no 3.1.1 contexts)
    timeout 120 ./browse 127.0.0.1 "$port" ps2 secret PS2SMB '\DVD\TEST.iso' share/DVD/TEST.iso >smb3only.txt 2>&1 &&
        grep -q '^ok: negotiated dialect 0x03' smb3only.txt || { cat smb3only.txt; false; }
}

run "in-game reader: UTF-8 names to UTF-16 (put_utf16)" ./utf16
run "browse driver: READ data at DataOffset (signed and not), short or oversized READ replies refused" ./readreply
for signing in mandatory disabled; do
    write_conf "$signing" ""
    printf 'secret\nsecret\n' | smbpasswd -c "$conf" -s -a ps2 >/dev/null
    start_smbd
    run "in-game reader, signing $signing, NTLMv2 user" ingame ps2 secret
    run "in-game reader, signing $signing, guest" ingame nobody x
    run "in-game reader, signing $signing, blank user and password, guest-ok share" ingame '' ''
    run "in-game reader, signing $signing, wrong password refused" denied
    run "browse driver, signing $signing" browse
    run "browse driver keepalive: lost session, reconnect, backoff, async open, no leaks, signing $signing" keepalive "$signing"
done
# A blank user name logs on as "Guest" with the password, as the menu does, not anonymously: here
# "Guest" is a real account (mapped to ps2) and there is no guest or anonymous access to the share.
printf 'ps2 = Guest\n' >"$work/usermap"
write_conf mandatory "map to guest = never
   username map = $work/usermap" no
start_smbd
run "in-game reader, blank user logs on as Guest (no guest or anonymous access)" ingame '' secret
write_conf mandatory "server max protocol = SMB2_02"
start_smbd
run "in-game reader, SMB 2.0.2 only, signing mandatory" ingame ps2 secret
write_conf mandatory "server max protocol = SMB2_02"
start_smbd
run "browse driver, SMB 2.0.2 only, signing mandatory" browse
# 3.x signs with AES-CMAC, which neither reader implements: such a server must be refused cleanly.
write_conf mandatory "server min protocol = SMB3_00"
start_smbd
run "browse driver refuses SMB 3.0-and-up with signing mandatory, cleanly" browse_refused
write_conf disabled "server min protocol = SMB3_00"
start_smbd
run "browse driver reaches an SMB 3.0-and-up server" browse_smb3only
. "$here/longname.sh"

[ $fails -eq 0 ] && echo "smb2-live: all passed" || echo "smb2-live: $fails failed"
exit $fails
