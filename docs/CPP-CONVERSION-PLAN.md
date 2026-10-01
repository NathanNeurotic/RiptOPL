# Plan: idiomatic C++ for src/ (classes, constexpr, visibility, decoupling)

Status: plan; phase 0 (safety net) done -- see section 8. Baseline = `ps2build` branch after the mechanical .c -> .cpp rename (commit
`ec17be0`). Numbers come from `tools/analyze_coupling.py` (regex heuristics, not a parser) and greps.

## 1. What the code looks like today

| Measure | Value |
|---|---|
| src/*.cpp / lines | 55 modules / ~62k (opl 5.4k, gui 5.3k, themes 4.0k, bdmsupport 3.6k, hddsupport 2.7k, supportbase 2.6k, vcdsupport 2.3k, system 2.3k) |
| Modules in ONE dependency cycle | **43 of 55** (acyclic: atlas, dialogs, httpcatalog, httpclient, ioprp, launchdiag, nbns, ps2cnf, rahash, renderman, tar, vmc_groups) |
| Upward (layer-violating) symbol deps | **224** across 88 module pairs (layering in section 3) |
| Extern globals in headers | 192; **137 in opl.h** alone (settings), 35 in dialogs.h (dialog tables) |
| File-scope `static` variables | ~730 |
| Non-static functions no other file references | **84** (gui 14, vcdsupport 6, hddsupport 5, supportbase 5, OSDHistory 5, tar 4, cuesupport 4, mmcesupport 4 ...) |
| `#define` numeric constants / func-like macros | ~406 / ~24 |
| malloc-family / free / goto / open+close | 123 / 308 / 46 / 130+154 |
| Semaphore ops (manual lock/unlock) | 133 |
| Giant functions | `sendIrxKernelRAM` 1468 lines, `bdmLaunchGame` 385, `thmLoad` 350, `drawCoverFlow` 295, `_loadConfig` 452, `_saveConfig` 302, `setDefaults` 198 |
| Hand-rolled vtable | `item_list_t`: 8 backends x ~25 function pointers, plus `void *priv`, `void *owner`; ~100 call sites |
| Hand-rolled vtable #2 | `menu_item_t` (6 callbacks + `void *userdata`), theme elements, `UIItem` dialogs |

Hot spots of coupling (symbol uses, most referenced first): config 928, dia 844, lang 494, gui 381,
opl 226, supportbase 209. The worst edges:

- Support backends -> `opl`: `initSupport/deinit/deinitEx/miniDeinit/applyConfig/saveConfig/
  oplPath2Mode/clearErrorMessageIf/setErrorMessage` (bdm 12, hdd 8, app 8, mmce 6, eth 5, udpfs 4).
  The backend calls back into the application that owns it.
- `themes` -> `gui` (9: `guiDrawIconAndText`, `guiDrawBGPlasma`, `guiAlignMenuHints` ...), `menusys`
  (5), `vcdsupport` (5: `vcdDisplayName*`), `opl` (3). The renderer depends on the UI that depends on it.
- Backends -> `libview` (`libViewMarkDirty`, `libViewActive`, `libViewConsumeDirty`), backends -> `gui`
  (`guiMsgBox`, `guiSetBootStatusSticky`, `guiHandleDeferedIO`).
- `system` (the lowest layer: IOP reset, module loading, ee_core config blob) -> `rawatch`, `cheatman`
  (`GetWatchList`, `GetCheatsList`, ...): the launch core reaches up into two feature modules.
- `util` -> `gui`; `menusys` -> `opl` (`moduleUpdateMenuInternal`, `saveConfig`).

## 2. Target design (what "good" looks like here)

Constraints that shape every choice: EE has ~32 MB and no heap to waste, so no exceptions, no RTTI,
no `std::string` in hot paths; virtual dispatch and small RAII wrappers are free by comparison. The
compiler default is `gnu++17` (g++ 15): `constexpr` if/lambdas, inline variables, structured bindings,
`std::string_view`, `std::array`, `std::optional` are available. `-std=` cannot be set per language in
ps2build (`cflags` hits C files too), so stay on the default.

### 2.1 Game sources: one abstract class replaces `item_list_t`
```cpp
class GameSource {                         // src/sources/game_source.h
public:
    virtual ~GameSource() = default;
    virtual Mode mode() const = 0;
    virtual int  textId() const = 0;  virtual int iconId() const = 0;
    virtual void init() {}            virtual void shutdown() {}
    virtual bool needsUpdate() { return false; }
    virtual int  update() = 0;        virtual int  count() const = 0;
    virtual const GameInfo *game(int id) const = 0;
    virtual void launch(int id, ConfigSet &cfg) = 0;
    virtual void cleanup(CleanupMode) {}  // defaults replace today's `if (list->itemX)` NULL checks
    // optional capabilities: rename, remove, checkVmc, launchVcd/Cue, art archive, view/sourceId ...
protected:
    explicit GameSource(HostServices &host) : host_(host) {}
    HostServices &host_;                  // the ONLY way a backend talks to the app (see 2.2)
};
class BdmSource : public GameSource { ... private: device table, scan state ... };
```
Backends: `BdmSource, EthSource, HddSource, AppSource, MmceSource, FavSource, UdpfsSource, HttpSource`.
A `SourceRegistry` indexed by `Mode` replaces the `mode` switch statements and the `ethGetObject`/
`hddGetObject`/... accessor family. `priv`/`owner` disappear (members); `FavSource` becomes a view over
other sources instead of the `favResolve` owner-juggling. Shared logic (`supportbase`: ISO scanning,
cheat/art discovery, compat flags) becomes a `GameListCache` helper the backends hold, not free
functions every backend includes.

### 2.2 `HostServices`: cut the backend -> `opl` / `gui` edges
A small pure interface the app implements and hands to every backend and to the UI-facing modules:
`reportError(code, text)`, `clearErrorIf(code)`, `requestSave()`, `pathToMode()`, `deferIo(fn)`,
`bootStatus(text)`, `msgBox/warning(...)`, `notifyListChanged(Mode)` (replaces `libViewMarkDirty` pokes:
the library view subscribes, backends only publish). Backends then depend on `core/` only; the 224
upward edges shrink to the host boundary. Same trick for `system`: `LaunchContext` carries the cheat
list and RA watch list in (`launchWith(ctx)`), so `system` stops importing `cheatman`/`rawatch`.

### 2.3 Settings: 137 globals -> one typed struct + a constexpr key table
`struct Settings { struct Net {...} net; struct Neutrino {...} neutrino; Ember ember; Popstarter pops; Vcd vcd;
Pad pad; Bdma bdma; Http http; Ui ui; ... }`, one instance `Settings &settings()`. Load/save/default become
a `constexpr` descriptor table (`{key, member-pointer, default, min, max}`) walked by one loop. This
replaces `_loadConfig` (452 lines), `_saveConfig` (302), `setDefaults` (198), and the matching externs
in opl.h. Config-file keys and format do not change (a golden round-trip test enforces it).

### 2.4 Subsystem classes (state owned by an object, lifetime explicit)
| Today | Becomes |
|---|---|
| `config_set_t` + 37 `config*` functions, hand-linked list | `ConfigSet` (RAII, move-only; `get<int>`/`getString` overloads; iterator over entries; format latch stays private) |
| `ioman` (worker thread, handler table, `void *` payloads) | `IoQueue` + typed `submit(Request<T>)`; handlers registered as `std::function`-free function objects (templates), `LOG` stays a macro |
| `renderman` `rm*` (globals for display state) | `Renderer` (explicit `instance()` for init order), private display state, `constexpr` scaling helpers |
| `texcache`/`textures`/`atlas`/`fntsys` | `TextureCache`, `TextureLoader`, `Atlas`, `Font` resource owners (RAII frees GS memory; no `goto fail`) |
| `themes` elements + draw-function pointers | `ThemeElement` base (`draw()`, `validate()`), derived `ItemsList, CoverFlow, GameImage, Text ...`; loader split from renderer |
| `menusys` `menu_item_t`/`submenu_item_t` callbacks | `MenuItem`/`SubMenu` with virtual `onCross/onTriangle/onRefresh`; `userdata` goes away |
| `dia`/`dialogs` `UIItem` arrays + 35 global dialog tables | `Dialog` builder returning a value; tables become `constexpr` where static |
| `pad`, `sound` | `PadManager` (per-port `Pad` objects), `AudioMixer` (BGM/SFX state private) |
| `tar`, `vmc_groups`, `artindex`, `ranet`, `httpcatalog` | one class each, already mostly self-contained |

### 2.5 constexpr / enum class
- Numeric `#define`s (~406: `UL_GAME_NAME_MAX`, `COMPAT_MODE_n`, `MODE_FLAG_*`, error codes, `MENU_*`) ->
  `constexpr`/`enum class` with `operator|` helpers for flag sets (`CompatMode`, `ModeFlag`).
- Function-like macros (~24: `OSD_HISTORY_GET_*`, `X_SCALE/Y_SCALE`, `ALLOC_FITS`, `GLYPH_PAGE_OK`,
  `makeSYNCV/makeDISPLAY`) -> `constexpr inline` functions (type-checked, still compile-time).
- Static lookup tables (button maps, vmode tables, key tables, extension lists, `htab`) -> `constexpr`
  arrays; the pad-macro slowdown inverse in guigame becomes a `constexpr` inverse built at compile time.
- Leave `#define` for: include guards, string-literal concatenation, `LOG(...)`, `__DEBUG` switches.
- Not constexpr: anything touching IOP/EE state, floating-point-heavy GS maths (no benefit).

### 2.6 Visibility
- Every file-scope `static` (~730) and every function nothing else mentions (**84**) goes into an
  anonymous namespace (or becomes a private member). Their header declarations are deleted.
- Headers expose classes and a handful of free functions only; the 192 externs are replaced by
  accessors on `Settings`/owner classes. Compiler help: `-Wmissing-declarations` flags any non-static
  free function without a prior declaration.
- Include hygiene: headers carry forward declarations, not other project headers (fan-in today:
  opl.h 44 files, ioman.h 43, util.h 26, gui.h 22, lang.h 21 -- opl.h is the worst offender).

### 2.7 RAII and error paths
`UniqueFd`, `UniqueFile`, `UniqueMalloc<T>` (free deleter; keeps `malloc` for large GS/IO buffers),
`SemaLock` (scoped wait/signal for the 133 manual sema operations), `ScopedIoBlock`. Removes the 46
`goto fail` sites (bdmsupport, tar, ...) without hoisting declarations, and the early-return leak
class. No exceptions: functions keep returning status codes (`Result`-style small struct where it helps).

## 3. Layering (enforced by the analyzer)

```
L5 app        opl (App lifecycle only)
L4 ui         menusys dia dialogs gui guigame libview
L3 sources    supportbase *support (bdm hdd eth app mmce udpfs http fav vcd cue disc net) ra* artindex ...
L2 gfx        renderman textures texcache fntsys themes
L1 core       config lang xparam cheatman gsm pad sound OSDHistory vmc_groups
L0 platform   system ioman util tar ps2cnf ioprp atlas httpclient nbns launchdiag hdd(rpc) md5 lz4 zso
```
Rule: a module may include/call only its own layer or lower; upward needs go through an interface
(`HostServices`, observer, or a callback passed in). `tools/analyze_coupling.py --check` exits non-zero
on any upward symbol and runs in CI; target is 0 (today 224). Physical layout follows:
`src/{platform,core,gfx,sources,ui,app}/`. ps2build lib targets cannot link other libs (v1 limit),
so the layers stay directories inside the single `riptopl` target and the script is the enforcer.

## 4. Phases (each ends green: clean ps2build build + flavour syntax sweep + size check + smoke test)

| # | Phase | Risk | Behaviour change |
|---|---|---|---|
| 0 | **Safety net.** Warning baseline (157 `-Wwrite-strings`, 34 `-Wsign-compare`), add `-Wmissing-declarations`, `-fno-exceptions -fno-rtti`; host-built unit tests (g++ on Linux, no PS2 headers) for the pure modules: config parser/writer round trip, tar index, lz4/zso, ps2cnf, httpcatalog, atlas, ioprp patching, vmc_groups; analyzer in CI as informational | low | none |
| 1 | **Visibility + constexpr.** Anonymous namespaces, delete the 84 dead exports and their header decls, defines -> constexpr/enum class, func macros -> constexpr functions, `const`/`constexpr` tables | low | none (binary size should not grow) |
| 2 | **RAII layer.** `UniqueFd/File/Malloc`, `SemaLock`; convert module by module, deleting the `goto fail` sites and manual `free` chains. Revert the declaration hoists from `ec17be0` as part of it | low-med | none intended; leak fixes |
| 3 | **Settings struct + descriptor table.** opl.h externs -> `settings()`; one load/save loop. Golden-file test: old config in, byte-identical config out | med | none (key names fixed) |
| 4 | **HostServices + observers.** Define interfaces in `core/`; make `opl` implement them; migrate backends one at a time behind an adapter so `item_list_t` still works. `system` takes `LaunchContext`. Cycle count drops step by step | med | none |
| 5 | **GameSource hierarchy.** Start with the smallest backend (`AppSource` or `HttpSource`), then `Mmce`, `Udpfs`, `Eth`, `Hdd`, `Bdm` last (385-line `bdmLaunchGame`, Neutrino path). Delete `item_list_t` when the last backend moves; split launch code into `BdmLauncher`/`NeutrinoLauncher` | med-high | none intended; this is where hardware testing matters most |
| 6 | **Subsystem classes.** Order by leaf-first: `ConfigSet`, `IoQueue`, `Atlas/TextureCache/Font`, `Renderer`, `Theme` (+ move `guiDraw*` helpers into a `ui/draw` module so `themes` stops importing `gui`), `MenuItem`, `Dialog`, `PadManager`, `AudioMixer` | med | none intended |
| 7 | **Split god files.** opl.cpp -> `App` (lifecycle), `SourceManager`, `AutoLaunch`, `ApaSetup`; gui.cpp -> `GuiCore`, `SettingsScreens` (the `guiShow*Config` family), `Overlays`; themes.cpp -> `ThemeLoader`, `ThemeRenderer`, `CoverFlow`; bdmsupport.cpp -> `BdmSource`, `BdmDevices`, `NeutrinoLauncher`; `sendIrxKernelRAM` (1468 lines) -> data-driven module list | med | none |
| 8 | **Directory move + CI gate.** `src/{platform,core,gfx,sources,ui,app}/`; `--check` becomes blocking | low | none |

Phases 1-3 are independent of the class work and pay off immediately (smaller headers, compile-time
tables, one place for settings). Phase 4 is the keystone: until backends stop calling `opl`/`gui`, no
class extraction can be reviewed in isolation.

## 5. Process rules
- One module (or one backend) per commit; every commit builds and keeps the old interface alive via a
  thin adapter until its last caller moves ("strangler"), so a revert is always a single commit.
- Refactors that move code do not change it; behaviour-changing fixes ride separate commits.
- Hardware/emulator smoke test after phases 3, 5, 6 (boot, each backend lists games, launch one,
  settings round trip, theme switch, BGM). Phase 5 needs real USB/HDD/SMB runs.
- Size budget: compare `riptopl-packed.elf` after each phase; > +2% needs an explanation (vtables and
  inline wrappers should be net-neutral once dead code is gone).
- Flavours: the debug/RA/IGS/RTL/DTL define sets must keep compiling (the syntax sweep used for the
  .cpp rename is worth keeping as `tools/syntax_sweep.py`).

## 6. Risks / decisions needed
1. **Config format drift** (phase 3): shared with wOPL/OPL-Launcher; keep keys/ordering/format latch; golden tests first.
2. **`item_list_t` is touched everywhere** (~100 call sites in opl, favsupport, menusys, guigame, libview ...); the adapter
   step keeps phase 5 incremental, but favsupport's owner juggling needs a design pass before starting.
3. **Static init order:** objects with constructors at namespace scope run before `main` on EE; use
   function-local statics or explicit `init()` for anything touching IOP/GS (a rule, enforced in review).
4. **Code size/RTTI:** keep exceptions and RTTI off; no `dynamic_cast`, no `std::function` in hot paths.
5. **Flavour breadth:** the Makefile can no longer build `src/`; non-main flavours depend on porting them to ps2build first.
6. **Scope:** `ee_core/` and `elfldr/` are separate ELFs (own crt0/linkfile, asm) and stay out of this plan; they get a
   later, much smaller pass.

## 7. Suggested first slices (smallest useful PRs)
1. `tools/analyze_coupling.py` in CI (informational) + `-Wmissing-declarations` + warning baseline.
2. Visibility sweep of `tar`, `OSDHistory`, `cuesupport`, `vcdsupport` (dead exports + anonymous namespaces).
3. `OSDHistory` macros -> `constexpr`; `iosupport.h` flags/enums -> `enum class` + flag helpers.
4. Host test harness + config round-trip test (unblocks phase 3).
5. `UniqueFd`/`SemaLock` and conversion of `tar.cpp` (also removes the `goto fail` hoist).

## 8. Phase 0 status (done)
- **Existing tests repaired.** The .c -> .cpp rename and the upstream root reorg had silently broken 25
  `.github/scripts/test_*.py` source checks and `tools/pc/http/tests` (paths, C snippets that now contain
  casts, `ioPutRequest` mocks). All pass again; `tools/run_tests.sh [-k]` runs everything.
- **Host unit tests** in `tests/host/` (ASan + UBSan, real source #included against stub headers in
  `tests/host/stub`): `config` (both syntaxes, format latch, merge/clone, VMC helpers, wOPL key
  translation), `tar` (index, cache round trip, concatenated archives, oversize, name rules),
  `ps2cnf` (BOOT2 variants), `vmc_groups` (table + lookup contract).
- **Warning baseline:** `tools/warning_gate.py build.log --check` (196 warnings in 28 files; 138 are
  `-Wwrite-strings`, 34 `-Wsign-compare`, 15 `-Wmissing-declarations`). `ps2.yaml` now builds with
  `-fno-exceptions -Wmissing-declarations`. `-fno-rtti` is NOT enabled: ps2build `cflags` also hit C
  files and gcc warns once per C file; revisit when ps2build gets per-language flags. No polymorphic
  types exist yet, so nothing is lost.
- **CI:** `.github/workflows/ps2build.yml` runs host tests, the warning gate and the coupling report.

Findings the tests surfaced (1 and 2 fixed afterwards, with regression tests; 3 and 4 still open):
1. FIXED `ps2cnfGetBootFile`: `BOOT2 <path>` with no `=` makes `CNFGetToken("=")` return NULL, which is only
   compared to `(const char *)-1` before `CNFGetKey(NULL, ...)` -> NULL dereference on a malformed SYSTEM.CNF.
2. FIXED `tarParseFile` (ARC_VERSION left at 4 to keep wOPL cache compatibility, so an existing art_cache.bin keeps its 46-char names until the archive changes): ART entry names kept 46 characters, not the documented 47 (`strncpy(nameMax)` then
   `fname[nameMax - 1] = 0`); same off-by-one for CFG/CHT.
3. `vmc_groups`: 12 titles are listed in more than one group (the later group is unreachable for them) and
   8 ids are not the usual `SLUS_000.00` shape.
4. `ioPutRequest(type, void *)` forces `reinterpret_cast<void *>(&fn)` at ~20 call sites; a typed overload
   for `io_simpleaction_t` (phase 6, IoQueue) removes them.

## 9. Phase 1 status (done: visibility, constants, macros, const-correctness)
- **Dead/local exports:** 55 functions only their own file used are now `static` (declarations removed from
  headers, forward declarations where needed); 24 functions nothing references were deleted (pad, hdd,
  menusys submenu helpers, renderman, hddsupport home staging, ...). `ioman` (queue contract) and `tar`
  (host tests) were left public on purpose. `static` is kept instead of anonymous namespaces: identical
  linkage, no churn for ~730 declarations.
- **Constants:** 272 integer `#define`s in C++-only headers/sources became `constexpr`. Headers that IOP modules or host
  tools compile as C (opl.h, config.h, iosupport.h, supportbase.h, system.h, dia.h, menusys.h, util.h,
  ...) keep their macros; so do names the preprocessor, the SDK headers or the source-scraping tests use.
  Verified with a `-Wundef` build and the per-flavour sweep (`tools/syntax_sweep.py`).
- **Function-like macros:** `pggsm.h` make*, OSD history date helpers, atlas, fntsys glyph page, renderman scale
  helpers are `constexpr`/inline functions.
- **Const-correctness:** string tables and the config path API take `const char *` (warnings 184 -> 67).
- Checked against the previous commit's ELF after each step: `.data` identical; the only code change was atlas
  inlining (-3 KB text). `tools/warning_gate.py` caught a macro->function rename that had turned
  `rmScaleX` into infinite recursion (an exported wrapper shared the name).
- Remaining 67 warnings: 34 `-Wsign-compare`, 21 `-Wwrite-strings` (SDK `char *` parameters and `gHDDPrefix`-style globals),
  5 `-Wmissing-declarations`, 4 unused variables, 2 pointer-arith, 1 register.

## 10. Phase 2 status (RAII: helpers and the cleanup paths done; bulk malloc conversion deliberately not done)
- `include/raii.h` (UniqueFd, UniqueFile, UniqueMalloc, ScopeExit) and `include/sema_lock.h` (SemaLock), unit-tested in
  `tests/host/test_raii.cpp`.
- Converted: `tar.cpp` (all fds and buffers; `goto fail` and the declaration hoist gone), `bdmTryNeutrinoLaunch` (abort path is a
  function), `sysLoadModuleBuffer` and the BDM transport lock (SemaLock), `discCheckWorker` (scope guards), the menusys info-size
  worker (split into helpers, no goto). The only gotos left are 23 backward `goto reshow_*` re-display loops in `gui.cpp`/`guigame.cpp`
  (control flow, not cleanup; they belong to the gui split in phase 7).
- Semaphores: only two of the ~140 Wait/Signal sites were scoped locks with early exits; the rest are producer/consumer signals
  (bgm ring, IO queue) or straight-line pairs, so a blanket SemaLock rewrite would not buy safety.
- malloc/free: a leak scan (allocation followed by a return before free) flagged ~45 sites; the ones read were correct ownership
  hand-offs, so no bulk conversion. One real defect fixed: `fntPrepareGlyphCachePage` dereferenced an unchecked `malloc`.

## 11. Phase 3 status (done: one table for the plain settings; the Settings struct is deferred)
- `src/settings.cpp` holds a constexpr table of 79 rows (key, variable, default) that drives `settingsApplyDefaults()`,
  `settingsLoad()` and `settingsSave()`. `opl.cpp` lost 242 lines (the matching lines of `setDefaults`, `_loadConfig`, `_saveConfig`)
  and calls the three functions; everything with migration, clamping or cross-key logic stayed where it was, in its original relative
  order and now AFTER the table load: the video-mode recovery combo (that row is `loadPlain = false`), the prefix sanitising, the
  AutoStart/GameView/AppsDisplay/VCD/ArtDelay/Coverflow clamps, the Popstarter device/path migration, the MMCE pacing migration,
  the network-protocol derivation, the plasma-colour migration and the boot-device reconcile. The extraction/verification scripts
  checked that no special code reads a table variable before its (now earlier) plain read, apart from `gVMode`.
- Verified by `tests/host/test_settings.cpp` (real headers via `tests/host/sdk_stub`, real config store): same 79 keys in the old save
  order, every default equals the old `setDefaults` value (checks generated from the old source in `settings_golden_defaults.inc`),
  full save -> text -> load round trip, absent keys leave ints/colours alone and empty strings (as `configGetStrCopy` always did).
- Behaviour notes: on a brand-new settings file, the few keys written by special code (theme, language, MMCE pacing marker, ember,
  swap-select-button) now land after the plain keys instead of between them; existing files keep their order. Key names/syntax unchanged.
- Deferred on purpose: regrouping the ~130 extern globals into a `Settings` struct. It is ~2,400 occurrences across src/include plus
  names the scraping tests match, with no behavioural gain now that the three lists are one table; do it per sub-struct when a module
  that owns a group (net, neutrino, popstarter, vcd, ember) is extracted in phase 4+.
- Also: the host test infrastructure can now compile the real `include/opl.h` (empty SDK stubs in `tests/host/sdk_stub`), which makes
  more modules testable without hand-written header stubs.

## 12. Phase 4a status (HostServices: backends no longer call opl/gui/libview directly)
- `include/host.h` defines three small interfaces (`UiServices`, `AppServices`, `LibraryServices`) with accessors
  `host::ui()/app()/library()`; `src/host.cpp` (application layer) forwards them to the existing functions. 276 call sites in 15
  modules (the support backends, supportbase, discsupport, vcdsupport, favsupport, util, lang, system, ranet) now use them.
- **Measurement correction:** the first numbers in section 1 (43-module cycle, 224 upward symbols) came from a naive scan that also
  counted words in comments and strings and member names (`obj.deinit`). `tools/analyze_coupling.py` now strips those. Honest
  before/after for this step: largest cycle 38 -> 31 modules, upward symbol dependencies 164 -> 70 (69 -> 38 module pairs).
- Remaining upward edges, by family: `system` -> rawatch/cheatman/gsm/lang/discsupport (launch config assembly), `themes` -> gui/menusys/
  vcdsupport/favsupport (drawing helpers, row text), `lang` -> fntsys/themes, `sound` -> themes/opl, `menusys`/`gui` -> opl,
  `texcache`/`textures` -> artindex/favsupport, `debug` -> ethsupport, `atlas` -> renderman, `favsupport`/`appsupport` -> opl
  (`oplGetModule` = the source registry of phase 5; `oplGetAppImageByMode` needs GSTEXTURE).
- Source-scraping tests read `src/*.cpp` through `.github/scripts/hostmap.py`, which maps the host calls back to the old function
  names their mocks use; host tests mock `host::ui()` in `tests/host/host_env.h`.

## 13. Phase 4b/4c status
- **4b `LaunchServices`** (`host::launch()`): `system.cpp` no longer includes cheatman/rawatch/gsm/discsupport; the feature-derived ee_core
  fields (cheat list, PS2RD image, RetroAchievements watch list + work area, GSM) are filled by `fillCoreConfig()` in `src/host.cpp`.
  `warningId()` lets `system` raise translated toasts without touching the language tables.
- **4c theme/UI split:** the hint-row helpers (`guiDrawIconAndText`, `guiDrawIconAndLiteralText`, `guiAlignMenuHints`,
  `guiAlignSubMenuHints`) moved from `gui.cpp` to `src/uidraw.cpp` (graphics layer; they only need textures, fonts, the renderer and the
  language table); `themes` calls the UI through `host::ui()` for lock/unlock/plasma and `host::app()` for `moduleUpdateMenu`.
  Behaviour note: the moved helpers read the live screen width (`rmGetScreenExtents`) instead of gui.cpp's cached copy, which is
  refreshed from the same call by `guiReloadScreenExtents()`.
- Analyzer (comments/strings/members ignored): upward dependencies 164 -> 47 symbols (69 -> 31 module pairs); largest cycle 38 -> 32.
- What is left is one-off edges, each needing its own answer: `themes` -> menusys (hint locks, item text) / vcdsupport (display names) /
  favsupport; `texcache`/`textures` -> artindex/favsupport/appsupport/hddsupport/bdmsupport (art path/mode queries: these become
  `GameSource` virtuals in phase 5); `lang` <-> fntsys/themes and `sound` -> themes (language/theme switching is app orchestration);
  `system` -> bdm/eth/mmce/menusys/gui/themes/renderman/OSDHistory/xparam (launch teardown and device probing); `gui`/`menusys` ->
  opl (`applyConfig`, `saveConfig`, `oplGetModule` = the source registry).

## 14. Atlas and ConfigSet
- **`Atlas`** (`include/atlas.h`, `src/atlas.cpp`): glyph atlas packer as a class (`create/destroy/place/texture`), nodes owned by
  `raii::UniquePtr`; fntsys holds an `Atlas *`.
- **`ConfigSet`** (`include/configset.h`, storage/key rules in `src/config.cpp`): `config_set_t` is now `typedef ConfigSet` in C++ and an
  opaque struct in C. Fields are private; `type()/uid()/filename()/modified()/format()/entries()` are accessors, `markModified()/
  clearModified()/setFormat()/rename()` the mutators. `configGet*/configSet*/configRemoveKey/configMerge/configClear/configGetVMC*`
  stay as inline forwarders in `config.h` so call sites did not change. Heap sets use `ConfigSet::create/destroy/clone` (malloc +
  placement new). File syntax (legacy/libconfig reader/writer, official-OPL translation, registry) is still free functions in
  `config.cpp` driving the class through its public interface. Key names, ordering and on-disk formats are unchanged.
  `filenameForOpen()` exists because `openFile()` patches an `mc?` placeholder in place.
- Emulator-verified after each of Atlas and ConfigSet (menus, config load/save, game list). Binary growth: +48 B for ConfigSet.
- Verification per step: clean `ps2build build`, `tools/run_tests.sh -k`, `tools/warning_gate.py build.log --check` (64 vs baseline 67),
  `tools/syntax_sweep.py` (only the pre-existing EESIO `SIOCookie.h` failure).

## 15. Next
1. **PlasmaBackground** out of `gui.cpp` (perlin tables, `guiCalcPerlin`, `guiDrawBGPlasma`, the 32x32 texture) into `src/plasma.cpp`;
   `gui` keeps a thin wrapper. First `gui` extraction; the VU0 asm helpers move with it.
2. Remaining phase 6 leaves: `IoQueue`, `TextureCache/Font`, `Renderer`, `Theme`.
3. Phase 5 `GameSource` hierarchy (needs go-ahead; hardware testing matters most there).
4. Open: vmc_groups duplicate-title decision; other flavours (RA, debug, DUALSENSE) and Makefile removal/CI cutover; the remaining
   analyzer upward edges listed in section 13.
