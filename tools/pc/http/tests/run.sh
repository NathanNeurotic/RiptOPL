#!/bin/sh
# Host tests for the two parsers that decide what RiptOPL believes about an HTTP server.
#
# Both compile the REAL source, not a copy. The stream test replaces only recv/select/send, so
# header framing, Content-Range validation, encoding refusal and truncation detection run exactly
# as they do on the IOP -- and because modules/network/common/httpstream.inc is shared with the
# in-game driver, testing it here tests the reader device-http.c uses too. The catalog test runs
# src/platform/httpcatalog.c unmodified and diffs it against pc/http/catalog_reference.py over the shared
# fixtures, in all three line-ending forms.
#
# Usage, from the repo root:
#     sh tools/pc/http/tests/run.sh
#
# With no host compiler, the same thing inside the build image:
#     docker run --rm -v "$PWD:/repo" -w /repo ps2dev/ps2dev:latest \
#         sh -c "apk add --no-cache build-base python3 >/dev/null 2>&1; sh tools/pc/http/tests/run.sh"

set -e

root=$(cd "$(dirname "$0")/../../../.." && pwd)
out=${TMPDIR:-/tmp}/riptopl-http-test
cc=${CC:-gcc}

echo "== streaming HTTP client =="
"$cc" -O1 -Wall -Wextra -Wno-unused-parameter \
    -I"$root/tools/pc/http/tests" \
    -I"$root/tools/pc/http/tests/stub" \
    -I"$root/modules/network/common" \
    -o "$out-stream" \
    "$root/tools/pc/http/tests/test_stream.c" \
    "$root/modules/network/httpclient/httpclient.c"
"$out-stream"

echo
echo "== catalog parser vs the Python reference =="
# src/platform/httpcatalog.cpp is C++ now, so the harness is built as C++ too (shared C++ linkage).
"${CXX:-g++}" -std=gnu++17 -O1 -Wall -Wextra -Wno-unused-parameter \
    -I"$root" \
    -o "$out-catalog" \
    -x c++ "$root/tools/pc/http/tests/test_catalog.c" \
    -x c++ "$root/src/platform/httpcatalog.cpp"
python3 "$root/tools/pc/http/tests/compare_catalog.py" --bin "$out-catalog"

python3 "$root/tools/pc/http/tests/test_catalog_limits.py"
python3 "$root/tools/pc/http/tests/test_conformance.py"
