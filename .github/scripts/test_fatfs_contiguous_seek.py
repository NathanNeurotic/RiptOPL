"""Seek and read a contiguous exFAT file on a disposable image with the shipped FatFs.

f_lseek jumps straight to the target cluster for a file with no FAT chain (obj.stat 2) opened for
reading. Every offset must land on the same bytes as before, forward and back, at cluster edges and
the end, and a file that is not contiguous, or open for writing, must still take the normal path.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
program = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ff.h"
#include "diskio.h"
static unsigned char *disk;
static const unsigned int sectors=262144; /* 128 MiB */
DSTATUS disk_initialize(BYTE n) {(void)n;return 0;}
DSTATUS disk_status(BYTE n) {(void)n;return 0;}
DRESULT disk_read(BYTE n,BYTE *out,LBA_t sector,UINT count) {
    (void)n;if(sector>=sectors||count>sectors-sector)return RES_ERROR;
    memcpy(out,disk+(size_t)sector*512,(size_t)count*512);return RES_OK;
}
DRESULT disk_write(BYTE n,const BYTE *in,LBA_t sector,UINT count) {
    (void)n;if(sector>=sectors||count>sectors-sector)return RES_ERROR;
    memcpy(disk+(size_t)sector*512,in,(size_t)count*512);return RES_OK;
}
DRESULT disk_ioctl(BYTE n,BYTE cmd,void *out) {
    (void)n;
    switch(cmd) {
    case CTRL_SYNC:return RES_OK;
    case GET_SECTOR_COUNT:*(LBA_t*)out=sectors;return RES_OK;
    case GET_SECTOR_SIZE:*(WORD*)out=512;return RES_OK;
    case GET_BLOCK_SIZE:*(DWORD*)out=1;return RES_OK;
    default:return RES_PARERR;
    }
}
DWORD get_fattime(void) {return (2026-1980U)<<25|11U<<21|25U<<16;}
int ff_mutex_create(int n) {(void)n;return 1;}
void ff_mutex_delete(int n) {(void)n;}
int ff_mutex_take(int n) {(void)n;return 1;}
void ff_mutex_give(int n) {(void)n;}

static uint32_t word_at(FSIZE_t ofs) {return (uint32_t)(ofs/4)*2654435761u ^ 0x5A5A5A5Au;}

/* The file's bytes are a pattern of 4-byte words, so any offset says what it must read. */
static void write_pattern(const char *path,FSIZE_t size,int contiguous,FIL *gap) {
    FIL f;UINT done;static uint32_t buf[16384];
    assert(f_open(&f,path,FA_CREATE_ALWAYS|FA_WRITE|FA_READ)==FR_OK);
    if(contiguous) assert(f_expand(&f,size,1)==FR_OK);
    for(FSIZE_t ofs=0;ofs<size;ofs+=sizeof(buf)) {
        for(unsigned int i=0;i<sizeof(buf)/4;i++) buf[i]=word_at(ofs+i*4);
        UINT n=(UINT)((size-ofs)<sizeof(buf)?(size-ofs):sizeof(buf));
        assert(f_write(&f,buf,n,&done)==FR_OK&&done==n);
        if(gap!=NULL&&ofs==0) { /* interleave another file so this one fragments */
            static uint32_t junk[4096];
            assert(f_write(gap,junk,sizeof(junk),&done)==FR_OK&&done==sizeof(junk));
            assert(f_sync(gap)==FR_OK);
        }
    }
    assert(f_close(&f)==FR_OK);
}

static void check_at(FIL *f,FSIZE_t ofs,FSIZE_t size) {
    unsigned char got[12];UINT n;
    assert(f_lseek(f,ofs)==FR_OK);
    assert(f_tell(f)==(ofs<size?ofs:size));
    assert(f_read(f,got,sizeof(got),&n)==FR_OK);
    FSIZE_t want=ofs<size?((size-ofs)<sizeof(got)?(size-ofs):sizeof(got)):0;
    assert(n==want);
    for(UINT i=0;i<n;i++) {
        FSIZE_t at=ofs+i;uint32_t w=word_at(at);
        assert(got[i]==((unsigned char *)&w)[at%4]);
    }
}

static void check_file(const char *path,FSIZE_t size,BYTE fs_type,int expect_contiguous,BYTE mode,int stale_stat) {
    FIL f;DWORD bcs;
    assert(f_open(&f,path,mode)==FR_OK);
    if(stale_stat) f.obj.stat=2; /* a reused FIL slot keeps an old exFAT flag; FAT must still walk its chain */
    assert(f_size(&f)==size);
    if(fs_type==FS_EXFAT) assert((f.obj.stat==2)==expect_contiguous);
    bcs=(DWORD)f.obj.fs->csize*512;
    FSIZE_t edges[]={0,1,511,512,bcs-1,bcs,bcs+1,2*bcs,3*bcs-5,size/2,size/2+bcs,size-bcs,size-12,size-1,size,size+4096};
    for(unsigned int i=0;i<sizeof(edges)/sizeof(edges[0]);i++) if(!(mode&FA_WRITE)||edges[i]<=size) check_at(&f,edges[i],size); /* write mode would grow the file */
    for(int i=(int)(sizeof(edges)/sizeof(edges[0]))-1;i>=0;i--) if(!(mode&FA_WRITE)||edges[i]<=size) check_at(&f,edges[i],size); /* backwards */
    srand(547);
    for(int i=0;i<400;i++) check_at(&f,((FSIZE_t)rand()*7919u)%(size+1),size);
    assert(f_close(&f)==FR_OK);
}

static void run(BYTE fmt,BYTE fs_type) {
    FATFS fs;FIL gap;BYTE work[FF_MAX_SS];MKFS_PARM opt={.fmt=fmt|FM_SFD,.n_fat=1,.align=1,.n_root=512,.au_size=fmt==FM_FAT32?512:4096}; /* FAT32 needs 65526+ clusters */
    const FSIZE_t size=40u*1048576u+1234u; /* not a whole number of clusters */
    memset(disk,0,(size_t)sectors*512);
    assert(f_mkfs("0:",&opt,work,sizeof(work))==FR_OK);
    assert(f_mount(&fs,"0:",1)==FR_OK);
    if(fs.fs_type!=fs_type){fprintf(stderr,"fs_type %d want %d\n",fs.fs_type,fs_type);abort();}
    write_pattern("0:/one.iso",size,1,NULL);
    assert(f_open(&gap,"0:/gap.bin",FA_CREATE_ALWAYS|FA_WRITE)==FR_OK);
    write_pattern("0:/frag.iso",size,0,&gap);
    assert(f_close(&gap)==FR_OK);
    check_file("0:/one.iso",size,fs_type,1,FA_READ,0);
    check_file("0:/frag.iso",size,fs_type,0,FA_READ,0);
    if(fs_type!=FS_EXFAT) check_file("0:/frag.iso",size,fs_type,0,FA_READ,1);
    check_file("0:/one.iso",size,fs_type,1,FA_READ|FA_WRITE,0); /* write mode keeps the chain walk */
    assert(f_unmount("0:")==FR_OK);
}

int main(void) {
    disk=calloc(sectors,512);assert(disk!=NULL);
    run(FM_EXFAT,FS_EXFAT);
    run(FM_FAT32,FS_FAT32);
    free(disk);
    puts("FatFs seek: contiguous exFAT jumps land on the right bytes forward, back, at cluster edges and the end; fragmented, write-mode and FAT32 files unchanged");
    return 0;
}
'''
fatfs = root / 'modules/bdm/bdmfs_fatfs/fatfs'
ff = (fatfs / 'ff.c').read_text()
assert 'fs->fs_type == FS_EXFAT && fp->obj.stat == 2 && !(fp->flag & FA_WRITE)' in ff, 'the contiguous-file seek shortcut is missing'
with tempfile.TemporaryDirectory() as temp:
    build = Path(temp)
    for source_file in [fatfs / 'ff.c', fatfs / 'ffunicode.c', *sorted((fatfs / 'include').glob('*.h'))]:
        shutil.copyfile(source_file, build / source_file.name)
    (build / 'module_debug.h').write_text('#include <stdint.h>\ntypedef uint64_t u64;\n#define M_DEBUG(...) ((void)0)\n#define M_PRINTF(...) ((void)0)\n#define DEBUG_U64_2XU32(x) ((void)0)\n')
    config = build / 'ffconf.h'
    text = config.read_text()
    assert re.search(r'#define FF_USE_FASTSEEK\s+0\b', text), 'this test covers the shipped no-fast-seek path'
    text, changed = re.subn(r'(#define FF_USE_MKFS\s+)[01]\b', r'\g<1>1', text)  # test images only
    assert changed == 1
    config.write_text(text)
    # MinGW defines _WIN32 but the library's VC-only integer branch is not GCC.
    header = build / 'ff.h'
    header.write_text(header.read_text().replace('#if defined(_WIN32)', '#if defined(_MSC_VER)'))
    c, exe = build / 'seek.c', build / 'seek.exe'
    c.write_text(program)
    subprocess.run(['gcc', '-std=gnu99', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-sign-compare',
                    '-Wno-unused-function', '-I', str(build), str(c), str(build / 'ff.c'), str(build / 'ffunicode.c'),
                    '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=120)
