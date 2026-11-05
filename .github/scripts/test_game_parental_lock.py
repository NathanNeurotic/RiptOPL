"""Compile parental-lock policy and Test Changes launch against controlled UI/config.

Checks cancellation, incorrect/correct/master passwords, repeated game prompts,
settings-unlock independence, unsaved lock removal, newly locked test configs,
allocation failure, and command-line launch integration guards.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
menu = (root / 'src/ui/menusys.cpp').read_text(encoding='utf-8')
game = (root / 'src/ui/guigame.cpp').read_text(encoding='utf-8')
items = (root / 'src/ui/menuitems.cpp').read_text(encoding='utf-8')
autolaunch = (root / 'src/app/autolaunch.cpp').read_text(encoding='utf-8')
fav = (root / 'src/sources/favsupport.cpp').read_text(encoding='utf-8')
config = (root / 'src/core/config.cpp').read_text(encoding='utf-8')

def function(source, sig):
    start = source.index(sig)
    return source[start:source.index('\n}', start)+2]

helpers = '\n'.join(function(menu, sig) for sig in (
    'static const char *menuGetParentalLockPassword(', 'int menuIsParentalLockPasswordSet(',
    'static int menuPromptParentalLockPassword(', 'int menuCheckParentalLock(',
    'int menuGameRequiresParentalLock(', 'int menuCheckGameParentalLock('))

select = function(items, 'static void itemExecSelect(')
assert select.index('menuCheckGameParentalLock(configSet)') < select.index('guiGameAutoCreateVmc(') < select.index('gsOf(support)->launch(')
assert 'strcmp(startup, gameIdStartup) == 0' in select[select.index('menuCheckGameParentalLock('):select.index('guiGameAutoCreateVmc(')]

for name, launch in [('autoLaunchHDDGame', 'hddLaunchGame'), ('autoLaunchBDMGame', 'bdmLaunchGame')]:
    source = function(autolaunch, 'void '+name+'(')
    assert 'if (!menuGameRequiresParentalLock(configSet))\n        '+launch+'(NULL, -1, configSet);' in source
    assert 'miniDeinit(configSet);' in source

# Favorites retain the selected game's config when forwarding to its owner.
fav_fn = function(fav, 'void FavSource::launch(')
assert 'gsOf(o)->launch(o, fav->id, configSet);' in fav_fn

save_branch = menu[menu.index('} else if (menuID == GAME_SAVE_CHANGES)'):menu.index('} else if (menuID == GAME_TEST_CHANGES)')]
assert save_branch.index('if (changed < 0)') < save_branch.index('menuSaveConfig()')

assert 'menuGameRequiresParentalLock(configSet) ? menuCheckGameParentalLock(configSet)' in function(game, 'void guiGameRemoveSettings(')

main_input = function(menu, 'void menuHandleInputMain(')
assert 'DisableCron == 0' in main_input and 'DisableCron = 1;' in main_input

harness = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define CONFIG_OPL 1
#define CONFIG_KEY_VALUE_LEN 64
#define CONFIG_OPL_PARENTAL_LOCK_PWD 1
#define CONFIG_ITEM_PARENTAL_LOCK 2
#define COMPAT_PARENTAL_LOCK 3
#define diaCompatConfig 1
#define OPL_PARENTAL_LOCK_MASTER_PASS "989765"
#define _STR_PARENLOCK_ENTER_PASSWORD_TITLE 3
#define _STR_PARENLOCK_DISABLE_WARNING 4
#define _STR_PARENLOCK_PASSWORD_INCORRECT 5
#define _STR_PARENLOCK_GAME_NO_PASSWORD 6
#define _STR_INVALID_CONSOLE_ILINK_ID 7
static int id_invalid;
static int guiGameReadILinkID(char out[17]) {out[0]=0;return id_invalid?-1:0;}
#define _l(x) (x)
typedef struct {int locked;char password[64];} config_set_t;
static config_set_t global, cloned;
static int parentalLockCheckEnabled=1, prompts, accept=1, notices, saved, launches, edits, frees, failClone, editLock, failInsert;
static const char *answer;
static config_set_t *configGetByType(int type) {assert(type==CONFIG_OPL);return &global;}
static int configGetStr(config_set_t *c,int key,const char **out) {assert(key==1);*out=c->password;return 1;}
static int configGetInt(config_set_t *c,int key,int *out) {assert(key==2);*out=c->locked;return 1;}
static void configRemoveKey(config_set_t *c,int key) {if(key==2)c->locked=0;else {assert(key==1);c->password[0]=0;}}
static int configSetInt(config_set_t *c,int key,int value) {assert(key==2);if(failInsert)return 0;c->locked=value;return 1;}
static int diaShowKeyb(char *out,int size,int hidden,int title) {
    assert(size==64 && hidden && title==3);prompts++;snprintf(out,size,"%s",answer);return accept;
}
static void guiMsgBox(int text,int flags,void *callback) {(void)text;(void)flags;(void)callback;notices++;}
static void saveConfig(int type,int flags) {assert(type==1 && flags==1);saved++;}
@HELPERS@
struct item_list { int dummy; };
typedef struct item_list item_list_t;
struct game_source_ops {void (*launch)(item_list_t *,int,config_set_t *);};
static void launch(item_list_t *s,int id,config_set_t *c) {(void)s;(void)c;assert(id==7);launches++;}
static struct game_source_ops source_ops={launch};
static inline struct game_source_ops *gsOf(item_list_t *s) {(void)s;return &source_ops;}
static config_set_t *configClone(config_set_t *c) {if(failClone)return NULL;cloned=*c;return &cloned;}
static void configFree(config_set_t *c) {assert(c==&cloned);frees++;}
static int guiGameApplyConfig(config_set_t *c,item_list_t *s) {(void)s;edits++;c->locked=editLock;return 1;}
static void diaGetInt(int dialog,int key,int *out) {assert(dialog==1&&key==3);*out=editLock;}
@PREPARE@
@SAVE_CONFIG@
@TEST_LAUNCH@
static void reset(void) {
    memset(&global,0,sizeof(global));strcpy(global.password,"secret");
    answer="secret";accept=1;parentalLockCheckEnabled=1;id_invalid=0;
    prompts=notices=saved=launches=edits=frees=failClone=editLock=failInsert=0;
}
int main(void) {
    config_set_t locked={1,""},unlocked={0,""};item_list_t support={0};
    reset();assert(!menuCheckGameParentalLock(&unlocked) && !prompts);
    assert(!menuCheckGameParentalLock(NULL) && !prompts);
    assert(menuGameRequiresParentalLock(&locked));
    assert(!menuCheckParentalLock() && prompts==1 && !parentalLockCheckEnabled);
    assert(!menuCheckGameParentalLock(&locked) && prompts==2);
    assert(!menuCheckGameParentalLock(&locked) && prompts==3);
    accept=0;assert(menuCheckGameParentalLock(&locked)==EACCES);
    accept=1;answer="wrong";assert(menuCheckGameParentalLock(&locked)==EACCES);
    answer=OPL_PARENTAL_LOCK_MASTER_PASS;assert(!menuCheckGameParentalLock(&locked));
    assert(!global.password[0] && saved==1 && !menuGameRequiresParentalLock(&locked));
    reset();accept=0;editLock=0;guiGameTestSettings(7,&support,&locked);
    assert(!launches && !edits && locked.locked==1); /* unsaved unlock cannot bypass stored lock */
    reset();editLock=0;guiGameTestSettings(7,&support,&locked);
    assert(launches==1 && prompts==1 && edits==1 && frees==1 && locked.locked==1);
    reset();editLock=1;accept=0;guiGameTestSettings(7,&support,&unlocked);
    assert(!launches && !edits && prompts==1 && frees==1 && unlocked.locked==0);
    reset();editLock=1;guiGameTestSettings(7,&support,&unlocked);
    assert(launches==1 && prompts==1 && unlocked.locked==0);
    reset();failClone=1;editLock=0;accept=0;guiGameTestSettings(7,&support,&locked);
    assert(!launches && !edits && locked.locked==1);
    reset();failClone=1;editLock=0;guiGameTestSettings(7,&support,&locked);
    assert(!launches && !edits && locked.locked==1 && prompts==1);
    reset();failClone=1;editLock=1;guiGameTestSettings(7,&support,&unlocked);
    assert(!launches && !edits && unlocked.locked==0);
    reset();answer="wrong";editLock=0;assert(guiGameSaveConfig(&locked,&support)<0);
    assert(locked.locked==1 && edits==0 && prompts==1);
    reset();accept=0;editLock=0;assert(guiGameSaveConfig(&locked,&support)<0);
    assert(locked.locked==1 && edits==0);
    reset();editLock=0;assert(guiGameSaveConfig(&locked,&support)>0);
    assert(locked.locked==0 && edits==1 && prompts==1);locked.locked=1;
    reset();editLock=1;assert(guiGameSaveConfig(&locked,&support)>0);
    assert(locked.locked==1 && edits==1 && prompts==0);
    reset();failInsert=1;editLock=1;assert(guiGameSaveConfig(&unlocked,&support)<0);
    assert(!edits && !unlocked.locked);
    guiGameTestSettings(7,&support,&unlocked);assert(!edits && !launches);
    reset();global.password[0]=0;assert(!menuGameRequiresParentalLock(&locked));
    assert(!menuCheckGameParentalLock(&locked) && !prompts);
    reset();id_invalid=1;editLock=0;locked.locked=1;
    assert(guiGameSaveConfig(&locked,&support)<0&&locked.locked==1&&!edits&&notices==1);
    puts("PASS: repeated prompts, cancel/wrong/master passwords, test edits and autolaunch guards");
    return 0;
}
""".replace('@PREPARE@', function(game, 'static int guiGamePrepareParentalLock(')).replace('@HELPERS@', helpers).replace('@SAVE_CONFIG@', function(game, 'int guiGameSaveConfig(')).replace('@TEST_LAUNCH@', function(game, 'void guiGameTestSettings('))

with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / 'parental.c'
    path.write_text(harness, encoding='utf-8')
    binary = path.with_suffix('.exe')
    flags = ['-std=gnu99', '-Wall', '-Wextra', '-Werror']
    if os.name != 'nt':
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
    subprocess.run([os.environ.get('CC', 'gcc'), *flags, str(path), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=10)

allocation = r"""
#include <assert.h>
#include <string.h>
#define CONFIG_KEY_NAME_LEN 32
#define CONFIG_KEY_VALUE_LEN 64
struct config_value_t {
    char key[32], val[64];
    config_value_t *next;
};
class ConfigSet {
    config_value_t *head_{nullptr}, *tail_{nullptr};
    int modified_{0};
    static int fail;
    static config_value_t item;
    static int configKeyValidate(const char *key) { (void)key; return 1; }
    static config_value_t *allocConfigItem(const char *key, const char *val) {
        if (fail) return nullptr;
        memset(&item, 0, sizeof(item));
        strcpy(item.key, key);
        strcpy(item.val, val);
        return &item;
    }
public:
    bool append(const char *key, const char *val);
    config_value_t *find(const char *name) const;
    int setStr(const char *key, const char *value);
    static void runTest() {
        ConfigSet c;
        assert(!c.setStr("$ParentalLock", "1") && !c.head_ && !c.tail_ && !c.modified_);
        fail = 0;
        assert(c.setStr("$ParentalLock", "1") && c.head_ == &item && c.tail_ == &item && c.modified_);
        c.modified_ = 0;
        fail = 1;
        assert(!c.setStr("another", "1") && c.tail_ == &item && !item.next && !c.modified_);
        assert(c.setStr("$ParentalLock", "0") && !strcmp(item.val, "0"));
    }
};
int ConfigSet::fail = 1;
config_value_t ConfigSet::item = {};
@FUNCTIONS@
int main(void) {
    ConfigSet::runTest();
    return 0;
}
""".replace('@FUNCTIONS@', '\n'.join(function(config, sig) for sig in (
    'bool ConfigSet::append(', 'config_value_t *ConfigSet::find(', 'int ConfigSet::setStr(')))

with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / 'allocation.cpp'
    source.write_text(allocation, encoding='utf-8')
    binary = source.with_suffix('.exe')
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++17', '-Wall', '-Werror', str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=10)
print('PASS: production config insertion fails without mutating list or modified flag')
