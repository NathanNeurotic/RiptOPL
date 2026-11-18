"""Execute the discs.txt parser, the starting-disc picker and the launch preflight (original #1034).

Only Ember's final argument changes. A missing manifest keeps the folder launch; a cancelled picker
must return before settings writes and teardown, and the HDD leg must restore its APA mount. The five
launch legs are class methods with deep dependencies, so their ordering is checked statically.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

import hostmap  # noqa: F401  (maps host::ui() calls back to the names the mocks use)

ROOT = Path(__file__).resolve().parents[2]


def function(path, signature):
    source = (ROOT / path).read_text(encoding='utf-8')
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]


header = (ROOT / 'include/cuesupport.h').read_text()
constants = '\n'.join(re.findall(r'^#define (?:CUE_(?:NAME_MAX|NAME_LAUNCH_MAX|MAX_DISCS)|EMBER_(?:FOLDER_DEFAULT|GAMES_FOLDER))\s+\S+', header, re.M))
disc_type = re.search(r'typedef struct cue_disc_list\s*\{.*?\} cue_disc_list_t;', header, re.S).group()
dia = (ROOT / 'include/dia.h').read_text()
enum = dia[dia.index('typedef enum'):dia.index('} UIItemType;') + len('} UIItemType;')]
start = dia.index('struct UIItem\n')
ui_type = dia[start:dia.index('\n};', start) + 3]
functions = '\n\n'.join(function('src/sources/cuesupport.cpp', signature) for signature in (
    'static char cueSep(', 'static const char *cueEmberFolder(', 'static void cueBuildGamesDir(',
    'int cueNameLaunchable(', 'int cueBuildDiscArgument(', 'int cueBuildDiscPath(',
    'static int cueAppendDiscLine(', 'static int cueConfirmDiscListAbsent(', 'int cueReadDiscList(',
    'int cuePrepareLaunch(',
))
gui = '\n\n'.join(function('src/ui/guisettings.cpp', signature) for signature in (
    'static void guiDiscListItem(', 'int guiShowEmberDiscList(',
))
HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#define LOG(...) ((void)0)
@CONSTANTS@
@DISC_TYPE@
@UI_TYPES@
enum { _STR_EMBER_SELECT_DISC=1, _STR_EMBER_DISC_ENTRY, _STR_EMBER_DISCS_INVALID,
       _STR_EMBER_DISC_ARGS_TOO_LONG, _STR_ERR_FILE_INVALID };
static const char *_l(int id) { return id == _STR_EMBER_DISC_ENTRY ? "Disc %d: %s" : "message"; }
static char root[1024];
static int picker, dialogs, errors;
static int fail_alloc, fail_open, fail_read, read_count, read_error;
static void *tracked_calloc(size_t n,size_t s) {
    if (fail_alloc) { fail_alloc=0;return NULL; } return calloc(n,s);
}
static const char *mapped(const char *path) {
    static char result[2048];
    if (!strncmp(path,"pfs0:/",6) || !strncmp(path,"mass0:/",7) ||
        !strncmp(path,"mmce0:/",7) || !strncmp(path,"smb:\\",5) || !strncmp(path,"udpfs:/",7))
        snprintf(result,sizeof(result),"%s/%s",root,strchr(path,':')+2);
    else snprintf(result,sizeof(result),"%s",path);
    for(char *p=result;*p;p++) if(*p=='\\') *p='/';
    return result;
}
static FILE *mapped_fopen(const char *path,const char *mode) {
    if(fail_open && mode[0]=='r') {errno=EIO;return NULL;}
    read_count=read_error=0;return fopen(mapped(path),mode);
}
static int mapped_open(const char *path,int flags) { return open(mapped(path),flags); }
static DIR *mapped_opendir(const char *path) { return opendir(mapped(path)); }
static int injected_getc(FILE *f) {
    if(fail_read && ++read_count>5) {read_error=1;return EOF;} return fgetc(f);
}
static int injected_ferror(FILE *f) {return read_error || ferror(f);}
static void guiMsgBox(const char *s,int n,void *p) {(void)s;(void)n;(void)p;errors++;}
static void guiMsgBoxMissing(const char *s,const char *p) {(void)s;(void)p;errors++;}
static int diaExecuteDialog(struct UIItem *ui,int focus,short menu,int (*update)(int)) {
    (void)menu;(void)update;dialogs++;assert(focus==1);
    int buttons=0;
    for(int i=0;ui[i].type!=UI_TERMINATOR;i++) {
        assert(ui[i].type!=UI_LABEL); /* no folder line: the disc rows name the game */
        if(ui[i].type==UI_BUTTON) {
            buttons++;assert(ui[i].id==buttons);
            assert(strstr(ui[i].label.text,"Disc ")&&!strstr(ui[i].label.text,".cue")&&!strstr(ui[i].label.text,".CUE"));
        }
    }
    assert(buttons>=2 && buttons<=CUE_MAX_DISCS);return picker;
}
#define calloc tracked_calloc
@GUI@
#define fopen mapped_fopen
#define open mapped_open
#define opendir mapped_opendir
#define fgetc injected_getc
#define ferror injected_ferror
@FUNCTIONS@
#undef fopen
#undef open
#undef opendir
#undef calloc
#undef fgetc
#undef ferror
static void reset(void) {
    picker=2;dialogs=errors=0;fail_alloc=fail_open=fail_read=read_error=0;
}
static void manifest(const char *text) {
    char path[1400];snprintf(path,sizeof(path),"%s/EMBER/games/Game/discs.txt",root);
    if(!text) {remove(path);return;}
    FILE *f=fopen(path,"wb");assert(f);assert(fwrite(text,1,strlen(text),f)==strlen(text));fclose(f);
}
int main(int argc,char **argv) {
    assert(argc==2);snprintf(root,sizeof(root),"%s",argv[1]);
    cue_disc_list_t *list=NULL;char argument[192],path[640],prefix[1100];
    snprintf(prefix,sizeof(prefix),"%s/",root);
    manifest(NULL);assert(cueReadDiscList(prefix,"Game",&list)==0 && !list);
    fail_open=1;assert(cueReadDiscList("mmce0:/","Game",&list)==0 && !list);fail_open=0;
    manifest("\xef\xbb\xbf" "Disc 2.cue\r\n\r\nDisc 1.cue");
    assert(cueReadDiscList(prefix,"Game",&list)==2);
    assert(!strcmp(list->names[0],"Disc 2.cue") && !strcmp(list->names[1],"Disc 1.cue"));free(list);
    const char *invalid[]={"", "\n\r\n", "../disc.cue\n", "mass:/disc.cue\n", "sub/disc.cue\n",
        "sub\\disc.cue\n", "image.bin\n", "Disc\t1.cue\n", "Disc 1.cue\n/escape.cue\n"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        manifest(invalid[i]);assert(cueReadDiscList(prefix,"Game",&list)==-1 && !list);
    }
    char oversized[9000];memset(oversized,'\n',sizeof(oversized)-1);oversized[sizeof(oversized)-1]=0;
    manifest(oversized);assert(cueReadDiscList(prefix,"Game",&list)==-1 && !list);
    oversized[0]=0;for(int i=0;i<CUE_MAX_DISCS+1;i++) strcat(oversized,"Disc 1.cue\n");
    manifest(oversized);assert(cueReadDiscList(prefix,"Game",&list)==-1 && !list);
    manifest("Disc 1.cue\nDisc 2.cue\n");fail_alloc=1;
    assert(cueReadDiscList(prefix,"Game",&list)==-1 && !list);
    fail_open=1;assert(cueReadDiscList(prefix,"Game",&list)==-1 && !list);fail_open=0;
    fail_read=1;assert(cueReadDiscList(prefix,"Game",&list)==-1 && !list);fail_read=0;
    assert(cueBuildDiscArgument("Game","Disc 2.cue",argument,sizeof(argument)));
    assert(!strcmp(argument,"Game/Disc 2.cue"));
    assert(!cueBuildDiscArgument("Game","Disc 2.cue",argument,8));
    assert(cueBuildDiscPath("smb:\\","Game","Disc 2.cue",path,sizeof(path)));
    assert(!strcmp(path,"smb:\\EMBER\\games\\Game\\Disc 2.cue"));
    /* Preflight: folder launch, single disc (no picker), picker choice, cancel, faults. */
    reset();manifest(NULL);
    assert(cuePrepareLaunch(prefix,"Game","mass:/EMBER/ember.elf",argument,sizeof(argument)));
    assert(!dialogs && !errors && !strcmp(argument,"Game"));
    reset();manifest("Disc 1.cue\n");
    assert(cuePrepareLaunch(prefix,"Game","mass:/EMBER/ember.elf",argument,sizeof(argument)));
    assert(!dialogs && !strcmp(argument,"Game/Disc 1.cue"));
    reset();manifest("Disc 1.cue\nDisc 2.cue\n");
    assert(cuePrepareLaunch(prefix,"Game","mass:/EMBER/ember.elf",argument,sizeof(argument)));
    assert(dialogs==1 && !errors && !strcmp(argument,"Game/Disc 2.cue"));
    reset();picker=0;
    assert(!cuePrepareLaunch(prefix,"Game","mass:/EMBER/ember.elf",argument,sizeof(argument)));
    assert(dialogs==1 && !errors);
    reset();assert(cueReadDiscList(prefix,"Game",&list)==2);
    fail_alloc=1; /* the picker's own allocation fails: refuse, never default to disc 1 */
    assert(guiShowEmberDiscList("Game",list)==-1 && !dialogs);
    picker=99;assert(guiShowEmberDiscList("Game",list)==-1);free(list);
    reset();manifest("Disc 1.cue\nMissing.cue\n");
    assert(!cuePrepareLaunch(prefix,"Game","mass:/EMBER/ember.elf",argument,sizeof(argument)));
    assert(errors==1);
    reset();manifest("bad/line.cue\n");
    assert(!cuePrepareLaunch(prefix,"Game","mass:/EMBER/ember.elf",argument,sizeof(argument)));
    assert(errors==1 && !dialogs);
    reset();manifest("Disc 1.cue\n");char elf[200];memset(elf,'e',sizeof(elf)-1);elf[sizeof(elf)-1]=0;
    assert(!cuePrepareLaunch(prefix,"Game",elf,argument,sizeof(argument)) && errors==1);
    reset();manifest(NULL); /* a plain folder needs the SAME total argv budget check */
    assert(!cuePrepareLaunch(prefix,"Game",elf,argument,sizeof(argument)) && errors==1);
    reset();assert(cuePrepareLaunch(prefix,"Game","mass:/EMBER/ember.elf",argument,sizeof(argument)));
    assert(!errors && !strcmp(argument,"Game"));
    puts("PASS: manifests, ordered picker, faults, argument bounds and launch preflight");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    folder = Path(directory)
    game = folder / 'EMBER/games/Game'
    game.mkdir(parents=True)
    for name in ('Disc 1.cue', 'Disc 2.cue'):
        (game / name).write_text('FILE "game.bin" BINARY\n')
    source = HARNESS.replace('@CONSTANTS@', constants).replace('@DISC_TYPE@', disc_type)
    source = source.replace('@UI_TYPES@', enum + '\n' + ui_type)
    source = source.replace('@FUNCTIONS@', functions).replace('@GUI@', gui)
    cfile = folder / 'discs.c'
    binary = folder / ('discs.exe' if sys.platform == 'win32' else 'discs')
    cfile.write_text(source)
    subprocess.run(['gcc', '-std=gnu99', '-Wall', '-Wextra', '-Werror', '-Wno-sign-compare',
                    '-Wno-missing-field-initializers', str(cfile), '-o', str(binary)], check=True)
    subprocess.run([str(binary), str(folder)], check=True, timeout=15)

# The five launch legs: the preflight runs after the BIOS check and before settings writes and
# teardown, Ember receives the copied argument, and HDD restores its mount when the user backs out.
LEGS = (
    ('src/sources/bdmlaunch.cpp', 'void BdmLauncher::launchCue(', 'ps1Prefix'),
    ('src/sources/mmcesupport.cpp', 'void MmceSource::launchCue(', 'ps1Root'),
    ('src/sources/ethsupport.cpp', 'void EthSource::launchCue(', 'gEthSupport.prefix()'),
    ('src/sources/udpfssupport.cpp', 'void UdpfsSource::launchCue(', 'udpfsPrefix'),
    ('src/sources/hddsupport.cpp', 'static void hddDoLaunchEmber(', '"pfs0:/"'),
)
for path, signature, prefix in LEGS:
    body = function(path, signature)
    call = 'cuePrepareLaunch(%s, ' % prefix
    assert body.count('cuePrepareLaunch(') == 1 and call in body, (path, 'preflight')
    at = body.index(call)
    assert body.index('cueResolveEmberBios(') < at, (path, 'after the BIOS check')
    for later in ('cueApplySettings(', 'cueGameHasImage(', 'deinit('):
        assert at < body.index(later), (path, 'before', later)
    assert 'sysLaunchEmber(emberElf, launchName);' in body, (path, 'copied argument')
    assert 'snprintf(launchName' not in body, (path, 'preflight owns the copy')
hdd = function('src/sources/hddsupport.cpp', 'static void hddDoLaunchEmber(')
cancel = hdd[hdd.index('if (!cuePrepareLaunch('):]
cancel = cancel[:cancel.index('return;')]
assert 'hddRestoreDataHome();' in cancel and 'ioBlockOps(0);' in cancel, 'HDD cancel restores APA'
print('PASS: five launch legs preflight before settings and teardown; HDD cancel restores its mount')
