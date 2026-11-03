"""Exercise SMB refresh scheduling and UDPFS completion with production code.

The BDM integration and MMCE/merge ownership cases live in test_bdm_scan_retry.py
and test_pr894_hardening.py. This checks recovery after a previously complete scan,
not only the first scan, and the mixed-library view as well as the PS1-only view.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
eth = (ROOT / 'src/sources/ethsupport.cpp').read_text(encoding='utf-8')
udp = (ROOT / 'src/sources/udpfssupport.cpp').read_text(encoding='utf-8')
header = (ROOT / 'include/ethsupport.h').read_text(encoding='utf-8')


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]


needs = function(eth, 'int EthSource::needsUpdate(').replace('EthSource::needsUpdate', 'ethNeedsUpdate')
start = eth.index('            int r = ps1FillGameList(')
publish = eth[start:eth.index('            // NULL sub', start)]
start = udp.index('        int r = ps1FillGameList(')
udp_publish = udp[start:udp.index('\n    }', start)]
start = udp.index('    if (host::library().listViewActive(itemList) == LIB_VIEW_PS1)', udp.index('int UdpfsSource::needsUpdate('))
udp_retry = udp[start:udp.index('\n    if (!gUdpfsSupport.isoScanned_)', start)]
# Use the real session declaration, including its default initialization and accessors.
session = header[header.index('class EthSupport\n'):header.index('extern EthSupport')]
limit = re.search(r'constexpr int ETH_PS1_SCAN_RETRY_MAX = \d+;', header).group(0)
# Every session reset, including reconnect and share selection, must discard the old budget.
for signature in ('void EthSupport::reconnectSmb(', 'void EthSource::init('):
    text = function(eth, signature)
    assert 'ps1ScanFailed' in text and 'ps1ScanRetries' in text

program = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
struct base_game_info_t { int tag; };
struct item_list_t { int mode; };
enum { LIB_VIEW_ISO, LIB_VIEW_PS1, LIB_VIEW_MIXED };
static int view, dirty, gNetworkStartup, scanResult, incomplete;
static char gPCShareName[16] = "games";
@LIMIT@
@SESSION@
EthSupport gEthSupport;
static int ps1FillGameList(const char*, base_game_info_t **rows) {
    static base_game_info_t data[2];
    if (scanResult >= 0) *rows = scanResult ? data : nullptr;
    return scanResult;
}
static int ps1GameListNeedsRetry() { return incomplete; }
struct Library {
    int consumeDirty(int) { int value = dirty; dirty = 0; return value; }
    int listViewActive(item_list_t*) { return view; }
};
namespace host { static Library &library() { static Library instance; return instance; } }
static int mock_stat(const char*, struct stat *st) { memset(st, 0, sizeof(*st)); return -1; }
#define stat(p,s) mock_stat(p,s)
static int sbIsSameSize(const char*, int) { return 1; }
@NEEDS@
static void publishEth() {
@PUBLISH@
}
static int udpfsPs1Scanned;
struct { base_game_info_t *ps1Games_; int ps1GameCount_; } gUdpfsSupport;
static const char *udpfsPrefix = "udpfs:/";
static void publishUdp(int answeredBefore) {
@UDP_PUBLISH@
}
static int retryUdp(item_list_t *itemList) {
@UDP_RETRY@
    return 0;
}
int main() {
    item_list_t list{};
    for (int mixed = 0; mixed < 2; mixed++) {
        view = mixed ? LIB_VIEW_MIXED : LIB_VIEW_PS1;
        gEthSupport.ulSizePrev() = -1;
        scanResult = 2; incomplete = 0; publishEth();
        assert(!ethNeedsUpdate(&list));
        scanResult = 1; incomplete = 1; publishEth();
        assert(gEthSupport.ps1GameCount() == 1 && ethNeedsUpdate(&list));
        scanResult = 2; incomplete = 0; publishEth();
        assert(gEthSupport.ps1GameCount() == 2 && !ethNeedsUpdate(&list));
        for (int full = 0; full < 2; full++) {
            scanResult = full ? -1 : 1; incomplete = 1; publishEth();
            auto lastGood = gEthSupport.ps1Games();
            for (int i = 0; i < ETH_PS1_SCAN_RETRY_MAX; i++) {
                assert(ethNeedsUpdate(&list)); publishEth();
                assert(gEthSupport.ps1ScanRetries() == i + 1);
            }
            assert(!ethNeedsUpdate(&list));
            if (full) assert(gEthSupport.ps1Games() == lastGood);
            dirty = 1; assert(ethNeedsUpdate(&list));
            assert(!gEthSupport.ps1ScanRetries());
            scanResult = 2; incomplete = 0; publishEth();
            assert(!ethNeedsUpdate(&list));
        }
        incomplete = 1; publishEth();
        view = LIB_VIEW_ISO; assert(!ethNeedsUpdate(&list));
        view = mixed ? LIB_VIEW_MIXED : LIB_VIEW_PS1;
        gNetworkStartup = 1; assert(!ethNeedsUpdate(&list));
        gNetworkStartup = 0; gPCShareName[0] = 0;
        assert(!ethNeedsUpdate(&list)); strcpy(gPCShareName, "games");
        scanResult = 0; incomplete = 0; publishEth();
        assert(!gEthSupport.ps1Games() && !gEthSupport.ps1ScanFailed());
        assert(!ethNeedsUpdate(&list));

        // A successful UDPFS scan must not poison a later incomplete refresh's latch.
        scanResult = 2; incomplete = 0; publishUdp(1);
        assert(udpfsPs1Scanned && !retryUdp(&list));
        scanResult = 1; incomplete = 1; publishUdp(1);
        assert(!udpfsPs1Scanned && gUdpfsSupport.ps1GameCount_ == 1 && retryUdp(&list));
        scanResult = 2; incomplete = 0; publishUdp(1);
        assert(udpfsPs1Scanned && !retryUdp(&list));
        publishUdp(0); assert(!udpfsPs1Scanned && retryUdp(&list));
    }
    puts("PASS: SMB partial/full scan recovery, retry limits, refresh and session/view guards; UDPFS rescans after prior success in PS1 and mixed views");
}
'''
for key, value in {'LIMIT': limit, 'SESSION': session, 'NEEDS': needs, 'PUBLISH': publish,
                   'UDP_PUBLISH': udp_publish, 'UDP_RETRY': udp_retry}.items():
    program = program.replace('@' + key + '@', value)
with tempfile.TemporaryDirectory(prefix='ps1-recovery-') as tmp:
    source, executable = Path(tmp) / 'recovery.cpp', Path(tmp) / 'recovery'
    source.write_text(program, encoding='utf-8')
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True, timeout=10)
