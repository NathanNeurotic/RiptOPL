"""Compile the production PS1 merge against failed, empty and nonempty halves.

Ported from the merge section of the original's test of the same name; RiptOPL already carried
the fix. The original's Favorites, raudp/rapull/rapops and raw ARP sections exercise
RetroAchievements code that RiptOPL has not ported yet, and come with that cluster.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
cue = (root / 'src/sources/cuesupport.cpp').read_text(encoding='utf-8')

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
''' + cue[cue.index('static int ps1ScanIncomplete;'):] + r'''
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
}
'''
with tempfile.TemporaryDirectory(prefix='pr894-') as tmp:
    src, exe = Path(tmp) / 'merge.cpp', Path(tmp) / 'merge'
    src.write_text(program, encoding='utf-8')
    subprocess.run(['g++', '-Wall', '-Werror', str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
