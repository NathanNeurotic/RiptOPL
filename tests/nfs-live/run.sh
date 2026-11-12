#!/bin/sh
# Live NFS check: runs the loader's in-game NFS reader on the host against PS2-Servers' NFS server.
#
#   ingame.c    drives modules/iopcore/cdvdman/nfs.c (nfs_cdvdman, the in-game reader) the way
#               device-smb.c and smb_mcemu do: connect from a port below 1024, mount an export that is
#               a folder of the share, a whole 5 MB image read back and compared byte for byte, the
#               connection cut part way and the read carrying on over a new one, end-of-file short
#               counts, VMC page writes and read-back, a 100 KB write across the 8 KB chunk after a cut,
#               a UTF-8 name, every file slot, a missing file, a missing export, nothing listening, an
#               unmount before any connection (the network never came up);
#               against a server that refuses writes, the VMC writes fail without reconnecting.
#   longname.c  (tests/smb2-live) compiles device-smb.c whole with nfs.c and launches games the way the
#               IOP does, from a settings block filled as the EE fills it: the longest file names the
#               menu lists, with the longest prefix and with none, a USBExtreme game in two parts, and a
#               missing image, whose read must fail.
#
# All are built with AddressSanitizer and UBSan; a sanitizer report fails the case.
# Needs: a C compiler, Python 3, and PS2-Servers (github.com/NathanNeurotic/PS2-Servers) for its NFS
# server, found through PS2_SERVERS (default: a PS2-Servers checkout next to this one). Run as root to
# check the port below 1024 (as a user the reader falls back to any port, which this server takes):
#   sh tests/nfs-live/run.sh          (NFS_LIVE_VERBOSE=1 prints every case's output)
# This is a host test, not a console test: it does not exercise SMSTCPIP, the IOP or real hardware.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
port=${NFS_LIVE_PORT:-20490}
servers=${PS2_SERVERS:-$root/../PS2-Servers}
server="$servers/nfs4_server/nfs4_server.py"
[ -f "$server" ] || { echo "PS2-Servers' NFS server not found at $server (set PS2_SERVERS)"; exit 2; }
command -v timeout >/dev/null || { echo "timeout not installed (coreutils)"; exit 2; }

work=$(mktemp -d)
pid=
stop_server() {
    if [ -n "$pid" ]; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
        pid=
    fi
}
cleanup() {
    stop_server
    rm -rf "$work"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# ---- the share: an export folder PS2 with the image, a card and a UTF-8 name ---------------------
u8name=$(printf 'Caf\303\251 \346\227\245\346\234\254.iso')
mkdir -p "$work/share/PS2/DVD" "$work/share/PS2/VMC"
python3 - "$work/share/PS2" "$u8name" <<'EOF'
import os, sys
root, u8 = sys.argv[1], sys.argv[2]
data = bytes((i * 7 + (i >> 11)) & 0xFF for i in range(5 * 1024 * 1024 + 1234))  # not a whole number of reads
open(os.path.join(root, "DVD", "TEST.iso"), "wb").write(data)
open(os.path.join(root, "VMC", "card.bin"), "wb").write(bytes(64 * 528))
open(os.path.join(root, "DVD", u8), "wb").write(data[:4096])
EOF

start_server() { # $@: extra server options
    stop_server
    python3 "$server" --root-dir "$work/share" --bind 127.0.0.1 --port "$port" "$@" >"$work/server.log" 2>&1 &
    pid=$!
    i=0
    while ! python3 -c "import socket,sys; socket.create_connection(('127.0.0.1', int(sys.argv[1])), 1).close()" "$port" 2>/dev/null; do
        i=$((i + 1))
        [ $i -lt 50 ] || { echo "the NFS server did not start:"; cat "$work/server.log"; exit 2; }
        sleep 0.1
    done
}

# ---- builds --------------------------------------------------------------------------------------
san="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g -O1"
host="-include stdlib.h -include unistd.h" # unfs4.c's host headers, before nfs.c's mappings rename what they declare
inc="-I$root/tests/smb2-live/inc-ingame -I$root/modules/iopcore/cdvdman -I$root/modules/iopcore/common -I$root/modules/network/nfsman"
cc $san -Wall -Wno-unused-parameter $host -DSMB_DRIVER -DNFS_DRIVER $inc "$here/ingame.c" -o "$work/ingame"
namemax=$(sed -n -E 's/^#define[[:space:]]+ISO_GAME_NAME_MAX[[:space:]]+([0-9]+).*/\1/p' "$root/include/supportbase.h")
[ -n "$namemax" ] || { echo "ISO_GAME_NAME_MAX not found in include/supportbase.h"; exit 2; }
ld="$root/tests/smb2-live"
cc $san -Wall -Wno-unused-parameter $host -DSMB_DRIVER -DNFS_DRIVER -DISO_GAME_NAME_MAX="$namemax" -I"$ld/inc-device" $inc \
    "$ld/longname.c" "$root/modules/iopcore/cdvdman/device-smb.c" "$root/modules/iopcore/cdvdman/nfs.c" -o "$work/longname"

# ---- the cases -----------------------------------------------------------------------------------
cd "$work"
passed=0
failed=0
run() { # $1 description, then the command
    desc=$1
    shift
    if "$@" >"$work/case.log" 2>&1; then
        passed=$((passed + 1))
        echo "PASS: $desc"
        [ -z "${NFS_LIVE_VERBOSE:-}" ] || sed 's/^/    /' "$work/case.log"
    else
        failed=$((failed + 1))
        echo "FAIL: $desc"
        sed 's/^/    /' "$work/case.log"
    fi
}
ingame() { # $1 rw or ro
    timeout 120 ./ingame 127.0.0.1 "$port" /PS2 '\DVD\TEST.iso' '\VMC\card.bin' "$1" "\\DVD\\$u8name" "share/PS2/DVD/$u8name" &&
        { [ "$1" = ro ] || cmp readback.bin share/PS2/DVD/TEST.iso; }
}
longname() { # $1 format: old, plain, utf8, ul or missing; $2 prefix or none
    timeout 120 ./longname 127.0.0.1 "$port" "" "" / share "$@"
}

start_server --allow-writes
run "in-game reader, server allows writes" ingame rw
run "in-game launch, longest old-format name, longest prefix" longname old prefix
run "in-game launch, longest old-format name, no prefix" longname old none
run "in-game launch, longest <name>.iso, no prefix" longname plain none
run "in-game launch, longest <name>.iso, UTF-8 in the name, longest prefix" longname utf8 prefix
run "in-game launch, USBExtreme game in two parts, longest prefix" longname ul prefix
run "in-game launch, a missing image: the read fails" longname missing prefix 20
start_server
run "in-game reader, server refuses writes" ingame ro

echo "$passed passed, $failed failed"
[ "$failed" -eq 0 ]
