# RiptOPL Round 4 — Section 2a: DS3/DS4 controller drivers, standalone vs pademu copies

- Repo: NathanNeurotic/RiptOPL, pin `2e347d5ac885` (rebuild/main), clone `~/workspace/audit-clones-2026-11-13/RiptOPL`. Pin re-verified with `git rev-parse` at audit start; no fetch, no branch switch, no writes in the repo.
- Files in scope (all read in full):
  1. `modules/ds34bt/iop/ds34bt.c` (1,719 lines) — standalone BT driver, IRX + SIF-RPC shell, `MAX_PADS 2`
  2. `modules/pademu/ds34bt.c` (1,425 lines) — pademu copy, in-process API, `MAX_PADS 4`
  3. `modules/ds34usb/iop/ds34usb.c` (998 lines) — standalone USB driver, IRX + SIF-RPC shell, `MAX_PADS 2`
  4. `modules/pademu/ds34usb.c` (616 lines) — pademu copy, in-process API, `MAX_PADS 4`
- Context files read for constants/callers only (not audited as findings targets): the four sibling `ds34bt.h`/`ds34usb.h` headers (report-length constants), `include/ds34common.h` (report struct layouts; `struct ds3report` = 47 bytes packed, `struct ds4report` ≈ 42 bytes packed, READ by field count), `modules/pademu/ds34common.c` (shared translators the pademu copies delegate to), `modules/pademu/pademu.c` (caller-side port/size provenance for the pademu entry points).
- Constants used throughout, READ from both headers of each pair: `PS3_01_REPORT_LEN = 0x30` (48), `PS4_11_REPORT_LEN = 0x4D` (77), `PS3_F4_REPORT_LEN = 0x04`. `output_01_report[]` template in both standalone files is 48 bytes (counted).
- Method (as tasked): real `diff -u` per pair, then comment/whitespace-stripped diff per pair, then every divergent region read in context in both copies; entry points of all four files read in full even where copies agree. Raw unified diffs: BT pair 19 hunks / 786 diff lines, USB pair 16 hunks / 832 diff lines. Stripped diffs: BT 649 lines, USB 691 lines (the stripped diffs stay large because the pademu copies are genuine rewrites of the report path, not cosmetic forks).
- No build, no hardware, no runtime claims anywhere below. Every finding is labelled READ (mechanism visible in the quoted lines) or INFERRED (reachability/impact depends on caller, protocol, or USBD behaviour not in these files).

---

## 1. Divergence tables

Tallies count the enumerated regions below, not diff hunks. Buckets: **intentional variant** (the copies serve different hosts: standalone = EE-facing RPC module for the OPL GUI phase; pademu = in-process pad emulation inside games), **drift** (same intent, cosmetically or accidentally different, no behavioural effect found), **suspected bug** (semantically different where the copies should agree, or wrong in both — detailed as findings in §2).

### 1a. BT pair — `modules/ds34bt/iop/ds34bt.c` vs `modules/pademu/ds34bt.c`

| # | Region | Bucket |
|---|--------|--------|
| B1 | `MAX_PADS` 2 → 4 (`ds34bt.c:344` vs `ds34bt.c:331` in pademu file order: standalone `#define MAX_PADS 2`, pademu `#define MAX_PADS 4`) | intentional variant (pad-count) |
| B2 | IRX shell: `IRX_ID`, `_start`, RPC thread/`rpc_sf`, `DS34BT_*` command IDs in standalone only; pademu exposes `ds34bt_init(u8 pads, u8 options)`, `ds34bt_get_data` returning int, `ds34bt_set_mode` | intentional variant (RPC/IRX shell vs in-process API) |
| B3 | Standalone-only services dropped in pademu: `ds34bt_set_led`, `ds34bt_get_bdaddr`, `ds34bt_get_ver`, `ds34bt_get_feat`, `ds34bt_init_charging` | intentional variant (feature) |
| B4 | HCI init chain `hci_read_bdaddr` → version → features dropped in pademu; reset now goes straight to `hci_write_scan_enable`; `bt_dev.vid/pid/rev` fields go with it | intentional variant (feature; feeds B3) |
| B5 | Fake-DS3 detection gate: standalone `disable_fake` (standalone:359, read at :728) is never written anywhere — detection is permanently on; pademu `enable_fake` (pademu:340) is opt-in via `options & 1` in `ds34bt_init` (pademu:1382) | intentional variant (config plumbing) — but see finding R4-2A-11 for the LED-header half that did not survive the port |
| B6 | Report translation: standalone open-coded in `hid_readReport`; pademu delegates to `translate_pad_ds3`/`translate_pad_ds4` (`ds34common.c`) + `padMacroPerform` | intentional variant (feature: pad macros) |
| B7 | `analog_btn`/`btn_delay` state + PS+Select (DS3) / PS+Share (DS4) analog-toggle feature, pademu only | intentional variant (feature) |
| B8 | DS3 LED/rumble report construction rewritten: standalone copies the 48-byte `output_01_report` template; pademu memsets 48 bytes and copies a 5-byte LED payload group only for lit LEDs (pademu:234–235, :1155, :1168–1175) | intentional variant (rewrite; wire bytes differ, hardware equivalence INFERRED) |
| B9 | L2CAP `L2CAP_CMD_CONFIG_RESPONSE` on the control channel: standalone unconditionally requests the interrupt channel (standalone:1069–1070 in the connection-response path; config path equivalent); pademu requests it only `if (ds34pad[pad].type == DS4)` (pademu:994–997) and sets `btn_delay = 0xFF` after config responses (pademu:1014) | intentional variant (semantic; DS3 interrupt channel is device-initiated in the pademu reading — which copy is more correct is INFERRED, not claimed as a bug) |
| B10 | `ds34bt_get_status`: standalone returns `bt_dev.status \| pad status` gated on `DS34BT_STATE_USB_AUTHORIZED` (standalone:1559–1571); pademu returns pad status only (pademu:1360–1370) — its caller (`pademu.c:427`) tests the RUNNING bit itself | intentional variant (caller-side guard) |
| B11 | `ds34pad_clear`: pademu adds `status \|= DS34BT_STATE_USB_CONFIGURED` and `btn_delay = 0` (pademu:727–728) | intentional variant (feeds B7/B10) |
| D1 | Brace style / comment reflow throughout both files | drift |
| D2 | Explicit `= 0` initialisers for `identifier`, `g_press_emu`, `enable_fake` in pademu | drift |
| D3 | `hid_LEDRumble` → `hid_LEDRumbleCommand` rename | drift |
| D4 | DS3 LED-conf mask `led[0] & 0x7F` (standalone:1233) vs `& 0x1F` (pademu:1167): every internally produced value (`led_patterns` ≤ 0x10, `power_level` ≤ 0x1E, READ from the tables) fits in 0x1F, so no effect on internal callers | drift |
| D5 | DS3 blink byte placement: standalone writes `0x32` at template-absolute `led_buf[15,20,25,30]`; pademu writes payload-relative `command[10 + i*5 + 3]` — verified to be the same byte (group start + 3) in both encodings | drift (verified equivalent) |
| D6 | Pademu's LED function no longer saves `lrum`/`rrum` into the pad struct (standalone:1270–1271); its sole internal caller passes the stored values back in, so no effect in the current call graph (READ) | drift |
| S1–S9 | Findings R4-2A-01 (shared), -03, -04, -05, -06 (BT half), -07, -11, -12 (BT half), -13 (BT half) | suspected bug — see §2 |

**BT tally: intentional variant 11, drift 6, suspected bug 9.**

### 1b. USB pair — `modules/ds34usb/iop/ds34usb.c` vs `modules/pademu/ds34usb.c`

| # | Region | Bucket |
|---|--------|--------|
| U1 | `MAX_PADS` 2 → 4 | intentional variant (pad-count) |
| U2 | IRX/RPC shell in standalone only (`_start`, `rpc_sf`, `DS34USB_*` IDs); standalone-only `ds34usb_get_bdaddr`/`ds34usb_set_bdaddr`/`ds34usb_set_led` (and the `link_key[]` table) dropped in pademu; pademu adds `ds34usb_get_model`, `ds34usb_set_mode`, `ds34usb_init(u8 pads, u8 options)` returning int | intentional variant (RPC/IRX shell vs in-process API, feature) |
| U3 | Guitar support, pademu only: probe/connect for `GUITAR_HERO_PS3_PID`/`ROCK_BAND_PS3_PID`, `translate_pad_guitar`, `MODEL_GUITAR`; DS5 support (`DS5_ENABLE` branches, standalone:90–97, :146–149, DS5 report parse and 48-byte LED send) dropped in pademu; type constants renumbered in the pademu header (`GUITAR_GH 2, GUITAR_RB 3, SHANWAN 4` vs standalone `DS5 2, SHANWAN 3`) — internal to each module, no shared consumer of the numeric values was found in scope | intentional variant (feature) |
| U4 | Input-transfer redesign: standalone uses one shared `usb_buf` for all pads plus a single global `usb_resulCode` (standalone:65, :228–246); pademu uses per-pad `data_buf[MAX_PADS]` + `data_sema`/`data_result`/`data_pending` (pademu:67, :228–238) and drains late completions at the top of the next `ds34usb_get_data` | intentional variant (concurrency model; standalone is serialised by its single RPC thread in practice, INFERRED) |
| U5 | Report translation delegated to `translate_pad_*` + `padMacroPerform`; `analog_btn`/`btn_delay` + PS+Select / PS+Share toggle, as BT B6/B7 | intentional variant (feature) |
| U6 | DS3 enable report: pademu gives it a dedicated static buffer `ds3_enable_report[]` with a comment naming the shared-buffer hazard (pademu:279–285) | intentional variant (fix, pademu side) — standalone side is finding R4-2A-10 |
| U7 | ShanWan no-output handling: standalone routes it through the `LEDRUM` wrapper which stores rumble and sends nothing (standalone:652–672); pademu clears `update_rum` inside `ds34usb_get_data` (pademu:511–512) — same outward behaviour | intentional variant |
| U8 | `usb_config_set`: standalone `DelayThread(20000)` after `LEDRumble` (standalone:283) dropped in pademu (pademu:274–278) | intentional variant (timing; no evidence either way) |
| U9 | `LEDRUM`/`LED`/`Rumble` wrapper layer (standalone:650–683) removed; pademu sets `update_rum` in `ds34usb_set_rumble` and performs the pending `LEDRumble` at the tail of `ds34usb_get_data` | intentional variant (structure) — the tail call is also finding R4-2A-08 |
| U10 | DS4 touchpad: standalone open-coded finger→Share/Option mapping; pademu passes `have_touchpad = 1` to `translate_pad_ds4` (pademu:352) — equivalent for a fixed 64-byte USB transfer | intentional variant |
| D1 | Brace style / comment reflow throughout | drift |
| D2 | `readReport` parameter renamed `pad` → `pad_idx` with a `ds34usb_device *pad` local in pademu | drift |
| D3 | Pademu `LEDRumble` no longer saves `lrum`/`rrum` at its tail (standalone:626–627); its caller passes the stored values, no effect in current call graph (READ) | drift |
| D4 | Pademu guitar branch in `readReport` (pademu:297–305) falls through instead of returning; the DS3/DS4 branches cannot match guitar types 2/3 and `btn_delay` is never set for guitars, so the fall-through is inert (READ) | drift |
| S1–S8 | Findings R4-2A-02, -06 (USB half), -07, -08, -09, -10, -12 (USB half), -13 (USB half) | suspected bug — see §2 |

**USB tally: intentional variant 10, drift 4, suspected bug 8.**

---

## 2. Findings

### R4-2A-01 — BT DS4 output report: buffer zeroed with the DS3 length, sent with the DS4 length (both BT copies)

- Category: report-length / uninitialised memory sent to device. Severity: high (for this class).
- Paths: `modules/ds34bt/iop/ds34bt.c:1208,1243,1263` and `modules/pademu/ds34bt.c:1146,1179,1198`. Constants: `modules/ds34bt/iop/ds34bt.h:244,248` and `modules/pademu/ds34bt.h:224,228`.
- Problem + mechanism (READ): This is the POPSLoader round-3 bug class, present here in **both** copies, in the DS4 branch of the LED/rumble sender:
  - Buffer: `u8 led_buf[PS4_11_REPORT_LEN + 2];` — 77 + 2 = 79 bytes of stack.
  - Zeroing: `mips_memset(led_buf, 0, PS3_01_REPORT_LEN + 2);` — 48 + 2 = **50 bytes** only.
  - Send length: `size += PS4_11_REPORT_LEN;` with `size` starting at 2 → **79 bytes** passed to `HID_command`, which memcpys `length` bytes into `l2cap_cmd_buf` and bulk-transfers `8 + length`.
  - The DS4 branch assigns only indices 0,1,2,4,7,8,9,10,11,12,13. Indices 50–78 (29 bytes) are therefore **uninitialised IOP stack** transmitted to the controller in every DS4 LED/rumble report; indices 14–49 happen to be covered by the 50-byte memset.
- Contrast (READ): the USB copies do this correctly — `LEDRumble` memsets the whole 96-byte `usb_buf` and sends 32 bytes for DS4 (`modules/ds34usb/iop/ds34usb.c:560,598`; `modules/pademu/ds34usb.c:397,437`). The BT DS3 branch in both copies is also correct (48-byte template/memset, send 50).
- Fix in words: zero the full `led_buf` (its declared size, equivalently `PS4_11_REPORT_LEN + 2`) in the DS4 branch of both BT copies. One constant per copy; do not shrink the send length, the DS4 report is 77 bytes.
- Benefit: stops leaking 29 stack bytes per report to the controller and makes the report tail deterministic (the DS4 presumably ignores the tail today, INFERRED, but any future/reserved field in that range is currently garbage). Effort: trivial. Risk: none identified — the bytes are supposed to be zero.
- Confidence: **READ** (arithmetic is fully determined by the quoted lines and header constants).

### R4-2A-02 — Pademu USB: `ds34usb_init` never initialises `outEndp` to −1, so the first connection never opens the output endpoint

- Category: initialisation omission / drift from the standalone copy. Severity: high for pademu USB rumble/LED.
- Paths: `modules/pademu/ds34usb.c:571–616` (init loop; the omission), with the tests that depend on it at `modules/pademu/ds34usb.c:164,175,212–217,437`. Standalone counterpart: `modules/ds34usb/iop/ds34usb.c:958` (`ds34pad[pad].outEndp = -1;` in `_start`).
- Problem + mechanism (READ): The pademu init loop initialises every sibling field to −1 — `sema`, `cmd_sema`, `data_sema`, `controlEndp`, `interruptEndp` — but never `outEndp`. `ds34pad` is a file-scope array (`modules/pademu/ds34usb.c:73`), so `outEndp` starts at **0**, not −1. Consequences on the first connection after module load:
  - `usb_connect` only opens the OUT endpoint `if (... && ds34pad[pad].outEndp < 0)` (:164) — false, so the real output endpoint is never opened.
  - The acceptance check `(ds34pad[pad].outEndp < 0 && type != SHANWAN)` (:175) also passes for the wrong reason, so the pad is marked connected/running with no output path.
  - DS4 `LEDRumble` then issues `UsbInterruptTransfer(ds34pad[pad].outEndp /* = 0 */, usb_buf, 32, ...)` (:437) against endpoint id 0, whatever that id denotes in USBD — not this pad's OUT endpoint (which endpoint id 0 actually is: INFERRED, outside these files).
  - `usb_release` treats 0 as a real endpoint: `if (outEndp >= 0) UsbCloseEndpoint(outEndp)` (:212–213) and only then sets −1 — so the defect self-heals after the first disconnect/replug cycle, which is exactly the shape that hides it in testing.
- Fix in words: initialise `outEndp = -1` in the `ds34usb_init` loop alongside `controlEndp`/`interruptEndp`, matching the standalone `_start`.
- Benefit: first-plug DS3/DS4 LED and rumble work in pademu instead of only working after a replug; removes a spurious `UsbCloseEndpoint(0)`. Effort: trivial. Risk: none identified.
- Confidence: **READ** (omission, the −1 convention, and all three dependent tests are quoted; C static-storage zero-initialisation is a language guarantee, not an inference).

### R4-2A-03 — Standalone copies index `power_level[]` with the unguarded device-supplied DS3 battery byte; pademu copies guard it

- Category: unchecked device-supplied index. Severity: medium (out-of-bounds read of a small static table).
- Paths: `modules/ds34bt/iop/ds34bt.c:261–263` (table), `:1332–1333` (use); `modules/ds34usb/iop/ds34usb.c:45–47` (table), `:337–338` (use). Guarded counterparts: `modules/pademu/ds34bt.c:1251–1252`, `modules/pademu/ds34usb.c:332–333`.
- Problem + mechanism (READ): `power_level[]` has exactly 6 entries (indices 0–5) in all four files. Both standalone copies do:
  - BT: `if (report->PSButton && report->Power != 0xEE) ds34pad[pad].oldled[0] = power_level[report->Power];`
  - USB: identical form at :337–338.
  `report->Power` is a full device-supplied byte of `struct ds3report` (`include/ds34common.h`, "Battery status ? 05=full – 02=dying, 01=just before shutdown, EE=charging"). The only excluded value is 0xEE. Any other out-of-range value a real, clone, or malfunctioning pad reports while PS is held — 0x06–0xED, 0xEF–0xFF — reads past the 6-byte table into adjacent statics and uses the byte as an LED pattern. One mitigating quirk in standalone BT only: when its sticky pressure-emulation flag is on (finding R4-2A-05) it first overwrites `report->Power = 0x05`, accidentally bounding the index on that path.
  Both pademu copies already carry the fix: `if (report->Power <= 0x05) pad->oldled[0] = power_level[report->Power];` (pademu BT routes through `reported_power`, with the pressure-emulation case preset to 0x05).
- Fix in words: port the pademu `<= 0x05` guard to both standalone copies; keep the existing 0xEE charging handling, which already lives outside this expression in the following lines.
- Benefit: removes an out-of-bounds static read driven by an untrusted peripheral byte, for the cost of the guard the sibling copies already prove acceptable. Effort: trivial. Risk: a pad reporting a nonsense level >5 simply keeps its previous LED pattern (the pademu behaviour) instead of displaying a garbage byte.
- Confidence: **READ**.

### R4-2A-04 — Pademu BT: charging-blink flag `oldled[3]` is set in `hid_readReport` but never cleared there (DS3 and DS4)

- Category: state drift / missing else-branch present in the other three copies. Severity: low (cosmetic, but permanent for the connection).
- Paths: `modules/pademu/ds34bt.c:1266–1269` (DS3: `if (reported_power == 0xEE || reported_power == 0x01) pad->oldled[3] = 1;` — no else), `modules/pademu/ds34bt.c:1305–1307` (DS4: `if (report->Power != 0xB && report->Usb_plugged) pad->oldled[3] = 1;` — no else). Clearing counterparts: `modules/ds34bt/iop/ds34bt.c:1337–1340,1434–1437`; `modules/pademu/ds34usb.c:346–349,382–385`; `modules/ds34usb/iop/ds34usb.c:342–345` (all if/else).
- Problem + mechanism (READ): Within pademu BT's report path, nothing ever writes `oldled[3]` back to 0: `ds34pad_clear` does not touch `oldled`, the config-response path zeroes it once at connection time (pademu:1002,1007), and `hid_LEDRumbleCommand`'s trailer copies `led[3]` into `oldled[3]` — but its in-tree caller passes `ds34pad[ret].oldled` itself, a self-copy. So once a DS3 reports charging (0xEE) or near-empty (0x01), or a DS4 reports plugged-in-and-not-full, the LED blinks for the rest of the connection — after unplugging, after charging completes, after the battery recovers.
- Fix in words: add the missing else-clear in both pademu BT branches, mirroring the other three copies (DS3: clear when the power byte is neither 0xEE nor 0x01; DS4: clear when the charging condition no longer holds).
- Benefit: LED state again tracks the live battery state, as in the other three copies. Effort: trivial. Risk: none identified.
- Confidence: **READ** (set/clear sites enumerated by grep over the whole file; the three sibling copies all clear).

### R4-2A-05 — Standalone BT: pressure-emulation flag is global, sticky, and cross-pad

- Category: shared-state pollution. Severity: low–medium (wrong pressure data for the other pad).
- Paths: `modules/ds34bt/iop/ds34bt.c:358` (`static u8 g_press_emu;`), `:820` (reset only in `ds34pad_init`), `:1295–1298` (`if (bytes == 21 && !g_press_emu) g_press_emu = 1; if (g_press_emu) { ... }`), `:1317` (emu path also forces `report->Power = 0x05`). Pademu counterpart: `modules/pademu/ds34bt.c:1215–1225` (per-report decision).
- Problem + mechanism (READ for the code, INFERRED for the trigger's real-world frequency): a single 21-byte DS3 report from **either** pad sets the one global flag, and from then on **both** pads have their real pressure bytes discarded and pressures synthesised as digital × 255, until all pads are re-initialised. The trigger is also narrower than the pademu copy's: exactly `bytes == 21`, where pademu treats any report shorter than `sizeof(struct ds3report)` as pressure-less for that report only (`press_emu = 1` if `bytes < sizeof(...)`, else the — never set in pademu, always 0 — global) and additionally drops reports under 19 bytes outright (pademu:1214–1217).
- Fix in words: make the decision per report (and per pad) as the pademu copy does: short report → emulate pressures for that report; full report → use the real pressure bytes. Delete the sticky global write.
- Benefit: one pad with a short-report clone/firmware no longer degrades the other pad's analog pressures for the whole session. Effort: small. Risk: a pad that genuinely alternates report sizes would flip modes per report — which is precisely the pademu behaviour already shipped in the sibling copy.
- Confidence: mechanism **READ**; how often a 21-byte report occurs in the field is **INFERRED** (not verifiable from source).

### R4-2A-06 — Pademu copies dropped the standalone port guards on every public entry point; pademu USB `set_mode` also dropped the semaphore

- Category: missing caller-side guards present in one copy only. Severity: low today (see caller analysis), hardening.
- Paths: pademu BT `modules/pademu/ds34bt.c:1323` (`ds34bt_set_rumble`), `:1334` (`ds34bt_get_data`), `:1348` (`ds34bt_set_mode`), `:1360` (`ds34bt_get_status`) — none checks `port`. Pademu USB `modules/pademu/ds34usb.c:468,479,528,544,555` — same, and `ds34usb_set_mode` (:528–534) additionally takes **no semaphore at all**, unlike every other pademu USB entry point and unlike BT's `ds34bt_set_mode` (pademu BT:1348, which does take `hid_sema`). Standalone guards: `modules/ds34bt/iop/ds34bt.c:1451–1454,1521–1524` (`if (port >= MAX_PADS) return;` plus the AUTHORIZED check), `modules/ds34usb/iop/ds34usb.c:687,703,851` (same port check in set_rumble/get_data/get_status and siblings).
- Problem + mechanism (READ for the code): the pademu functions index `ds34pad[port]` (and take `ds34pad[port].sema`) as their first action. Any out-of-range or negative `port` is an immediate out-of-bounds access on a static array, and USB `set_mode`'s unsynchronised `analog_btn` write races the `readReport`/`get_data` path that reads and toggles the same byte under the semaphore.
- Why it is only low today (INFERRED from the caller, read for context): the sole in-tree caller, `modules/pademu/pademu.c`, derives `port` from SIO register bits (`td->regdata[0] & 0x03`, i.e. 0–3, or the literals 0/1) — inside pademu's `MAX_PADS 4`, so no current call overflows. The standalone copies could not rely on this because their port arrives as an EE-supplied RPC byte. The guard is therefore defence-in-depth for the pademu copies, not a live overflow — but it is one caller change away from becoming one, and the standalone copies show the project's own convention.
- Fix in words: restore the `port >= MAX_PADS` early-out (and a negative check, since these signatures take `int`) in the eight pademu entry points, and put `ds34usb_set_mode` under the pad semaphore like its siblings. Do not add the standalone AUTHORIZED gates — pademu's caller already gates on RUNNING status.
- Benefit: the in-process API becomes as safe to call as the RPC one; removes the one unsynchronised state write in the USB copy. Effort: small. Risk: none identified (in-range callers unaffected).
- Confidence: code **READ**; present-day unreachability **INFERRED** from the single caller.

### R4-2A-07 — `get_data` memcpy length is unchecked in all four copies; only the pademu copies expose a variable length

- Category: unchecked length / over-read. Severity: low.
- Paths: `modules/ds34bt/iop/ds34bt.c:1519,1529` (`mips_memcpy(dst, ds34pad[port].data, size)`), `modules/pademu/ds34bt.c:1334,1340`, `modules/ds34usb/iop/ds34usb.c:701,725`, `modules/pademu/ds34usb.c:479,508`. The pad data union is 18 bytes (`u8 data[18]` in all four headers).
- Problem + mechanism (READ for the code): none of the four clamps `size` to the 18-byte source. In the standalone copies the exposure is theoretical — the only caller is their own RPC dispatch, which hardcodes 18 (`ds34bt.c:1657`, `ds34usb.c:923`). In the pademu copies the length is genuinely caller-supplied: `pademu.c:467` passes `out_size - 3`, derived from the SIO transfer size, into `PAD_GET_DATA`. A size above 18 reads past the pad's data union into the following struct fields (and, for the last pad, past the `ds34pad` array) and copies those bytes into the game's pad-response buffer.
- Fix in words: clamp the copy length to 18 (the data union size) inside all four `get_data` functions, so the contract holds regardless of caller.
- Benefit: one-line-per-file guarantee at the only place the bound is known; the standalone copies gain it before anyone reuses their RPC size field. Effort: trivial. Risk: none — no legitimate caller can want bytes past the 18-byte report.
- Confidence: code **READ**; whether a game/SIO path ever presents `out_size - 3 > 18` is **INFERRED** (normal DS2 poll frames are 21 bytes total, which is exactly 18 after the 3-byte header — the clamp would be a no-op for them).

### R4-2A-08 — Pademu USB `ds34usb_get_data` overwrites its analog-mode return value with the LED/rumble transfer result

- Category: return-value clobber / drift from the BT pademu copy. Severity: low (one-poll mode glitch).
- Paths: `modules/pademu/ds34usb.c:508–520`: `mips_memcpy(...); ret = ds34pad[port].analog_btn & 1;` then `} else if (ds34pad[port].update_rum) { ret = LEDRumble(...); ... }`. BT pademu counterpart: `modules/pademu/ds34bt.c:1334–1345` returns `analog_btn & 1` and performs rumble work elsewhere (`l2cap_event_cb`), so its return is never clobbered. Standalone USB `get_data` returns void, so the issue is unique to this copy.
- Problem + mechanism (READ): the caller contract is visible in `pademu.c:467–471` (context read): the return of `PAD_GET_DATA` is compared against the pad's current mode and, on mismatch, adopted as the new mode. Whenever a rumble/LED update is pending — which includes the first poll after init (`update_rum = 1` at pademu USB:581) and any poll in which `readReport` just set it — `get_data` returns the `UsbInterruptTransfer` result (0 on success) instead of the analog bit. A pad toggled into analog mode therefore reports digital for that poll and is flipped back by the caller logic, then re-detected on a later poll.
- Fix in words: keep the two results separate — save the analog return value before the LED/rumble tail and return that, using the transfer result only for the wait/error logging, as the current code already does internally.
- Benefit: analog/digital mode reported by pademu USB matches the pad's actual toggle state on every poll, matching BT pademu. Effort: trivial. Risk: none identified.
- Confidence: mechanism **READ**; the visible symptom (a one-frame mode flicker at toggle/first poll) is **INFERRED** from the caller's compare-and-adopt logic.

### R4-2A-09 — Standalone USB `ds34usb_set_bdaddr`: DS4 pairing report sends 24 bytes, byte 23 is never written

- Category: stale shared-buffer byte sent to device. Severity: low.
- Paths: `modules/ds34usb/iop/ds34usb.c:802–831` (function at :802; DS4/else branch writes `usb_buf[0]` at :823, address bytes at :826–827, `link_key` at :828–829, transfer of **24** at :831). No pademu counterpart (the API was dropped there, U2).
- Problem + mechanism (READ): the else branch writes indices 0–22 (1 report byte + 6 address + 16 link-key) and transfers 24 bytes; index 23 is whatever `usb_buf` previously held. `usb_buf` is the module's single shared buffer (standalone USB:65) — by the time pairing is offered it has typically carried pad input reports and LED reports, so byte 23 is a leftover report byte; on a virgin module it is BSS zero. Nothing in the function memsets the buffer first (contrast `LEDRumble` at :560, which does).
- Fix in words: zero `usb_buf` (or at least the sent range) at the top of `ds34usb_set_bdaddr`, or send the 23 bytes actually written if the report is defined as 23 — the former is the safe choice absent protocol documentation in-tree.
- Benefit: the pairing feature report becomes deterministic. Effort: trivial. Risk: negligible (byte 23 is padding on any reading of the writes).
- Confidence: **READ** (write/transfer lengths are quoted); whether the receiving device inspects byte 23 is **INFERRED** (likely ignored padding — hence low severity).

### R4-2A-10 — Standalone USB: DS3 enable report is staged in the shared `usb_buf` that `LEDRumble` wipes immediately afterwards, with no completion wait

- Category: shared-buffer lifetime / divergence from the pademu fix. Severity: low–medium.
- Paths: standalone `modules/ds34usb/iop/ds34usb.c:305–313` (`DS3USB_init` writes `usb_buf[0..3]`, control transfer with `NULL` callback), called from `usb_config_set` at :274, followed in the same function by `LEDRumble(led, 0, 0, pad)` at :283 — whose first act is `mips_memset(usb_buf, 0, sizeof(usb_buf))` (:560). Pademu fix: `modules/pademu/ds34usb.c:279–285` — a dedicated `ds3_enable_report[]` with the comment "Own buffer: nothing waits for this transfer, and LEDRumble() rewrites usb_buf right after it."
- Problem + mechanism (READ for the sequence, INFERRED for USBD semantics): the pademu copy's own comment establishes that nothing waits for this transfer to complete. If the control transfer reads its data buffer asynchronously (the premise the pademu author coded against), the standalone sequence can deliver a zeroed/LED-shaped buffer as the DS3 enable report (feature 0xF4), i.e. the enable may silently not take effect on some adapters/timing. The standalone BT driver stages its equivalent init in a private stack buffer (`hid_initDS34`), so this exposure is unique to standalone USB.
- Fix in words: give the standalone USB enable report its own static 4-byte buffer, exactly as the pademu copy does; no other change.
- Benefit: removes a race the sibling copy already diagnosed and fixed; less code than any wait-based alternative. Effort: trivial. Risk: none identified (4 bytes of BSS).
- Confidence: sequence and pademu comment **READ**; that USBD actually consumes the buffer asynchronously is **INFERRED** (the pademu comment is the in-tree evidence; not re-verified against USBD source in this section).

### R4-2A-11 — Pademu BT: fake-DS3 LED reports lost the fake transaction header (0xA2)

- Category: dropped special case in port. Severity: low.
- Paths: standalone `modules/ds34bt/iop/ds34bt.c:1212–1215` (`if (ds34pad[pad].isfake) led_buf[0] = 0xA2; else led_buf[0] = HID_THDR_SET_REPORT_OUTPUT;`). Pademu `modules/pademu/ds34bt.c:1150–1151` unconditionally uses `HID_THDR_SET_REPORT_OUTPUT`.
- Problem + mechanism (READ for the divergence, INFERRED for the consequence): the pademu copy kept the rest of the fake-DS3 machinery — the `enable_fake` option (B5), the genuine-MAC table, and the fake rumble clamp `if (rrum < 5) rrum = 0` (pademu:1157–1161, matching standalone:1221–1225) — but not the one place the standalone driver changes the wire format for a fake pad: the first byte of the DS3 output report. If fake DS3s need the 0xA2 data header to accept LED/rumble reports (the only evident purpose of the special case), pademu's fake-DS3 support enables detection and then sends reports the pad may ignore — LED/rumble dead on exactly the pads the option exists for.
- Fix in words: restore the `isfake` choice of the first byte in the pademu DS3 branch; no other change.
- Benefit: makes the pademu `enable_fake` option self-consistent. Effort: trivial. Risk: none for genuine pads (their branch already uses 0x52 in both copies).
- Confidence: divergence **READ**; device consequence **INFERRED** (no fake-pad behaviour is documented in-tree beyond the existence of the special case).

### R4-2A-12 — DS4 rumble scaling `rrum * 255` exists only in the pademu copies and is correct only if `rrum` is a boolean

- Category: semantic divergence between copies. Severity: low, conditional.
- Paths: pademu BT `modules/pademu/ds34bt.c:1186` (`led_buf[7] = rrum * 255;`), pademu USB `modules/pademu/ds34usb.c:424` (`usb_buf[4] = rrum * 255;`). Pass-through counterparts: standalone BT `modules/ds34bt/iop/ds34bt.c:1250` (`led_buf[7] = rrum;`), standalone USB `modules/ds34usb/iop/ds34usb.c:586` (`usb_buf[4] = rrum;`). All four DS3 branches pass `rrum` through unscaled.
- Problem + mechanism (READ for the arithmetic, INFERRED for which input range is real): `rrum` is a `u8`; the product is truncated back into a `u8` field. For `rrum ∈ {0,1}` this is a sensible boolean→full-scale conversion (and the pademu caller context supports a boolean reading: in the DS2 actuator alignment the right/small motor is actuator 0, an on/off actuator — `pademu.c:529–532`, context read). For any magnitude value it wraps non-monotonically: 2→254, 128→128, and a full-strength 255→**1**. The standalone copies, and the DS3 branch of the very same pademu functions, treat the byte as a magnitude. The two readings cannot both be right for the same input byte; the copies disagree about the contract and nothing in-tree documents it.
- Fix in words: decide the contract once and make all four DS4 branches match it — if the byte is boolean, scale by `rrum ? 255 : 0` (identical result, no wrap for stray values >1); if it is a magnitude, drop the multiplication in the pademu copies. Either way, leave a one-line comment stating the expected range, since the next porter will otherwise re-derive it wrong.
- Benefit: eliminates a wrap that turns maximum rumble into almost none under the magnitude reading, and documents an undocumented cross-copy contract. Effort: trivial. Risk: choosing the wrong reading changes rumble feel for one caller family — which is why this is flagged as a decision, not silently normalised.
- Confidence: **INFERRED** (divergence and arithmetic READ; the true input range depends on the EE/RPC and game/SIO callers, outside the four files).

### R4-2A-13 — Report-input guards present in some copies only: standalone BT has no minimum-length check; standalone USB DS3 lacks the null-report rejection

- Category: missing input guards. Severity: low (buffers are static, so the failure mode is stale-data parsing, not memory corruption — READ).
- Paths: standalone BT `hid_readReport` at `modules/ds34bt/iop/ds34bt.c:1275` — indexes `data[8]`, `data[9]` and casts `struct ds3report *`/`struct ds4report *` at `&data[11]` with no reference to `bytes` except the touchpad and 21-byte checks. Pademu BT guards: `modules/pademu/ds34bt.c:1214–1217` (`if (bytes < 19) return;`) and `:1220–1225` (full-vs-short report split). Standalone USB DS3 parse at `modules/ds34usb/iop/ds34usb.c:315–345` — no rejection of the all-zero "null report"; the rejection exists at standalone BT :1285–1286, pademu BT (in its DS3 branch), and pademu USB `modules/pademu/ds34usb.c:318–319` (`if (report->RightStickX == 0 && report->RightStickY == 0) return; // ledrumble cmd causes null report sometime`).
- Problem + mechanism (READ): (a) a short or malformed BT interrupt packet is parsed as a full report in standalone BT; because the source is the static `l2cap_buf`, the tail is the previous report's bytes — phantom held buttons/sticks for one cycle rather than a crash. (b) On standalone USB, the null report the other three copies explicitly reject (it is emitted around LED/rumble commands) is accepted, overwriting the pad state with zeros — centred sticks read as 0,0 (full deflection) and all pressures 0 — for one poll. Note both USB copies call `readReport` with no length argument at all, so guard (a) cannot be ported to USB without a signature change; guard (b) needs no length.
- Fix in words: add the pademu BT minimum-length early-out to standalone BT's `hid_readReport`; add the two-line null-report rejection to standalone USB's DS3 branch. Do not restructure the USB signature in this fix.
- Benefit: standalone copies stop acting on packets every sibling copy already considers invalid. Effort: trivial (BT) / trivial (USB). Risk: none identified — the guards only drop reports the other copies drop today.
- Confidence: **READ**.

---

## 3. Checked and clean

Items specifically hunted (per the section brief) and found **not** to be defects, with the evidence:

1. **Report-length class, all other senders — clean.** BT DS3 (both copies): 48-byte template/memset, send 50. USB LED/rumble (both copies): whole 96-byte `usb_buf` memset before use; DS3 sends 48, DS4 sends 32, DS5 (standalone, `DS5_ENABLE` only) sends 48 — all within the zeroed range. `hid_initDS34` (both BT copies): DS3 writes all 6 sent bytes, DS4 sends only the 2 written bytes. `DS3USB_init` length 4 = 4 bytes written (its buffer-lifetime issue is R4-2A-10, not a length issue).
2. **HCI/L2CAP command lengths — clean (both BT copies).** Every `HCI_Command`/`L2CAP_Command`/`HID_command` call's length is fully written by its builder (reset 3, accept/reject 10, disconnect 6, link-key reply 9+16, config response 14, etc.); no stack `cmd_buf` is sent partially uninitialised.
3. **Shared tables — identical and safely indexed (all four files).** `led_patterns` (4×2), `power_level` (6), `rgbled_patterns` (4×2×3) have identical values in all four files (verified in the full reads). Pad-indexed uses are safe: 4 entries ≥ `MAX_PADS` in both variants (2 standalone, 4 pademu). The only unsafe table indexing found is R4-2A-03.
4. **Dpad handling — equivalent.** Standalone copies' open-coded DS4/DS5 dpad switch covers values 0–8 of a 4-bit field; 9–15 fall through to "all released", which is exactly what the pademu path's clamp in `translate_pad_ds4` (`dpad > 8 → 8`) produces.
5. **HCI disconnect path — clean (both BT copies).** `HCI_EVENT_DISCONN_COMPLETE` calls `ds34pad_clear(i)` with `i` possibly equal to `MAX_PADS`; `ds34pad_clear` itself guards `if (pad >= MAX_PADS) return;` in both copies. `CONNECT_COMPLETE` and `REMOTE_NAME_COMPLETE` break out on `i >= MAX_PADS` before indexing.
6. **USB connect endpoint walk guard — present in both USB copies.** The `epCount < 0` early-out (with its explanatory comment) is identical in standalone and pademu `usb_connect`; the fixed count 20 for DS4 is shared heritage code, agreed by both copies, not a divergence.
7. **BT first/re-arm transfer sizes — agreed, not a divergence.** Both BT copies arm the first L2CAP transfer at `MAX_BUFFER_SIZE` (64) in `bt_config_set` and re-arm at `MAX_BUFFER_SIZE + 23` (87) in `l2cap_event_cb`, into a 96-byte `l2cap_buf`. The asymmetry is identical in both copies; the fields actually parsed (report at offset 11, ≈42-byte DS4 struct) fit in 64 bytes, so no finding is raised — recorded here because it was checked.
8. **DS4 touchpad gating over BT — equivalent.** Standalone requires `bytes > 63` before finger→Share/Option mapping; pademu passes `bytes > 63` as `have_touchpad` to `translate_pad_ds4`. Same condition, same effect.
9. **Standalone USB `get_bdaddr` read lengths — clean.** DS3 branch reads `usb_buf[2..7]` from an 8-byte transfer; DS4 branch reads `usb_buf[10..15]` from a 16-byte transfer. In bounds.
10. **Pademu USB guitar fall-through — inert.** See table item D4 in §1b; verified no DS3/DS4 branch can match, and `btn_delay` is never set for guitar types.
11. **Pademu BT DS3 LED rewrite — internally consistent.** The per-LED payload copy, the `& 0x1F` mask, and the blink byte were each compared against the standalone template encoding (see D4/D5 in §1a); apart from the template-vs-per-group wire difference already bucketed as B8, no off-by-one was found in the group offsets (`10 + i*5`, +3 for blink).
12. **Negative-port handling in standalone RPC paths — not reachable as negative.** The standalone guards test only `port >= MAX_PADS`, but their dispatchers pass a `u8` (`*(u8 *)data`), so the value is 0–255 and the upper-bound test suffices there. (This does not extend to the pademu `int` signatures — see R4-2A-06.)

---

## 4. Coverage statement

- All four in-scope files were read in full at the pin (1,719 + 1,425 + 998 + 616 = 4,758 lines), including every entry point (`_start`/`rpc_sf` in the standalone copies; `*_init`, `*_get_data`, `*_set_rumble`, `*_set_mode`, `*_get_status`, `*_get_model`, `ds34bt_init_charging`, bdaddr getters/setters in the pademu copies), all USB probe/connect/disconnect/config paths, the full HCI and L2CAP state machines in both BT copies, and both `LEDRumble`/`hid_readReport`/`readReport` implementations per pair.
- Method executed as tasked: `diff -u` per pair (BT 19 hunks, USB 16 hunks), comment/whitespace-stripped diff per pair (BT 649 lines, USB 691 lines), and every divergent region re-read in context in both copies before bucketing. The report-length hunt (memset/memcpy length vs send length, DS3/DS4 constant mix-ups), device-byte-as-index hunt, and caller-guard hunt were each run as separate passes across all four files (grep-verified line lists for `power_level[`, `oldled[3]`, `outEndp`, `* 255`, `PS3_01_REPORT_LEN`/`PS4_11_REPORT_LEN` uses).
- Supporting reads outside the four files were used only for constants and caller provenance and are cited as context where used: the four sibling headers, `include/ds34common.h`, `modules/pademu/ds34common.c`, `modules/pademu/pademu.c`. No finding rests solely on those files.
- Not covered / not claimed: no build was run, no hardware was used, USBD's transfer-completion semantics were not re-read from PS2SDK source (R4-2A-10 is labelled accordingly), and the EE-side RPC client libraries (`libds34bt.c`/`libds34usb.c`) were only checked to confirm they pass rumble bytes through unscaled — relevant context for R4-2A-12, not audited themselves.
- Findings: 13 (R4-2A-01 … R4-2A-13). Divergence tallies: BT pair — intentional variant 11, drift 6, suspected bug 9; USB pair — intentional variant 10, drift 4, suspected bug 8 (shared findings counted in each pair they touch).
