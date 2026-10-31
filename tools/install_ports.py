#!/usr/bin/env python3
"""Install the released ogg/vorbis packages for the selected ps2build compiler."""
import argparse
import hashlib
import http.client
import io
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import struct
import subprocess
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile


HOST = "https://git.techwritescode.dev"
LIBRARIES = {"ogg": ("libogg.a",), "vorbis": ("libvorbis.a", "libvorbisfile.a")}


def download(url):
    request = urllib.request.Request(url, headers={"User-Agent": "RiptOPL-build/1.0"})
    for attempt in range(5):
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                return response.read()
        except (urllib.error.URLError, TimeoutError, ConnectionError, http.client.IncompleteRead) as error:
            # Any 5xx is transient here, including Cloudflare's 520-524 when the origin is briefly down.
            if isinstance(error, urllib.error.HTTPError) and error.code not in (408, 429) and error.code < 500:
                raise
            if attempt == 4:
                raise
            print(f"retrying {url} after {type(error).__name__}: {error}", flush=True)
            time.sleep(1 << attempt)


def select_asset(release, package, toolchain):
    tag = release["tag_name"]
    names = [f"{package}-{tag}-{toolchain}.zip"]
    if toolchain == "gcc":
        names.append(f"{package}-{tag}.zip")  # Older GCC-only releases.
    for name in names:
        matches = [asset for asset in release.get("assets", []) if asset["name"] == name]
        if len(matches) == 1:
            return matches[0]["browser_download_url"]
    raise ValueError(f"{package} {tag} has no unique {toolchain} package archive")


def unpack(data, destination, package):
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        required = {"package.yaml", *(f"lib/{lib}" for lib in LIBRARIES[package])}
        if not required.issubset(archive.namelist()):
            raise ValueError(f"{package} archive is missing its manifest or libraries")
        for entry in archive.infolist():
            path = PurePosixPath(entry.filename.replace("\\", "/"))
            if path.is_absolute() or ".." in path.parts or ":" in entry.filename or stat.S_ISLNK(entry.external_attr >> 16):
                raise ValueError(f"unsafe package member: {entry.filename}")
        archive.extractall(destination)


def mirror_pin(repo):
    """TAG, SHA256SUMS hash and versions of the newest ps2build-mirror release, from its pin.env."""
    base = os.environ.get("PS2BUILD_SDK_MIRROR_LATEST_URL") or f"https://github.com/{repo}/releases/latest/download"
    values = {}
    for line in download(f"{base}/pin.env").decode().splitlines():
        name, _, value = line.partition("=")
        if value:
            values[name.strip()] = value.strip().strip('"')
    return values


def mirror_archive(package, version, toolchain):
    """The package from a ps2build-mirror release, or None. Used only when the official host fails.

    The release is the one the SDK step resolved (PS2BUILD_SDK_TAG), else the newest one, whose own
    version of the package is taken when it is older. The archive must match that release's SHA256SUMS,
    and SHA256SUMS the hash its pin names."""
    tag, sums_sha = os.environ.get("PS2BUILD_SDK_TAG"), os.environ.get("PS2BUILD_SDK_SUMS_SHA256")
    repo = os.environ.get("PS2BUILD_SDK_MIRROR") or "NathanNeurotic/ps2build-mirror"
    try:
        if not tag or not sums_sha:
            pin = mirror_pin(repo)
            tag, sums_sha = pin.get("PS2BUILD_SDK_TAG"), pin.get("PS2BUILD_SDK_SUMS_SHA256")
            mirrored = pin.get(f"{package.upper()}_VERSION")
            if not tag or not sums_sha or not mirrored:
                raise ValueError("the newest release's pin.env is incomplete")
            if mirrored != version:
                print(f"warning: the newest ps2build-mirror release has {package} {mirrored}, not {version}", flush=True)
            version = mirrored
        if version == "latest":
            return None
        base = os.environ.get("PS2BUILD_SDK_MIRROR_URL") or f"https://github.com/{repo}/releases/download/{tag}"
        name = f"{package}-{version}-{toolchain}.zip"
        sums = download(f"{base}/SHA256SUMS")
        if hashlib.sha256(sums).hexdigest() != sums_sha:
            raise ValueError("SHA256SUMS does not match the pinned hash")
        expected = [line.split()[0] for line in sums.decode().splitlines() if line.endswith("  " + name)]
        if len(expected) != 1:
            raise ValueError(f"SHA256SUMS does not list {name}")
        data = download(f"{base}/{name}")
        if hashlib.sha256(data).hexdigest() != expected[0]:
            raise ValueError(f"{name} does not match SHA256SUMS")
    except (urllib.error.URLError, TimeoutError, ConnectionError, http.client.IncompleteRead, ValueError) as error:
        print(f"warning: the ps2build-mirror fallback failed for {package} ({error})", flush=True)
        return None
    return f"{base}/{name}", data, version


def install(sdk, package, toolchain):
    version = os.environ.get(f"{package.upper()}_VERSION", "latest")
    release_path = "latest" if version == "latest" else "tags/" + urllib.parse.quote(version, safe="")
    world = sdk / ("packages-llvm" if toolchain == "llvm" else "packages") / "world"
    installed = world / package
    # A pinned package an SDK cache restore already holds needs no download at all.
    if version != "latest" and (installed / ".release").is_file() \
            and (installed / ".release").read_text().strip() == version \
            and all((installed / name).is_file() for name in ("package.yaml", *(f"lib/{lib}" for lib in LIBRARIES[package]))):
        print(f"{package} {version} ({toolchain}) already installed")
        return
    try:
        release = json.loads(download(f"{HOST}/api/v1/repos/ps2/{package}/releases/{release_path}"))
        url = select_asset(release, package, toolchain)
        data = download(url)
    except (urllib.error.URLError, TimeoutError, ConnectionError, http.client.IncompleteRead) as error:
        # The official host has outages of several minutes. A missing release (4xx) is a real error;
        # otherwise take the same pinned package from the ps2build-mirror release.
        client_error = isinstance(error, urllib.error.HTTPError) and error.code < 500
        mirrored = None if client_error else mirror_archive(package, version, toolchain)
        if mirrored:
            print(f"warning: {HOST} unreachable ({error}); using the ps2build-mirror copy", flush=True)
            url, data, mirrored_version = mirrored
            release = {"tag_name": mirrored_version}
        # A complete package restored from the SDK cache is the one a previous run installed, so build
        # with it rather than fail; a pinned version is never substituted.
        elif version != "latest" or client_error \
                or not all((installed / name).is_file() for name in ("package.yaml", *(f"lib/{lib}" for lib in LIBRARIES[package]))):
            raise
        else:
            print(f"warning: {HOST} unreachable ({error}); keeping the installed {package} ({toolchain})", flush=True)
            return
    world.mkdir(parents=True, exist_ok=True)
    world = world.resolve()
    target = world / package
    if target.is_symlink() or (target.exists() and not target.is_dir()):
        raise ValueError(f"package destination is not a regular directory: {target}")
    # Stage and validate before replacing anything. The temporary directory and
    # any previous package retained inside it are confined to this packages root.
    scratch = Path(tempfile.mkdtemp(prefix=f".{package}-", dir=world)).resolve()
    staged, backup = scratch / "new", scratch / "previous"
    try:
        if scratch.parent != world:
            raise ValueError("package staging directory escaped the SDK packages root")
        staged.mkdir()
        unpack(data, staged, package)
        (staged / ".release").write_text(release["tag_name"] + "\n")
        if target.exists():
            target.rename(backup)
        try:
            staged.rename(target)
        except OSError:
            if backup.exists():
                backup.rename(target)
            raise
    finally:
        if backup.exists() and not target.exists():
            print(f"could not restore previous package; preserved backup at {backup}")
        else:
            shutil.rmtree(scratch)
    print(f"installed {package} {release['tag_name']} ({toolchain}) from {url}")
    print(f"  SHA256 {hashlib.sha256(data).hexdigest()}")


# ps2build's jpeg package used to ship objects whose symbol tables list some local symbols after the first
# global one, under a .symtab sh_info that says otherwise. GNU ld accepts that; ld.lld rejects every such object
# ("symbol (N) has invalid binding: 0"), so the LLVM link of riptopl failed as soon as JPG art pulled libjpeg in.
# The official sdk-world v2026.10.28 ships a clean LLVM jpeg (checked 2026-10-31: 0 of 101 members bad), but an
# older SDK can still arrive (a cached one, or the NathanNeurotic/ps2build-mirror fallback, last built from
# v2026.10.15). So each library is checked first and llvm-objcopy rewrites it (locals first; no code or data
# changes) only when it really has the fault. Remove this once the mirror carries a fixed world as well.
LLVM_REPAIRS = ("jpeg/lib/libjpeg.a", "jpeg_ps2_addons/lib/libjpeg_ps2_addons.a")


def _archive_objects(data):
    """The ELF members of a GNU ar archive."""
    if data[:8] != b"!<arch>\n":
        return
    at = 8
    while at + 60 <= len(data):
        header = data[at:at + 60]
        size = int(header[48:58].decode().strip())
        body = data[at + 60:at + 60 + size]
        if body[:4] == b"\x7fELF":
            yield body
        at += 60 + size + (size & 1)


def _locals_after_globals(elf):
    """1 when .symtab's sh_info (the first non-local) is wrong either way, as ld.lld refuses both: a local symbol
    at or after it, or a non-local one before it."""
    is64, endian = elf[4] == 2, "<" if elf[5] == 1 else ">"
    shoff = struct.unpack_from(endian + ("Q" if is64 else "I"), elf, 0x28 if is64 else 0x20)[0]
    shentsize, shnum = struct.unpack_from(endian + "HH", elf, 0x3A if is64 else 0x2E)
    for i in range(shnum):
        sh = shoff + i * shentsize
        if struct.unpack_from(endian + "I", elf, sh + 4)[0] != 2:  # SHT_SYMTAB
            continue
        if is64:
            offset, size = struct.unpack_from(endian + "QQ", elf, sh + 0x18)
            info, = struct.unpack_from(endian + "I", elf, sh + 0x2C)
            entsize, bind_at = struct.unpack_from(endian + "Q", elf, sh + 0x38)[0], 4
        else:
            offset, size = struct.unpack_from(endian + "II", elf, sh + 0x10)
            info, = struct.unpack_from(endian + "I", elf, sh + 0x1C)
            entsize, bind_at = struct.unpack_from(endian + "I", elf, sh + 0x24)[0], 12
        binds = [elf[offset + k * entsize + bind_at] >> 4 for k in range(size // entsize)]
        return any(b == 0 for b in binds[info:]) or any(b != 0 for b in binds[1:info])  # [0] is the null symbol
    return False


def needs_symtab_repair(library):
    """True unless every member could be read and none has the fault: a library this cannot read (a cut-short
    member, a thin or BSD archive, no ELF members at all) is rewritten anyway, as before this check existed.
    llvm-objcopy leaves a clean library as it was."""
    try:
        objects = list(_archive_objects(Path(library).read_bytes()))
        return not objects or any(_locals_after_globals(obj) for obj in objects)
    except (ValueError, struct.error, IndexError):
        return True


def repair_llvm_symtabs(sdk):
    world = sdk / "packages-llvm" / "world"
    candidates = [sdk / "llvm-toolchain" / "llvm" / "bin" / "llvm-objcopy", shutil.which("llvm-objcopy")]
    objcopy = next((str(c) for c in candidates if c and Path(c).is_file()), None)
    for relative in LLVM_REPAIRS:
        library = world / relative
        if not library.is_file():
            continue
        if not needs_symtab_repair(library):
            print(f"symbol tables already in order in {library}; no repair needed")
            continue
        if objcopy is None:
            print(f"warning: no llvm-objcopy; {library} keeps symbol tables ld.lld rejects", flush=True)
            return
        subprocess.run([objcopy, str(library)], check=True)
        print(f"repaired symbol tables in {library}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    executable = shutil.which("ps2build")
    default_sdk = os.environ.get("PS2BUILD_HOME") or os.environ.get("PS2DEV_INSTALL_DIR")
    if not default_sdk and executable:
        default_sdk = str(Path(executable).resolve().parent)
    parser.add_argument("--sdk", default=default_sdk, help="installed ps2build SDK root")
    parser.add_argument("--toolchain", choices=("gcc", "llvm", "all"),
                        default=os.environ.get("PS2BUILD_TOOLCHAIN", "gcc"))
    args = parser.parse_args()
    if not args.sdk:
        parser.error("ps2build is not on PATH; supply --sdk or install the SDK first")
    sdk = Path(args.sdk).resolve()
    if not any((sdk / name).is_file() for name in ("ps2build", "ps2build.exe")):
        parser.error(f"no ps2build executable in {sdk}")
    compilers = ("gcc", "llvm") if args.toolchain == "all" else (args.toolchain,)
    for compiler in compilers:
        for package in LIBRARIES:
            install(sdk, package, compiler)
        if compiler == "llvm":
            repair_llvm_symtabs(sdk)


if __name__ == "__main__":
    main()
