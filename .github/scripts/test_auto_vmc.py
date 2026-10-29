"""Execute actual automatic-card policy and modal launch ownership/identity guards."""
from pathlib import Path
import os
import subprocess
import re
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
policy_source = Path(sys.argv[1]).read_text(encoding='utf-8') if len(sys.argv) > 1 else (root / 'src/ui/guigame.cpp').read_text(encoding='utf-8')
def read(path):
    return (root / path).read_text(encoding='utf-8')
def function(source, signature):
    first = source.index(signature)
    return source[first:source.index('\n}', first) + 2]
def run(name, program):
    startup_limit = re.search(r'constexpr int APP_STARTUP_MAX\s*=\s*\d+;', read('include/appsupport.h')).group(0)
    program = program.replace('@STARTUP_LIMIT@', startup_limit)
    directory = root / 'build/test-tmp'
    directory.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=directory) as temp:
        cpp, exe = Path(temp) / (name+'.cpp'), Path(temp) / name
        cpp.write_text(program, encoding='utf-8')
        flags = ['-std=c++17', '-Wall', '-Wextra', '-Werror']
        if os.name != 'nt':
            flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=undefined']
        subprocess.run(['c++', *flags, str(cpp), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=20)

run('auto_vmc', r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
enum {FAV_MODE=12, APP_MODE=10, HTTP_MODE=14, HDD_MODE=9, ETH_MODE=8, UDPFS_MODE=15, LIB_VIEW_PS1=1,FAV_KIND_ISO=0};
enum {CONFIG_ITEM_CORE_LOADER, _STR_AUTO_VMC_FAILED, _STR_AUTO_VMC_TIMEOUT,
      _STR_AUTO_VMC_CREATING,_STR_VMC_FRAGMENTED_ON_CREATE,_STR_ERROR_SAVING_SETTINGS,
      _STR_AUTO_VMC_INVALID,_STR_AUTO_VMC_STILL_RUNNING,_STR_VMC_PFS_LIMIT_ON_CREATE};
@STARTUP_LIMIT@
static int gAutoCreateVmc=1,gDefaultCoreLoader=0;
#define SYS_VMC_PROBE_FORMAT (-4)
struct config_set_t {int loader=2,disabled=0;char assigned[32]={};};
struct item_list_t {int mode=0,enabled=1;void *source=nullptr;void *priv=nullptr;};
static int rowView,kind,probe= -1,submitted= -1,probes,creates,polls,aborts,writes,dialogs,contiguity;
static int completionError,cancelAt= -1,completeAt=2,chain=1,contiguous= -1;
static int completedSize=8,rowCount=3,mutation;
static bool active=true,ownerPresent=true,cap=true,saveOk=true,statusFails,assignOk=true;
static bool jobStarted,initialBusy,initialFailure,udpbd;
static const char *title="SLUS_123.45";
struct statusVMCparam_t {int VMC_status,VMC_error,VMC_progress;char VMC_msg[64];};
class GameSource {
public:
    bool hasCheckVmc() {return cap;}
    const char *startup(item_list_t*,int) {return title;}
    int count(item_list_t*) {return rowCount;}
    int createVmc(item_list_t*,char *name,int size,int requireContiguous) {
        assert(!strcmp(name,"GROUP_0") && size==8);creates++;jobStarted=true;contiguous=requireContiguous;return submitted;
    }
    int checkVmc(item_list_t*,char *name,int size) {
        assert(!strcmp(name,"GROUP_0") && size==SYS_VMC_PROBE_FORMAT); // creation goes through createVmc
        probes++;return creates ? completedSize : probe;
    }
};
static GameSource source;
static item_list_t owner;
static GameSource *gsOf(item_list_t*) {return &source;}
static struct {
    item_list_t *ownerView(int,item_list_t *out) {if(!ownerPresent)return nullptr;*out=owner;return out;}
} gFavSupport;
static int favGetItemKind(int) {return kind;}
static int libListRowView(item_list_t*,int) {return rowView;}
[[maybe_unused]] static int bdmSupportIsUDPBD(const item_list_t*) {return udpbd;}
[[maybe_unused]] static int bdmModeIsUDPBD(int) {return udpbd;}
[[maybe_unused]] static int bdmModeIsPCCF(int) {return 0;}
static int guiIsActive() {return active;}
static void configGetInt(config_set_t *c,int,int *out) {*out=c->loader;}
[[maybe_unused]] static int bdmNetBlockCoreLoader(config_set_t *c) {return c->loader==0?0:1;}
static void configGetVMC(config_set_t *c,char *out,int,int) {strcpy(out,c->assigned);}
static void configGetVMCDisable(config_set_t *c,int,int *out) {*out=c->disabled;}
static void configSetVMC(config_set_t *c,const char *name,int) {if(assignOk)strcpy(c->assigned,name);}
static int configWrite(config_set_t*) {writes++;return saveOk;}
static const char *getGroupIdForTitleId(const char*) {return "GROUP";}
static const char *_l(int) {return "message";}
static int guiMsgBox(const char*,int,void*) {dialogs++;return 0;}
static int sysVMCContiguity() {contiguity++;return chain;}
static const char *sysVMCCreatePath() {return "mass0:/VMC/GROUP_0.bin";}
static int hddVMCMapFits(const char*) {assert(0);return 1;}
static void readPads() {}
static int guiCancelKey() {return 1;}
static int getKeyOn(int) {return polls==cancelAt;}
static void guiRenderTextScreen(const char*) {}
static int fileXioDevctl(const char*,unsigned command,void*,int,void *out,int) {
    if(command==0xC0DE0002) {aborts++;return 0;}
    assert(command==0xC0DE0003);
    auto *s=static_cast<statusVMCparam_t*>(out);
    if(!jobStarted) {s->VMC_status=initialBusy;return initialFailure ? -1 : 0;}
    polls++;
    if (polls==1) {
        if(mutation==1)title="OTHER_GAME";
        if(mutation==2)rowCount=0;
        if(mutation==3)ownerPresent=false;
        if(mutation==4)owner.priv=&source;
        if(mutation==5)owner.mode=ETH_MODE;
        if(mutation==6)owner.source=&source;
    }
    if(statusFails)return -1;
    s->VMC_status=polls<completeAt;s->VMC_error=completionError;s->VMC_progress=50;
    return 0;
}
constexpr int AUTO_VMC_SIZE_MB=8, AUTO_VMC_MAX_FRAMES=5, AUTO_VMC_CANCEL_FRAMES=5;
''' + function(policy_source,'static int guiGameVMCLayoutWarning(') + '\n' + function(policy_source,'int guiGameAutoCreateVmc(') + r'''
static void reset() {
    gAutoCreateVmc=1;gDefaultCoreLoader=0;rowView=kind=0;probe=submitted=-1;
    probes=creates=polls=aborts=writes=dialogs=contiguity=completionError=0;contiguous= -1;
    completeAt=2;cancelAt=-1;chain=1;active=ownerPresent=cap=saveOk=assignOk=true;
    statusFails=jobStarted=initialBusy=initialFailure=udpbd=false;title="SLUS_123.45";owner={};
    completedSize=8;rowCount=3;mutation=0;
}
static void noCard(config_set_t &c) {assert(!c.assigned[0] && !creates && !writes);}
int main() {
    item_list_t list;config_set_t cfg;
    reset(); assert(guiGameAutoCreateVmc(&list,1,&cfg)==1);
    assert(creates==1 && polls==2 && probes==2 && writes==1 && !strcmp(cfg.assigned,"GROUP_0") && contiguity==1);
    assert(contiguous==1); // a native card needs one reserved extent (original #994)
    for(int size : {8,16,32,64}) {reset();cfg={};probe=size;assert(guiGameAutoCreateVmc(&list,1,&cfg));assert(!creates && writes==1);}
    for(int size : {0,-2,-3,4}) {reset();cfg={};probe=size;assert(guiGameAutoCreateVmc(&list,1,&cfg));noCard(cfg);assert(dialogs==1);}
    for(int result : {0,-2,-3}) {reset();cfg={};submitted=result;assert(guiGameAutoCreateVmc(&list,1,&cfg));assert(creates==1 && !polls && !writes && !cfg.assigned[0]);}
    reset();cfg={};submitted=8;assert(guiGameAutoCreateVmc(&list,1,&cfg) && !polls && writes==1);
    reset();cfg={};probe=8;initialBusy=true;assert(!guiGameAutoCreateVmc(&list,1,&cfg));noCard(cfg);
    reset();cfg={};probe=8;initialFailure=true;assert(guiGameAutoCreateVmc(&list,1,&cfg));noCard(cfg);
    reset();cfg={};completionError=-7;assert(guiGameAutoCreateVmc(&list,1,&cfg));assert(!writes && !cfg.assigned[0]);
    reset();cfg={};cancelAt=1;assert(!guiGameAutoCreateVmc(&list,1,&cfg));assert(aborts==1 && !writes && !cfg.assigned[0]);
    reset();cfg={};statusFails=true;cancelAt=1;assert(!guiGameAutoCreateVmc(&list,1,&cfg));assert(aborts==1 && polls==6 && !writes);
    reset();cfg={};completeAt=99;cancelAt=1;assert(!guiGameAutoCreateVmc(&list,1,&cfg));assert(aborts==1 && !writes);
    reset();cfg={};completeAt=13;assert(guiGameAutoCreateVmc(&list,1,&cfg));assert(polls==13 && !aborts && writes==1);
    for(int size : {-1,-3,0,4,16}) {reset();cfg={};completedSize=size;assert(guiGameAutoCreateVmc(&list,1,&cfg));assert(!writes && !cfg.assigned[0] && dialogs==1);}
    for(int change : {1,2}) {reset();cfg={};mutation=change;assert(!guiGameAutoCreateVmc(&list,1,&cfg));assert(!writes && !cfg.assigned[0]);}
    reset();cfg={};saveOk=false;assert(guiGameAutoCreateVmc(&list,1,&cfg));assert(writes==1 && dialogs==1);
    reset();cfg={};assignOk=false;assert(guiGameAutoCreateVmc(&list,1,&cfg));assert(!writes && !cfg.assigned[0]);
    reset();cfg={};cfg.loader=1;assert(guiGameAutoCreateVmc(&list,1,&cfg) && !contiguity && contiguous==0);
    reset();cfg={};cfg.loader=2;gDefaultCoreLoader=1;assert(guiGameAutoCreateVmc(&list,1,&cfg) && contiguous==0);
    for(int scenario=0;scenario<10;scenario++) {
        reset();cfg={};list={};
        if(scenario==0)gAutoCreateVmc=0;
        if(scenario==1)active=false;
        if(scenario==2)list.mode=APP_MODE;
        if(scenario==3)rowView=LIB_VIEW_PS1;
        if(scenario==4)list.mode=HTTP_MODE;
        if(scenario==5){list.mode=HDD_MODE;cfg.loader=1;}
        if(scenario==6)cfg.disabled=1;
        if(scenario==7)strcpy(cfg.assigned,"MY_SAVES");
        if(scenario==8)cap=false;
        if(scenario==9)title=nullptr;
        assert(guiGameAutoCreateVmc(&list,1,&cfg));assert(!creates && !writes);
    }
    reset();cfg={};list.mode=FAV_MODE;owner.mode=HTTP_MODE;
    assert(guiGameAutoCreateVmc(&list,1,&cfg));noCard(cfg);
    reset();cfg={};list.mode=FAV_MODE;kind=3;
    assert(guiGameAutoCreateVmc(&list,1,&cfg));noCard(cfg);
    reset();cfg={};list.mode=FAV_MODE;ownerPresent=false;
    assert(guiGameAutoCreateVmc(&list,1,&cfg));noCard(cfg);
    reset();cfg={};list.mode=FAV_MODE;
    assert(guiGameAutoCreateVmc(&list,1,&cfg) && writes==1 && contiguous==1);
    for(int change : {3,4,5,6}) {
        reset();cfg={};list.mode=FAV_MODE;mutation=change;
        assert(!guiGameAutoCreateVmc(&list,1,&cfg) && !writes && probes==1);
    }
    for(int transport : {0,1,2,3}) {
        reset();cfg={};list={};probe=0;
        if(transport==0)cfg.loader=1;
        if(transport==1)list.mode=UDPFS_MODE;
        if(transport==2){udpbd=true;cfg.loader=2;} // UDPBD on Default: Neutrino whatever the global
        if(transport==3){list.mode=FAV_MODE;owner.mode=UDPFS_MODE;}
        assert(guiGameAutoCreateVmc(&list,1,&cfg));
        assert(writes==1 && !creates && !contiguity && !strcmp(cfg.assigned,"GROUP_0"));
    }
    for(int size : {4,128}) {
        reset();cfg={};list={};cfg.loader=1;probe=size;
        assert(guiGameAutoCreateVmc(&list,1,&cfg) && writes==1 && !creates);
    }
    reset();cfg={};list={};list.mode=ETH_MODE;cfg.loader=1;probe=0;
    assert(guiGameAutoCreateVmc(&list,1,&cfg));noCard(cfg);
    // UDPBD with its own <OPL> key launches native, which refuses the odd-size card like SMB does.
    reset();cfg={};list={};udpbd=true;cfg.loader=0;probe=0;
    assert(guiGameAutoCreateVmc(&list,1,&cfg));noCard(cfg);
    reset();cfg={};list={};char longStartup[257];memset(longStartup,'x',256);longStartup[256]=0;title=longStartup;
    assert(!guiGameAutoCreateVmc(&list,1,&cfg));noCard(cfg);
}
''')
run('auto_vmc_launch', r'''
#include <cassert>
#include <cstdio>
#include <cstring>
enum {SFX_CONFIRM,BDM_MODE,BDM_MODE_LAST=7,IO_MENU_UPDATE_DEFFERED};
@STARTUP_LIMIT@
static int gAutoCreateVmc=1;
struct config_set_t {int freed=0;};
struct item_list_t {int enabled=1,mode=9;void *owner=nullptr;};
struct row {struct {int isFolder=0,id=4;char text[32]={};} item;};
struct menu_item {void *userdata=nullptr;row *current=nullptr;};
struct opl_io_module_t {item_list_t *support;};
static config_set_t config,replacement,*itemConfig;
static item_list_t support,*itemConfigList;
static unsigned int itemConfigGeneration;
static int itemConfigId,menuSemaId=1,sema=1,launches,autoCalls,autoResult=1,mutation,gameIds;
static menu_item menu;
static row selected;
static const char *name="SLUS_123.45";
static int WaitSema(int) {assert(sema==1);sema=0;return 0;}
static int SignalSema(int) {assert(sema==0);sema=1;return 0;}
static void configFree(config_set_t *cfg) {assert(cfg && !cfg->freed);cfg->freed++;}
static config_set_t *menuLoadConfig() {return &config;}
static struct {
    char *startup(item_list_t*,int) {return const_cast<char *>(name);}
    void launch(item_list_t*,int id,config_set_t *cfg) {assert(id==4 && cfg==&config && !cfg->freed);launches++;}
} source;
static auto *gsOf(item_list_t*) {return &source;}
static void sfxPlay(int) {}
static void padRumbleFlush() {}
static int folderDescend(int,const char*) {return 0;}
static int ioPutRequest(int,void*) {return 0;}
static void itemInitSupport(item_list_t*) {}
static opl_io_module_t *oplGetModule(int) {return nullptr;}
static int guiMsgBox(const char*,int,void*) {return 0;}
static void guiLock() {}
static void guiUnlock() {}
static int menuCheckGameParentalLock(config_set_t*) {return 0;}
static int guiGameAutoCreateVmc(item_list_t*,int,config_set_t *cfg) {
    autoCalls++;assert(cfg==&config && itemConfig==nullptr && !cfg->freed);
    // Simulate background cache replacement while the progress screen yields.
    itemConfig=&replacement;
    if(mutation==1)selected.item.id=7;
    if(mutation==2)name="OTHER_GAME";
    if(mutation==3)support.enabled=0;
    if(mutation==4)menu.current=nullptr;
    return autoResult;
}
static void guiShowGameID(const char*) {gameIds++;if(mutation==5)selected.item.id=8;}
// Recents are recorded only after every identity check, right before the handoff.
static int recents;
template <class S, class I> static void itemRecordRecent(S*, I*) {recents++;}
''' + function(read('src/ui/menusys.cpp'),'config_set_t *menuTakeLaunchConfig(')
    + '\n' + function(read('src/ui/menusys.cpp'),'void menuInvalidateConfig(')
    + '\n' + function(read('src/ui/menuitems.cpp'),'static void itemExecSelect(') + r'''
static void reset() {
    config={};replacement={};support={};selected={};menu={&support,&selected};
    itemConfig=&config;itemConfigId=4;itemConfigList=&support;
    gAutoCreateVmc=1;name="SLUS_123.45";launches=autoCalls=gameIds=mutation=0;autoResult=1;
}
int main() {
    recents=0;reset();itemExecSelect(&menu);assert(launches==1 && recents==1 && config.freed==1 && !replacement.freed);
    for(int value=1;value<=5;value++) {
        recents=0;reset();mutation=value;itemExecSelect(&menu);assert(!launches && !recents && config.freed==1 && !replacement.freed);
    }
    reset();autoResult=0;itemExecSelect(&menu);assert(!launches && !gameIds && config.freed==1);
    reset();itemConfig=&replacement;itemExecSelect(&menu);assert(!autoCalls && !launches && !config.freed && !replacement.freed);
    reset();gAutoCreateVmc=0;itemExecSelect(&menu);assert(launches==1 && !autoCalls && !config.freed);
    reset();assert(menuTakeLaunchConfig(&replacement,&support,4)==nullptr && itemConfig==&config);
    reset();itemConfigId=8;itemExecSelect(&menu);assert(!autoCalls && !launches && !config.freed);
    char longPath[256];memset(longPath,'x',255);longPath[255]='\0';
    reset();name=longPath;itemExecSelect(&menu);assert(launches==1 && config.freed==1);
}
''')
print('Automatic VMC: effective loader, verified card, slow jobs/cancel, owned config and changed-row refusal pass')
