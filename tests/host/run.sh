#!/bin/sh
# Host unit tests for the PS2-independent modules in src/. Each test #includes the REAL source file with
# stub headers in tests/host/stub; everything is built with ASan+UBSan.
#   tests/host/run.sh            run all
set -e
cd "$(dirname "$0")/../.."
CXX=${CXX:-g++}
out=${TMPDIR:-/tmp}/riptopl-host-tests
mkdir -p "$out"
fail=0
for t in tests/host/test_*.cpp; do
    name=$(basename "$t" .cpp)
    "$CXX" -std=gnu++17 -g -O1 -Wall -Wextra -Wno-unused-parameter -Wno-write-strings -Wno-sign-compare \
        -fsanitize=address,undefined -fno-sanitize-recover=undefined \
        -Itests/host/stub -Itests/host/stub/include -Iinclude -I. \
        "$t" -o "$out/$name"
    if "$out/$name"; then :; else fail=$((fail+1)); fi
done
[ "$fail" = 0 ]
