"""Execute C++ Ember filtering, VMC warning policy and queued SMB Refresh recovery."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text(encoding='utf-8')


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]


def run(name, code):
    directory = ROOT / 'build/test-tmp'
    directory.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=directory) as tmp:
        src, exe = Path(tmp) / (name + '.cpp'), Path(tmp) / name
        src.write_text(code, encoding='utf-8')
        compiler = shlex.split(os.environ.get('CXX', 'c++'))
        subprocess.run([*compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', str(src), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], cwd=tmp, check=True, timeout=15)


cue = read('src/sources/cuesupport.cpp')
exts = re.search(r'static const char \*const cueDiscExts\[\] = \{[^}]*\};', cue).group(0)
run('ember_images', r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cerrno>
#include <set>
#include <string>
enum { CUE_MAX_ITEMS = 2048, ISO_GAME_NAME_MAX = 160, CUE_NAME_LAUNCH_MAX = 160 };
struct cue_entry_t { char name[256]; };
static char gamesRoot[288];
static int opens;
static bool corePresent = true;
static int directoryError = -1;
static std::string lastProbe;
static int readdirFailAt = -1, readdirs, fileOpenError;
#define LOG(...) ((void)0)
static char cueSep(const char *prefix) { return strchr(prefix, '\\') ? '\\' : '/'; }
static int cueResolveEmber(const char*, char*, int) { return corePresent; }
static void cueBuildGamesDir(const char*, char *out, int length) { snprintf(out, length, "%s", gamesRoot); }
static DIR *probeDir(const char *path) {
    ++opens; lastProbe = path; std::string mapped(path);
    if (directoryError >= 0) { errno = directoryError; return nullptr; }
    for (char &c : mapped) if (c == '\\') c = '/';
    struct stat st;
    if (fileOpenError && stat(mapped.c_str(), &st) == 0 && S_ISREG(st.st_mode)) { errno = fileOpenError; return nullptr; }
    return opendir(mapped.c_str());
}
static dirent *probeRead(DIR *d) {
    if (readdirs++ == readdirFailAt) { errno = EIO; return nullptr; }
    return readdir(d);
}
#define opendir probeDir
#define readdir probeRead
''' + exts + '\n' + '\n'.join(function(cue, s) for s in (
    'int cueNameLaunchable(', 'static int cueNameIsDiscImage(',
    'static int cueEntryIsGame(', 'int cueScanDir(')) + r'''
#undef opendir
#undef readdir
static void put(const char *path) { FILE *f = fopen(path, "wb"); assert(f); fputs("image", f); fclose(f); }
int main() {
    strcpy(gamesRoot, "games"); assert(mkdir(gamesRoot, 0700) == 0);
    const char *names[] = {"cue", "exe", "bin", "empty", "group", "nested"};
    for (const char *name : names) { std::string path = std::string("games/") + name; assert(mkdir(path.c_str(), 0700) == 0); }
    put("games/cue/title.CUE"); put("games/exe/title.ExE"); put("games/bin/title.bin");
    put("games/group/MC1.vmc"); put("games/group/readme.txt"); put("games/loose.bin");
    assert(mkdir("games/nested/subdir", 0700) == 0); put("games/nested/subdir/title.bin");
    dirent entry{}; strcpy(entry.d_name, "empty"); entry.d_type = DT_DIR;
    assert(!cueEntryIsGame("mass0:/", gamesRoot, &entry));
    strcpy(entry.d_name, "loose.bin"); assert(!cueEntryIsGame("mass0:/", gamesRoot, &entry));
    strcpy(entry.d_name, "cue"); entry.d_type = DT_REG; // A lying d_type cannot hide a real game folder.
    assert(cueEntryIsGame("mass0:/", gamesRoot, &entry));
    assert(cueEntryIsGame("smb0:\\", gamesRoot, &entry) && lastProbe == "games\\cue");
    cue_entry_t *list = nullptr;
    assert(cueScanDir("mass0:/", &list) == 3);
    std::set<std::string> found; for (int i = 0; i < 3; ++i) found.insert(list[i].name);
    assert((found == std::set<std::string>{"cue", "exe", "bin"})); free(list);
    // Missing core no longer hides valid games; launch diagnostics name the absent file.
    corePresent = false; list = nullptr; assert(cueScanDir("mass0:/", &list) == 3 && list); free(list);
    for (int error : {0, EPERM, ENOENT, EIO, EBUSY}) {
        directoryError = error; list = nullptr;
        assert(cueScanDir("mass0:/", &list) == (error == EIO || error == EBUSY ? -1 : 0) && !list);
        corePresent = true;
        assert(cueScanDir("mass0:/", &list) == (error == ENOENT ? 0 : -1) && !list);
        corePresent = false;
    }
    directoryError = -1; corePresent = true;
    // A loose file that FatFs answers with EIO (FR_NO_PATH) is still just a file.
    fileOpenError = EIO; list = nullptr; assert(cueScanDir("mass0:/", &list) == 3 && list); free(list);
    fileOpenError = 0;
    // A listing cut short anywhere -- the games folder or a game folder -- keeps the last-good list.
    int total = 0; readdirs = 0; list = nullptr; assert(cueScanDir("mass0:/", &list) == 3); free(list); total = readdirs;
    for (int at = 0; at < total; ++at) {
        readdirFailAt = at; readdirs = 0; list = nullptr;
        assert(cueScanDir("mass0:/", &list) == -1 && !list);
    }
    readdirFailAt = -1;
    assert(opens > 0);
}
''')

gui = read('src/ui/guigame.cpp')
start = gui.index('if ((coreLoader == 2 ? gDefaultCoreLoader : coreLoader)')
condition = gui[start:gui.index('guiMsgBox(', start)]
run('vmc_warning', r'''
#include <cassert>
static int coreLoader, gDefaultCoreLoader, probes, result, warning;
static struct { int VMC_error; } vmc_status;
static int guiGameVMCLayoutWarning() { ++probes; return result == 0 ? 7 : -1; }
static bool warns() {
''' + condition + r'''
        return true;
    return false;
}
int main() {
    for (coreLoader = 0; coreLoader <= 2; ++coreLoader)
        for (gDefaultCoreLoader = 0; gDefaultCoreLoader <= 1; ++gDefaultCoreLoader)
            for (vmc_status.VMC_error = 0; vmc_status.VMC_error <= 1; ++vmc_status.VMC_error)
                for (result = -1; result <= 1; ++result) {
                    probes = 0;
                    bool opl = (coreLoader == 2 ? gDefaultCoreLoader : coreLoader) == 0;
                    assert(warns() == (opl && !vmc_status.VMC_error && result == 0));
                    assert(probes == int(opl && !vmc_status.VMC_error));
                }
}
''')

eth = read('src/sources/ethsupport.cpp')
header = read('include/ethsupport.h')
start = header.index('class EthSupport\n')
session = header[start:header.index('\n};', start) + 3]
menu = read('src/ui/menuitems.cpp')
run('smb_refresh', r'''
#include <cassert>
#include <cstring>
#include <ctime>
#include <cstddef>
enum { NET_PROTO_OFF, NET_PROTO_SMB, NET_PROTO_UDPFS, NET_PROTO_UDPFSBD, NET_PROTO_UDPBD, NET_PROTO_HTTP, NET_PROTO_NFS,
       START_MODE_DISABLED = 0, ETH_MODE = 7, FAV_MODE, HDD_MODE, LIB_VIEW_PS1, LIB_VIEW_MIXED,
       IO_CUSTOM_SIMPLEACTION, IO_MENU_UPDATE_DEFFERED, IO_OK = 0, SMB_DEVCTL_ECHO = 20, SFX_CONFIRM };
struct base_game_info_t { int id; };
struct item_list_t { int mode = ETH_MODE, enabled = 1, delay = 0; };
struct menu_item { void *userdata; };
struct smbEcho_in_t { char echo[64]; int len; };
static item_list_t source;
static item_list_t *ethInstance() { return &source; }
static int gNetworkProtocol, gETHStartMode, gNetworkStartup, gArtDelay;
static int semaResult, echoResult, queueResult, semas, echoes, queues, rebuilds, dirty, reconnects;
static int favReloads, hddInvalidations, sounds, view;
static void (*pending)();
static void (*duringEcho)();
static int netInitSema() { ++semas; return semaResult; }
static int ioPutRequest(int type, void *arg) {
    if (type == IO_CUSTOM_SIMPLEACTION) { ++queues; if (queueResult == IO_OK) pending = reinterpret_cast<void(*)()>(arg); return queueResult; }
    assert(type == IO_MENU_UPDATE_DEFFERED && arg); ++rebuilds; return IO_OK;
}
static int fileXioDevctl(const char *base, int command, void *arg, int size, void*, int) {
    assert(!strcmp(base, "smb0:") && command == SMB_DEVCTL_ECHO && size == sizeof(smbEcho_in_t));
    auto *echo = static_cast<smbEcho_in_t*>(arg); assert(!strcmp(echo->echo, "OPL REFRESH") && echo->len == 11);
    ++echoes; if (duringEcho) duringEcho(); return echoResult;
}
static void libViewMarkDirty(int) { ++dirty; }
namespace host { struct Library { void markDirty(int mode) { libViewMarkDirty(mode); } }; static Library &library() { static Library l; return l; } }
static void loadFavourites() { ++favReloads; }
static int libListViewActive(item_list_t*) { return view; }
static void hddVcdInvalidateCache() { ++hddInvalidations; }
static void sfxPlay(int) { ++sounds; }
#define LOG(...) ((void)0)
#define LITE_HAS_NFS 1
#define NET_PROTO_IS_ETH(p) ((p) == NET_PROTO_SMB || (p) == NET_PROTO_NFS)
#define private public
''' + session + r'''
#undef private
static EthSupport gEthSupport;
void EthSupport::reconnectSmb() { ++reconnects; reconnectQueued_ = reconnectRequested_ = 0; }
''' + '\n'.join(function(eth, s) for s in (
    'bool EthSupport::protocolLive(', 'static void ethReconnectSMB(', 'static void ethCheckedRefreshSMB(',
    'void EthSupport::checkedRefreshSmb(', 'void EthSupport::requestCheckedRefresh(',
    'void ethRequestCheckedRefresh(', 'void EthSupport::requestReconnect('))
    + '\n' + function(menu, 'void itemExecRefresh(') + r'''
static void reset() {
    gNetworkProtocol = NET_PROTO_SMB; gETHStartMode = 1; gNetworkStartup = 0;
    semaResult = echoResult = queueResult = semas = echoes = queues = rebuilds = dirty = reconnects = 0;
    favReloads = hddInvalidations = sounds = 0; pending = duringEcho = nullptr; source = item_list_t{};
    gEthSupport.smbModuleLoaded_ = 1; gEthSupport.reconnectQueued_ = 0; gEthSupport.setBase("smb0:");
    gEthSupport.reconnectRequested_ = 0; gEthSupport.nfs_ = 0;
}
static void dispatch() { assert(pending); auto callback = pending; pending = nullptr; callback(); }
int main() {
    for (int protocol = NET_PROTO_OFF; protocol <= NET_PROTO_NFS; ++protocol) {
        if (protocol == NET_PROTO_SMB) continue; // NFS while smbman is resident: restart-only, nothing queued
        reset(); gNetworkProtocol = protocol; gEthSupport.requestCheckedRefresh(); assert(!queues && !semas);
    }
    reset(); gETHStartMode = START_MODE_DISABLED; gEthSupport.requestCheckedRefresh(); assert(!queues && !semas);
    reset(); source.enabled = 0; gEthSupport.requestCheckedRefresh(); assert(!queues && !semas);
    reset(); semaResult = -1; gEthSupport.requestCheckedRefresh(); assert(!queues && !gEthSupport.reconnectQueued_);
    reset(); queueResult = -1; gEthSupport.requestCheckedRefresh(); assert(queues == 1 && !gEthSupport.reconnectQueued_);
    queueResult = 0; gEthSupport.requestCheckedRefresh(); assert(queues == 2 && pending); dispatch();
    reset(); base_game_info_t game{42}; gEthSupport.games() = &game; gEthSupport.gameCount() = 1;
    gEthSupport.requestCheckedRefresh(); gEthSupport.requestCheckedRefresh();
    assert(queues == 1); dispatch();
    assert(echoes == 1 && !reconnects && rebuilds == 1 && dirty == 1 && !gEthSupport.reconnectQueued_);
    assert(gEthSupport.games() == &game && gEthSupport.gameCount() == 1);
    reset(); gEthSupport.requestCheckedRefresh(); gEthSupport.requestReconnect(); assert(queues == 1); dispatch();
    assert(reconnects == 1 && echoes == 0); // Settings reconnect upgrades a queued healthy-session probe.
    reset(); duringEcho = [] { gEthSupport.requestReconnect(); };
    gEthSupport.requestCheckedRefresh(); dispatch(); assert(queues == 1 && echoes == 1 && reconnects == 1);
    reset(); duringEcho = [] { gNetworkProtocol = NET_PROTO_UDPFS; };
    gEthSupport.requestCheckedRefresh(); dispatch(); assert(echoes == 1 && !reconnects && !rebuilds && !gEthSupport.reconnectQueued_);
    reset(); gEthSupport.requestReconnect(); gEthSupport.requestCheckedRefresh(); assert(queues == 1); dispatch(); assert(reconnects == 1);
    for (int failure = 0; failure < 4; ++failure) {
        reset();
        if (failure == 0) echoResult = -1;
        if (failure == 1) gNetworkStartup = 1;
        if (failure == 2) gEthSupport.smbModuleLoaded_ = 0;
        if (failure == 3) gEthSupport.setBase(nullptr);
        gEthSupport.requestCheckedRefresh(); dispatch(); assert(reconnects == 1 && !gEthSupport.reconnectQueued_);
        assert(echoes == int(failure == 0));
    }
    for (int changed = 0; changed < 3; ++changed) {
        reset(); gEthSupport.requestCheckedRefresh();
        if (changed == 0) gNetworkProtocol = NET_PROTO_UDPFS;
        if (changed == 1) gETHStartMode = START_MODE_DISABLED;
        if (changed == 2) source.enabled = 0;
        dispatch(); assert(!echoes && !reconnects && !gEthSupport.reconnectQueued_);
    }
    // NFS: the same queue, no echo (nfsman reconnects on use); SMB asked for while nfsman is resident is restart-only.
    reset(); gNetworkProtocol = NET_PROTO_NFS; gEthSupport.nfs_ = 1; gEthSupport.setBase("nfs:/NFS/");
    gEthSupport.requestCheckedRefresh(); assert(queues == 1); dispatch();
    assert(!echoes && !reconnects && rebuilds == 1 && dirty == 1 && !gEthSupport.reconnectQueued_);
    reset(); gNetworkProtocol = NET_PROTO_NFS; gEthSupport.nfs_ = 1; gNetworkStartup = 1;
    gEthSupport.requestCheckedRefresh(); dispatch(); assert(!echoes && reconnects == 1);
    reset(); gEthSupport.nfs_ = 1; gEthSupport.requestCheckedRefresh(); gEthSupport.requestReconnect(); assert(!queues && !semas);
    reset(); gNetworkProtocol = NET_PROTO_NFS; gEthSupport.smbModuleLoaded_ = 0; gEthSupport.requestReconnect(); assert(queues == 1);
    reset(); menu_item menu{&source}; itemExecRefresh(&menu); assert(queues == 1 && sounds == 1); dispatch(); assert(echoes == 1);
    reset(); gNetworkStartup = 1; itemExecRefresh(&menu); dispatch(); assert(reconnects == 1);
    reset(); source.mode = FAV_MODE; itemExecRefresh(&menu); assert(favReloads == 1 && !queues && !rebuilds);
    reset(); source.mode = HDD_MODE; view = LIB_VIEW_PS1; itemExecRefresh(&menu); assert(hddInvalidations == 1 && rebuilds == 1 && !queues);
    reset(); source.enabled = 0; itemExecRefresh(&menu); assert(!queues && !sounds);
}
''')
print('Legacy parity: Ember top-level images, Neutrino VMC policy, healthy/stale SMB and queued protocol changes passed')
