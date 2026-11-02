"""A language switch never leaves a Settings row drawing freed strings, and an unsaved pick survives a device coming up.

A language switch frees the strings of the language it leaves. The GUI thread swaps on the Language page; the IO
thread swaps when a device with language files comes up (lngAddLanguages -> lngPublishLanguages re-applies the
saved language_text) and for a settings load. Rows that kept _l() pointers from the frame that opened them drew
freed memory in the next frame, and the publish undid a pick that had not been saved yet.

This compiles the real src/core/lang.cpp, the real enum and label text lookups (src/ui/dia.cpp), the real setters
(src/ui/diaitems.cpp), the real Settings Index label array (src/ui/guisettings.cpp SettingsShell::showIndex) and two
real enum lists of the Interface page, against host stubs for the file system, the config, fonts and the kernel
semaphores. free() is a shim that overwrites the block with a FREED marker and keeps it allocated, so a row that
reads freed strings is caught every time, without a sanitizer. The threads take turns, as on the EE, where the IO
worker (priority 32) runs only while the GUI thread (31) waits between frames.

Scenarios:
1. Boot applies the saved language; the user picks another on the Language page and does not save; a device with
   language files comes up. The pick stays, and the enum rows, the labels and the Index draw its live strings.
2. A settings load on the IO thread switches language while the Index and a page are open: the next frame draws the
   new language's strings, not the freed ones of the old.
3. The saved language's file is on a device that comes up after boot: when it does, the saved language is applied.

The steps the harness takes for the Language page and for a settings load are the calls those make; the source
checks below hold them to it. --asan builds with -fsanitize=address and the real free() instead of the shim.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
failures = []


def read(rel):
    return (root / rel).read_text(encoding='utf-8').replace('\r\n', '\n')


def blank_includes(text):
    return '\n'.join('' if line.lstrip().startswith('#include') else line for line in text.split('\n'))


def whole(rel):
    return '#line 1 "%s"\n%s\n' % (rel, blank_includes(read(rel)))


def block_close(text, start):
    """Index of the '}' closing the first '{' at or after start (comments and literals skipped)."""
    depth = 0
    i, n = start, len(text)
    while i < n:
        c = text[i]
        if text.startswith('//', i):
            i = text.find('\n', i)
            i = n if i < 0 else i
            continue
        if text.startswith('/*', i):
            i = text.find('*/', i + 2) + 2
            continue
        if c in '"\'':
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == '\\' else 1
            i = j + 1
            continue
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise SystemExit('unbalanced braces after offset %d' % start)


def by_signature(rel, signature, tail=''):
    """The definition that starts with the line `signature`, verbatim, with a #line marker."""
    text = read(rel)
    hits = [m.start() for m in re.finditer(r'^' + re.escape(signature) + r'\s*$', text, re.M)]
    if len(hits) != 1:
        failures.append('%s: "%s" found %d times' % (rel, signature, len(hits)))
        return ''
    start = hits[0]
    end = block_close(text, start)
    if tail:
        end = text.index(tail, end) + len(tail) - 1
    return '#line %d "%s"\n%s\n' % (text.count('\n', 0, start) + 1, rel, text[start:end + 1])


def declaration(rel, function, pattern):
    """The statement in `function` that matches pattern (a regex for its start), up to its ';', with a #line marker."""
    text = read(rel)
    at = text.find(function)
    if at < 0:
        failures.append('%s: %s not found' % (rel, function))
        return ''
    body_end = block_close(text, at)
    m = re.compile(pattern, re.M).search(text, at, body_end)
    if not m:
        failures.append('%s: %s no longer declares /%s/' % (rel, function, pattern))
        return ''
    end = text.index(';', text.index('}', m.end()) if '{' in text[m.start():m.end() + 1] else m.end())
    line_start = text.rfind('\n', 0, m.start()) + 1
    return '#line %d "%s"\n%s\n' % (text.count('\n', 0, line_start) + 1, rel, text[line_start:end + 1])


SETTINGS = 'src/ui/guisettings.cpp'
INDEX_FN = 'int SettingsShell::showIndex(int *page)'
INTERFACE_FN = 'static int guiSettingsShowInterface(void)'

extracted = ''.join([
    whole('include/lang.h'),
    '#line 1 "include/sema_lock.h (namespace raii)"\n',
    (lambda s: s[s.index('namespace raii'):s.index('} // namespace raii') + len('} // namespace raii')])(
        read('include/sema_lock.h')) + '\n',
    whole('src/core/lang.cpp'),
    by_signature('include/dia.h', 'typedef enum {', tail=';'),
    by_signature('include/dia.h', 'struct UIItem', tail=';'),
    by_signature('src/ui/diaitems.cpp', 'struct UIItem *diaFindByID(struct UIItem *ui, int id)'),
    by_signature('src/ui/diaitems.cpp', 'int diaSetLabel(struct UIItem *ui, int id, const char *text)'),
    by_signature('src/ui/diaitems.cpp', 'int diaSetLabelId(struct UIItem *ui, int id, int stringId)'),
    by_signature('src/ui/diaitems.cpp',
                 'int diaSetEnumIds(struct UIItem *ui, int id, const char **enumvals, const int *enumids)'),
    by_signature('src/ui/diaitems.cpp', 'int diaSetEnum(struct UIItem *ui, int id, const char **enumvals)'),
    by_signature('src/ui/dia.cpp', 'static const char *diaGetLocalisedText(const char *def, int id)'),
    by_signature('src/ui/dia.cpp', 'static const char *diaLabelText(const struct UIItem *item)'),
    by_signature('src/ui/dia.cpp', 'static const char *diaEnumEntryText(const struct UIItem *item, int index)'),
    by_signature('src/ui/dia.cpp', 'static const char *diaEnumText(const struct UIItem *item)'),
    by_signature('src/ui/guisettings.cpp', 'enum gui_settings_page {', tail=';'),
])

# The Settings Index's label array and two of the Interface page's enum lists, as the source declares them. They go
# inside the harness functions that open the Index and the page, where the source has them.
index_decl = declaration(SETTINGS, INDEX_FN, r'^\s*[^;\n]*\blabels\[SETTINGS_PAGE_COUNT \+ 1\]\s*=')
row_decls = ''.join(declaration(SETTINGS, INTERFACE_FN, r'^\s*[^;\n]*\b%s\[\]\s*=' % name)
                    for name in ('sortNames', 'sortNameIds', 'ps1DupNames', 'ps1DupNameIds'))


def ids_in(decl):
    return re.findall(r'\b_STR_[A-Z0-9_]+\b', re.sub(r'^#line.*$', '', decl, flags=re.M))


index_ids = ids_in(index_decl)
if not index_ids:
    failures.append('%s: the Settings Index label array names no _STR_ ids' % SETTINGS)

# ---- source checks: the code the harness models by its calls ----
settings = read(SETTINGS)
index_body = settings[settings.find(INDEX_FN):block_close(settings, settings.find(INDEX_FN)) + 1]
frame_open = index_body.find('guiStartFrame();')
frame_close = index_body.find('guiEndFrame();', frame_open)
lookups = [m.start() for m in re.finditer(r'_l\(labels\[i\]\)', index_body)]
if not lookups:
    failures.append('%s: the Settings Index no longer looks its labels up as _l(labels[i])' % SETTINGS)
for at in lookups:
    if not frame_open < at < frame_close:
        failures.append('%s: the Settings Index looks a label up outside guiStartFrame()..guiEndFrame()' % SETTINGS)
if re.search(r'(?<!_l\()\blabels\[i\]', index_body.replace('_l(labels[i])', '')):
    failures.append('%s: the Settings Index draws labels[i] without looking it up in the frame' % SETTINGS)

language_fn = 'static int guiSettingsShowLanguage(void)'
language_body = settings[settings.find(language_fn):block_close(settings, settings.find(language_fn)) + 1]
pick = language_body.find('lngSetUserPicked(1);')
apply = language_body.find('applyConfig(-1, langID, 1);')
if pick < 0 or apply < 0 or pick > apply:
    failures.append('%s: the Language page must call lngSetUserPicked(1) before applyConfig(-1, langID, 1)' % SETTINGS)

settingsio = read('src/app/settingsio.cpp')
m = re.search(r'if \(configGetStr\(configOPL, CONFIG_OPL_LANGUAGE, &temp\)\) \{(.*?)\n\s*\}', settingsio, re.S)
if not m or 'langID = lngFindGuiID(temp);' not in m.group(1) or 'lngSetUserPicked(0);' not in m.group(1):
    failures.append('src/app/settingsio.cpp: a settings load that names the language must call lngSetUserPicked(0)')

HARNESS = r'''
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define LOG(...) ((void)0)

/* free() keeps the block and overwrites it with FREED: a later read finds the marker, never reused memory. */
static void *g_freed[1 << 14];
static int g_freedCount;
static void shimFree(void *p)
{
    if (p == NULL)
        return;
    size_t n = malloc_usable_size(p);
    memset(p, 0, n);
    memcpy(p, "FREED", n < 6 ? n : 6);
    assert(g_freedCount < (int)(sizeof(g_freed) / sizeof(g_freed[0])));
    g_freed[g_freedCount++] = p;
}
static int wasFreed(const void *p)
{
    for (int i = 0; i < g_freedCount; i++)
        if (g_freed[i] == p)
            return 1;
    return 0;
}
#ifndef REAL_FREE
#define free shimFree
#endif

@ENUM@

/* opl.h */
char gBootDir[256] = ""; /* empty: lngScanBootDirectory returns at once */
char gBaseMCDir[] = "mc0:OPL";

/* The GUI lock: a binary semaphore, never taken twice. */
static int g_guiLock;
namespace host
{
struct Ui
{
    void lock() { assert(!g_guiLock && "GUI lock taken twice"); g_guiLock = 1; }
    void unlock() { assert(g_guiLock && "GUI lock released while free"); g_guiLock = 0; }
};
struct App
{
    void moduleUpdateMenu(int, int, int) {}
};
static Ui &ui() { static Ui u; return u; }
static App &app() { static App a; return a; }
} // namespace host

/* EE kernel semaphores, binary as lngInit makes lngSwitchSema: waiting on a held one would block for ever. */
typedef struct
{
    int count, max_count, init_count, wait_threads;
    unsigned int attr, option;
} ee_sema_t;
static int g_sema[8], g_semaNext = 1;
static int CreateSema(ee_sema_t *s) { g_sema[g_semaNext] = s->init_count; return g_semaNext++; }
static int WaitSema(int id) { assert(g_sema[id] > 0 && "the language switch lock is not recursive"); g_sema[id]--; return id; }
static int SignalSema(int id) { g_sema[id]++; return id; }

/* The config: only language_text. */
struct config_set_t
{
    int unused;
};
#define CONFIG_OPL 1
static config_set_t g_config;
static const char *g_saved; /* NULL: no language_text */
static config_set_t *configGetByType(int) { return &g_config; }
static int configGetStr(config_set_t *, const char *key, const char **value)
{
    if (strcmp(key, "language_text") != 0 || g_saved == NULL)
        return 0;
    *value = g_saved;
    return 1;
}
static const char *configGetHomePath(void) { return ""; }

static void fntInit(void) {}
static int fntLoadDefault(const char *) { return -1; }
static int fntLoadFallback(const char *) { return -1; }
static int thmReload(void) { return 1; }
static void bgmMute(void) {}
static void bgmUnMute(void) {}

/* Language files: line 0 is the language's name, line i the string of id i, "<prefix>:<id name>". */
struct FakeLang
{
    const char *path, *dir, *file, *name, *prefix;
};
static const FakeLang kLangs[] = {
    {"mc0:OPL/lang_French.lng", "mc0:OPL", "lang_French.lng", "French", "fr"},
    {"mc0:OPL/lang_German.lng", "mc0:OPL", "lang_German.lng", "German", "de"},
    {"mass0:LNG/lang_Spanish.lng", "mass0:LNG", "lang_Spanish.lng", "Spanish", "es"},
};
typedef struct
{
    const FakeLang *lang;
    int next;
} file_buffer_t;
static file_buffer_t *openFileBuffer(char *path, int, short, unsigned int)
{
    for (const FakeLang &l : kLangs) {
        if (strcmp(l.path, path) == 0) {
            file_buffer_t *b = (file_buffer_t *)malloc(sizeof(*b));
            b->lang = &l;
            b->next = 0;
            return b;
        }
    }
    return NULL;
}
/* util.cpp readFileBuffer with allocResult 1: each line it returns is malloc'd, and lngFreeFromFile frees it. */
static int readFileBuffer(file_buffer_t *b, char **out)
{
    char line[96];
    if (b->next >= LANG_STR_COUNT)
        return 0;
    if (b->next == 0)
        snprintf(line, sizeof(line), "%s", b->lang->name);
    else
        snprintf(line, sizeof(line), "%s:%s", b->lang->prefix, kIdName[b->next]);
    b->next++;
    *out = strdup(line);
    return 1;
}
static int closeFileBuffer(file_buffer_t *b)
{
    free(b);
    return 0;
}
/* smb0:LNG lists nothing: a device without language files. */
static int listDir(char *path, const char *separator, int maxElem,
                   int (*readEntry)(int index, const char *path, const char *separator, const char *name, unsigned char d_type))
{
    int index = 0;
    for (const FakeLang &l : kLangs)
        if (index < maxElem && strcmp(l.dir, path) == 0)
            index = readEntry(index, path, separator, l.file, DT_REG);
    return index;
}

@EXTRACTED@

/* ---- the harness ---- */
static int g_failures;

static const char *prefixOf(const char *language)
{
    if (strcmp(language, "English") == 0)
        return "en";
    for (const FakeLang &l : kLangs)
        if (strcmp(l.name, language) == 0)
            return l.prefix;
    return "??";
}

/* What a row must draw now: the live string of `id` in `language`. */
static void expectText(const char *where, const char *text, const char *language, int id)
{
    char want[96];
    snprintf(want, sizeof(want), "%s:%s", prefixOf(language), kIdName[id]);
    if (text == NULL || wasFreed(text) || strncmp(text, "FREED", 5) == 0) {
        printf("FAIL %s: draws freed memory (want \"%s\")\n", where, want);
        g_failures++;
    } else if (strcmp(text, want) != 0) {
        printf("FAIL %s: draws \"%s\", want \"%s\"\n", where, text, want);
        g_failures++;
    }
}

static void expectLiteral(const char *where, const char *text, const char *want)
{
    if (text == NULL || strcmp(text, want) != 0) {
        printf("FAIL %s: draws \"%s\", want \"%s\"\n", where, text ? text : "(null)", want);
        g_failures++;
    }
}

static void expectLanguage(const char *when, const char *language)
{
    if (strcmp(lngGetValue(), language) != 0) {
        printf("FAIL %s: the language is %s, want %s\n", when, lngGetValue(), language);
        g_failures++;
    }
}

enum { ROW_SORT = 1, ROW_PS1DUP, ROW_STATUS, ROW_BUTTON };
static struct UIItem g_page[5];
static int g_sortCurrentId, g_ps1DupId;

/* Settings > Interface opens: two of its enum lists as the source declares them, and two labels. */
static void openPage(void)
{
@ROWS@
    memset(g_page, 0, sizeof(g_page));
    g_page[0].type = UI_ENUM;
    g_page[0].id = ROW_SORT;
    g_page[1].type = UI_ENUM;
    g_page[1].id = ROW_PS1DUP;
    g_page[2].type = UI_LABEL;
    g_page[2].id = ROW_STATUS;
    g_page[2].label.stringId = -1;
    g_page[3].type = UI_BUTTON;
    g_page[3].id = ROW_BUTTON;
    g_page[3].label.stringId = -1;
    g_page[4].type = UI_TERMINATOR;
    diaSetEnumIds(g_page, ROW_SORT, sortNames, sortNameIds);
    g_page[0].intvalue.current = 1;
    g_sortCurrentId = sortNameIds[1];
    diaSetEnumIds(g_page, ROW_PS1DUP, ps1DupNames, ps1DupNameIds);
    g_page[1].intvalue.current = 0;
    g_ps1DupId = ps1DupNameIds[0];
    assert(g_ps1DupId >= 0 && ps1DupNameIds[1] == -1);
    /* the VMC status: a formatted text, then a translated one by id (the last setter wins) */
    diaSetLabel(g_page, ROW_STATUS, "formatted");
    diaSetLabelId(g_page, ROW_STATUS, g_sortCurrentId);
    diaSetLabelId(g_page, ROW_BUTTON, g_ps1DupId);
}

/* One locked frame of the page: diaRenderUI reads every row's text through these. */
static void pageFrame(const char *when, const char *language)
{
    char where[96];
    host::ui().lock();
    snprintf(where, sizeof(where), "%s, sort row", when);
    expectText(where, diaEnumText(&g_page[0]), language, g_sortCurrentId);
    snprintf(where, sizeof(where), "%s, PS1 duplicates row", when);
    expectText(where, diaEnumText(&g_page[1]), language, g_ps1DupId);
    snprintf(where, sizeof(where), "%s, status label", when);
    expectText(where, diaLabelText(&g_page[2]), language, g_sortCurrentId);
    snprintf(where, sizeof(where), "%s, button label", when);
    expectText(where, diaLabelText(&g_page[3]), language, g_ps1DupId);
    host::ui().unlock();
}

/* A literal entry of a translated list, and a list set again without ids, show their own text. */
static void literalRows(void)
{
    static const char *literals[] = {"one", "two", NULL};
    g_page[1].intvalue.current = 1;
    expectLiteral("PS1 duplicates row, a literal entry", diaEnumText(&g_page[1]), "Ember");
    g_page[1].intvalue.current = 0;
    diaSetEnum(g_page, ROW_SORT, literals);
    expectLiteral("sort row set again by diaSetEnum", diaEnumText(&g_page[0]), "two");
    diaSetLabel(g_page, ROW_STATUS, "formatted");
    expectLiteral("status label set again by diaSetLabel", diaLabelText(&g_page[2]), "formatted");
}

/* The Index draws each label as the source does: _l(labels[i]) for an id, the pointer itself otherwise. */
static const char *indexText(int id) { return _l(id); }
static const char *indexText(const char *text) { return text; }
static const int kIndexIds[] = {@INDEX_IDS@};

template <typename T, size_t N>
static void indexFrame(const T (&labels)[N], const char *when, const char *language)
{
    char where[96];
    assert(N == sizeof(kIndexIds) / sizeof(kIndexIds[0]));
    host::ui().lock();
    for (size_t i = 0; i < N; i++) {
        snprintf(where, sizeof(where), "%s, Settings Index row %d", when, (int)i);
        expectText(where, indexText(labels[i]), language, kIndexIds[i]);
    }
    host::ui().unlock();
}

/* settingsio.cpp _loadConfig names a language (lngSetUserPicked(0)); its applyConfig switches to it. */
static void settingsLoad(const char *language)
{
    g_saved = language;
    lngSetUserPicked(0);
    lngSetGuiValue(lngFindGuiID(language));
}

/* guisettings.cpp guiSettingsShowLanguage applies a pick: lngSetUserPicked(1), then applyConfig's switch. */
static void languagePagePick(const char *language)
{
    lngSetUserPicked(1);
    lngSetGuiValue(lngFindGuiID(language));
}

/* 1: an unsaved pick, then a device with language files comes up. */
static void pickSurvivesDevice(void)
{
    lngInit();
    settingsLoad("German");
    expectLanguage("after boot", "German");
    languagePagePick("French");
    expectLanguage("after the pick", "French");
@INDEX@
    openPage();
    pageFrame("before the device", "French");
    indexFrame(labels, "before the device", "French");
    lngAddLanguages((char *)"mass0:LNG", "/", 2); /* IO thread, between two frames */
    expectLanguage("after the device came up", "French");
    pageFrame("after the device", "French");
    indexFrame(labels, "after the device", "French");
    literalRows();
}

/* 2: a settings load on the IO thread while the Index and a page are open. */
static void switchUnderOpenRows(void)
{
    lngInit();
    settingsLoad("French");
@INDEX@
    openPage();
    pageFrame("before the load", "French");
    indexFrame(labels, "before the load", "French");
    settingsLoad("German"); /* IO thread, between two frames: frees the French strings */
    expectLanguage("after the load", "German");
    pageFrame("after the load", "German");
    indexFrame(labels, "after the load", "German");
}

/* 3: the saved language's file comes up after boot. */
static void deferredApply(void)
{
    lngInit();
    settingsLoad("Spanish"); /* no Spanish file yet: English */
    expectLanguage("at boot", "English");
    openPage();
    pageFrame("at boot", "English");
    lngAddLanguages((char *)"smb0:LNG", "/", 1); /* a device without language files */
    expectLanguage("after a device without language files", "English");
    lngAddLanguages((char *)"mass0:LNG", "/", 2);
    expectLanguage("after the device with the saved language", "Spanish");
    pageFrame("after the deferred apply", "Spanish");
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    int scenario = argc > 1 ? atoi(argv[1]) : 0;
    if (scenario == 1)
        pickSurvivesDevice();
    else if (scenario == 2)
        switchUnderOpenRows();
    else if (scenario == 3)
        deferredApply();
    else
        return 2;
    assert(!g_guiLock && g_sema[1] == 1);
    return g_failures ? 1 : 0;
}
'''

ids = sorted(set(re.findall(r'\b_STR_[A-Z0-9_]+\b', extracted + index_decl + row_decls)))
names = ['_STR_HARNESS_LANGUAGE_NAME'] + ids
enum = 'enum lang_strings {\n%s,\n    LANG_STR_COUNT\n};\n' % ',\n'.join('    ' + n for n in names)
enum += 'static const char *kIdName[LANG_STR_COUNT] = {%s};\n' % ', '.join('"%s"' % n for n in names)
enum += 'char *internalEnglish[LANG_STR_COUNT] = {(char *)"English", %s};\n' % ', '.join(
    '(char *)"en:%s"' % n for n in ids)

source = (HARNESS.replace('@ENUM@', enum)
          .replace('@EXTRACTED@', extracted + '@RESYNC@')
          .replace('@ROWS@', row_decls + '@RESYNC@')
          .replace('@INDEX@', index_decl + '@RESYNC@')
          .replace('@INDEX_IDS@', ', '.join(index_ids)))
# The lines after an excerpt are the harness's own again: number them as rows.cpp, so a failure or a sanitizer report
# in the harness is not put down to the file the excerpt came from.
lines = source.split('\n')
for i, line in enumerate(lines):
    if line == '@RESYNC@':
        lines[i] = '#line %d "rows.cpp"' % (i + 2)
source = '\n'.join(lines)

if failures:
    print('\n'.join(failures))
    sys.exit(1)

asan = '--asan' in sys.argv[1:]
flags = ['-std=c++17', '-g']
if asan:
    flags += ['-fsanitize=address', '-fno-omit-frame-pointer', '-DREAL_FREE']
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path / 'rows.cpp').write_text(source, encoding='utf-8')
    built = subprocess.run(['c++'] + flags + [str(path / 'rows.cpp'), '-o', str(path / 'rows.exe')],
                           capture_output=True, text=True)
    if built.returncode != 0:
        print(built.stdout + built.stderr)
        sys.exit('the harness does not compile')
    for scenario, what in ((1, 'an unsaved pick, then a device with language files'),
                           (2, 'a settings load while the Index and a page are open'),
                           (3, 'the saved language comes up after boot')):
        run = subprocess.run([str(path / 'rows.exe'), str(scenario)], capture_output=True, text=True)
        if run.returncode != 0:
            failures.append('scenario %d (%s):\n%s%s' % (scenario, what, run.stdout, run.stderr))

if failures:
    print('\n'.join(failures))
    sys.exit(1)
print('ok: Settings rows, labels and the Index draw the live language in every frame; an unsaved pick survives a '
      'device coming up; the saved language is applied when its file comes up after boot%s'
      % (' (AddressSanitizer, real free)' if asan else ''))
