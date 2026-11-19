# RiptOPL Round 4 — Section 1b: settingsio / dialogs / bdmsupport

Pin: `2e347d5ac885cd0ff73e9ad4143781946396f492` (rebuild/main), clone `~/workspace/audit-clones-2026-11-13/RiptOPL`, pin re-verified unchanged at audit start (`git rev-parse HEAD`), working tree clean, no fetch / no branch switch. READ-ONLY in the repo; this section file is the only write.

**Coverage statement (exact):** all three assigned files read end-to-end, every line, at this pin —
`src/app/settingsio.cpp` lines 1–2611 (EOF), `src/ui/dialogs.cpp` lines 1–2409 (EOF), `src/sources/bdmsupport.cpp` lines 1–2408 (EOF). Cross-checks into headers/callers (`include/dia.h`, `include/opl.h`, `include/iosupport.h`, `src/ui/diaitems.cpp`, `src/ui/dia.cpp`, `src/ui/guisettings.cpp`) were read only where a mechanism inside an assigned file depended on them, and are cited as supporting evidence. No build or hardware claims anywhere; every behavioural consequence that needs hardware is labelled INFERRED.

**Finding count: 14** (settingsio 5, dialogs 3, bdmsupport 6).

---

## settingsio.cpp

### R4-1B-01 — config.path MC mirror creates the folder on the wrong card
- Category: 6 (bug)
- Path: `src/app/settingsio.cpp:361-367` (mirror leg of `writeConfigPathRedirect`); contrast `src/app/settingsio.cpp:242-262` (`preferredMcSlot` / `mcConfigPathRedirect`)
- Problem/mechanism (READ): the mirror target is built by `mcConfigPathRedirect`, which deliberately uses `preferredMcSlot()` — the concrete boot slot, so an mc1 boot mirrors to mc1 even with both cards inserted:
  - `snprintf(out, outLen, "mc%d:/OPL/%s", slot, configPathRedirectFile);` (via `mcConfigPathRedirect`, slot from `preferredMcSlot`)
  But when the first `open(mcPath, O_WRONLY|O_CREAT|O_TRUNC)` fails because `mcN:/OPL` does not exist yet, the folder-creation fallback ignores that slot:
  - `snprintf(mcDir, sizeof(mcDir), "mc%d:/OPL", sysCheckMC());` — `settingsio.cpp:364`
  `sysCheckMC()` returns the *first present* card, not the preferred/boot slot. On an mc1 boot with both cards inserted it creates `mc0:/OPL` (a folder on a card this boot does not use — the exact unwanted-folder failure mode the rest of this file guards against at length), the retry still opens the mc1 path whose folder was never created, `mirrorOk` stays 0, and because a deferred (network) boot returns `mirrorOk`, the whole redirect save reports failure. If `sysCheckMC()` returns −1 the `mkdir` target is literally `mc-1:/OPL`.
- Fix in words: build `mcDir` from the same slot that produced `mcPath` (return the slot from `mcConfigPathRedirect` or recompute with `preferredMcSlot()`); delete the `sysCheckMC()` call from this leg.
- Benefit: network-boot settings redirect actually persists on dual-card mc1 setups; no stray folder stamped on the other card.
- Effort: S. Risk: low (one snprintf argument source). Confidence: READ (mechanism); the dual-card mc1-boot trigger is READ from `preferredMcSlot`'s own contract, the on-hardware symptom is INFERRED.

### R4-1B-02 — Network IP strings are parsed with unchecked `sscanf`, unlike the RA parser in the same file
- Category: 6 (bug/security-adjacent robustness)
- Path: `src/app/settingsio.cpp:1931` (SMB server IP), `:1936` (HTTP server IP), `:1950` (PS2 IP), `:1952` (netmask), `:1954` (gateway), `:1956` (DNS)
- Problem/mechanism (READ): all six sites are bare `sscanf(temp, "%d.%d.%d.%d", &pc_ip[0], …)` with no return-value check and no 0–255 range check, writing into `int[4]` globals (`include/opl.h:119,125`). A hand-edited or corrupt `conf_network.cfg` value such as `999.1.2.3`, `-5.0.0.1` or `1.2.3` is accepted as-is (partial parses leave the remaining octets at whatever previous/default value was there, silently mixing two sources). The values are later formatted back into the `ip=%d.%d.%d.%d` module argument for the UDP ministack (see `bdmsupport.cpp:769,1331`) and into SMB connection targets, so an out-of-range octet reaches a network stack as a nonsense address instead of being rejected at load. The same file already contains the correct pattern: `configReadRA` at `settingsio.cpp:1534` requires `sscanf(...) == 4`, a trailing-character check (`%c` must not consume), and per-octet 0–255 range checks before accepting `CONFIG_OPL_RA_HOST_IP`.
- Fix in words: route all six sites through one small validated parse helper with the RA parser's contract (exactly 4 fields, no trailing junk, each octet 0–255; on failure keep defaults and log). This is a justified ADD under LESS-NOT-MORE only because it replaces six unvalidated copies with one validated one — net code shrinks.
- Benefit: a malformed network config can no longer produce a mixed/garbage IP configuration or an invalid ministack argument; one parser to audit instead of six.
- Effort: S. Risk: low. Confidence: READ.

### R4-1B-03 — `config.path` bootstrap redirect is rewritten non-atomically (O_TRUNC, two writes, unchecked close), and the write leg is duplicated
- Category: 6 (bug) + 2 (duplication)
- Path: `src/app/settingsio.cpp:348-354` (primary leg), `:361-377` (MC mirror leg) in `writeConfigPathRedirect`
- Problem/mechanism (READ): both legs open the live redirect file with `O_WRONLY | O_CREAT | O_TRUNC`, then issue two separate writes —
  - `int n1 = write(fd, path, strlen(path)); int n2 = write(fd, "\n", 1); close(fd); primaryOk = (n1 == (int)strlen(path) && n2 == 1);` (`:350-353`, repeated verbatim at `:369-372`)
  There is no temp-file-plus-rename, no single combined write, and `close()`'s return is discarded, so a reset/power loss (or a failing card) between truncate and completion leaves an empty or partial `config.path`. The reader (`readConfigPathRedirect`, `:313-341`) tolerates that by returning 0 — i.e. the Custom Settings Path bootstrap is silently lost and the next boot falls back to discovery/defaults, with no error anywhere. This file is otherwise extremely careful about exactly this class of loss (the deferred-home and FifthFox comment blocks); the redirect file itself is the one artifact that exists solely to survive a reboot, and it is the one written least defensively. The payload configs' own write discipline lives in `config.cpp` (outside this section) and is not assessed here.
- Fix in words: compose the full line (`path` + `"\n"`) in one stack buffer and issue one `write`; check `close()`; factor the identical open/write/close sequence into one static helper used by both legs (deleting the second copy). A temp+rename would be stronger still but is an ADD — only adopt it if the single-write form is judged insufficient; the single-write + helper form is the LESS-NOT-MORE fix.
- Benefit: shrinks the corruption window to one write and removes ~10 duplicated lines; mirror and primary can no longer drift.
- Effort: S. Risk: low. Confidence: READ (mechanism); power-loss-mid-write timing is INFERRED (no hardware test).

### R4-1B-04 — Per-device config load/save legs are copy-pasted per slot and per direction
- Category: 2 (duplication)
- Path: `src/app/settingsio.cpp:88-118` (`checkLoadConfigMMCE`), `:1984-2001` (`trySaveConfigMMCE`), with the same shape in `:120-149` (`checkLoadConfigHDD`) / `:2028-2040` (`trySaveConfigHDD`) and `trySaveConfigBDM`/`trySaveConfigMC` (`:2003-2047`)
- Problem/mechanism (READ): `checkLoadConfigMMCE` contains two near-identical legs differing only in the literal `"mmce0:"`/`"mmce1:"` (opendir → closedir → `configEnd()` → `configInit()` → `configReadMulti` → set start-mode AUTO → return). `trySaveConfigMMCE` repeats the two-leg pattern a second time in the save direction. Each additional slot or policy tweak must be edited in four places, and the legs have already had to grow slot-preference logic elsewhere in this file (`tryMissingConfigPathRecovery`'s slot-order array, `:855-880`) that these older legs do not share.
- Fix in words: one loop over a two-entry slot table (`{"mmce0:", "mmce1:"}`) inside each function, or one shared probe helper parameterised by direction. Pure deletion/restructure, no new behaviour.
- Benefit: ~25 lines deleted; load and save slot policy cannot diverge again.
- Effort: S. Risk: low-medium (must preserve the early-return-on-first-success and the AUTO-mode side effect per leg). Confidence: READ.

### R4-1B-05 — `prepareCustomApaSettingsPath` is a ~190-line hand-rolled parser with three ad-hoc separator-eating loops and a dual-purpose buffer
- Category: 3 (overengineering / complexity)
- Path: `src/app/settingsio.cpp:400-590` (whole function); separator loops at `:451-458`, `:477-495`, `:505-512`; buffer reuse at `:400-402` vs `:518-527`
- Problem/mechanism (READ): the function re-implements path tokenisation at least three times — one loop to skip `/`/`:`/`\` after a pfs alias, one while-loop chain to consume partition label + redundant `pfs:` components in the hdd branch, one to consume a redundant `OPL` component — each with slightly different terminator sets. The 64-byte `subfolder` buffer is used for two unrelated meanings in sequence (first the APA partition label for the `gOPLPart` comparison, then, reused from `:518`, the remaining sub-path), so a reader must track which lifetime is live. The function is correct on the paths traced (see checked-and-clean for the `canonical[64]` bound), but its size is out of proportion to the other path normalisers in the same file (`normalizeMcPath` does its job in 14 lines), and every new APA spelling (the comments show several already: `pfs:`, `pfs0:`, `+OPL`, `hdd0:/__common/OPL`, …) has historically been added as another special-case block.
- Fix in words: split the path once into components (device / partition-label / rest) with a single separator-skipping helper, then express the alias rules as data checks on those components; do not add new spelling branches without removing an equivalent special case. No behaviour change intended.
- Benefit: the highest-risk string code in the settings path becomes auditable in one pass; future APA spellings stop accreting blocks.
- Effort: L. Risk: medium (this parser guards real partition-ownership checks — refactor only behind the existing LOG/reject behaviour, characterisation first). Confidence: READ (structure); no claim any current spelling is mis-parsed.

---

## dialogs.cpp

Scope note (READ): this file contains no executable logic — it is declarative `UIItem` tables plus the static `UIString` storage for every text row (`dialogs.cpp:14-42`). The findings below are therefore about the storage contract and table structure this file imposes on its consumers; consumer-side evidence is cited where the mechanism completes there.

### R4-1B-06 — All dialog text rows share 32-byte storage, silently truncating the Custom Settings Path (64) and Default BGM Path (128) on any Settings round-trip
- Category: 6 (bug — silent data loss)
- Path: `src/ui/dialogs.cpp:24` (`diaStr_CFG_CUSTOMCFGPATH`), `:35` (`diaStr_CFG_DEFAULT_BGM_PATH`); storage type at `include/dia.h:56-61`
- Problem/mechanism (READ): every `UI_STRING`/`UI_PASSWORD` row in this file stores its text in a `struct UIString`, which is two `char[UI_STRING_SIZE]` arrays with `UI_STRING_SIZE` = 32 (`include/dia.h:56-60`). `diaSetString` truncates into that storage: `strncpy(item->stringvalue.text, text, UI_STRING_SIZE); item->stringvalue.text[UI_STRING_SIZE - 1] = '\0';` (`src/ui/diaitems.cpp:130-144`), and the on-screen keyboard edits a `char tmp[32]` (`src/ui/dia.cpp:937-948`). Two rows in this file feed destinations far larger than 31 usable chars with a *direct* set/get round-trip and no handler/edit-buffer side channel:
  - `CFG_CUSTOMCFGPATH` → `gCustomSettingsPath[64]` (`include/opl.h:484`; set at `src/ui/guisettings.cpp:2375`, read back at `:2398` with `sizeof(gCustomSettingsPath)`)
  - `CFG_DEFAULT_BGM_PATH` → `gDefaultBGMPath[128]` (`include/opl.h:445`; set/get at `src/ui/guisettings.cpp:1979` / `:1954`)
  So a custom settings path or BGM path longer than 31 characters — perfectly valid in the config file, and for the settings path explicitly supported up to 63 chars by all of settingsio.cpp's machinery — is silently cut to 31 chars the moment the user opens the containing Settings page and confirms it; the next save then persists the truncated path (and, for the settings path, writes a `config.path` redirect pointing at the wrong folder, cf. R4-1B-03). The longer path rows that *do* work (ExitTo / POPSTARTER / Neutrino paths, `dialogs.cpp:23` and the `CFG_*_PATH` rows) only work because guisettings gives them full-size private edit buffers plus handlers — i.e. the workaround exists in-tree and these two rows never got it.
- Fix in words: give these two rows the same treatment as the other path rows (full-size edit buffer + existing handler pattern in guisettings), or — smaller — widen only the storage for path-class rows. Any widening is a justified ADD: it is tied to this cited silent-truncation bug, and the alternative (capping the config keys at 31) would delete a documented settingsio feature. Do NOT raise `UI_STRING_SIZE` globally — that would grow every row's storage for two rows' benefit.
- Benefit: two settings stop being silently corrupted by merely opening their page.
- Effort: M. Risk: medium (touch the two rows' set/get call sites, nothing else). Confidence: READ (full chain read: dialogs storage → dia.h size → diaitems truncation → guisettings round-trip → destination sizes).

### R4-1B-07 — `diaPadEmuInfo` hand-enumerates all 64 Bluetooth feature bits as literal rows
- Category: 2 (duplication) / 4 (size)
- Path: `src/ui/dialogs.cpp:1741-1913` (`diaPadEmuInfo`), esp. the feature grid `:1797-1911`
- Problem/mechanism (READ): the table contains 64 individually written label rows, `PADCFG_FEAT_START + 0` through `PADCFG_FEAT_START + 63` (`grep -c PADCFG_FEAT_START` = 64), laid out in a transposed order (each visual row runs +0,+8,+16,…,+56, then +1,+9,…) with the `"00."`–`"63."` captions also written out literally. That is ~115 lines of table for what is one repeating pattern, and any renumbering or layout slip is invisible to the compiler — a transposed pair would simply label the wrong feature bit in the BT info screen. The same unrolled pattern appears in miniature in `diaFileManager` (12 × `FM_ROW_FIRST + n`, `:1969-2003`) and `diaDriveStatus` (16 × `DSTAT_LINE_FIRST + n`, `:2044-2081`).
- Fix in words: build the feature grid (and, if touched anyway, the FM/DSTAT row runs) into a static `UIItem` array at first use with a loop — the dialog arrays are already mutated at runtime by consumers (`diaSetLabel` etc.), so a lazily initialised table is consistent with existing practice. Keep the transposed index arithmetic in exactly one place, commented.
- Benefit: ~100 lines deleted from this file; the bit-to-label mapping becomes provable by reading one loop.
- Effort: M. Risk: medium (row order/ids must be preserved exactly — this is a display-only dialog, no persisted state). Confidence: READ.

### R4-1B-08 — The IP-quad row pattern and the POPSTARTER network tables duplicate each other
- Category: 2 (duplication)
- Path: `src/ui/dialogs.cpp:44-283` (`diaNetConfig`), `:283-352` (`diaPopsUdpfsConfig`), `:354-450` (`diaPopsNetConfig`)
- Problem/mechanism (READ): one IPv4 address costs 9–11 table rows here (4 × `UI_INT` + 3 × `"."` `UI_LABEL` + label/spacer/break), and the pattern is written out in full at least ten times in `diaNetConfig` alone (PS2 IP, netmask, gateway, DNS, SMB IP, HTTP IP, RA host IP, plus the POPSTARTER tables). More pointedly, `diaPopsUdpfsConfig` and `diaPopsNetConfig` define the *same* fields with the *same* ids (`NETCFG_POPS_IP_0..3`, `NETCFG_POPS_MASK_0..3`, `NETCFG_POPS_GW_0..3`, `NETCFG_POPS_SMB_IP_0..3`) in two separate tables — the UDPFS sub-dialog is a near-copy of the top half of the SMB dialog — so any change to those rows (range, hint, ordering) must be made twice and can silently drift.
- Fix in words: introduce one C macro that expands to a full quad's rows inside these initialisers (the tables are plain array initialisers, so a multi-row macro is a pure deletion), and express the shared POPSTARTER quads once. No runtime change.
- Benefit: roughly 150–200 table lines deletable; the duplicated POPSTARTER fields get a single definition.
- Effort: M. Risk: low-medium (initialiser order is display order — expand in place, change nothing else). Confidence: READ.

---

## bdmsupport.cpp

### R4-1B-09 — `bdmIsReadOnlyPath` parses the mass unit with the unbounded accumulate that `bdmParseBootStem` — in the same file — explicitly fixed
- Category: 6 (bug/security robustness)
- Path: `src/sources/bdmsupport.cpp:887-897`; contrast `:179-215` (`bdmParseBootStem`)
- Problem/mechanism (READ):
  - `while (*p >= '0' && *p <= '9') unit = unit * 10 + (*p++ - '0');` then `bdmModeIsReadOnly(BDM_MODE + unit)` (`:893-895`)
  There is no digit-count bound, so a path like `mass99999999999999999999:/…` overflows signed `int` (UB) during the accumulate. `bdmParseBootStem`, 700 lines earlier in the same file, documents exactly this failure — "an unbounded accumulate over launcher-controlled argv[0] bytes would run signed arithmetic into UB on a mangled path (`mass4294967296:`)" — and caps the parse at 2 digits. The hardening was applied to the boot-path parser and missed this one. The result is *probably* benign today (`bdmModeIsReadOnly` range-checks its argument before indexing, `bdmsupport.cpp:880-885`, so most wrapped values return 0), but a wrapped value that lands inside `BDM_MODE..BDM_MODE_LAST` would report an unrelated device's read-only state for this path, and its only caller (`BdmSource::config`, `:2055`) uses that answer to decide where per-game settings for a read-only device are stored. The input is a config home path, not raw network data — hence robustness, not a claimed exploit.
- Fix in words: reuse the same bound as `bdmParseBootStem` (reject >2 digits, or call that parser). Deletion-shaped: replace the hand accumulate with the existing bounded helper.
- Benefit: removes the file's last unbounded unit parse; the two parsers can no longer disagree about what a mass path is.
- Effort: S. Risk: low. Confidence: READ.

### R4-1B-10 — The network block-device load chain is implemented twice, with divergent latch/dev9 handling
- Category: 2 (duplication)
- Path: `src/sources/bdmsupport.cpp:751-825` (`BdmSupport::ensureNetworkSourceModules`) vs `:1325-1375` (network leg of `bdmLoadBlockDeviceModules`)
- Problem/mechanism (READ): both legs build the same `ip=%d.%d.%d.%d` argument from `ps2_ip`, call `sysInitDev9()`, and load the same chains in the same order — UDPFS: `udpfs_smap_irx` → `udpfs_ministack_irx` (with ip arg) → `udpfs_bd_irx`; UDPBD: `smap_udpbd_irx` (with ip arg) — setting the same latches (`udpbdModLoaded`, `udpbdNicClaimed`, loaded protocol). They already differ in the details that matter: the `ensure…` version writes the member `udpbdLoadedProtocol_` directly (including after the SMAP step, before the ministack has succeeded, `:789-790`), the worker version goes through `gBdmSupport.setLoadedNetProtocol(...)`; the worker version wraps loads in the diag stage helpers and releases dev9 only `if (!udpbdNicClaimed)` at the end, the `ensure…` version releases it inline on the `!udpbdNicClaimed` branch. Any future IRX-chain change (order, args, a fourth module) must be made in both, and a one-sided edit leaves the custom-path loader and the menu loader describing different resident transports — the exact failure the long comment at `:71-80` says the loaded-protocol identity exists to prevent.
- Fix in words: one static chain-loader (protocol + diag-stage flag in, result + latch updates out) called by both entry points; keep each caller's own locking/timeout policy outside it.
- Benefit: ~50 lines deleted; one chain definition, so the two entry points cannot diverge again.
- Effort: M. Risk: medium (semaphore ownership differs between callers — `ensure…` holds `bdmLoadModuleLock` across its leg; the worker takes it for the whole function). Confidence: READ.

### R4-1B-11 — `bdmHDDIsPresent` can burn its timeout twice on one call, on the settings-save path
- Category: 5 (performance / blocking)
- Path: `src/sources/bdmsupport.cpp:2276-2323`; caller `src/app/settingsio.cpp:2015` (`bdmHDDIsPresent(5000)`)
- Problem/mechanism (READ): when `bdmWaitForDevice(hdd_id, timeoutMs)` *succeeds* but the answered device turns out not to be ATA, neither `timedout` branch is taken, so step 3 runs:
  - `if (!timedout) { … DelayThread(timeoutMs * 1000); }` (`:2317-2320`)
  i.e. after already waiting up to `timeoutMs` for a device that did answer, the function sleeps a *second full* `timeoutMs` before rescanning. On the settings-save path that is up to ~10 s of IO-worker blocking for a single probe (5 s wait + 5 s sleep), inside a deferred-IO save whose overall abandon bound is 30 s (`OPL_DEFERRED_IO_TIMEOUT_MS`, `include/gui.h:194`). The other branches set `timedout = 1` and skip the second sleep — only the "device answered but is not ATA" branch pays double.
- Fix in words: charge the step-3 sleep only the *remaining* budget (`timeoutMs` minus elapsed wait), or skip it when the wait already consumed the budget. No new mechanism.
- Benefit: worst-case save-path probe drops from ~2× to 1× the requested timeout.
- Effort: S. Risk: low (the rescan still happens; only the redundant full-length sleep shrinks). Confidence: READ (arithmetic from the code); actual on-hardware save latency is INFERRED.

### R4-1B-12 — One-shot probe latches inside `bdmLoadBlockDeviceModules` / `needsUpdate` survive an IOP reset that `bdmIopWasReset` cannot reach
- Category: 6 (bug) / 8 (half-finished reset handling)
- Path: `src/sources/bdmsupport.cpp:62-70` (`bdmIopWasReset`) vs function-local statics at `:1384` (`moduleLoadFirstPassDone`), `:1415` (`mx4sioSecondProbeDone`), `:1462` (`usbSecondProbeDone`), `:1472` (`ilinkProbeDone`), and `:1806` (`lastModuleLoadGen` in `BdmSource::needsUpdate`)
- Problem/mechanism (READ): `bdmIopWasReset` exists precisely because "an in-menu IOP reset unloads every module, but these latches kept reading 'loaded'" — and it clears every *global* latch (`iUSBModLoaded`, `mx4sioModLoaded`, `hddModLoaded`, `udpbdModLoaded`, …). The one-shot *function-local* statics cannot be cleared from there: after a reset, the transports reload (globals cleared) but the USB/MX4SIO settle-and-second-probe legs and the iLink recheck never run again this session, because their `*SecondProbeDone`/`ilinkProbeDone` flags are still 1 — the fast-stick/slow-card cases those probes were added for (see the long comments at `:1403-1470`) silently lose their only retry until reboot. `moduleLoadFirstPassDone` staying 1 is at least consistent with its comment (late loads should bump), but `lastModuleLoadGen` also survives: if no hotplug/generation bump follows the reset, `needsUpdate`'s module-queue gate (`lastModuleLoadGen != BdmGeneration`) can suppress re-queueing the block-module load for the current generation.
- Fix in words: move the four probe flags (and `lastModuleLoadGen`) to file-scope statics and clear them in `bdmIopWasReset`, next to the latches it already clears — this completes the reset function's stated contract rather than adding a new mechanism.
- Benefit: post-reset device discovery behaves like first-boot discovery again.
- Effort: S. Risk: low-medium (re-running a bounded 600 ms settle probe after a reset is the pre-existing first-boot cost). Confidence: READ (latch inventory and reset contract); the post-reset on-hardware symptom is INFERRED.

### R4-1B-13 — The per-slot identity-enumeration loop is copy-pasted at least four times, and the network wait loop re-runs it from slot 0 every 100 ms
- Category: 2 (duplication) / 5 (performance)
- Path: `src/sources/bdmsupport.cpp:495-525` (`bdmGetDeviceRootByType`), `:527-553` (`bdmGetDeviceSlotsByType`), `:555-585` (`bdmBuildTypedHandoffPath`), `:631-675` (`bdmProbeMassSlots`); polling caller `:815-822`
- Problem/mechanism (READ): four separate loops each hand-roll the same probe — `snprintf("mass%d:/")` → `fileXioDopen`/identity read (driver-name ioctl + device-number ioctl) → `bdmDetermineDeviceType` — differing only in what they do with a match (first root / all slots / typed-index counting / summary). A fifth copy exists in the `__DEBUG` typed-identity resolver (`:372-450`). Beyond the maintenance cost, `ensureNetworkSourceModules` waits for the UDPBD root by calling `bdmGetDeviceRootByType` in a `DelayThread(100 ms)` poll until timeout (`:815-822`), and every poll restarts the scan at slot 0 with fresh Dopen+ioctl round-trips on every preceding slot — on the IO worker, for the whole timeout, when the target device is by definition the last one the scan reaches.
- Fix in words: one enumeration helper that fills a small per-slot identity array once; the four consumers read from it. The wait loop can then poll the helper (or just the one slot range) instead of re-probing from zero. Pure restructure.
- Benefit: one probe implementation to harden (cf. R4-1B-09, which is what divergent copies cost); materially fewer IOP round-trips during network-source waits.
- Effort: M. Risk: medium (callers have subtly different tolerance for a failed device-number ioctl — the comments at `:510-514` document one; the helper must preserve "driver name alone is enough"). Confidence: READ.

### R4-1B-14 — `bdmDeviceIsATA` stack-allocates a whole `bdm_device_data_t` to hold one 32-byte driver string, and double-checks the match
- Category: 4 (size/memory) / 3 (overengineering, minor)
- Path: `src/sources/bdmsupport.cpp:2244-2263`
- Problem/mechanism (READ): the function declares `bdm_device_data_t data;` — the full per-device struct (roots, prefixes, game-array pointers, counts and flags) — solely to `memset` and fill its `bdmDriver` field via `bdmReadDriverName`. It then returns `(!strcmp(data.bdmDriver, "ata") && strlen(data.bdmDriver) == 3)`, where the `strlen == 3` clause is fully implied by the `strcmp` against a 3-char literal. Every other identity reader in the file uses a plain `char driver[32]` (e.g. `bdmReadDeviceIdentity`, `:301-318`).
- Fix in words: use `char driver[32]` like the rest of the file and return the `strcmp` result alone. Net deletion.
- Benefit: smaller stack frame on a function called in the HDD-presence scan loop (`bdmGetATADeviceId`, `:2265-2274`); one less struct-layout dependency.
- Effort: S. Risk: low. Confidence: READ.

---

## Checked-and-clean (candidates that dissolved on a full read, with reasoning)

**settingsio.cpp**
- `normalizeMcPath` memmove arithmetic (`:593-600`): `memmove(&path[5], &path[4], len - 3)` — count is `len-4+1`, i.e. the tail plus its NUL; correct, and the `len + 1 >= size` guard precedes it.
- `createMcSettingsFolder` level loop (`:603-640`): iterates each path level, restores the truncated separator each iteration, returns on first `mkdir` failure with `gLastSaveErrno` set; root case (`len <= 5`) early-outs correctly.
- `writeConfigPathRedirect` returning `mirrorOk` (ignoring `primaryOk`) for deferred boots (`:385-388`): deliberate and correct — on a network boot the primary CWD copy is on the unmountable share, so the MC mirror is the only copy the next boot can read; the comment states this and the read side matches it.
- `readConfigPathRedirect` (`:313-341`): reads at most `outPathLen - 1`, trims trailing CR/LF/space/tab, always NUL-terminates; no overflow path found.
- `prepareCustomApaSettingsPath`'s `canonical[64]` bound (`:400-402, 543-560`): looks like a truncation risk against 256-byte path buffers elsewhere, but the source value is capped globally — `gCustomSettingsPath` is `char[64]` (`include/opl.h:484`) — and over-long compositions are rejected with `ENODEV`, not truncated. Not a bug (the function's complexity is filed separately as R4-1B-05).
- `configWriteChecked` (`:2095-2115`): correctly closes the documented `configWriteMulti` sum hole by re-auditing every requested set's `modified()` flag after the write; the bit-walk covers all `CONFIG_INDEX_COUNT` types.
- `_saveConfig`'s BDM save retry (`:2450-2465`): re-resolves through `bdmResolveBootDir` pinned to the same boot identity and only re-points the config home if the resolved dir actually changed; it does not scatter to another device. Clean.
- `tryMissingConfigPathRecovery`'s 1500 ms USB probe (`:895`): a real blocking cost, but bounded, read-only, confined to the no-config-anywhere recovery path, and explicitly documented in-code with its profiling note. Accepted as designed; not filed.
- `checkLoadConfigHDD` reusing `value` as fd then result (`:120-149`): ugly but sound — the fd is closed before the variable is reused, and the legacy-filename fallback composes into the same 64-byte buffer with `snprintf`.

**dialogs.cpp**
- The "legacy callers" sub-dialog tables (`diaMmceCommConfig`, `diaMmcePathConfig`, `diaBdmaConfig`, `diaVcdListConfig`, `diaSecurityConfig`, `diaAdvancedConfig`, and peers): suspected dead code from their comments, but every one is referenced outside this file — composed into Settings-shell pages or executed directly in `src/ui/guisettings.cpp` (e.g. `:359`, `:1103`, `:1243-1248`, `:2358`). Not dead.
- Handler-mediated long-path rows (`CFG_EXITTO` `dialogs.cpp:468`, POPSTARTER and Neutrino path rows): these bypass the 32-byte limit in R4-1B-06 via full-size private edit buffers in guisettings (`exitPathEdit[sizeof(gExitPath)]`, `neutrinoPathEdit`/`popstarterPathEdit[sizeof(...256)]`, `src/ui/guisettings.cpp:128,1071-1072`) plus their `gui*PathHandler`s. Checked and excluded from R4-1B-06, which is scoped to the two rows lacking that treatment.
- SMB share/user/password and NB-address `UIString` rows (`dialogs.cpp:14-20`): their destinations are 32-byte (share/user/pass) and 17-byte (NB address) globals (`include/opl.h:128-131`), so the 31-usable-char dialog storage does not truncate any value the rest of the program can hold. Clean.
- `diaDateTimeConfig` ranges (`:910-940` region): year 2000–2099, month 1–12, day 1–31, hour 0–23, minute 0–59 are all bounded in the table itself; the day-vs-month-length question belongs to the consumer's RTC code, not this file.

**bdmsupport.cpp**
- `bdmFindPartition`'s unbounded `sprintf(target, …)` (`:2371-2382`): dissolves on reading the only call sites — `trySaveConfigBDM` passes `char path[64]` (`settingsio.cpp:2006,2011,2016`) and `gBDMPrefix` is `char[32]` (`include/opl.h:499`), so the longest composition (`mass7:/` + 31 chars + NUL = 40) cannot overflow today. Latent fragility only (no length in the signature); not filed as a finding.
- `itemList->mode` used directly as the mass slot in `bdmBuildPs1Prefix` calls and in `BdmSource::shutdown`'s fallback path (`:2066, 2195` region): looks like a mode/slot confusion, but `BDM_MODE = 0` (`include/iosupport.h:11`), so BDM item modes *are* slot indices. Clean.
- The `__DEBUG`/`__OPLDIAG` diagnostic apparatus (`bdmDiagProbeIdentity`, `bdmDiagResolveTypedIdentity`, `bdmLogIdentityDiagnostic`, snapshot plumbing; roughly `:95-110, 372-490, 950-1050`): large, but compile-gated with non-debug stubs provided (`bdmCaptureConfigWriteDiag` reduces to `valid = 0`, `bdmLogIdentityDiagnostic` to a no-op), so it costs no release size and is a deliberate hardware-triage tool. Not filed; noted here so the coverage is explicit.
- `bdmReadDriverName`'s missing precondition probe (`:259-299`): the long comment documents a *retraction* — the earlier probe was sent to the wrong ioctl entry point and broke identity reads on every device. The current direct `fileXioIoctl2` matches the form the file's other call sites used historically. Checked; the reasoning in-code is sound on its own terms.
- `BdmSupport::ensureNetworkSourceModules` semaphore handling (`:751-825`): every early return after `WaitSema(bdmLoadModuleLock)` was traced — partial-claim, wrong-protocol, dev9-failure and success paths all `SignalSema` (or return before taking it). No leak found. (Its duplicated chain is R4-1B-10; its latch handling is not separately faulted.)
- `bdmProbeMassSlots` summary composition (`:631-675`): appends are bounds-checked per iteration (`len > 0 && len < sizeof(summary)`, break on `n <= 0`); the 96-byte summary cannot overflow. Clean.
- `BdmSource::cleanUp` vs `shutdown` free legs (`:2155-2215`): the two functions share a ~6-line free sequence, but `shutdown` additionally carries the terminal-only STOP UNIT devctl gate and a NULL-`priv` fallback the cleanup path does not need; the overlap is below the threshold worth a finding.

---

*End of section 1b. No repo files were modified; no build was run; no hardware claims are made.*
