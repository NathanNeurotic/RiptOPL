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
