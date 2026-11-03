"""Compile the production PS1 merge and MMCE retry handling against scan failures.

Ported from the merge section of the original's test of the same name; RiptOPL already carried
the fix. The original's Favorites, raudp/rapull/rapops and raw ARP sections exercise
RetroAchievements code that RiptOPL has not ported yet, and come with that cluster.
"""
from pathlib import Path
import subprocess
import tempfile
import re

root = Path(__file__).resolve().parents[2]
cue = (root / 'src/sources/cuesupport.cpp').read_text(encoding='utf-8')
mmce = (root / 'src/sources/mmcesupport.cpp').read_text(encoding='utf-8')

# Exercise the caller as well as the merge: a positive count can still need recovery.
start = mmce.index('        int r = ps1FillGameList(ps1Root, &gMmceSupport.ps1Games);')
publish = mmce[start:mmce.index('        result += gMmceSupport.ps1GameCount;', start)]
start = mmce.index('        if (!gMmceSupport.ps1Scanned')
retry = mmce[start:mmce.index('        if (host::library().viewActive', start)]
retry_max = re.search(r'^constexpr int MMCE_VCD_SCAN_RETRY_MAX = (\d+);', mmce, re.M).group(1)

program = r'''
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
typedef struct { int tag; char name[64]; } base_game_info_t; /* name: the PS1 Duplicates twin check reads it */
enum { PS1_DUP_BOTH = 0, PS1_DUP_EMBER, PS1_DUP_POPSTARTER };
static int gPs1Duplicates;
static int gAutosort, vc, cc, oom;
#define LOG(...) ((void)0)
static int ps1RowCmp(const void *a,const void *b) { (void)a;(void)b;return 0; }
static void *memalign(int a,size_t n) { (void)a;return oom ? NULL : malloc(n); }
static int half(int count,base_game_info_t **out) {
    *out=count>0 ? (base_game_info_t *)calloc(count,sizeof(**out)) : NULL; return count;
}
static int vcdFillGameList(const char *p,base_game_info_t **out) { (void)p;return half(vc,out); }
static int cueFillGameListUnsorted(const char *p,base_game_info_t **out) { (void)p;return half(cc,out); }
''' + cue[cue.index('static int ps1ScanIncomplete;'):] + '''
struct {
    base_game_info_t *ps1Games;
    int ps1GameCount, ps1Scanned, ps1ScanFailed, ps1ScanRetries;
} gMmceSupport;
struct MmceInstance { int updateDelay; } instance;
static MmceInstance *mmceInstance(void) { return &instance; }
#define MMCE_MODE_UPDATE_DELAY 2
#define MMCE_VCD_SCAN_RETRY_MAX ''' + retry_max + '''
static void publishMmce(void) {
    const char *ps1Root = "mmce0:/";
''' + publish + '''
}
static int retryMmce(void) {
    int result = 0;
''' + retry + '''
    return result;
}
''' + r'''
int main(void) {
    for(vc=-1;vc<=1;vc++) for(cc=-1;cc<=1;cc++) {
        base_game_info_t *rows=(base_game_info_t *)malloc(sizeof(*rows)), *old=rows; rows->tag=123;
        int got=ps1FillGameList("mass0:/",&rows);
        assert(ps1GameListNeedsRetry()==(vc<0 || cc<0));
        if((vc<0 && cc<=0)||(cc<0 && vc<=0)) assert(got==-1 && rows==old && rows->tag==123);
        else if(vc==0 && cc==0) assert(got==0 && rows==NULL);
        else assert(got==(vc>0?vc:0)+(cc>0?cc:0) && rows!=NULL);
        free(rows);
    }
    base_game_info_t *rows=(base_game_info_t *)malloc(sizeof(*rows)), *old=rows;
    vc=cc=1;oom=1;
    assert(ps1FillGameList("mass0:/",&rows)==-1 && rows==old);free(rows);
    puts("PASS: failed/empty merge retains old rows; nonempty half publishes; OOM preserves ownership");

    oom=0;
    assert(retryMmce()); // first scan is scheduled without spending the retry budget
    assert(gMmceSupport.ps1ScanRetries==0);
    vc=1; cc=-1; publishMmce();
    assert(gMmceSupport.ps1GameCount==1 && retryMmce());
    vc=cc=1; publishMmce(); // the automatic retry restores the Ember half
    assert(gMmceSupport.ps1GameCount==2 && !gMmceSupport.ps1ScanFailed);
    assert(gMmceSupport.ps1ScanRetries==0 && !retryMmce());
    for(int failedCore=0;failedCore<2;failedCore++) {
        vc=failedCore ? -1 : 1; cc=failedCore ? 1 : -1;
        publishMmce();
        assert(gMmceSupport.ps1GameCount==1 && gMmceSupport.ps1ScanFailed);
        for(int i=0;i<MMCE_VCD_SCAN_RETRY_MAX;i++) {
            assert(retryMmce());
            assert(gMmceSupport.ps1ScanRetries==i+1);
            publishMmce();
        }
        assert(!retryMmce()); // publishing a partial list must not reset the budget
        vc=cc=1; publishMmce(); // a later user refresh recovers both halves
        assert(gMmceSupport.ps1GameCount==2 && !gMmceSupport.ps1ScanFailed);
        assert(gMmceSupport.ps1ScanRetries==0 && !retryMmce());
    }
    base_game_info_t *lastGood=gMmceSupport.ps1Games;
    vc=cc=-1; publishMmce();
    assert(gMmceSupport.ps1Games==lastGood && gMmceSupport.ps1GameCount==2);
    for(int i=0;i<MMCE_VCD_SCAN_RETRY_MAX;i++) {
        assert(retryMmce()); publishMmce();
    }
    assert(!retryMmce()); // fully failed scans must also stop at the budget
    vc=cc=0; publishMmce();
    assert(!gMmceSupport.ps1Games && gMmceSupport.ps1GameCount==0);
    assert(!gMmceSupport.ps1ScanFailed && !retryMmce());
    puts("PASS: MMCE retries either missing core, bounds partial/full failures, and settles on complete or empty scans");
}
'''
with tempfile.TemporaryDirectory(prefix='pr894-') as tmp:
    src, exe = Path(tmp) / 'merge.cpp', Path(tmp) / 'merge'
    src.write_text(program, encoding='utf-8')
    subprocess.run(['g++', '-Wall', '-Werror', str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
