"""Execute native BDM/PFS launch checks, including actual slot writes and refusal paths."""
import hostmap
from pathlib import Path
import subprocess
import tempfile
import os
import re

root = Path(__file__).resolve().parents[2]
bdm = (root / 'src/sources/bdmlaunch.cpp').read_text(encoding='utf-8')
hdd = (root / 'src/sources/hddsupport.cpp').read_text(encoding='utf-8')
start = bdm.index('int bdmCheckNativeVmcLayout(int iop_fd')
helper = bdm[start:bdm.index('\n}\n', start) + 3]
start = bdm.index('static u64 bdmNativeVmcLimit(')
limit = bdm[start:bdm.index('\n}\n', start) + 3]
start = bdm.index('    char vmc_name[32], vmc_path[256];', bdm.index('void bdmLaunchGame('))
bdm_slots = bdm[start:bdm.index('    void *irx = NULL;', start)]
start = hdd.index('    if (part_valid) {', hdd.index('void HddSource::launch('))
hdd_slots = hdd[start:hdd.index('    if (gRememberLastPlayed)', start)]
start = hdd.index('static int hddClassifyVMCMap(')
classify = hdd[start:hdd.index('\n}\n', start) + 3]

program = r'''
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef unsigned int u32;
typedef unsigned long long u64;
#include "nativevmc.h"
typedef struct { u64 sector; u32 count; } bd_fragment_t;
#define O_RDONLY 0
#define BDM_TYPE_PCCF 9
#define USBMASS_IOCTL_GET_LBA 1
#define USBMASS_IOCTL_GET_FRAGLIST 2
#define LOG(...) ((void)0)
#define HDD_VMC_MAP_ENTRIES 11
#define MODE_FLAG_READ_ONLY 0x08
typedef struct { unsigned char flags; } item_list_t;
static item_list_t bdmList;
enum { _STR_ERR_VMC_CONTINUE, _STR_ERR_VMC_FRAGMENTED_CONTINUE,
       _STR_ERR_VMC_LBA_RANGE_CONTINUE, _STR_ERR_VMC_LAYOUT_CONTINUE, _STR_ERR_VMC_PFS_LIMIT_CONTINUE, _STR_ERR_VMC_FRAGMENTS_CONTINUE, _STR_VMC_DEFRAGMENT_HINT };
typedef struct { int page_size, block_size, card_size; } specs_t;
typedef struct { int number, subpart, count; } pfs_blockinfo_t;
typedef struct { int active, flags; specs_t specs; u64 start_sector; } bdm_vmc_infos_t;
typedef struct { int active, flags; specs_t specs; pfs_blockinfo_t blocks[11]; } hdd_vmc_infos_t;
typedef struct { int mc_flag, page_size, pages_per_block, pages_per_cluster, clusters_per_card; } vmc_superblock_t;
typedef struct { int unused; } config_set_t;
typedef struct { int bdmDeviceType; const char *bdmPrefix; const char *bdmDriver; int bdmHddIsLBA48, ataHighestUDMAMode; } device_t;
typedef device_t bdm_device_data_t;
static device_t device = {0, "mass0:/", "usb", -1, -1};
static int resolves, lba48_answer;
static int bdmDriverIsATA(const char *driver) { return !strcmp(driver, "ata"); }
static void bdmResolveLBA_UDMA(device_t *d) { resolves++; d->bdmHddIsLBA48 = lba48_answer; d->ataHighestUDMAMode = 4; }
static config_set_t config;
static u32 bdm_mcemu_irx[128], hdd_mcemu_irx[128];
static int bdmMcemuSlots[2], hddMcemuSlots[2];
static int size_bdm_mcemu_irx = sizeof(bdm_mcemu_irx), size_hdd_mcemu_irx = sizeof(hdd_mcemu_irx);
static const char *gHDDPrefix = "pfs0:/";
static void *gAutoLaunchBDMGame, *gAutoLaunchGame;
static char names[2][32], messages[4][256];
static int sizes[2], opens[2], closes[2], open_result[2], lba_result[2], write_lba[2], frags[2], maps[2];
static u64 lbas[2];
static int write_extent[2], short_extent[2], wrong_extent[2];
static int finished, prompts, accept, synced_ok, syncs;
static int slot(const char *name) { return strstr(name, "card1") != NULL; }
static int open(const char *name, int flags) { (void)flags; int i=slot(name); opens[i]++; return open_result[i]; }
static int close(int fd) { assert(fd>=10 && fd<=11); closes[fd-10]++; return 0; }
static int ps2sdk_get_iop_fd(int fd) { return fd; }
static int fileXioIoctl2(int fd, int command, void *arg, int arglen, void *out, int outlen) {
    int i=fd-10; assert(i>=0 && i<2 && arg==NULL && arglen==0);
    if (command == USBMASS_IOCTL_GET_LBA) {
        assert(out && outlen==sizeof(u64) && *(u64 *)out==(u64)-1);
        if (write_lba[i]) *(u64 *)out=lbas[i];
        return lba_result[i];
    }
    assert(command==USBMASS_IOCTL_GET_FRAGLIST && out && outlen==sizeof(bd_fragment_t));
    bd_fragment_t *extent=static_cast<bd_fragment_t *>(out);
    assert(extent->sector==(u64)-1 && extent->count==0);
    if (write_extent[i]) {
        extent->sector=lbas[i]+wrong_extent[i];
        extent->count=short_extent[i] ? 1 : 8*2048;
    }
    return frags[i];
}
static int sysCheckVMC(const char *prefix, const char *sep, const char *name, int size, vmc_superblock_t *sb) {
    (void)prefix; (void)sep; assert(size==0); memset(sb,0,sizeof(*sb)); return sizes[slot(name)];
}
static void configGetVMC(config_set_t *cfg, char *name, int length, int i) { (void)cfg; snprintf(name,length,"%s",names[i]); }
static const char *_l(int id) {
    switch(id) {
        case _STR_ERR_VMC_CONTINUE: return "invalid %s slot %d";
        case _STR_ERR_VMC_FRAGMENTED_CONTINUE: return "fragmented %s slot %d";
        case _STR_ERR_VMC_FRAGMENTS_CONTINUE: return "fragmented %s in %d fragments slot %d";
        case _STR_VMC_DEFRAGMENT_HINT: return "Back up the VMC; never delete your only copy";
        case _STR_ERR_VMC_LBA_RANGE_CONTINUE: return "range %s slot %d";
        case _STR_ERR_VMC_LAYOUT_CONTINUE: return "unknown %s slot %d";
        case _STR_ERR_VMC_PFS_LIMIT_CONTINUE: return "maplimit %s slot %d";
    }
    assert(0); return "";
}
static int guiMsgBox(const char *message, int type, void *dialog) {
    assert((type==0 || type==1) && dialog==NULL && prompts<4);
    snprintf(messages[prompts++],sizeof(messages[0]),"%s",message); return accept;
}
static int hddGetFileBlockInfo(const char *path, void *parts, pfs_blockinfo_t *blocks, int max) {
    (void)parts; assert(max==11); memset(blocks,0,max*sizeof(*blocks)); return maps[slot(path)];
}
static int hddSyncForVMCMap(void) { syncs++; return synced_ok; }
@CLASSIFY@
@LIMIT@
@HELPER@
static void run_bdm(void) {
    int fd, iop_fd;
    device_t *pDeviceData=&device;
    config_set_t *configSet=&config;
    item_list_t *itemList=&bdmList;
    @BDM_SLOTS@
    (void)size_mcemu_irx; finished=1;
}
static void run_hdd(void) {
    int part_valid=1, nparts, i, size_mcemu_irx=0, parts[65]={0};
    char vmc_name[2][32]; memcpy(vmc_name,names,sizeof(names));
    hdd_vmc_infos_t hdd_vmc_infos={};
    @HDD_SLOTS@
    (void)size_mcemu_irx; finished=1;
}
static bdm_vmc_infos_t bdm_slot(int i) { bdm_vmc_infos_t v; memcpy(&v,bdm_mcemu_irx+i*64,sizeof(v)); return v; }
static hdd_vmc_infos_t hdd_slot(int i) { hdd_vmc_infos_t v; memcpy(&v,hdd_mcemu_irx+i*64,sizeof(v)); return v; }
static void reset(void) {
    memset(names,0,sizeof(names)); strcpy(names[0],"card0");
    memset(bdm_mcemu_irx,0,sizeof(bdm_mcemu_irx)); memset(hdd_mcemu_irx,0,sizeof(hdd_mcemu_irx));
    for(int i=0;i<2;i++) { bdmMcemuSlots[i]=hddMcemuSlots[i]=-2; bdm_mcemu_irx[i*64]=hdd_mcemu_irx[i*64]=0xC0DEFAC0u+i; }
    memset(opens,0,sizeof(opens)); memset(closes,0,sizeof(closes)); memset(messages,0,sizeof(messages));
    for(int i=0;i<2;i++) { sizes[i]=8; open_result[i]=10+i; lba_result[i]=0; write_lba[i]=write_extent[i]=1; short_extent[i]=wrong_extent[i]=0; frags[i]=1; lbas[i]=100; maps[i]=2; }
    finished=prompts=syncs=0; accept=synced_ok=1; gAutoLaunchGame=gAutoLaunchBDMGame=NULL;
    device.bdmDriver="usb"; device.bdmHddIsLBA48=device.ataHighestUDMAMode=-1; resolves=0; lba48_answer=1;
}
int main(void) {
    reset(); run_bdm(); assert(finished && prompts==0 && bdm_slot(0).active && !bdm_slot(1).active);
    assert(bdm_slot(0).start_sector==100 && opens[0]==1 && closes[0]==1);
    reset(); bdmList.flags=MODE_FLAG_READ_ONLY; run_bdm(); bdmList.flags=0;
    assert(finished && prompts==0 && opens[0]==0 && !bdm_slot(0).active); /* read-only server: no VMC, no question */
    reset(); frags[0]=2; run_bdm(); assert(finished && prompts==1 && !bdm_slot(0).active && closes[0]==1);
    assert(strstr(messages[0],"fragmented card0 in 2 fragments slot 1"));
    for(int bad=-2;bad<=0;bad++) { reset(); frags[0]=bad; run_bdm(); assert(!bdm_slot(0).active && prompts==1 && strstr(messages[0],"unknown") && closes[0]==1); }
    reset(); write_lba[0]=0; run_bdm(); assert(!bdm_slot(0).active && strstr(messages[0],"unknown"));
    reset(); lba_result[0]=-1; run_bdm(); assert(!bdm_slot(0).active && strstr(messages[0],"unknown"));
    reset(); write_extent[0]=0; run_bdm(); assert(!bdm_slot(0).active && strstr(messages[0],"unknown"));
    reset(); short_extent[0]=1; run_bdm(); assert(!bdm_slot(0).active && strstr(messages[0],"unknown"));
    reset(); wrong_extent[0]=1; run_bdm(); assert(!bdm_slot(0).active && strstr(messages[0],"unknown"));
    /* past 2 TiB: mcemu and the in-game drivers take 64-bit sectors; nothing asked of a non-ATA drive */
    reset(); lbas[0]=0x100000000ULL-8*2048+1; run_bdm(); assert(bdm_slot(0).active && prompts==0 && resolves==0);
    reset(); lbas[0]=1ULL<<44; run_bdm(); assert(bdm_slot(0).active && bdm_slot(0).start_sector==1ULL<<44 && prompts==0);
    reset(); lbas[0]=(u64)-2; run_bdm(); assert(!bdm_slot(0).active && strstr(messages[0],"range")); /* no wrap */
    reset(); sizes[0]=INT_MAX; run_bdm(); assert(!bdm_slot(0).active && strstr(messages[0],"unknown")); /* extent too short */
    /* internal drive: 48-bit confirmed (asked once), or 28-bit commands that wrap past 128 GiB */
    reset(); device.bdmDriver="ata"; lbas[0]=1ULL<<40; run_bdm(); assert(bdm_slot(0).active && resolves==1 && prompts==0);
    reset(); device.bdmDriver="ata"; device.bdmHddIsLBA48=1; device.ataHighestUDMAMode=4; lbas[0]=1ULL<<40; run_bdm();
    assert(bdm_slot(0).active && resolves==0);
    reset(); device.bdmDriver="ata"; lba48_answer=0; lbas[0]=(1ULL<<28)-8*2048; run_bdm(); assert(bdm_slot(0).active && resolves==1);
    reset(); device.bdmDriver="ata"; lba48_answer=0; lbas[0]=(1ULL<<28)-8*2048+1; run_bdm();
    assert(!bdm_slot(0).active && strstr(messages[0],"range"));
    reset(); device.bdmDriver="ata"; lba48_answer=0; lbas[0]=1ULL<<28; run_bdm(); assert(!bdm_slot(0).active && strstr(messages[0],"range"));
    reset(); sizes[0]=0; run_bdm(); assert(!bdm_slot(0).active && opens[0]==0 && strstr(messages[0],"invalid"));
    reset(); open_result[0]=-1; run_bdm(); assert(!bdm_slot(0).active && closes[0]==0);
    reset(); frags[0]=2; accept=0; run_bdm(); assert(!finished && prompts==2 && closes[0]==1 && strstr(messages[1],"never delete your only copy"));
    reset(); frags[0]=2; strcpy(names[1],"card1"); run_bdm(); assert(prompts==1 && !bdm_slot(0).active && bdm_slot(1).active && closes[1]==1);
    names[0][0]=names[1][0]=0; run_bdm(); assert(!bdm_slot(0).active && !bdm_slot(1).active);
    reset(); frags[0]=2; gAutoLaunchBDMGame=&config; run_bdm(); assert(finished && prompts==0 && !bdm_slot(0).active);
    reset(); frags[0]=5; device.bdmDriver="ata"; lba48_answer=0; lbas[0]=1ULL<<28; accept=0; run_bdm();
    assert(!finished && prompts==1 && strstr(messages[0],"range")); // range wins over fragmentation; no defrag hint
    reset(); frags[0]=INT_MAX; run_bdm();
    assert(finished && !bdm_slot(0).active && prompts==1 && strstr(messages[0],"2147483647 fragments slot 1"));
    reset(); frags[0]=2; strcpy(names[1],"card1"); frags[1]=7; run_bdm();
    assert(finished && prompts==2 && strstr(messages[0],"2 fragments slot 1") && strstr(messages[1],"7 fragments slot 2"));
    assert(!bdm_slot(0).active && !bdm_slot(1).active);
    reset(); strcpy(names[1],"card1"); frags[1]=3; run_bdm();
    assert(finished && prompts==1 && strstr(messages[0],"card1 in 3 fragments slot 2") && bdm_slot(0).active && !bdm_slot(1).active);
    reset(); sizes[0]=0; strcpy(names[1],"card1"); run_bdm();
    assert(finished && prompts==1 && strstr(messages[0],"invalid") && bdm_slot(1).active);
    reset(); run_hdd(); assert(finished && hdd_slot(0).active && !hdd_slot(1).active && prompts==0);
    reset(); maps[0]=-ENOMEM; run_hdd(); assert(!hdd_slot(0).active && strstr(messages[0],"maplimit"));
    reset(); maps[0]=-EIO; run_hdd(); assert(!hdd_slot(0).active && strstr(messages[0],"unknown"));
    reset(); maps[0]=0; run_hdd(); assert(!hdd_slot(0).active && strstr(messages[0],"unknown"));
    reset(); maps[0]=1; run_hdd(); assert(!hdd_slot(0).active && strstr(messages[0],"unknown")); // inode only
    reset(); synced_ok=0; run_hdd(); assert(syncs==1 && !hdd_slot(0).active && strstr(messages[0],"unknown"));
    reset(); maps[0]=-ENOENT; accept=0; run_hdd(); assert(!finished && prompts==1);
    reset(); maps[0]=-EIO; strcpy(names[1],"card1"); run_hdd(); assert(prompts==1 && !hdd_slot(0).active && hdd_slot(1).active);
    reset(); maps[0]=-EIO; gAutoLaunchGame=&config; run_hdd(); assert(finished && prompts==0 && !hdd_slot(0).active);
    reset(); run_bdm(); names[0][0]=0; run_bdm(); assert(!bdm_slot(0).active);
    reset(); run_hdd(); names[0][0]=0; run_hdd(); assert(!hdd_slot(0).active);
    reset(); run_bdm(); frags[0]=-EIO; run_bdm(); assert(!bdm_slot(0).active);
    reset(); run_hdd(); maps[0]=-EIO; run_hdd(); assert(!hdd_slot(0).active);
    unsigned int tiny[4]={0,0,0,0xC0DEFAC0}; int cache[2]={-2,-2};
    assert(nativeVmcSlotWord(tiny,sizeof(tiny),0,cache,8)==-1);
    assert(nativeVmcSlotWord(tiny,sizeof(tiny),-1,cache,4)==-1);
    assert(nativeVmcSlotWord(tiny,sizeof(tiny),2,cache,4)==-1);
    assert(nativeVmcSlotWord(nullptr,sizeof(tiny),0,cache,4)==-1);
    cache[0]=999; assert(nativeVmcSlotWord(tiny,sizeof(tiny),0,cache,4)==-1);
    puts("native VMC: verified layouts, precise refusals, both slots, cancel and autolaunch pass");
    return 0;
}
'''.replace('@CLASSIFY@', classify).replace('@LIMIT@', limit).replace('@HELPER@', helper).replace('@BDM_SLOTS@', bdm_slots).replace('@HDD_SLOTS@', hdd_slots)

labels = re.findall(r"^- label: (\w+)", (root / 'lng/tmpl/_base.yml').read_text(encoding='utf-8'), re.M)
# External .lng files are positional: these shipped as one appended run; later labels go after them.
at = labels.index('ERR_VMC_LBA_RANGE_CONTINUE')
assert labels[at:at + 4] == ['ERR_VMC_LBA_RANGE_CONTINUE', 'ERR_VMC_LAYOUT_CONTINUE', 'ERR_VMC_PFS_LIMIT_CONTINUE', 'VMC_PFS_LIMIT_ON_CREATE']
assert labels.index('NEUTRINO_DVR_FILE_MISSING') < at

# Appended after the last pre-existing string, in this order, so earlier string IDs keep their values.
# Later strings may follow them.
fragments = labels.index('ERR_VMC_FRAGMENTS_CONTINUE')
assert labels[fragments - 1:fragments + 2] == ['HINT_SHOW_HIDDEN_GAMES', 'ERR_VMC_FRAGMENTS_CONTINUE', 'VMC_DEFRAGMENT_HINT']

with tempfile.TemporaryDirectory() as temp:
    path = Path(temp)
    source = path / 'native_vmc.c'
    exe = path / 'native_vmc.exe'
    source.write_text(program, encoding='utf-8')
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    *(['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if os.name != 'nt' else []),
                    '-I', str(root / 'include'), str(source), '-o', str(exe)], check=True, timeout=30)
    subprocess.run([str(exe)], check=True, timeout=15)
