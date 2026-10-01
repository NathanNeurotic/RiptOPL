#!/bin/sh
# Install ps2build plus the two port packages it does not ship (ogg, vorbis/vorbisfile).
# Pin with PS2DEV_INSTALL_VERSION (ps2build) and OGG_VERSION / VORBIS_VERSION (default: latest).
set -e
HOST=git.techwritescode.dev
SDK="${PS2DEV_INSTALL_DIR:-$HOME/.local/share/ps2build}"

curl -sSfL "https://$HOST/ps2/install/raw/branch/master/install.sh" | PS2DEV_INSTALL_DIR="$SDK" sh

for pkg in ogg vorbis; do
    case $pkg in ogg) ver="${OGG_VERSION:-latest}" ;; vorbis) ver="${VORBIS_VERSION:-latest}" ;; esac
    if [ "$ver" = latest ]; then api="https://$HOST/api/v1/repos/ps2/$pkg/releases/latest"
    else api="https://$HOST/api/v1/repos/ps2/$pkg/releases/tags/$ver"; fi
    url=$(curl -sSf "$api" | grep -o '"browser_download_url": *"[^"]*\.zip"' | head -n 1 | cut -d'"' -f4)
    [ -n "$url" ] || { echo "no zip asset for $pkg" >&2; exit 1; }
    tmp=$(mktemp -d)
    curl -sSfL "$url" -o "$tmp/$pkg.zip"
    rm -rf "$SDK/packages/world/$pkg"
    mkdir -p "$SDK/packages/world/$pkg"
    unzip -q "$tmp/$pkg.zip" -d "$SDK/packages/world/$pkg"
    rm -rf "$tmp"
    echo "installed $pkg from $url"
done
