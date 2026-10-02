#!/bin/sh
# Run every host-side test (no PS2 toolchain needed): the .github/scripts/test_*.py source/behaviour
# tests and the HTTP parser tests. Usage: tools/run_tests.sh [-k]   (-k: keep going after a failure)
cd "$(dirname "$0")/.." || exit 1
keep=0; [ "$1" = "-k" ] && keep=1
fail=0
for t in .github/scripts/test_*.py; do
    case "$t" in *test_site_sync.py) python3 -c "import bs4" 2>/dev/null || { echo "SKIP $t (needs bs4)"; continue; };; esac
    if python3 "$t" >/tmp/riptopl-test.log 2>&1; then echo "ok   $t"; else echo "FAIL $t"; tail -5 /tmp/riptopl-test.log; fail=$((fail+1)); [ $keep = 1 ] || exit 1; fi
done
if sh tools/pc/http/tests/run.sh >/tmp/riptopl-test.log 2>&1; then echo "ok   tools/pc/http/tests/run.sh"; else echo "FAIL tools/pc/http/tests/run.sh"; tail -5 /tmp/riptopl-test.log; fail=$((fail+1)); fi
if tests/host/run.sh >/tmp/riptopl-test.log 2>&1; then echo "ok   tests/host/run.sh ($(grep -c "^PASS" /tmp/riptopl-test.log) suites)"; else echo "FAIL tests/host/run.sh"; grep -v "warning\|note:" /tmp/riptopl-test.log | tail -8; fail=$((fail+1)); fi
[ $fail = 0 ] && echo "all tests passed" || { echo "$fail failed"; exit 1; }
