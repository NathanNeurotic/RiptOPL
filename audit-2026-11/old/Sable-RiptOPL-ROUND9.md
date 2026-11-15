# Sable — RiptOPL ROUND 9 (headers line-read as a corpus, outside modules/ and third_party/)

**Pin:** `2e347d5ac885cd0ff73e9ad4143781946396f492` (rebuild/main) — clone HEAD == live head at round start and end; no delta, no revalidations owed. Clone git-clean at end; read-only held throughout (the only file written is this report).

**Scope:** every header (`.h` / `.hpp` / `.inc`) in the repo **excluding** anything under `modules/` and `third_party/` (separate round-9 passes), line-read in full. Method: five parallel chunk line-reads (include/ split four ways by load, everything else as a fifth chunk), corpus-wide mechanical censuses by the coordinator (include guards, divergent duplicate `#define`s, duplicate type names), and coordinator spot-verification of every headline mechanism against the consumer/definition sites before numbering.

**Result: 24 new findings, RIPTOPL-344…367.** Finding count verified by grep against this file's numbered headings after writing (see the count statement at the end).

## Corpus ledger and reconciliation with earlier figures

Measured at the pin with `find` + `wc -l` (coordinator, this round):

| Tree | Files | Lines |
|---|---|---|
| `include/` | 138 | 12,511 |
| `ee_core/include/` | 31 | 1,939 |
| `tests/` | 79 | 947 |
| `tools/` | 8 | 218 |
| `.github/` (fixtures + 1 `.inc`) | 4 | 414 |
| **This round's corpus** | **260** | **16,029** |
| `modules/` + `third_party/` (excluded, other passes) | 435 | 96,354 |
| **Repo total (all `.h`/`.hpp`/`.inc`)** | **695** | **112,383** |

Reconciliation: round 7 reported "439 first-party headers, 35,808 lines" for its census pass and round 8 was briefed "35,590 lines" for headers overall. Neither figure reproduces at this pin under any extension set measured this round (repo-wide all-extensions = 112,383 lines; outside the excluded trees = 16,029; `.h`-only counts do not reach either figure either). The 439-file count is closest to the excluded trees' 435 files, but no exact derivation was found; both earlier figures are recorded here as **not reproducible**, and this round's ledger above is the measured one. There are no headers under `src/` (the loader's headers all live in `include/`), which the brief's "plus headers under src/" anticipated but the tree does not contain.

Chunk ledger (each chunk read every line of every file; consumer/definition sites additionally read for cross-checks as noted per finding):

| Chunk | Files | Lines | Coverage |
|---|---|---|---|
| A (include/) | 36 | 3,323 | full |
| B (include/) | 38 | 3,043 | full |
| C (include/) | 28 | 3,102 | full |
| D (include/) | 36 | 3,043 | full |
| E (ee_core/include + tests + tools + .github) | 122 | 3,518 | full |
| **Total** | **260** | **16,029** | **line-closed** |

## Findings index

| ID | Title (short) | Cat |
|---|---|---|
| RIPTOPL-344 | `CNFGetKey` can return an unterminated 64-byte buffer — caller scans past its stack buffer on crafted SYSTEM.CNF | 6 |
| RIPTOPL-345 | Six headers have no include guard — incl. the shared `modules.h` contract header (double inclusion is a proven hard error) | 7 |
| RIPTOPL-346 | Stub `thsemap.h` (browse copy): `DeleteSema` sits outside its own include guard; copies diverged | 7 |
| RIPTOPL-347 | `tests/host/check.h`: `CHECK_STR` uses `strcmp` but the header never includes `<string.h>` | 7 |
| RIPTOPL-348 | `raVcdBootName()` free API is dead — wrapper and the `RaHash` method behind it have no caller | 1 |
| RIPTOPL-349 | `guiGameHandleDeferedIO` is defined and declared but never called | 1 |
| RIPTOPL-350 | `NetSupport::netifLinkStatus()` is a dead declaration — never defined, never called | 1 |
| RIPTOPL-351 | `Remove_Padhook` — declared, never defined, never called | 1 |
| RIPTOPL-352 | `igrovl.h` — dead forward declaration of a struct tag that does not exist | 1 |
| RIPTOPL-353 | Dead lite-build gates: `LITE_HAS_TCPIP` and `LITE_DEVICE_NAME` are consumed nowhere | 1 |
| RIPTOPL-354 | `BG_REQUEST_EXTRA_IDLE_FRAMES` is a dead constant whose described mechanism does not exist | 1 |
| RIPTOPL-355 | `CONFIG_OPL_ENABLE_ANALOG_NAV` — config key macro for a deliberately removed feature; zero readers | 1 |
| RIPTOPL-356 | `CHEAT_VERSION` value is consumed by no code (test script uses the line only as a text anchor) | 1 |
| RIPTOPL-357 | `ISO_GAME_FNAME_MAX` is a dead macro — no product-code consumer anywhere | 1 |
| RIPTOPL-358 | `APA_IOCTL2_GETHEADER` — dead APA ioctl constant; no APA ioctl2 call site exists | 1 |
| RIPTOPL-359 | `GSM_ARGS` — dead constexpr in the per-game GSM header | 1 |
| RIPTOPL-360 | `IO_ERR_UNKNOWN_REQUEST_TYPE` — error code never returned and never tested | 1 |
| RIPTOPL-361 | `HDD_LOADMODULES_STATUS_COUNT` — trailing status enumerator never used as a count or bound | 1 |
| RIPTOPL-362 | `FAV_MAGIC` comment spells the on-disk magic wrong — the bytes are "OFAF", not "OFAV" | 8 |
| RIPTOPL-363 | Stub `ps2smb.h`: `smbOpenShare_in_t` size comment contradicts the struct (520 vs 516) | 8 |
| RIPTOPL-364 | favsupport.h points readers at a source file that does not exist (`favsupport.c`) | 8 |
| RIPTOPL-365 | Orphaned contract comment attached to the wrong declaration in themes.h | 8 |
| RIPTOPL-366 | ioman.h carries an orphan doc comment for a remove-by-type function that does not exist | 1 |
| RIPTOPL-367 | Duplicate `#include <debug.h>` in opl.h | 7 |

---

### RIPTOPL-344 — `CNFGetKey` can return an unterminated 64-byte buffer — caller scans past its stack buffer on crafted SYSTEM.CNF
- **Category:** 6 (bugs/security)
- **Locations:**
  - `include/ps2cnf.h:1` — `#define CNF_PATH_LEN_MAX 64` (the constant does double duty: copy limit and buffer size)
  - `include/ps2cnf.h:3` — `int ps2cnfGetBootFile(const char *path, char *bootfile);`
  - `src/platform/ps2cnf.cpp:54-67` — `CNFGetKey` copy loop; coordinator-verified: loop bound `i < CNF_PATH_LEN_MAX && *line != '\0'`, terminating NUL written **only** on the `isspace` break path (:59-61)
  - `src/platform/ps2cnf.cpp:70,123` — `ps2cnfGetBootFile` calls `CNFGetKey(pChar, bootfile)` and returns 0 (success)
  - `src/sources/supportbase.cpp:116` — sole caller's buffer: `char ps2disc_boot[CNF_PATH_LEN_MAX] = ""` (exactly 64 bytes)
  - `src/sources/supportbase.cpp:120-128` — `GetStartupExecName` scans the result for `':'` / `'\0'` (plus `%s` LOGs at :129/:151)
- **Problem (READ mechanism, INFERRED trigger):** A BOOT2 token of ≥64 consecutive graph characters exits the copy loop at i=64 having written exactly 64 bytes and no NUL, and `ps2cnfGetBootFile` returns success. The caller's 64-byte stack buffer is fully overwritten and unterminated; `GetStartupExecName` then walks it looking for `':'`, `';'` and `'\0'`, reading past the stack buffer until it happens to hit a zero byte. Trigger (INFERRED) requires a disc/ISO whose SYSTEM.CNF has a ≥64-char BOOT2 token — crafted or malformed media; normal BOOT2 values are ~22 chars. This is a crafted-media stack over-read, not a normal-use crash.
- **Fix (LESS):** Make the copy bound one less than the buffer size and terminate on every exit path (loop to `CNF_PATH_LEN_MAX - 1`, write `*key = '\0'` unconditionally), or return -1 on truncation, matching the function's existing -1 error idiom. Do not enlarge the buffer instead; the defect is the missing terminator, not the size.
- **Benefit:** Removes a stack over-read on untrusted disc metadata; makes the header constant mean one thing (buffer size) instead of two contradictory things.
- **Effort:** S
- **Risk:** Low — normal BOOT2 values are far below the bound; only the ≥64-char path changes behaviour.
- **Confidence:** High on the mechanism (READ at both sites, coordinator re-verified); the trigger is INFERRED as above.

### RIPTOPL-345 — Six headers have no include guard — incl. the shared `modules.h` contract header (double inclusion is a proven hard error)
- **Category:** 7 (build/CI/repo clutter — latent build breakage)
- **Locations (ALL):**
  - `ee_core/include/modules.h:1-94` — whole file, no `#ifndef`/`#pragma once` (coordinator-verified: guard count 0; file opens directly with `enum OPL_MODULE_ID`); defines `enum OPL_MODULE_ID`, `irxptr_t`, `irxtab_t`; consumers `ee_core/src/{freeze_diag.c:23, igrmbox.c:15, iopmgr.c:14, modmgr.c:11, patches.c:13, padhook.c:25}`, `src/platform/{system.cpp:47, sysdisc.cpp:46, sysneutrino.cpp:45, sysvmc.cpp:44}`
  - `include/OSDHistory.h:1` — opens directly with `constexpr int MAX_HISTORY_ENTRIES = 21;`; contains `struct HistoryEntry` (:91-99, a hard redefinition on double inclusion); includers `src/core/OSDHistory.cpp:23`, `src/platform/{sysdisc.cpp:43, sysneutrino.cpp:42, system.cpp:44, sysvmc.cpp:41}`
  - `include/nbns.h:1-25` — enum + struct definitions, no guard; includers in nbns/main.c, nbns/nbns.c, nbns.cpp, ethsupport.cpp, netsupport.cpp
  - `include/xparam.h:10-34` — `constexpr` definitions, no guard; included by 10 TUs
  - `include/ioprp.h:1-6` — prototypes only, no guard (benign today)
  - `include/ps2cnf.h:1-3` — one macro + one prototype, no guard (contents currently idempotent; see RIPTOPL-344 — this is the header most likely to gain a declaration)
- **Problem (READ; one case executed):** Every other header in the 260-file corpus is guarded. For `modules.h`, compiling a TU that includes it twice with host `cc` fails with `error: redeclaration of 'enum OPL_MODULE_ID'` (executed by the chunk-E worker in /tmp). The same class of failure applies to `OSDHistory.h` (`HistoryEntry`), `nbns.h` and `xparam.h` by construction. Today every consumer includes each of these exactly once and no header includes any of them transitively, so all six are **latent**: the first header that pulls one in transitively breaks every TU that also includes it directly.
- **Fix (LESS):** Add the tree-conventional `#ifndef __<NAME>_H` guards to all six files. This is the one justified addition in this round: it prevents a concrete, demonstrated redefinition failure rather than adding a feature.
- **Benefit:** Removes compile-break landmines from six headers, including the one header both sides of the EE-core↔loader contract share.
- **Effort:** S
- **Risk:** Low — guard names checked unique across `include/` + `ee_core/include/` (coordinator census: no collisions; the only repeated guard names in the corpus are inside tests/ stub pairs, see RIPTOPL-346's territory).
- **Confidence:** High (absences READ in all six files; `modules.h` failure executed).

### RIPTOPL-346 — Stub `thsemap.h` (browse copy): `DeleteSema` sits outside its own include guard; the two stub copies have diverged
- **Category:** 7 (build/CI/repo clutter — test-stub divergence)
- **Locations:**
  - `tests/smb2-live/inc-browse/thsemap.h:22` — `#endif` closing guard `HOST_THSEMAP` (coordinator-verified)
  - `tests/smb2-live/inc-browse/thsemap.h:23-27` — `DeleteSema` defined **after** the `#endif`
  - `tests/smb2-live/inc-ingame/thsemap.h:22-26` — the same function correctly inside the guard
- **Problem (READ + executed):** Including the browse copy twice in one TU — which its own guard claims to prevent — fails with `error: redefinition of 'DeleteSema'` (verified with host `cc`). The browse/ingame copies of the same stub have silently diverged in structure. Coordinator census additionally found both copies share the guard name `HOST_THSEMAP` (as do the two `tamtypes.h` copies with `TAMTYPES_H`); that is benign while the copies live in separate include paths, and is recorded here rather than filed separately.
- **Fix (LESS):** Move `DeleteSema` above the `#endif` at :22, matching the ingame copy. Pure move, no new code.
- **Benefit:** The stub honours its guard; the two copies agree again.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High (READ both copies; failure executed).

### RIPTOPL-347 — `tests/host/check.h`: `CHECK_STR` uses `strcmp` but the header never includes `<string.h>`
- **Category:** 7 (build/CI/repo clutter — header self-containment)
- **Locations:**
  - `tests/host/check.h:3` — only `#include <stdio.h>` (coordinator-verified; header has `#pragma once`)
  - `tests/host/check.h:15` — `strcmp` inside the `CHECK_STR` macro
  - `tests/host/test_grid.cpp:2` — first-position consumer (includes check.h first; only expands `CHECK`, which is why it survives)
- **Problem (READ + executed):** A TU that includes `check.h` first and expands `CHECK_STR` gets `implicit declaration of function 'strcmp'` (verified with host `cc`). All current consumers happen to include `<string.h>` before `check.h`, so this is latent, masked by include order in every existing test.
- **Fix (LESS):** Add `#include <string.h>` beside :3 (addition justified by the executed implicit-declaration failure).
- **Benefit:** The shared test header is self-contained regardless of include order.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High.

### RIPTOPL-348 — `raVcdBootName()` free API is dead — wrapper and the `RaHash` method behind it have no caller
**Verification note (2026-11-14, full verification): WITHDRAWN — REFUTED on adversarial re-derivation: a live caller exists — `.github/scripts/test_ra_ps1.py:137` generates `raVcdBootName(...)` assertions for four VCD files into a compiled harness the suite runs with `check=True`, and the suite passes at the pin; only the narrower "no production caller" is true, which is not what was filed; see Sable-VERIFY2-RIPTOPL.md. Do not act on this finding.**
- **Category:** 1 (dead code)
- **Locations (ALL):**
  - `include/rahash.h:22` — declaration `int raVcdBootName(const char *vcdpath, char *boot, int boot_max);`
  - `src/sources/rahash.cpp:142-144` — the free wrapper (forwards to `gRaHash.vcdBootName(...)`)
  - `src/sources/rahash.cpp:89` — `RaHash::vcdBootName` method declaration; `src/sources/rahash.cpp:826` — its definition
- **Problem (READ):** Every sibling free function in rahash.h has a live caller (`raHashVcd`, `raHashDisc`, `raHashHdl`/`raHashIsoDirect`, `raVcdWatchKey`/`raVcdWatchGuard*`); `raVcdBootName` alone is uncalled — whole-repo grep returns only the declaration plus the wrapper. The VCD hashing path (`RaHash::hashVcd`, after `rahash.cpp:847`) reaches the boot name internally via `hashPsx`/`psxBootName`, never via `vcdBootName`. The declaration, wrapper, and method (~25 lines incl. its PSX.EXE fallback logic) are all unreachable.
- **Fix (LESS):** Delete `include/rahash.h:22`, the wrapper at `rahash.cpp:142-144`, and `RaHash::vcdBootName` (decl :89, body :826 ff.) together — the method has no other caller, so deleting only the wrapper would strand it.
- **Benefit:** Removes a dead public RA API and ~25 lines of unexercised disc-parsing code that still has to be read and maintained as if live.
- **Effort:** S
- **Risk:** Low (grep-proven no caller, incl. tests/)
- **Confidence:** High.

### RIPTOPL-349 — `guiGameHandleDeferedIO` is defined and declared but never called
- **Category:** 1 (dead code)
- **Locations:**
  - `include/gui.h:197` — declaration
  - `src/ui/guiwait.cpp:155-159` — definition (5 lines)
- **Problem (READ):** Tree-wide grep (src/, include/, ee_core/, modules/, tests/, .github/) finds exactly these two mentions (coordinator re-verified: 2 occurrences). Its sibling `guiHandleDeferedIO` (gui.h:195) is used; this dialog-variant wrapper around `DeferredIoWait` has zero callers, including no test-harness use.
- **Fix (LESS):** Delete the declaration and the 5-line definition.
- **Benefit:** Removes a dead public entry point that invites callers into an untested wait path.
- **Effort:** S
- **Risk:** Low (no callers exist to break)
- **Confidence:** High.

### RIPTOPL-350 — `NetSupport::netifLinkStatus()` is a dead declaration — never defined, never called
- **Category:** 1 (dead code)
- **Locations:**
  - `include/netsupport.h:94` — declaration `int netifLinkStatus();` (private member of `NetSupport`); whole-repo grep returns this one line only (coordinator re-verified: 1 occurrence)
  - The live function it apparently duplicates: `include/netsupport.h:83` (`static int netGetNetIFLinkStatus(void);`), defined `src/sources/netsupport.cpp:63`, used as the `waitValidState` callback at `netsupport.cpp:101`
- **Problem (READ):** The class carries two link-status names; only the `netGetNetIFLinkStatus` static exists and is used. `netifLinkStatus()` has no body — any future caller would link-fail, and its presence invites a caller to pick the wrong one. It is a leftover of the free-function→class migration documented in the header's own comments.
- **Fix (LESS):** Delete the single declaration at `include/netsupport.h:94`. Nothing else changes.
- **Benefit:** One less phantom API surface; removes a link-error trap.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High (exhaustive whole-tree grep).

### RIPTOPL-351 — `Remove_Padhook` — declared, never defined, never called
- **Category:** 1 (dead code)
- **Locations:**
  - `ee_core/include/padhook.h:25` — `void Remove_Padhook(void);` (sole occurrence in the entire repo; coordinator re-verified: 1 occurrence, whole-repo grep over all file types)
- **Problem (READ):** No definition and no caller anywhere. Every other prototype in padhook.h (Install_PadOpen_Hook, Install_IGR, Invalidate_PadBuffer, Reset_Padhook, IGR_Exit) resolves to a definition/use.
- **Fix (LESS):** Delete the line.
- **Benefit:** One less phantom entry point a reader must chase.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High.

### RIPTOPL-352 — `igrovl.h` — dead forward declaration of a struct tag that does not exist
- **Category:** 1 (dead code)
- **Locations:**
  - `ee_core/include/igrovl.h:28` — `struct igr_cheats;` (sole occurrence repo-wide)
  - The real type: `ee_core/include/igrcheats.h:38-44` — anonymous-struct `typedef … igr_cheats_t`, no `igr_cheats` tag
- **Problem (READ):** The forward declaration names a type that is never defined and never used in igrovl.h (the `igrovl_api_t` callbacks at :33-34 take `const void *` precisely to avoid it). A reader who trusts it and writes `struct igr_cheats *` gets an incomplete type unrelated to `igr_cheats_t`. (The neighbouring :27, `struct EECoreConfig_t;`, is real and used at :32 — keep it.)
- **Fix (LESS):** Delete line 28 only.
- **Benefit:** Removes a false type from the overlay ABI header.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High.

### RIPTOPL-353 — Dead lite-build gates: `LITE_HAS_TCPIP` and `LITE_DEVICE_NAME` are consumed nowhere
- **Category:** 1 (dead code)
- **Locations:**
  - `include/riptlite.h:113` — `LITE_HAS_TCPIP`
  - `include/riptlite.h:23,29,35,44,52,58,65,71` — `LITE_DEVICE_NAME`, one define per lite-device branch
- **Problem (READ):** Whole-tree grep (excluding .git/build) finds zero mentions of either name outside riptlite.h itself. `LITE_HAS_TCPIP`'s comment claims it represents the menu's TCP/IP stack, but nothing tests it — lite composition is decided elsewhere. `LITE_DEVICE_NAME` is defined in all seven device branches and read by no code, script, or build file. (Coordinator census note: the remaining `LITE_HAS_*` defines in the same file are live per-branch gates with consumers; only these two names are dead.)
- **Fix (LESS):** Delete `LITE_HAS_TCPIP` and the seven `LITE_DEVICE_NAME` defines.
- **Benefit:** Two fewer gates a reader must trace to nowhere; the lite matrix shrinks to gates that actually gate.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High.

### RIPTOPL-354 — `BG_REQUEST_EXTRA_IDLE_FRAMES` is a dead constant whose described mechanism does not exist
- **Category:** 1 (dead code)
- **Locations:**
  - `include/themeint.h:23-27` — comment + definition (only occurrences tree-wide)
  - The art-delay path that actually exists: `src/gfx/texcache.cpp:373-376` (`list->delay` clamped by `ART_MIN_SETTLE_FRAMES`, compared to `guiInactiveFrames`)
- **Problem (READ):** Zero mentions outside themeint.h. The comment describes per-game backgrounds waiting the art delay plus 30 extra idle frames ("see drawGameImage"), but no code implements an extra-idle term under any name.
- **Fix (LESS):** Delete the constant and its comment block. (Implementing the gate in texcache is the alternative, but deletion is the LESS-NOT-MORE default since current behaviour was hardware-tuned without it.)
- **Benefit:** Removes a tuning knob that silently does nothing.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High.

### RIPTOPL-355 — `CONFIG_OPL_ENABLE_ANALOG_NAV` — config key macro for a deliberately removed feature; zero readers
- **Category:** 1 (dead code)
- **Locations:**
  - `include/config.h:114` — definition; the only occurrence of the macro or of the string `"enable_analog_nav"` in the tree
  - Corroborating: `src/ui/dialogs.cpp:2260-2265` — comment recording that checklist item 44 "Left Stick Navigation" was deliberately NOT re-added, that `gEnableAnalogNav` "has no reader anywhere" and its checkbox "was never synced", removed "because … this feature is never coming back"
- **Problem (READ):** Whole-tree grep returns only the definition. It is the only one of the 202 `CONFIG_OPL_*`/`CONFIG_NET_*`/`CONFIG_ITEM_*` macros in config.h with zero uses in `src/` + `ee_core/src/`. No compat loss: config files are parsed generically by key string, so an old `enable_analog_nav` line in a user's file is ignored exactly as it is today. The settings dialog was already cleaned up; this macro is the leftover half.
- **Fix (LESS):** Delete the one `#define` line.
- **Benefit:** One less apparently-live setting that does nothing; matches the recorded removal decision.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High.

### RIPTOPL-356 — `CHEAT_VERSION` value is consumed by no code (test script uses the line only as a text anchor)
- **Category:** 1 (dead code)
- **Locations:**
  - `include/cheatman.h:37` — definition
  - `.github/scripts/test_cheat_capacity.py:24` — uses the `#define CHEAT_VERSION` line purely as a text anchor to slice the header; the "0.5.3.7" value itself is read by nothing
- **Problem (READ):** No C/C++ mention anywhere outside the define. The macro implies a versioned cheat format that nothing checks.
- **Fix (LESS):** Delete the define and re-anchor the capacity script on `extern cheat_entry_t *gCheats` (its other anchor) in the same change; or, if the anchor is deliberately wanted, keep the line and say so in a comment. Deletion plus re-anchor is the cleaner end state.
- **Benefit:** Removes a version string that implies a versioned cheat format nothing checks.
- **Effort:** S
- **Risk:** Low (must update the script anchor in the same change)
- **Confidence:** High.

### RIPTOPL-357 — `ISO_GAME_FNAME_MAX` is a dead macro — no product-code consumer anywhere
- **Category:** 1 (dead code)
- **Locations:**
  - `include/supportbase.h:11` — definition
  - `.github/scripts/test_iso_cache.py:57,106` — the host-test harness carries **its own copy** of the define in its generated stub prelude and uses that copy
- **Problem (READ):** Whole-tree grep finds no use of the header's macro in any `.c/.cpp/.h`. `base_game_info_t` consumers size buffers from `ISO_GAME_NAME_MAX + 1` / `ISO_GAME_EXTENSION_MAX + 1` directly (e.g. `src/sources/httpsupport.cpp:216` uses `sizeof(...extension)`). The header copy survives only as a name the test script independently redefines for itself, so it misleads readers into thinking it is the sizing authority for filename buffers.
- **Fix (LESS):** Delete the define from supportbase.h; the test harness is unaffected (it defines its own). A future consumer can spell `ISO_GAME_NAME_MAX + ISO_GAME_EXTENSION_MAX`.
- **Benefit:** Removes a plausible-looking but unused sizing constant next to the real ones.
- **Effort:** S
- **Risk:** Low (verified zero in-tree consumers; out-of-tree consumers cannot be ruled out — INFERRED none exist for this internal header)
- **Confidence:** High for in-tree deadness.

### RIPTOPL-358 — `APA_IOCTL2_GETHEADER` — dead APA ioctl constant; no APA ioctl2 call site exists
- **Category:** 1 (dead code)
- **Locations:**
  - `include/hddsupport.h:17` — definition; only occurrence of the name in the tree (the two `0x6836` hits in `modules/bdm/bdmfs_fatfs/fatfs/ffunicode.c` are unrelated Unicode table entries)
- **Problem (READ):** Whole-tree grep finds no user. Every `fileXioIoctl2` call site in `src/` uses `USBMASS_IOCTL_*` commands (autolaunch.cpp, system.cpp, sysvmc.cpp, bdmdevices.cpp, bdmlaunch.cpp, bdmsupport.cpp); nothing issues an APA GETHEADER ioctl, by name or by literal.
- **Fix (LESS):** Delete the `#define`.
- **Benefit:** Removes a constant that implies an APA header-query path the code does not have.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High.

### RIPTOPL-359 — `GSM_ARGS` — dead constexpr in the per-game GSM header
- **Category:** 1 (dead code)
- **Locations:**
  - `include/pggsm.h:20` — definition; only occurrence of the name in the tree
- **Problem (READ):** Whole-tree grep returns only the definition. Its neighbour `GSM_VERSION` (pggsm.h:19) **is** live (About dialog, `src/ui/gui.cpp:156`), and `PrepareGSM`/`InitGSMConfig`/`predef_vmode_struct` from the same header are all used in `src/core/gsm.cpp` — `GSM_ARGS` alone has no consumer, not even inside its own header.
- **Fix (LESS):** Delete the one line.
- **Benefit:** Dead constant removed from a header whose live parts are easy to confuse with it.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High.

### RIPTOPL-360 — `IO_ERR_UNKNOWN_REQUEST_TYPE` — error code never returned and never tested
- **Category:** 1 (dead code)
- **Locations:**
  - `include/ioman.h:8` — definition; only occurrence of the name in the tree
  - Contrast: `src/platform/ioman.cpp:291,310` — `IoQueue::put` / `putUnlessWaiting` return `IO_ERR_INVALID_HANDLER` when `findHandler(type)` fails
- **Problem (READ):** Every sibling code in the ioman.h:7-13 block is live (`IO_ERR_TOO_MANY_HANDLERS`, `IO_ERR_DUPLICIT_HANDLER`, `IO_ERR_INVALID_HANDLER`, `IO_ERR_IO_BLOCKED`, `IO_ERR_TOO_MANY_REQUESTS` are all returned in ioman.cpp; the last three are even re-declared in `.github/scripts/test_io_queue_trims.py:91-93` — which pointedly does not re-declare this one). This code has no producer and no consumer, and the name promises a distinct "unknown request type" outcome that the implementation folds into INVALID_HANDLER.
- **Fix (LESS):** Delete the `#define`. Do not "fix" it by adding a new return path — no caller distinguishes the two codes.
- **Benefit:** The error-code block becomes an exact list of outcomes callers can actually observe.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High.

### RIPTOPL-361 — `HDD_LOADMODULES_STATUS_COUNT` — trailing status enumerator never used as a count or bound
- **Category:** 1 (dead code)
- **Locations:**
  - `include/hddsupport.h:79` — enumerator; only occurrence of the name in the tree
  - Contrast: `src/sources/hddsupport.cpp:597-698` — the producer returns/assigns UNK, ERROR, NOERROR, ALREADYLOADED, BUSYLOADING, never COUNT; `include/hddsupport.h:139-143` — `hddLoadModulesReady()` compares against NOERROR/ALREADYLOADED only
- **Problem (READ):** Nothing iterates to it, bounds-checks against it, or sizes an array by it. Caveat stated plainly: a trailing `_COUNT` enumerator is a common idiom, so this may be deliberate future-proofing rather than an oversight.
- **Fix (LESS):** Delete the enumerator if the idiom is not wanted here; otherwise leave it — this is the weakest candidate in this round.
- **Benefit:** The status enum becomes an exact list of states the loader can be in.
- **Effort:** S
- **Risk:** Low
- **Confidence:** Medium that removal is wanted; High that it is currently unused.

### RIPTOPL-362 — `FAV_MAGIC` comment spells the on-disk magic wrong — the bytes are "OFAF", not "OFAV"
- **Category:** 8 (half-finished/redundant features — documentation drift)
- **Locations:**
  - `include/favsupport.h:9` — `constexpr int FAV_MAGIC = 0x4641464F; // 'OFAV' (little-endian on the EE)` (coordinator-verified line)
  - `src/sources/favsupport.cpp:432-439` — `bufU32` little-endian appender; `:471` write; `:239` read/compare
- **Problem (READ):** Serialized little-endian by `bufU32`, the value's bytes in the file are 4F 46 41 46 = "OFAF". (To spell "OFAV" in the file the constant would have to be 0x5641464F.) The code is self-consistent — reader and writer use the same constant — so favourites.bin works; only the comment (and the "OFAV" format name used in favsupport.cpp comments/logs) misdescribes the actual bytes, which will mislead anyone inspecting or reimplementing the format.
- **Fix (LESS):** Correct the comment to 'OFAF'. Do NOT change the constant — that would orphan every existing favourites.bin.
- **Benefit:** Format documentation matches the bytes on disk.
- **Effort:** S
- **Risk:** Low (comment only)
- **Confidence:** High.

### RIPTOPL-363 — Stub `ps2smb.h`: `smbOpenShare_in_t` size comment contradicts the struct (520 vs 516)
- **Category:** 8 (half-finished/redundant features — documentation drift)
- **Locations:**
  - `tests/smb2-live/inc-sdk/ps2smb.h:58-63` — `// size = 520` over `char ShareName[256]; char Password[256]; int PasswordType;`
- **Problem (READ + executed):** Field arithmetic is architecture-independent (no pointers): 256+256+4 = **516**, confirmed by compiling `sizeof(smbOpenShare_in_t)` = 516. All neighbouring size comments in the same file are correct (LogOn 536, Echo 260, QueryDiskInfo 16, ShareEntry 512, GetPasswordHashes_out 32 — all compiled and matched). No runtime effect: both consumers use `sizeof` (`tests/smb2-live/browse.c:63ff`; `modules/network/smb2man/smb2man.c` `arglen < sizeof(smbOpenShare_in_t)`).
- **Fix (LESS):** Change the comment to 516.
- **Benefit:** The one wrong size annotation in the DEVCTL contract copy stops misleading anyone hand-building a DEVCTL buffer.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High.

### RIPTOPL-364 — favsupport.h points readers at a source file that does not exist (`favsupport.c`)
- **Category:** 8 (half-finished/redundant features — documentation drift / rename residue)
- **Locations:**
  - `include/favsupport.h:13` — "Defined in favsupport.c"
  - `include/favsupport.h:164` — "See favsupport.c"
  - The same header correctly states the real file at `include/favsupport.h:59` (`src/sources/favsupport.cpp`)
- **Problem (READ):** There is no `favsupport.c` anywhere in the tree; the implementation is `src/sources/favsupport.cpp`.
- **Fix (LESS):** Change both references to favsupport.cpp.
- **Benefit:** Readers land on the real file.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High.

### RIPTOPL-365 — Orphaned contract comment attached to the wrong declaration in themes.h
- **Category:** 8 (half-finished/redundant features — documentation drift; no code effect)
- **Locations:**
  - `include/themes.h:317` — `void thmGlyphSetRefresh(void);` (no comment)
  - `include/themes.h:320` — thmHintRank's declaration line ends with a second, unrelated comment fragment: "GUI thread, under guiLock: swap the glyphs when the pick changed"
  - Definition site: `src/gfx/themes.cpp:1266-1270`
- **Problem (READ):** The fragment describes `thmGlyphSetRefresh` exactly — its body swaps the glyph set when `thmGlyphSetFor(...) != glyphSetLoaded` — but it sits on `thmHintRank`'s declaration, whose own comment ("its place in hints_order; every unnamed hint shares the last") is left grammatically unfinished by it. A reader trusting the comment would attribute a threading contract (guiLock) to the wrong function.
- **Fix (LESS):** Move the fragment to line 317 as thmGlyphSetRefresh's trailing comment; leave thmHintRank's comment as-is.
- **Benefit:** The guiLock contract lands on the function it constrains.
- **Effort:** S
- **Risk:** Low (comment-only)
- **Confidence:** High.

### RIPTOPL-366 — ioman.h carries an orphan doc comment for a remove-by-type function that does not exist
- **Category:** 1 (dead code — stale documentation)
- **Locations:**
  - `include/ioman.h:38-40` — orphan `/** removes all requests of a given type from the queue … @return the count of the requests removed */` block
  - `include/ioman.h:42-43` — the next declaration, `ioGetPendingRequestCount`, has its own correct comment immediately below the orphan
- **Problem (READ):** No declaration follows the orphan block; no function matching its description is declared in the header, defined in `src/platform/ioman.cpp` (full function list read — handlers, put/putUnlessWaiting, pending counters, block ops; no remove-by-type), or present anywhere in the tree (grep for `ioRemove*` = 0 hits). It reads as documentation of a neighbour and describes an operation a reader will search for in vain.
- **Fix (LESS):** Delete the 3-line comment block only.
- **Benefit:** Header documentation again maps 1:1 onto real declarations.
- **Effort:** S
- **Risk:** Low
- **Confidence:** High.

### RIPTOPL-367 — Duplicate `#include <debug.h>` in opl.h
- **Category:** 7 (build/CI/repo clutter)
- **Locations:**
  - `include/opl.h:33` and `include/opl.h:43` — the same SDK header included twice in opl.h's include block, once after `<ps2ips.h>` and once after `<smem.h>`
- **Problem (READ):** Harmless today only because the SDK header has its own guard; it is pure clutter in the most-included header in the tree.
- **Fix (LESS):** Delete one of the two lines (keep the first).
- **Benefit:** One less redundant include in a header pulled in by nearly every TU.
- **Effort:** S
- **Risk:** Low (guard makes it a no-op change; still needs a normal build to confirm)
- **Confidence:** High.

---

## Checked-clean (verified negatives, by class)

- **Wrong size/count macros (the LAUNCHSTATION-119 class):** negative across the corpus, extending round 7's macro census to a full line-read. Coordinator census extracted every `#define` in the 260 files: the only textually divergent duplicate definitions are conditional-compilation branches of the same file (riptlite.h per-device `LITE_*` branches, ioman.h `LOG`/`PREINIT_LOG` `#ifdef __DEBUG` branches, ratls.h `MBEDTLS_CONFIG_FILE`, ee_core.h `DPRINTF`/`DINIT` debug branches, per-tool `PROGRAM_*` in tools/) or numerically identical stub copies (`GS_PSM_*` in tests/host/sdk_stub/gsKit.h vs igs_api.h). No macro is defined with two different values in two headers. Per-chunk consumer cross-checks additionally verified, among others: `HTTP_CAT_*` maxima agree at 255 in all three places that must match (httpcatalog.h:16, httpsupport.h:14, tools/pc/http/catalog_reference.py:33); `THM_MAX_FILES 64` enforced at its consumer; `IO_REQ_TYPE_COUNT 4` bounds-checked at consumers; apa_header_t and pfs_inode_t field arithmetic totals exactly 1024 bytes each with `nsub` clamped to `APA_MAXSUB` before indexing; `vmc_superblock_t` sums to 512; `hdl_layout_header_t` sums to 1024 and carries its own static assert; xparam `PARAM_*` indices 0x00–0x11 match the 18-entry table in xparam.cpp:146-163 in order; `HDD_VMC_MAP_ENTRIES 11` vs `blocks[10]` verified consistent end-to-end (accept bound <11, copy loop writes at most indices 0–9); grid.h `GRID_MAX_CELLS 24` reconciled against 4×8 presets by `gridClamp`; textures.h enum↔table census executed both ways (118 ids ↔ 118 rows in each `#ifdef` variant); dialogs.h `UI_ITEMS` evaluated numerically under both PADEMU variants — no duplicate non-zero IDs within any single dialog (the `CFG_ENABLEUDPBD`/`CFG_NETBOOTPROTOCOL` placeholders are deliberately retired and documented as such at dialogs.h:73-74 and dialogs.cpp:495 — not filed).
- **Struct size/packing vs consumers:** `EECoreConfig_t` has a single shared definition (coreconfig.h:30-133) included by both ee_core and the loader — layout agreement by construction; the loader locates it by the `EE_CORE_MAGIC_0/1` scan. `GsmConfig_t` likewise single-defined (coreconfig.h:14); pggsm.h:64 only uses it. `gsm_defines.h` `.equ` offsets match gsm_api.c's packed structs and gsm_engine.S allocations; `GSMSourceGSRegs` (igs_api.h, 88 B) matches the asm allocation and ra_overlay.c's private mirror field-for-field. `bdm_event_packet_t` is a single shared definition used by both the EE side and the IOP module (which includes this same header — no private copy). padpatterns.h pattern/mask pairs compiled: all 7 pairs equal length. iso2opl/opl2iso `cfg_t` size difference (64 vs 66 B) is deliberate NUL-room padding with field-by-field IO at the 64 B on-disk sizes — not a format disagreement.
- **Duplicate enum definitions across the loader↔ee_core boundary (values identical, recorded not filed):** `ETH_OP_MODES` (ee_core/include/ee_core.h:44-52 vs include/opl.h:108-117 — byte-identical, coordinator-verified) and `COMPAT_MODE_1..8` (ee_core.h:59-66 vs include/iosupport.h:97-104, plus a third partial copy of `COMPAT_MODE_4` at ee_core/include/patches.h:14 — all values identical; no TU includes both headers). These follow the same pattern earlier rounds recorded as verified-consistent for the COMPAT family; the two sides are separately linked images that exchange the values as plain ints through `EECoreConfig_t`, so the duplication is structural. Any future divergence would be silent — noted here as the standing caveat, not a new finding.
- **Dead declarations (beyond the findings):** per-chunk exhaustive sweeps (353 declared names in chunk C, 541 in chunk B, full prototype greps for ee_core/include, per-family verification in chunk D) found no other declaration-only or never-called names. Non-obvious live cases verified rather than filed: `guiNeutrinoArgs*Handler` (defined by token-pasting macro, guisettings.cpp:1706), `debugSetActive` (via the `LOG_ENABLE()` macro, oplog.h:21), `settingsKeyAt` (host test harness), debug-gated `ListModules` (declared modmgr.h:94-96, defined under the same `__EESIO_DEBUG` gate in modmgr.c:269), all 96 real `IMPORT_BIN2C` symbols in extern_irx.h have a `size_<name>` consumer.
- **Include guards (beyond RIPTOPL-345/346):** coordinator census — no guard-macro name is shared between two different first-party headers in `include/` + `ee_core/include/`; 39 corpus files use `#pragma once`. The census's naive first-lines check flagged 73 files, but manual verification showed most open with comment banners above a present guard; the true unguarded set is exactly the six files in RIPTOPL-345. Style inconsistency (guard spelling, guard below a comment preamble in libview.h/selfupdate.h) was not filed, per the hunt rules.
- **Stub-vs-real divergence (tests/):** host `iosupport.h` stub enumerator values match the real header (ETH 8, HDD 9, APP 10); sdk_stub/gsKit.h PSM values match igs_api.h:42-45; the two `tamtypes.h` stubs are identical; `settings_golden_*.inc` and `ripps2_text_cases.inc` are split-included by design. Real `bdm.h`/`ps2smb.h` are external PS2SDK headers not in the repo, so the pccf/inc-sdk stubs cannot be diffed in-repo beyond RIPTOPL-363 (INFERRED: `GS_PSM_T8 19` in the gsKit stub likewise has no in-repo real counterpart).
- **Fixture drift (.github/scripts/fixtures/):** `diff` vs the live `modules/network/common/ra_{snap,watch}.h` — every shared struct field and macro value identical (incl. `RA_SNAP_HEAD_BYTES 191`, `RA_SNAP_PARTS 9`); the live files only *add* post-alpha16 definitions, and the fixture is a pinned upstream client copy used as such by test_ra_upstream_client.py:21-30 — absence is by design, not drift. `snapshot.h` has no live in-repo counterpart.
- **Round-7 settled groups:** not re-filed; nothing in this corpus contradicted them (RA snapshot macros, IGR_CHEATS_BYTES, igs_api GET_* extractors, MEM_ALIGN, FD_SET family, LZ4_COMPRESSBOUND, UDPRDMA sizes, GS_GIFTAG). One item investigated and dropped as unverifiable: the `// Sony` comment on `SONY_VID 0x12ba` (ds34common.h:9) — the repo alone does not establish the comment is wrong.

## Updated RiptOPL residue statement

After round 9, RiptOPL's headers outside `modules/` and `third_party/` are **line-closed**: 260 files / 16,029 lines read in full, with the classes above either filed (RIPTOPL-344…367) or verified negative. What remains unread in RiptOPL is exactly: (1) the `modules/` tree (~96k header lines plus its sources — the subject of the parallel round-9 modules pass, including the ds34 copies censused under X-49); (2) the `third_party/` tree (parallel round-9 vendored passes); (3) shipped binaries/IRX payloads — sized and hashed in earlier rounds, never disassembled (the binary class is the subject of the parallel round-9 cross-repo binary pass); (4) all hardware behaviour — no PS2 build or console exists in this VM, Category 5 remains structural only, and nothing in any round is hardware-verified. A further round on *this* corpus at this pin has no unread line to take; productive continuations are a pin move, the modules/third_party/binary passes, or a build/hardware rig.

## Count statement

**Findings filed in this report: 24 (RIPTOPL-344…367).** Verified by counting the numbered `### RIPTOPL-` headings in this file after writing; the count in the coordinator's report to the parent is taken from that grep, not from the candidate tally (27 worker candidates, of which 4 guard candidates were consolidated into RIPTOPL-345 per the audit's list-once rule, and the coordinator's own ETH_OP_MODES census candidate was dissolved into checked-clean for consistency with the COMPAT_MODE precedent).
