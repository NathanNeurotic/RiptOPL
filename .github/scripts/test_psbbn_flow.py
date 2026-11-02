"""Exercise PSBBN Flow geometry, row rendering, cover draws and the theme catalog (original #977/#1023).

PSBBN Flow and Coverflow were separate built-in themes; they are views of <OPL> now (multi-view themes), so
the catalog lists <OPL> and the disk themes only and a saved legacy name resolves to <OPL>.

Compiles the real header, the real draw functions from src/gfx/psbbnflow.cpp, the animation trigger
from src/gfx/coverflow.cpp and the ThemeCatalog methods from src/gfx/themes.cpp against stubs.
"""
from pathlib import Path
import os
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FLOW = (ROOT / 'src/gfx/psbbnflow.cpp').read_text(encoding='utf-8')
COVERFLOW = (ROOT / 'src/gfx/coverflow.cpp').read_text(encoding='utf-8')
THEMES = (ROOT / 'src/gfx/themes.cpp').read_text(encoding='utf-8')
SEMA = (ROOT / 'include/sema_lock.h').read_text(encoding='utf-8')
# The real raii::SemaLock, built on the WaitSema/SignalSema stubs each harness defines.
SEMA_LOCK = SEMA[SEMA.index('namespace raii'):SEMA.index('} // namespace raii') + len('} // namespace raii')]


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]


def run(name, program):
    with tempfile.TemporaryDirectory() as directory:
        folder = Path(directory)
        source = folder / (name + '.cpp')
        binary = folder / (name + ('.exe' if os.name == 'nt' else ''))
        source.write_text(program, encoding='utf-8')
        subprocess.run(['g++', '-std=gnu++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                        '-I', str(ROOT), str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=10)


geometry = r'''
#include <assert.h>
#include <stdio.h>
#include "include/psbbnflow.h"
int main(void) {
 assert(psbbnFlowSize(640,480)==204 && psbbnFlowSize(200,480)==80 && psbbnFlowSize(640,576)>0);
 psbbn_flow_card_t focal=psbbnFlowCard(0,204,204,454,240);
 assert(focal.xl==352 && focal.xr==556 && focal.hl==204 && focal.hr==204 && focal.yc==240);
 assert(focal.shade==128 && focal.alpha==128);
 // The first tail card tucks behind the focal cover's left edge, turned away (left edge shorter).
 psbbn_flow_card_t first=psbbnFlowCard(1000,204,204,454,240);
 assert(first.xr==360 && first.xl==322 && first.hr==106 && first.hl==86 && first.alpha==128);
 // The tail recedes: every card is smaller, further left and no brighter than the one before it.
 psbbn_flow_card_t prev=first;
 for(int slot=2;slot<=PSBBN_FLOW_TAIL;slot++) {
  psbbn_flow_card_t c=psbbnFlowCard(slot*1000,204,204,454,240);
  assert(c.xr<prev.xr && c.xl<prev.xl && c.hr<prev.hr && c.hl<c.hr && c.shade<prev.shade && c.alpha<=prev.alpha);
  assert(c.alpha>0 && c.xl>0 && c.yc==240);
  prev=c;
 }
 // Beyond the visible tail the card has faded out completely; halfway it is half faded in.
 assert(psbbnFlowCard((PSBBN_FLOW_TAIL+1)*1000,204,204,454,240).alpha==0);
 assert(psbbnFlowCard(99000,204,204,454,240).alpha==0);
 assert(psbbnFlowCard(PSBBN_FLOW_TAIL*1000+500,204,204,454,240).alpha==26);
 // Turning toward the viewer interpolates every edge.
 psbbn_flow_card_t turning=psbbnFlowCard(500,204,204,454,240);
 assert(turning.xr<focal.xr && turning.xr>first.xr && turning.hl<turning.hr && turning.hr<204);
 // Outgoing: swells to the upper right and fades out.
 psbbn_flow_card_t out=psbbnFlowCard(-500,204,204,454,240);
 assert(out.alpha==32 && out.hr==357 && out.hl==out.hr && out.xl==362 && out.xr==718 && out.yc==220);
 assert(psbbnFlowCard(-1000,204,204,454,240).alpha==0 && psbbnFlowCard(-5000,204,204,454,240).alpha==0);
 // Widescreen squeezes widths only.
 psbbn_flow_card_t wide=psbbnFlowCard(0,153,204,454,240);
 assert(wide.xr-wide.xl==153 && wide.hr==204);
 assert(psbbnFlowOffset(1000,0)==1000 && psbbnFlowOffset(1000,25)==0);
 assert(psbbnFlowOffset(-3000,12)<0 && psbbnFlowOffset(3000,12)>0 && psbbnFlowOffset(3000,~0u)==0);
 float uv[4];
 psbbnFlowCrop(140,200,uv);assert(uv[0]==0 && uv[2]==140 && uv[1]==30 && uv[3]==170);
 psbbnFlowCrop(256,128,uv);assert(uv[0]==64 && uv[2]==192 && uv[1]==0 && uv[3]==128);
 psbbnFlowCrop(256,256,uv);assert(uv[0]==0 && uv[1]==0 && uv[2]==256 && uv[3]==256);
 float pts[PSBBN_FLOW_ROUND_POINTS*2];
 psbbnFlowRoundRect(10,20,110,220,8,8,pts);
 assert(pts[0]==10 && pts[1]==28 && pts[8]==18 && pts[9]==20);
 for(int i=0;i<PSBBN_FLOW_ROUND_POINTS;i++)
  assert(pts[i*2]>=10 && pts[i*2]<=110 && pts[i*2+1]>=20 && pts[i*2+1]<=220);
 // Clockwise outline: the top edge comes before the right one, which comes before the bottom.
 assert(pts[10]==102 && pts[19]==28 && pts[20]==110 && pts[28]==102 && pts[29]==220 && pts[30]==18);
 puts("PSBBN: focal, tail, fade-in, turn, outgoing, widescreen, crop and rounded outline geometry passed");
}
'''
run('psbbn_geometry', geometry)

state = FLOW[FLOW.index('// Glide state'):FLOW.index('static int focalSize(')]
render = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "include/psbbnflow.h"
typedef unsigned long long u64;
typedef int config_set_t;
typedef struct { int *cache; } mutable_image_t;
typedef struct theme_element { void *extended; int width, posX, posY, font; } theme_element_t;
struct submenu_list { struct { int id; } item; struct submenu_list *prev, *next; };
struct menu_item { struct submenu_list *submenu, *last; };
struct menu_list { struct menu_item *item; };
static int guiFrameId = 100;
static struct { u64 textColor; void *psbbnFlow; } theme, *gTheme = &theme;
static int gCoverflowAnimSpeed, cfIsAnimating, cfAnimDirection, cfAnimFrameCount;
static clock_t cfAnimStartTime;
static int drawn[9], positions[9], rims[9], nDrawn, priorities, frames;
static char counter[32];
#define ALIGN_LEFT 0
static int fntCalcDimensions(int, const char *s) { return strlen(s) * 8; }
static int rmUnScaleX(int x) { return x; }
static int rmWideScale(int x) { return x; }
static int focalSize(const theme_element_t *) { return 204; }
static void drawFrame(struct menu_list *, theme_element_t *) { frames++; }
static void fntRenderString(int, int, int, int, int, int, const char *s, u64) { snprintf(counter, sizeof(counter), "%s", s); }
static theme_element_t *thmGetElemForItem(struct menu_list *, struct submenu_list *, theme_element_t *e) { return e; }
static void *thmGetItemSource(struct menu_list *, struct submenu_list *) { return NULL; }
static void getGameImageTextureEx(int *, void *, void *, int priority) { priorities += priority; }
static void drawCover(struct menu_list *, struct submenu_list *i, theme_element_t *, int p, int rim) {
 assert(nDrawn<9);drawn[nDrawn]=i->item.id;rims[nDrawn]=rim;positions[nDrawn++]=p;
}
@STATE@
@TRIGGER@
@RENDER@
static void checkRows(int count,int selected,int flow) {
 struct submenu_list rows[10]={};
 for(int i=0;i<count;i++) {
  rows[i].item.id=i;rows[i].prev=i?&rows[i-1]:NULL;rows[i].next=i+1<count?&rows[i+1]:NULL;
 }
 struct menu_item page={count?rows:NULL,count?&rows[count-1]:NULL};
 struct menu_list menu={&page}; int cache=1;mutable_image_t image={&cache};
 theme_element_t elem={&image,204,454,240,0};
 nDrawn=priorities=frames=0;counter[0]=0;guiFrameId++;
 startOffset=flow;startFrame=guiFrameId;
 drawPsbbnFlow(&menu,count?&rows[selected]:NULL,NULL,&elem);
 assert(frames==1 && priorities==(count?1:0) && nDrawn<=count && nDrawn<=9);
 int selectedDraws=0,rimDraws=0;
 for(int i=0;i<nDrawn;i++) {
  assert(drawn[i]>=0 && drawn[i]<count);
  selectedDraws+=drawn[i]==selected;
  rimDraws+=rims[i];
  if(rims[i]) assert(drawn[i]==selected && positions[i]==0);
  for(int j=0;j<i;j++) assert(drawn[i]!=drawn[j]);
 }
 assert(selectedDraws==(count?1:0));
 int moving=count>=2 && flow!=0;
 assert(rimDraws==(count && !moving ? 1 : 0));
 assert(psbbnFlowIsMoving()==moving);
 if(count<2) {assert(startOffset==0);if(count)assert(positions[0]==0);}
 if(count && !moving) {
  char expected[32];snprintf(expected,sizeof(expected),"%d/%d",selected+1,count);
  assert(!strcmp(counter,expected));
 } else
  assert(!counter[0]);
}
int main(void) {
 // PSBBN Flow keeps its own glide when Coverflow's animation is off, and arms Coverflow too.
 gTheme->psbbnFlow=&theme;gCoverflowAnimSpeed=0;
 thmTriggerCoverflowAnim(1);assert(startOffset==1000 && !cfIsAnimating);
 thmTriggerCoverflowAnim(-1);assert(startOffset==0);
 gCoverflowAnimSpeed=220;cfAnimFrameCount=99;
 thmTriggerCoverflowAnim(-1);
 assert(startOffset==-1000 && cfAnimDirection==-1 && cfAnimFrameCount==0 && cfIsAnimating);
 for(int i=0;i<5;i++)thmTriggerCoverflowAnim(-1);
 assert(startOffset==-3000);
 guiFrameId+=25;thmTriggerCoverflowAnim(1);assert(startOffset==1000);
 gTheme->psbbnFlow=NULL;thmTriggerCoverflowAnim(-1);
 assert(startOffset==1000 && cfAnimDirection==-1);
 for(int n=0;n<=10;n++)for(int s=0;s<(n?n:1);s++)for(int f=-3000;f<=3000;f+=1000)
  checkRows(n,s,f);
 // A theme load drops a glide in flight and shows the title again.
 startOffset=2000;movingFrame=guiFrameId;psbbnFlowReset();assert(!startOffset && !psbbnFlowIsMoving());
 puts("PSBBN: both glide directions, empty/single/short lists, wrap deduplication, rim and resting count passed");
}
'''
render = (render.replace('@STATE@', state)
          .replace('@TRIGGER@', function(COVERFLOW, 'void thmTriggerCoverflowAnim('))
          .replace('@RENDER@', function(FLOW, 'static void drawPsbbnFlow(')))
run('psbbn_render', render)

draws = '\n'.join([
    FLOW[FLOW.index('constexpr int PSBBN_FLOW_STRIPS'):FLOW.index('// Glide state')],
    function(FLOW, 'static int focalSize('), function(FLOW, 'static void vertex('),
    function(FLOW, 'static void drawRing('), function(FLOW, 'static void drawTile('),
    function(FLOW, 'static void drawCard('), function(FLOW, 'static void drawCover('),
])
covers = r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include "include/psbbnflow.h"
typedef unsigned long long u64;
typedef struct {int Width,Height;void *Mem;} GSTEXTURE;
typedef struct {GSTEXTURE source;int upperLeft_x,upperLeft_y,upperRight_x,upperRight_y,lowerLeft_x,lowerLeft_y,lowerRight_x,lowerRight_y;} image_texture_t;
typedef struct {int *cache;image_texture_t *defaultTexture,*overlayTexture,*overlayTexture2;} mutable_image_t;
typedef struct theme_element {int width,height,posX,posY;void *extended;} theme_element_t;
struct submenu_list {int item;};
static int gThmScreenWidth=640,gThmScreenHeight=480,hidden,overlays,batches,tris,textured,wDraw,hDraw;
static float uMin,uMax,vMin,vMax;
static GSTEXTURE *loaded,*drawn;
static theme_element_t *redirect;
#define GS_SETREG_RGBA(r,g,b,a) ((u64)(r)|((u64)(g)<<8)|((u64)(b)<<16)|((u64)(a)<<24))
#define ALIGN_CENTER 3
#define SCALING_RATIO 1
#define COVER_DEFAULT 0
static GSTEXTURE fallback={128,256,(void *)1};
static GSTEXTURE *thmGetTexture(int) {return &fallback;}
static int rmWideScale(int x) {return x;}
static int isHiddenArtworkCache(int *) {return hidden;}
static theme_element_t *thmGetElemForItem(struct menu_list *,struct submenu_list *,theme_element_t *e) {return redirect?redirect:e;}
static void *thmGetItemSource(struct menu_list *,struct submenu_list *) {return NULL;}
static GSTEXTURE *getGameImageTexture(int *,void *,int *) {return loaded;}
static void rmDrawBlendedTriangles(GSTEXTURE *t,int count,const float *,const float *uv,const u64 *) {
 batches++;tris+=count;
 if(!t) {assert(uv==NULL);return;}
 textured++;drawn=t;
 for(int i=0;i<count*3;i++) {
  uMin=uv[i*2]<uMin?uv[i*2]:uMin;uMax=uv[i*2]>uMax?uv[i*2]:uMax;
  vMin=uv[i*2+1]<vMin?uv[i*2+1]:vMin;vMax=uv[i*2+1]>vMax?uv[i*2+1]:vMax;
 }
}
static void rmDrawPixmap(GSTEXTURE *,int,int,int,int w,int h,int,u64,int) {wDraw=w;hDraw=h;}
static void rmDrawOverlayPixmap(GSTEXTURE *,int,int,int,int w,int h,int,u64,GSTEXTURE *,
 int,int,int urx,int,int,int lly,int lrx,int lry,int) {
 assert(urx==w && lly==h && lrx==w && lry==h);overlays++;wDraw=w;hDraw=h;
}
@DRAWS@
static void reset(void) {batches=tris=textured=0;drawn=NULL;uMin=vMin=1e9f;uMax=vMax=-1e9f;}
int main(void) {
 struct menu_list *menu=NULL;struct submenu_list row={0};int cache=0;
 mutable_image_t image={&cache,NULL,NULL,NULL};theme_element_t elem={204,204,454,240,&image};
 GSTEXTURE portrait={128,256,(void *)1},square={256,256,(void *)1},empty={256,256,NULL};
 // Settled selection: rim ring, rounded fan cropped to the centre square, inner rim.
 reset();loaded=&portrait;drawCover(menu,&row,&elem,0,1);
 assert(batches==3 && textured==1 && drawn==&portrait && tris==PSBBN_FLOW_ROUND_POINTS*5);
 assert(uMin==0 && uMax==128 && vMin==64 && vMax==192);
 reset();drawCover(menu,&row,&elem,0,0);assert(batches==1 && tris==PSBBN_FLOW_ROUND_POINTS);
 // Turned tail card: perspective strips spanning the whole crop.
 reset();loaded=&square;drawCover(menu,&row,&elem,1000,0);
 assert(batches==1 && tris==2*PSBBN_FLOW_STRIPS && uMin==0 && uMax==256 && vMin==0 && vMax==256);
 // Outgoing covers stay flat; fully faded or off-tail cards draw nothing.
 reset();drawCover(menu,&row,&elem,-400,0);assert(batches==1 && tris==PSBBN_FLOW_ROUND_POINTS);
 reset();drawCover(menu,&row,&elem,-1000,0);assert(batches==0);
 reset();drawCover(menu,&row,&elem,(PSBBN_FLOW_TAIL+1)*1000,0);assert(batches==0);
 reset();loaded=&empty;drawCover(menu,&row,&elem,0,0);assert(drawn==&fallback);
 image_texture_t app={{256,256,(void *)1},0,0,0,0,0,0,0,0};
 mutable_image_t appImage={&cache,&app,NULL,NULL};theme_element_t appElem={204,204,454,240,&appImage};
 reset();redirect=&appElem;loaded=NULL;drawCover(menu,&row,&elem,0,0);assert(drawn==&app.source);
 redirect=NULL;
 image_texture_t overlay={{256,256,(void *)1},0,0,204,0,0,204,204,204};
 reset();image.overlayTexture=&overlay;loaded=&portrait;drawCover(menu,&row,&elem,0,1);
 assert(overlays==1 && batches==0 && wDraw==204 && hDraw==204);
 reset();hidden=1;drawCover(menu,&row,&elem,0,0);assert(batches==0 && overlays==1);
 hidden=0;image.overlayTexture=NULL;square.Width=0;loaded=&square;
 reset();drawCover(menu,&row,&elem,0,0);assert(batches==0);
 puts("PSBBN: rounded square crop, rim, turned card, fades, missing art, family fallback, overlays and hidden art passed");
}
'''.replace('@DRAWS@', draws)
run('psbbn_covers', covers)

header = (ROOT / 'include/themecatalog.h').read_text(encoding='utf-8')
catalog_class = header[header.index('class ThemeCatalog'):header.index('\n};', header.index('class ThemeCatalog')) + 3]
methods = '\n'.join([
    function(THEMES, s) for s in ('void ThemeCatalog::rebuildNames(', 'int ThemeCatalog::addElements(',
                                  'void ThemeCatalog::reinit(', 'int ThemeCatalog::setGuiValue(',
                                  'int ThemeCatalog::setGuiValueLocked(',
                                  'int ThemeCatalog::findGuiID(', 'char *ThemeCatalog::filePath(',
                                  'int ThemeCatalog::guiValue(', 'const char **ThemeCatalog::guiList(')])
registry = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#define LOG(...) ((void)0)
constexpr int THM_MAX_FILES = 16;
#define CONFIG_OPL 0
#define MODE_COUNT 0
typedef struct {char *name,*filePath;} theme_file_t;
static int loads,loaded,colors,failLoad,locks,gTheme;
// The theme switch lock (thmSwitchSema): a scan, a removal and every theme load hold it. It is not recursive.
static int thmSwitchSema=5,switchDepth;
static void WaitSema(int id) {assert(id==thmSwitchSema && !switchDepth);switchDepth++;}
static void SignalSema(int id) {assert(id==thmSwitchSema && switchDepth);switchDepth--;}
@SEMALOCK@
static int configGetByType(int t) {return t;}
static int configGetStr(int,const char *,const char **) {return 0;}
struct Ui { void lock() {assert(!locks);locks++;} void unlock() {assert(locks);locks--;} };
struct App { void moduleUpdateMenu(int,int,int) {} };
namespace host { static Ui u; static App a; Ui &ui() {return u;} App &app() {return a;} }
static void thmSetColors(int) {colors++;}
static int thmLoad(const char *path);
@CLASS@
static ThemeCatalog gCatalog;
static int thmLoad(const char *path) {
 assert(switchDepth==1 && !locks); // one theme switch at a time; thmLoad takes the GUI lock itself, for the swap
 loads++;loaded=path?3:0;
 return failLoad?-1:0;
}
typedef int (*entry_cb)(int, const char *, const char *, const char *, unsigned char);
static theme_file_t *slots;
static int *counter;
static int listDir(const char *,const char *,int,entry_cb) {
 slots[*counter].name=strdup("C");slots[*counter].filePath=strdup("mass1:/THM/C/");return 1;
}
int ThemeCatalog::readEntryCallback(int,const char *,const char *,const char *,unsigned char) {return 0;}
@METHODS@
int main(void) {
 ThemeCatalog &c=gCatalog;
 slots=c.themes_;counter=&c.count_;
 c.themes_[0].name=strdup("A");c.themes_[0].filePath=strdup("mass0:/THM/A/");
 c.themes_[1].name=strdup("B");c.themes_[1].filePath=strdup("mc0:/THM/B/");
 c.count_=2;
 c.addElements((char *)"none","/",0); // publishes the name list (and adds C at id 3)
 c.reinit("mass1:");
 const char **names=c.guiList();
 assert(!strcmp(names[0],"<OPL>") && !strcmp(names[1],"A") && !strcmp(names[2],"B") && !names[3]);
 // The old built-in names are views of <OPL> now: they resolve to id 0.
 assert(c.findGuiID("<psbbn flow>")==0 && c.findGuiID("<Coverflow>")==0 && c.findGuiID("b")==2);
 assert(c.setGuiValue(2,0) && loaded==3 && c.guiValue()==2 && c.filePath(2));
 failLoad=1;assert(!c.setGuiValue(1,0) && c.guiValue()==2);failLoad=0;
 assert(c.setGuiValue(0,0) && loaded==0 && !c.filePath(0));
 c.setGuiValue(0,0);assert(colors==1); // same id, no reload: re-applies the picker palette
 int oldLoads=loads;
 c.reinit("mc0:");assert(c.count()==1 && c.guiValue()==0 && loads==oldLoads);
 assert(c.setGuiValue(1,0) && loaded==3);
 oldLoads=loads;
 // A disk theme appearing keeps the live selection.
 assert(c.addElements((char *)"mass1:","/",0)==1 && c.guiValue()==1 && loads==oldLoads);
 assert(!strcmp(c.guiList()[c.guiValue()],"A") && !strcmp(c.guiList()[2],"C") && !c.guiList()[3]);
 // Removing another device re-finds the active theme by name; removing its own device drops to <OPL>.
 c.reinit("mass1:");assert(c.guiValue()==1 && loads==oldLoads);
 c.reinit("mass0:");assert(c.count()==0 && c.guiValue()==0 && loads==oldLoads+1 && loaded==0);
 assert(!c.setGuiValue(99,0) && !c.setGuiValue(1,0) && c.guiValue()==0);
 assert(!switchDepth && !locks);
 puts("Theme catalog: <OPL> + disk themes, legacy names, palette, failed-load rollback and discovery/removal passed");
}
'''
# The test seeds the theme table the scanner would fill, so it reaches the private members.
registry = (registry.replace('@CLASS@', catalog_class.replace('private:', 'public:'))
            .replace('@METHODS@', methods).replace('@SEMALOCK@', SEMA_LOCK))
run('psbbn_registry', registry)

# The built-in config, embedded asset and wiring.
cfg = (ROOT / 'misc/theme_psbbn_flow.cfg').read_text(encoding='utf-8')
assert cfg.count('type=PSBBNFlow') == 3 and cfg.count('default=psbbn_bg') == 2
assert not re.search(r'^(?:bg|text|ui|sel)_color=', cfg, re.M), 'the built-in keeps the picker palette'
assets = (ROOT / 'tools/assets.py').read_text(encoding='utf-8')
assert '"misc/theme_psbbn_flow.cfg", "theme_psbbn_flow_cfg"' in assets and re.search(r'\bpsbbn_bg\b', assets)
assert '{PSBBN_BACKGROUND, "psbbn_bg", &psbbn_bg_png}' in (ROOT / 'src/gfx/textures.cpp').read_text(encoding='utf-8')
assert 'src/gfx/psbbnflow.cpp' in (ROOT / 'ps2.yaml').read_text(encoding='utf-8')
assert '"PSBBNFlow"' in THEMES and 'initPsbbnFlow(themePath, themeConfig, theme, elem, name);' in THEMES
items = (ROOT / 'src/gfx/themeelems.cpp').read_text(encoding='utf-8')
assert 'submenu_list_t *ps = psbbnFlowIsMoving() ? NULL : menu->item->pagestart;' in items, 'the title row hides while covers move'
assert 'int editable = thmGetFilePath(themeID) == NULL;' in (ROOT / 'src/ui/guisettings.cpp').read_text(encoding='utf-8')
png = (ROOT / 'gfx/psbbn_bg.png').read_bytes()
assert png[:8] == b'\x89PNG\r\n\x1a\n' and png[24] == 8, 'psbbn_bg.png must be an 8-bit PNG'
assert struct.unpack('>II', png[16:24]) == (160, 120)
print('PSBBN: built-in config, embedded nebula asset and theme wiring passed')

# Hold L3 through the real thmCycleView: Coverflow (3) and (5) are one theme view, so moving between them
# changes only gCoverflowCount; a view change loads the theme, and an abandoned load keeps the old count.
views_src = (ROOT / 'src/gfx/themeviews.cpp').read_text(encoding='utf-8')
picks = views_src[views_src.index('static const signed char pickViews'):]
picks = picks[:picks.index('const char *thmViewPrefix(')]
cycle = r'''
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64; typedef int32_t s32;
#include "include/themeviews.h"
struct theme_t { unsigned int viewMask; int view; };
static theme_t theme;
static theme_t *gTheme = &theme;
static int gCoverflowCount = 3;
static int loads, failLoad, loadedCovers;
static int thmSwitchSema = 5, switchDepth;
static void WaitSema(int id) { assert(id == thmSwitchSema && !switchDepth); switchDepth++; }
static void SignalSema(int id) { assert(id == thmSwitchSema && switchDepth); switchDepth--; }
@SEMALOCK@
// thmCycleView holds the theme switch lock across its look at gTheme and the reload.
static int thmSetViewLocked(int v) {
 assert(switchDepth == 1);
 loads++;
 if (failLoad) return 0;
 loadedCovers = gCoverflowCount;
 gTheme->view = v;
 return 1;
}
@PICKS@
@CYCLE@
int main(void) {
 // A theme that offers only Coverflow still toggles 3 and 5, with no reload.
 theme.viewMask = 1u << THM_VIEW_COVERFLOW; theme.view = THM_VIEW_COVERFLOW; gCoverflowCount = 3;
 assert(thmCycleView() == 1 && gCoverflowCount == 5 && loads == 0);
 assert(thmCycleView() == 1 && gCoverflowCount == 3 && loads == 0);
 // A single-view theme without Coverflow has nothing to cycle.
 theme.viewMask = 1u << THM_VIEW_LIST; theme.view = THM_VIEW_LIST;
 assert(thmCycleView() == 0 && loads == 0);
 // Every view: List -> CF3 (load) -> CF5 (no load) -> PSBBN (load) -> Grid (load) -> List (load).
 theme.viewMask = 0xFu; theme.view = THM_VIEW_LIST; gCoverflowCount = 5;
 assert(thmCycleView() == 1 && theme.view == THM_VIEW_COVERFLOW && gCoverflowCount == 3 && loads == 1 && loadedCovers == 3);
 assert(thmCycleView() == 1 && theme.view == THM_VIEW_COVERFLOW && gCoverflowCount == 5 && loads == 1);
 assert(thmCycleView() == 1 && theme.view == THM_VIEW_PSBBN && loads == 2);
 assert(thmCycleView() == 1 && theme.view == THM_VIEW_GRID && loads == 3);
 assert(thmCycleView() == 1 && theme.view == THM_VIEW_LIST && loads == 4);
 // An abandoned load keeps the old view and its count.
 theme.viewMask = (1u << THM_VIEW_LIST) | (1u << THM_VIEW_COVERFLOW); theme.view = THM_VIEW_LIST; gCoverflowCount = 5; failLoad = 1;
 assert(thmCycleView() == 0 && theme.view == THM_VIEW_LIST && gCoverflowCount == 5 && loads == 5);
 assert(!switchDepth);
 puts("L3 cycle: Coverflow (3)/(5) toggle without a reload, view changes load, a failed load keeps the count");
}
'''
run('psbbn_cycle', cycle.replace('@PICKS@', picks).replace('@SEMALOCK@', SEMA_LOCK)
    .replace('@CYCLE@', function(THEMES, 'int thmCycleView(void)')))
