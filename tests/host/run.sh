#!/bin/sh
# Host unit tests for the PS2-independent modules in src/. Each test #includes the REAL source file; everything
# is built with ASan+UBSan. The first line of a test may carry a marker:
#     // HOST-TEST: sdk-headers; link=lib_config_host.cpp; globals=settings
#   sdk-headers   compile against the real include/*.h with empty SDK stubs (tests/host/sdk_stub) instead of the
#                 small hand-written stubs in tests/host/stub
#   link=FILE     also build FILE (a lib_*.cpp in this directory, always against the small stubs) and link it
#   globals=settings   generate $out/settings_globals.h (definitions of the settings table's variables)
# lib_*.cpp files are libraries, not tests.
set -e
cd "$(dirname "$0")/../.."
CXX=${CXX:-g++}
out=${TMPDIR:-/tmp}/riptopl-host-tests
mkdir -p "$out"
fail=0
FLAGS="-std=gnu++17 -g -O1 -Wall -Wextra -Wno-unused-parameter -Wno-write-strings -Wno-sign-compare -fsanitize=address,undefined -fno-sanitize-recover=undefined"
SMALL="-Itests/host/stub -Itests/host/stub/include -Iinclude -I."
REAL="-I$out -Itests/host/sdk_stub -Imodules/hdd/common -Imodules/iopcore/common -Iinclude -I."
for t in tests/host/test_*.cpp; do
    name=$(basename "$t" .cpp)
    marker=$(head -1 "$t" | sed -n 's#^// HOST-TEST: *##p')
    incs=$SMALL; objs=""
    case "$marker" in *sdk-headers*) incs=$REAL;; esac
    case "$marker" in *globals=settings*) python3 tests/host/gen_settings_globals.py > "$out/settings_globals.h";; esac
    lib=$(echo "$marker" | sed -n 's#.*link=\([A-Za-z0-9_.]*\).*#\1#p')
    if [ -n "$lib" ]; then
        # shellcheck disable=SC2086
        "$CXX" $FLAGS $SMALL -c "tests/host/$lib" -o "$out/$lib.o"
        objs="$out/$lib.o"
    fi
    # shellcheck disable=SC2086
    "$CXX" $FLAGS $incs "$t" $objs -o "$out/$name"
    if "$out/$name"; then :; else fail=$((fail+1)); fi
done
[ "$fail" = 0 ]
