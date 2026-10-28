"""Count file-probe starts across the art port with controlled nav interleaving.

Actual commit-pinned queue/discovery/format-routing functions run over stubs.
PNG decoding, filesystem latency and SIO2 scheduling are not simulated.
"""

from pathlib import Path
import argparse
import json
import shutil
import subprocess
import tempfile

from probe_navigation_input_gaps import ROOT, function, source


DEFAULT_REFS = ("d7d238d4^", "d7d238d4", "b4c23bdac8f983a4b4e3456d4036b8645cf95842")

HARNESS = r'''
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
using u32 = uint32_t;
struct GSTEXTURE { int unused; };
struct image_cache_t;
struct cache_entry_t;
struct item_list_t;
static int intrDepth;
static void DIntr() { assert(intrDepth++==0); }
static void EIntr() { assert(--intrDepth==0); }
@QUEUE_HEADER@
@QUEUE_FUNCTIONS@
static ArtQueue q;
constexpr int ERR_BAD_FILE=-1, ERR_FILE_IO=-2, ERR_LOAD_ABORTED=-3, ERR_SET_JMP=-4;
constexpr int TEX_BG_MAX_W=720, TEX_BG_MAX_H=512, TEX_ART_MAX_W=512, TEX_ART_MAX_H=512;
constexpr int O_RDONLY=0;
#define LOG(...) ((void)0)
static int probes, whileHeld, indexState, pngPresent, transient, aborted, abortAfterFirst;
static bool resumeAfterFirst;
static int fileProbe(const char *path) {
    ++probes;
    whileHeld+=q.navActive();
    if (resumeAfterFirst && probes==1) q.setNavActive(true);
    if (abortAfterFirst && probes==1) aborted=1;
    if (transient) return ERR_FILE_IO;
    return pngPresent && strstr(path,".png") ? 0 : ERR_BAD_FILE;
}
static int open(const char *path, int, ...) {
    int result=fileProbe(path);
    errno=result==ERR_FILE_IO ? EIO : ENOENT;
    return result>=0 ? 3 : -1;
}
static int close(int) { return 0; }
static int texLoad(GSTEXTURE *, const char *path) { return fileProbe(path); }
static int texLoadAbortRequested() { return aborted; }
static int texStagedOpenIsAbsence(const char *, int error) { return error==ENOENT; }
static int texStageExternalFileIntoMemory(int, void **, u32 *, int) { return ERR_FILE_IO; }
static int texIsJpg(const void *, u32) { return 0; }
static int texLoadJpgMemory(GSTEXTURE *, const void *, u32, int, int) { return ERR_FILE_IO; }
static int artIndexMayExist(const char *) { return indexState!=0; }
static int artIndexFindPicture(const char *stem, char *out, int size) {
    if (indexState==1) snprintf(out,size,"%s.png",stem);
    return indexState;
}
static struct { const char *name; } internalDefault[]={{"unused"}};
@TEXTURE_FUNCTIONS@
static int discover(GSTEXTURE *texture,const char *path) { return @DISCOVER@; }
static void reset() {
    q.reset();
    probes=whileHeld=pngPresent=transient=aborted=abortAfterFirst=0;
    indexState=-1; resumeAfterFirst=false;
}
static void report(const char *name, int result) {
    printf("{\"case\":\"%s\",\"result\":%d,\"probe_starts\":%d,\"starts_after_nav_resumed\":%d}\n",name,result,probes,whileHeld);
}
int main() {
    GSTEXTURE texture{};
    for (const char *piece : {"COV","BG"}) {
        char path[128], name[128];
        snprintf(path,sizeof(path),"mmce0:ART/SLUS_123.45_%s",piece);
        reset();
        load_image_request_t request{}; request.sio2=1;
        q.push(&request);
        q.setNavActive(true);
        assert(q.pop()==nullptr && q.queued()==1); // positive control: held before pop.
        q.setNavActive(false); // what a failed sample exposes to cacheTickArt.
        assert(q.pop()==&request);
        resumeAfterFirst=true;
        int result=discover(&texture,path);
        assert(intrDepth==0 && result==ERR_BAD_FILE);
        snprintf(name,sizeof(name),"missing_unindexed_%s",piece);
        report(name,result);
        q.release(&request);
    }
    reset(); indexState=0;
    int result=discover(&texture,"mmce0:ART/SLUS_123.45_BG");
    assert(probes==0); report("indexed_absence",result);
    reset(); pngPresent=1;
    result=discover(&texture,"mmce0:ART/SLUS_123.45_BG");
    assert(probes==1 && result==0); report("present_png",result);
    reset(); transient=1;
    result=discover(&texture,"mmce0:ART/SLUS_123.45_BG");
    assert(probes==1 && result==ERR_FILE_IO); report("transient_error",result);
    reset(); abortAfterFirst=1;
    result=discover(&texture,"mmce0:ART/SLUS_123.45_BG");
    assert(probes==1); report("explicit_abort_after_first",result);
}
'''


def run(ref, compiler, folder):
    tex = source(ref, "src/gfx/textures.cpp")
    header = source(ref, "include/artqueue.h")
    header = "\n".join(line for line in header.splitlines() if not line.startswith("#include"))
    queue = source(ref, "src/gfx/artqueue.cpp")
    queue_functions = "\n".join(function(queue, signature) for signature in (
        "void ArtQueue::push(", "load_image_request_t *ArtQueue::pop(",
        "void ArtQueue::release(", "void ArtQueue::reset("))
    signatures = []
    if "static int texFindArt(" in tex:
        signatures += ["static int texLoadJpgFile(", "static int texArtIsBackground(",
                       "static int texLoadArtFile(", "static int texFindArt(", "int texArtAlias("]
    signatures += ["int texDiscoverLoad("]
    if "int texDiscoverArt(" in tex:
        signatures += ["int texDiscoverArt("]
        discover = "texDiscoverArt(texture,path)"
    else:
        discover = "texDiscoverLoad(texture,path,-1)"
    functions = "\n".join(function(tex, signature) for signature in signatures)
    program = HARNESS
    for key, value in {"QUEUE_HEADER": header, "QUEUE_FUNCTIONS": queue_functions,
                       "TEXTURE_FUNCTIONS": functions, "DISCOVER": discover}.items():
        program = program.replace(f"@{key}@", value)
    cpp = folder / "art.cpp"
    exe = folder / ("art.exe" if __import__("os").name == "nt" else "art")
    cpp.write_text(program, encoding="utf-8")
    subprocess.run([compiler, "-std=c++17", "-O2", "-Wall", "-Wextra",
                    "-Wno-unused-function", "-Wno-unused-variable", "-include", "initializer_list",
                    str(cpp), "-o", str(exe)], check=True)
    output = subprocess.check_output([str(exe)], text=True, timeout=10)
    return [{"ref": ref, **json.loads(line)} for line in output.splitlines()]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--refs", nargs="+", default=DEFAULT_REFS)
    parser.add_argument("--compiler", default=shutil.which("g++") or shutil.which("clang++"))
    args = parser.parse_args()
    if not args.compiler:
        parser.error("A host C++ compiler is required")
    with tempfile.TemporaryDirectory(prefix="navigation-art-") as temp:
        for ref in args.refs:
            for result in run(ref, args.compiler, Path(temp)):
                print(json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    main()
