#!/bin/sh
# Install the pinned ps2build SDK (unless already present) plus the two port packages it does not ship
# (ogg, vorbis/vorbisfile). Versions come from .github/ps2build-sdk.env; PS2BUILD_TOOLCHAIN selects gcc
# (default), llvm, or all for the port packages.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="${PS2DEV_INSTALL_DIR:-$HOME/.local/share/ps2build}"
PIN="${PS2BUILD_SDK_PIN:-$ROOT/.github/ps2build-sdk.env}"

# shellcheck disable=SC1090
. "$PIN"
export PS2BUILD_SDK_MIRROR PS2BUILD_SDK_TAG PS2BUILD_SDK_SUMS_SHA256
export OGG_VERSION VORBIS_VERSION

# CI installs the pinned SDK with tools/ps2build_sdk.sh first. Locally, only bootstrap a missing SDK:
# an existing install (perhaps newer, from "ps2build update") is left alone.
if [ -n "${GITHUB_ACTIONS:-}" ] || [ ! -x "$SDK/ps2build" ]; then
    case "${PS2BUILD_TOOLCHAIN:-gcc}" in llvm|all) llvm=--llvm ;; *) llvm= ;; esac
    PS2BUILD_SDK_PIN="$PIN" sh "$ROOT/tools/ps2build_sdk.sh" $llvm "$SDK"
fi

exec python3 "$ROOT/tools/install_ports.py" --sdk "$SDK" --toolchain "${PS2BUILD_TOOLCHAIN:-gcc}"
