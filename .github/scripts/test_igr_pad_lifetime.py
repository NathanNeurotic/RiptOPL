"""Execute pad publication/invalidation and VBLANK reads across IOP reset."""
from pathlib import Path
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[2]
source = (Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "ee_core/src/padhook.c").read_text(encoding="utf-8")


def function(signature):
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2]


# Stop before physical-button registers and teardown; execute the actual input reads the VBLANK_END
# handler makes (IGR_CheckInputs). The handler's own RA snapshot call is covered by test_igr_polling.py.
handler = function("static void IGR_CheckInputs(")
handler = handler[:handler.index("#ifdef RETROACHIEVEMENTS", handler.index("if (Pad_Data.pad_buf"))]
handler = handler.replace("static void IGR_CheckInputs(void)", "static int IGR_Intc_Handler(int cause)")
handler = handler.replace("    int intr = DI();", "    (void)cause;")
handler += "    return Pad_Data.combo_type;\n}\n"
prefix = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned char u8;
typedef unsigned short u16;
#define IGR_LIBPAD 1
#define IGR_LIBPAD2 2
#define IGR_PAD_STABLE_V1 6
#define IGR_PAD_STABLE_V2 1
#define IGR_COMBO_R1_L1_R2_L2 0xf0
#define IGR_COMBO_START_SELECT 0xf6
#define IGR_COMBO_R3_L3 0xf9
struct cfg { int IgrAltCombo; };
static struct cfg g_cfg;
#define USE_LOCAL_EECORE_CONFIG struct cfg *config = &g_cfg;
#define kINTC_VBLANK_END 3
#define PADOPEN_CHECK 1
#define DPRINTF(...) ((void)0)
typedef struct {int port,slot;} pad2socketparam_t;
static int checks;
static int Install_PadOpen_Hook(unsigned a,unsigned b,int mode) {
 assert(a==0x100000 && b==0x1ff0000 && mode==PADOPEN_CHECK);checks++;return 0;
}
static int original_port(int p,int slot,void *a) {(void)p;(void)slot;(void)a;return 1;}
static int original_socket(pad2socketparam_t *p,void *a) {(void)p;(void)a;return 2;}
static int (*scePadPortOpen)(int,int,void *)=original_port;
static int (*scePad2CreateSocket)(pad2socketparam_t *,void *)=original_socket;
static int intrEnabled = 1, reads;
static int DI(void) { int previous=intrEnabled;intrEnabled=0;return previous; }
static void EI(void) { intrEnabled=1; }
static void *uncached(void *p) { assert(!intrEnabled);reads++;return p; }
#define UNCACHED_SEG(p) uncached(p)
static struct { u16 libpad,libversion;u8 *pad_buf;int vb_count;
 int pos_combo1,pos_combo2,pos_state,pos_frame;u8 combo_type,prev_frame; } Pad_Data;
static struct { int press,vb_count,latched; } Power_Button;
static int IGR_Thread_ID=-1, IGR_Intc_ID=-1, padOpen_hooked;
static int creates, starts, handlers, enables, deletes;
static int createResult=5, startResult=0, handlerResult=7;
static int _gp;
static u8 IGR_Stack[4096];
#define IGR_STACK_SIZE sizeof(IGR_Stack)
typedef struct { void *gp_reg;void (*func)(void *);void *stack;
 unsigned stack_size;int initial_priority; unsigned attr,option; int current_priority; } ee_thread_t;
static void IGR_Thread(void *p) {(void)p;}
static int CreateThread(ee_thread_t *p) {assert(p->func==IGR_Thread);assert(!p->attr && !p->option && !p->current_priority);creates++;return createResult;}
static int StartThread(int id,void *p) {assert(id==5 && !p);starts++;return startResult;}
static int DeleteThread(int id) {assert(id==5);deletes++;return 0;}
static int AddIntcHandler(int cause,int (*fn)(int),int n) {
 assert(cause==kINTC_VBLANK_END && fn && !n);handlers++;return handlerResult;
}
static void EnableIntc(int n) {assert(n==kINTC_VBLANK_END);enables++;}
#ifdef RETROACHIEVEMENTS
#endif
'''
test = r'''
static int tick(void) {
 int previous=intrEnabled;intrEnabled=0;
 int result=IGR_Intc_Handler(0);
 assert(!intrEnabled);intrEnabled=previous;return result;
}
int main(void) {
 u8 old[128]={0}, fresh[128]={0};
 createResult=-1;Install_IGR();
 assert(IGR_Thread_ID<0 && !starts && !handlers && !enables && !deletes);
 createResult=5;startResult=-1;Install_IGR();
 assert(IGR_Thread_ID<0 && starts==1 && deletes==1 && !handlers && !enables);
 startResult=0;handlerResult=-1;Install_IGR();
 assert(IGR_Thread_ID==5 && IGR_Intc_ID<0 && handlers==1 && !enables);
 int created=creates,started=starts;handlerResult=7;Install_IGR();
 assert(creates==created && starts==started && IGR_Intc_ID==7 && enables==1);
 Install_IGR();assert(creates==created && starts==started && handlers==2 && enables==1);
 IGR_Thread_ID=IGR_Intc_ID=-1;creates=starts=handlers=enables=deletes=0;
 Install_IGR();assert(creates==1 && starts==1 && handlers==1 && enables==1);
 Pad_Data.libpad=IGR_LIBPAD;Pad_Data.libversion=0x160;
 Set_libpad_Params(old);assert(intrEnabled && Pad_Data.pos_state==112);
 old[112]=IGR_PAD_STABLE_V1;old[3]=IGR_COMBO_R1_L1_R2_L2;
 old[2]=IGR_COMBO_START_SELECT;
 Pad_Data.vb_count=7;Pad_Data.prev_frame=44;padOpen_hooked=1;
 Invalidate_PadBuffer();assert(intrEnabled && !Pad_Data.pad_buf && padOpen_hooked==1);
 assert(!Pad_Data.vb_count && !Pad_Data.prev_frame);
 assert(tick()==0 && reads==0); /* reused old memory looks like a reset combo */
 /* Frozen pad frames retain the installed patch across prompts/loading. */
 Pad_Data.combo_type=0;old[2]=0;
 Set_libpad_Params(old);
 for(int i=0;i<33;i++) assert(tick()==0 && padOpen_hooked==1);
 Invalidate_PadBuffer();assert(!Pad_Data.pad_buf && padOpen_hooked==1);
 /* A never-installed hook remains eligible for the normal discovery path. */
 padOpen_hooked=0;Invalidate_PadBuffer();assert(!padOpen_hooked);
 padOpen_hooked=1;
 Power_Button.press=1;Power_Button.vb_count=17;Pad_Data.combo_type=IGR_COMBO_START_SELECT;
 intrEnabled=0;Invalidate_PadBuffer();Install_IGR();
 assert(!intrEnabled && Pad_Data.combo_type==IGR_COMBO_START_SELECT);
 assert(Power_Button.press==1 && Power_Button.vb_count==17);
 assert(creates==1 && starts==1 && handlers==1 && enables==1);
 Pad_Data.vb_count=9;Pad_Data.prev_frame=99;
 Set_libpad_Params(fresh);
 assert(!intrEnabled && !Pad_Data.vb_count && !Pad_Data.prev_frame && Pad_Data.pad_buf==fresh);
 Pad_Data.combo_type=0;int before_reads=reads;
 assert(tick()==0 && reads==before_reads+4 && !intrEnabled);
 fresh[112]=IGR_PAD_STABLE_V1;fresh[3]=IGR_COMBO_R1_L1_R2_L2;
 fresh[2]=IGR_COMBO_START_SELECT;
 assert(tick()==IGR_COMBO_START_SELECT && !intrEnabled);
 intrEnabled=1;Invalidate_PadBuffer();assert(intrEnabled);
 Set_libpad_Params(fresh);assert(intrEnabled);
 /* All supported layout variants restart liveness and publish consistent offsets. */
 Pad_Data.libversion=0x150;Set_libpad_Params(fresh);
 assert(Pad_Data.pos_state==4 && Pad_Data.pos_frame==0 && Pad_Data.pos_combo1==11);
 Pad_Data.libpad=IGR_LIBPAD2;Set_libpad_Params(fresh);
 assert(Pad_Data.pos_state==4 && Pad_Data.pos_frame==124 && Pad_Data.pos_combo1==29);
 memset(fresh,0,sizeof(fresh));Pad_Data.combo_type=0;
 fresh[4]=IGR_PAD_STABLE_V2;fresh[29]=IGR_COMBO_R1_L1_R2_L2;fresh[28]=IGR_COMBO_START_SELECT;
 assert(tick()==IGR_COMBO_START_SELECT && intrEnabled);
 /* #416: with the alternative combo, Start + Select is the game's and R3 + L3 leaves the game. */
 g_cfg.IgrAltCombo=1;Pad_Data.combo_type=0;
 assert(tick()==0);
 fresh[28]=IGR_COMBO_R3_L3;Pad_Data.combo_type=0;
 assert(tick()==IGR_COMBO_START_SELECT);
 g_cfg.IgrAltCombo=0;Pad_Data.combo_type=0;
 assert(tick()==IGR_COMBO_R3_L3);
 fresh[28]=IGR_COMBO_START_SELECT;Pad_Data.combo_type=0;
 padOpen_hooked=0;Pad_Data.combo_type=0;
 assert(Hook_scePadPortOpen(0,0,fresh)==1 && padOpen_hooked==1 && Pad_Data.pad_buf==fresh);
 padOpen_hooked=0;Invalidate_PadBuffer();
 pad2socketparam_t socket={0,0};
 assert(Hook_scePad2CreateSocket(&socket,fresh)==2 && padOpen_hooked==1 && Pad_Data.pad_buf==fresh);
 assert(checks==2);
 puts("PASS: stale pad refusal, reopen layouts, interrupt state and pending reset/power preservation");
}
'''
program = prefix + handler + "\n".join(function(sig) for sig in (
    "void Invalidate_PadBuffer(", "static void Set_libpad_Params(", "void Install_IGR(",
    "static int Hook_scePadPortOpen(", "static int Hook_scePad2CreateSocket(")) + test
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    c = path / "test.c"
    c.write_text(program, encoding="utf-8")
    for defines in ([], ["-DRETROACHIEVEMENTS"]):
        exe = path / "test"
        subprocess.run(["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                        *defines, str(c), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=10)
