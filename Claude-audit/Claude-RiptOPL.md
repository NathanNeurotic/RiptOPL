# Claude audit: RiptOPL

Status: IN PROGRESS. 76 of 154 reading passes are in; finding IDs are provisional and will be renumbered until this line says COMPLETE.

- Repository: NathanNeurotic/RiptOPL
- Audited branch: rebuild/main
- Audited commit: b00e773bf28c1790b458ab53f25caad69e79f237
- Audit date: 2026-11-16
- Mode: read-only review. This branch adds only the `Claude-audit/` folder; no source, build or workflow file is changed.

All `path:line` references are pinned to the audited commit above. Each finding was raised by a reader that read the cited files line by line, then re-checked by a second, independent pass that re-read the cited lines and re-ran the reference searches. Findings that did not survive that check were dropped.

## Totals

- Findings in this report: 21 verified, plus 521 not yet verified
- Raised by readers: 542. Dropped as wrong or unsupported on re-check: 0. Corrected on re-check: 9. Merged as duplicates: 0.

| Category | Count |
|---|---:|
| 1 Dead code | 7 |
| 2 Duplication | 2 |
| 3 Overengineering | 7 |
| 4 Size and memory | 0 |
| 5 Performance | 0 |
| 6 Bugs and security | 0 |
| 7 Build/CI/repo clutter | 5 |
| 8 Half-finished or redundant features | 0 |

| Benefit rank | Meaning | Count |
|---|---|---:|
| 5 | major: real crash/data-loss/security bug, or >= 1000 lines / >= 50 KB / >= 100 KB RAM / multi-second freeze | 1 |
| 4 | large: >= 300 lines / >= 10 KB / >= 32 KB RAM, clear reliability or responsiveness gain | 0 |
| 3 | moderate: >= 80 lines / >= 2 KB, minor bug, redundant CI job or card write | 1 |
| 2 | small: >= 15 lines, a few hundred bytes | 7 |
| 1 | trivial: a few lines, cosmetic | 12 |

## Findings (sorted by benefit)

### Benefit rank 5

#### RIPTOPL-001: ilink_bd_assault carries a second, drifting copy of iLinkman and IEEE1394_bd (about 3,500 lines) instead of building from modules/ilink

- Category: 2 (Duplication)
- Location(s): `RiptOPL/modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/sbp2_driver.c:1-801`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/scsi.c:1-386`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/include/sbp2_disk.h:1-183`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/include/scsi.h:1-19`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/include/module_debug.h:1-22`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/main.c:1-33`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/include/iLinkman.h:1-88`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/README`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink.c:1-369`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink_DMA.c:1-132`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink_PHY.c:1-74`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink_TxRx.c:1-212`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink_crom.c:1-398`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink_internal.c:1-190`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink_intr.c:1-469`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/include/iLink_internal.h:1-327`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/include/iLink_CROM.h:1-72`; `RiptOPL/modules/bdmassault/ilink_bd_assault/Makefile:1-36`; `RiptOPL/modules/bdmassault/ilink_bd_assault/README.md:102-104`; `RiptOPL/modules/ilink/IEEE1394_bd/src/sbp2_driver.c:403-427`; `RiptOPL/modules/ilink/IEEE1394_bd/ORIGIN.txt:18-20`
- Problem: Every file under ilink_bd_assault/src/{IEEE1394_bd,iLinkman} is a near copy of modules/ilink: iLink_intr/TxRx/DMA/PHY differ only in the include path, iLink.c only in the chained BD_iLink_start call, sbp2_driver.c by 57 diff lines (it lacks sbp2FreeDevNr), scsi.c is the older SDK one (no READ(16), 12-byte CDB). ORIGIN.txt:18-20 says 'A change to one belongs in both', and the #1024 and October 22 fixes were applied twice; the copies already drift.
- Fix: Build ilink_bd_assault.irx from modules/ilink/iLinkman/src and modules/ilink/IEEE1394_bd/src with one define (for example ILINK_BD_ASSAULT) that selects the static GetConsoleIDs, the chained _start/BD_iLink_start and no IRX_ID in IEEE1394_bd main.c, the malloc/free single definition, and skips bd->path for the older include/bdm.h; keep only Makefile, include/bdm.h, src/imports.lst and src/exports.tab here, then delete the copied trees and point the dual-copy tests at one source.
- Justification: Adds roughly 30-40 lines of #ifdef in modules/ilink to remove about 3,800 duplicated lines; it ends the double maintenance that ORIGIN.txt:18-20 documents (the #1024, October 22 and November 2 changes were each written twice). The ifdefs must keep the deliberate differences: no device-number change for this copy (README.md:102-104), the cdvdman-free GetConsoleIDs and the chained _start. Whether the newer READ(16) path is wanted under POPSTARTER is untested.
- Benefit: ~3,800 source lines removed (3,440 lines of cited bdmassault sources excluding the kept Makefile, plus iLink_internal.h 327 and iLink_CROM.h 72, minus ~40 ifdef lines); every future i.LINK fix made once. No menu ELF change claimed. INFERRED.
- Effort: L. Risk: High. Confidence: Med (INFERRED)

### Benefit rank 3

#### RIPTOPL-002: Five per-component import/export files inside ilink_bd_assault are never used by its Makefile

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/imports.lst:1-73`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/irx_imports.h:1-35`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/exports.tab:1-30`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/imports.lst:1-74`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/irx_imports.h:1-32`; `RiptOPL/modules/bdmassault/ilink_bd_assault/Makefile:9-16`
- Problem: The Makefile sets IOP_SRC_DIR := src/, so imports.o/exports.o come from src/imports.lst and src/exports.tab with src/irx_imports.h; no .c file includes irx_imports.h. The sub-tree copies are leftovers: IEEE1394_bd/src/imports.lst lacks I_PollEventFlag that sbp2_driver.c:392 calls, and iLinkman/src/imports.lst:1-4 still imports cdvdman sceCdRI/sceCdRM, which README.md says would abort loading under POPSTARTER.
- Fix: Delete the five files (independently of finding 1).
- Benefit: 244 lines and 5 stray files removed (73+35+30+74+32); no binary change. Removes a misleading cdvdman import list. INFERRED (standard PS2SDK Makefile.iopglobal rules, not in repo).
- Effort: S. Risk: Low. Confidence: High (INFERRED)

### Benefit rank 2

#### RIPTOPL-003: Never-enabled debug blocks (REQ_CHECK_DMAC_STAT, REQ_CHECK_ERRORS) and commented-out code in both iLinkman copies

- Category: 1 (Dead code)
- Location(s): `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink_internal.c:82-88`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink_internal.c:138`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink_intr.c:120-123`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink_intr.c:374-376`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink_intr.c:398-422`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink_intr.c:445-453`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink_TxRx.c:150-152`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/include/iLink_internal.h:17-18`; `RiptOPL/modules/ilink/iLinkman/src/iLink_internal.c:95-101`; `RiptOPL/modules/ilink/iLinkman/src/iLink_internal.c:151`; `RiptOPL/modules/ilink/iLinkman/src/iLink_intr.c:120-123`; `RiptOPL/modules/ilink/iLinkman/src/iLink_intr.c:374-376`; `RiptOPL/modules/ilink/iLinkman/src/iLink_intr.c:398-422`; `RiptOPL/modules/ilink/iLinkman/src/iLink_intr.c:445-453`; `RiptOPL/modules/ilink/iLinkman/src/iLink_TxRx.c:150-151`; `RiptOPL/modules/ilink/iLinkman/src/include/iLink_internal.h:17-18`
- Problem: REQ_CHECK_DMAC_STAT and REQ_CHECK_ERRORS are only commented-out #defines in iLink_internal.h:17-18 and appear in no Makefile, ps2.yaml or test, so the DMAC stall-recovery and HdrErr/SntBsyAck blocks never compile; the same file pair also keeps commented-out code (old ctrl0 setup, PHY config handling, ack test).
- Fix: Delete the #ifdef REQ_CHECK_DMAC_STAT / REQ_CHECK_ERRORS blocks, their two commented #defines and the commented-out code lines, in both copies (once, if finding 1 merges them).
- Benefit: ~50 lines per copy, ~100 in total; no binary change (code is not compiled). INFERRED.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-004: BgmPlayer: single-instance class reached only through two thread trampolines and five one-line forwarding wrappers

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/include/bgmplayer.h:1-50`; `RiptOPL/src/core/bgm.cpp:56-66`; `RiptOPL/src/core/bgm.cpp:903-928`
- Problem: BgmPlayer has exactly one instance, the file-static gBgm (bgm.cpp:56), and no code outside bgm.cpp names the class (include/bgmplayer.h is included only by bgm.cpp:14). Every outside use goes through the C-style sound.h functions, which only forward (bgm.cpp:905-928), and the thread bodies need two static trampolines (bgm.cpp:58-66) because they are member functions. The 768 KB ring already had to stay outside the class (bgm.cpp:72), and the running flags (bgm.cpp:73) are plain globals shared with sound.cpp through soundint.h:12.
- Fix: Make the BgmPlayer members file-static variables in bgm.cpp and the member functions static functions, name the public ones directly bgmStart/bgmStop/bgmQuiesce/isBgmPlaying/bgmDiscretionaryIoAllowed, pass the thread functions to CreateThread directly, and delete include/bgmplayer.h.
- Benefit: ~55 lines removed (50-line header replaced by ~15 static declarations, 9 trampoline lines, ~20 forwarding lines). No ELF change expected beyond a few bytes of call indirection (INFERRED).
- Effort: M. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-005: Unused pieces in both SBP-2 drivers: DeinitIEEE1394, the get_max_lun hook, the always-LOGIN mode parameter, stray include and comments

- Category: 1 (Dead code)
- Location(s): `RiptOPL/modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/sbp2_driver.c:775-780`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/sbp2_driver.c:70`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/include/sbp2_disk.h:180`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/sbp2_driver.c:684-689`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/sbp2_driver.c:71`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/sbp2_driver.c:87`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/scsi.c:170-171`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/include/scsi.h:10`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/sbp2_driver.c:619-628`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/sbp2_driver.c:13`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/sbp2_driver.c:45`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/main.c:13`; `RiptOPL/modules/ilink/IEEE1394_bd/src/sbp2_driver.c:802-807`; `RiptOPL/modules/ilink/IEEE1394_bd/src/sbp2_driver.c:711-716`; `RiptOPL/modules/ilink/IEEE1394_bd/src/sbp2_driver.c:646-655`
- Problem: DeinitIEEE1394 has no caller (only its prototype) and is the only reader that needs iLinkIntrCBThreadID global; sbp2_get_max_lun always returns 0 and scsi_warmup only M_DEBUGs it; ieee1394_SendManagementORB is only called with SBP2_LOGIN_REQUEST and rejects anything else; usbhdfsd-common.h supplies nothing used; a commented-out declaration and IRX_ID remain.
- Fix: Delete DeinitIEEE1394 and its prototype and make iLinkIntrCBThreadID a local; delete sbp2_get_max_lun, the get_max_lun member and its call; drop the mode parameter (always LOGIN) and its guard; drop the usbhdfsd-common.h include and the commented lines. Adjust the tests that extract these functions by signature (test_ilink_sbp2_completion.py, test_ilink_bd_reset.py, test_ilink_bd_assault_reset.py, test_ilink_scsi_geometry.py).
- Benefit: ~25 lines per copy, ~50 total; DeinitIEEE1394 and sbp2_get_max_lun are non-static or referenced, so a few dozen bytes per IRX (INFERRED, not measured).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-006: scsi_read and scsi_write are the same loop in both IEEE1394_bd copies

- Category: 2 (Duplication)
- Location(s): `RiptOPL/modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/scsi.c:238-290`; `RiptOPL/modules/ilink/IEEE1394_bd/src/scsi.c:304-358`
- Problem: scsi_read and scsi_write differ only in the const qualifier and the write flag passed to scsi_cmd_rw_sector; the 25-line chunking/retry loop is written twice in each copy.
- Fix: Keep one static scsi_rw(bd, sector, buffer, count, write) with the loop and make scsi_read/scsi_write one-line calls (or point the block_device read/write at thin wrappers).
- Benefit: ~20 lines per copy, ~40 total; roughly 100-150 bytes of IRX code per copy (INFERRED: one fewer copy of a small loop).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-007: cheatman parser: parse_line wrapper re-tests is_cheat_code, make_code has a redundant zero branch, term_str callback has one caller

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/core/cheatman.cpp:149-158`; `RiptOPL/src/core/cheatman.cpp:184-208`; `RiptOPL/src/core/cheatman.cpp:224-240`; `RiptOPL/src/core/cheatman.cpp:341-355`
- Problem: parse_buf already calls is_cheat_code (:346) and enters the parser branch for code lines and empty lines; parse_line (:187-200) runs is_cheat_code again only to return a zero code for the empty-line case, plus a per-line LOG. make_code pre-zeroes address/value and then has a failure branch that zeroes again (:149-153). term_str (:227-240) is a generic callback walker with a NULL check whose only caller passes is_cmt_str, which re-runs strlen at every character.
- Fix: In parse_buf, call make_code directly when is_cheat_code(line) is true and skip the line otherwise; delete parse_line. In make_code delete the sscanf failure branch: its only caller passes lines holding exactly CODE_DIGITS hex digits, so both fields always parse. Replace term_str+is_cmt_str with one inline scan that cuts the line at the first '#' or '//'.
- Benefit: ~30 lines removed; a few hundred bytes of .text at most (INFERRED; static functions, likely inlined already).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-008: OSDHistory: unused date getters, no-op DEBUG_PRINTF calls, commented-out loop and three stray includes of OSDHistory.h

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/OSDHistory.h:83-85`; `RiptOPL/src/core/OSDHistory.cpp:33`; `RiptOPL/src/core/OSDHistory.cpp:134`; `RiptOPL/src/core/OSDHistory.cpp:139`; `RiptOPL/src/core/OSDHistory.cpp:145-149`; `RiptOPL/src/core/OSDHistory.cpp:155-162`; `RiptOPL/src/core/OSDHistory.cpp:217-223`; `RiptOPL/src/core/OSDHistory.cpp:45-58`; `RiptOPL/src/platform/sysvmc.cpp:41`; `RiptOPL/src/platform/sysneutrino.cpp:42`; `RiptOPL/src/platform/sysdisc.cpp:43`
- Problem: osdHistoryGetYear/Month/Date have no use anywhere. DEBUG_PRINTF is defined empty at :33 and never redefined, so its 7 calls compile to nothing. :217-223 is a commented-out loop from the original. The three switch arms at :45-58 all set the same size. sysvmc.cpp, sysneutrino.cpp and sysdisc.cpp include OSDHistory.h but use nothing from it (only system.cpp:1350,1378 calls AddHistoryRecordUsingFullPath).
- Fix: Delete the three getters, the DEBUG_PRINTF define and its calls, and the commented-out loop; set size once after the switch (or pick only the icon pointer in it); drop the OSDHistory.h include from sysvmc.cpp, sysneutrino.cpp and sysdisc.cpp.
- Benefit: ~25 lines removed; no ELF change (constexpr getters and empty macros generate no code) (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-009: POPSTARTER i.LINK driver registers the full iLinkman export table that nothing imports

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/modules/bdmassault/ilink_bd_assault/src/exports.tab:1-30`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink.c:156-159`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink_crom.c:65-116`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink.c:266-276`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink.c:300-303`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink.c:366-369`
- Problem: The combined module links IEEE1394_bd statically to iLinkman, yet still exports 22 iLinkman entry points and calls RegisterLibraryEntries. Under POPSTARTER the only other driver is bdm_assault (usbd.irx.ilink), so nothing imports them; iLinkAddCROMUnit/DeleteCROMUnit, GetNodeCapabilities, SetNodeCapabilities, DisableSBus and GetLocalNodeEUI64 are reachable only through this table and stay in a memory-card IRX.
- Fix: Reduce src/exports.tab to the mandatory _start/_exit/_retonly slots (or drop the library registration) and let the export-only functions be removed; verify on a console that POPSTARTER loads the module without the iLinkman library.
- Benefit: Export-only functions are ~70 lines of C (cited ranges); a few hundred bytes of the 27,769-byte usbhdfsd.irx.ilink (INFERRED, not measured).
- Effort: M. Risk: Med. Confidence: Low (INFERRED)

### Benefit rank 1

#### RIPTOPL-010: OSDHistory.h carries a 79-line block of disassembly notes from the original OSDSYS

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/include/OSDHistory.h:3-81`
- Problem: Lines 3-73 are reverse-engineering notes (hex dumps of record bytes and a pseudo-C transcription of OSDSYS 1.00 code at fixed addresses) that the ported implementation in OSDHistory.cpp:126-264 already embodies; only the 'Conclusion' field layout at :74-80 documents the struct below it. The block is upstream-derived (INFERRED: same notes as upstream OPL).
- Fix: Keep the 6-line field layout (:74-80) next to struct HistoryEntry and delete the rest of the comment.
- Benefit: ~72 comment lines removed; no binary effect.
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-011: Dead USE_CUSTOM_ILINKMAN_MALLOC_AND_FREE block duplicates sbp2_driver.c malloc/free

- Category: 1 (Dead code)
- Location(s): `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink_internal.c:170-190`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/sbp2_driver.c:782-801`
- Problem: USE_CUSTOM_ILINKMAN_MALLOC_AND_FREE is defined nowhere, so the iLinkman malloc/free copy never compiles; the combined module uses the identical pair in sbp2_driver.c:782-801 (the #ifdef exists only to avoid duplicate symbols).
- Fix: Delete the #ifdef block in iLink_internal.c.
- Benefit: 22 lines removed; no binary change. INFERRED.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-012: scsiCommandBudget re-implements sbp2ElapsedUs, and the TEST UNIT READY try count duplicates the 30 s budget

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/scsi.c:17-46`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/scsi.c:183-190`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/IEEE1394_bd/src/sbp2_driver.c:143-155`; `RiptOPL/modules/ilink/IEEE1394_bd/src/scsi.c:17-46`; `RiptOPL/modules/ilink/IEEE1394_bd/src/sbp2_driver.c:144-156`
- Problem: scsi.c:36-41 repeats sbp2ElapsedUs's GetSystemTime/64-bit subtract/SysClock2USec sequence from the same module. The warm-up is bounded twice: SCSI_READY_TRIES 300 polls at 100 ms each cannot end before about 30 s, which is SCSI_READY_BUDGET_US, the elapsed bound checked in the same condition.
- Fix: Make sbp2ElapsedUs non-static (declare it in scsi.h) and call it from scsiCommandBudget; drop SCSI_READY_TRIES and the tries counter, keeping the 30 s elapsed-time budget (it allows at most one more poll), and update the turCalls <= 300 checks in test_ilink_bd_reset.py and test_ilink_bd_assault_reset.py plus the scsi.c extraction in those tests so they supply the shared helper.
- Benefit: ~10 lines per copy, ~20 total; small code-size saving (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-013: Upstream BDM leftovers: always-true MBR 'valid' counter, unused GPT GUID and attribute macros, unneeded includes

- Category: 1 (Dead code)
- Location(s): `RiptOPL/modules/bdm/src/part_driver_mbr.c:14-32`; `RiptOPL/modules/bdm/src/include/gpt_types.h:44`; `RiptOPL/modules/bdm/src/include/gpt_types.h:56`; `RiptOPL/modules/bdm/src/include/gpt_types.h:58-68`; `RiptOPL/modules/bdm/src/part_driver.c:2-9`
- Problem: In partitions_sanity_check_mbr 'valid' is incremented on every loop pass and the loop either returns early or ends with valid == 4, so 'valid == 4' is always true; MS_BASIC_DATA_PARTITION_GUID and the GPT_PART_ATTR_PLATFORM_REQUIRED/LEGACY_BOOTABLE/TYPE_* and GPT_BDP_ATTR_* macros are referenced nowhere; part_driver.c includes mbr_types.h, gpt_types.h, errno.h and string.h without using them.
- Fix: Return active > 0 and delete the valid counter; delete the unused GUID and macros; drop the four unused includes from part_driver.c. Upstream ps2sdk code: optional, or offer upstream.
- Benefit: ~15 lines; no binary change (static const arrays and macros are not emitted when unused). INFERRED.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-014: load_cheats: read_text_file's maxsize option is never used and the parse tail duplicates load_cheats_buf

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/core/cheatman.cpp:387-393`; `RiptOPL/src/core/cheatman.cpp:411-415`; `RiptOPL/src/core/cheatman.cpp:467-507`
- Problem: read_text_file's only caller passes maxsize 0 (:476), so the size-limit branch (:411-415) is dead. load_cheats (:467-489) repeats load_cheats_buf's ensureCheatTable/parse_buf/mode-return sequence (:495-507).
- Fix: Drop the maxsize parameter and its branch. In load_cheats_buf call ensureCheatTable before the NULL test, so a NULL buffer still clears the table; then make load_cheats read the file, call load_cheats_buf(buf), free the buffer and return that result. Do not simply read first and clear later: a failed read must still leave an empty table, or set_cheats_list (host.cpp:156) can flatten the previous game's cheats on a launch that continues past the error.
- Benefit: ~12 lines removed; no measurable ELF change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-015: bgm.cpp: stale section banner, orphaned member comment and two unused includes

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/core/bgm.cpp:8`; `RiptOPL/src/core/bgm.cpp:19`; `RiptOPL/src/core/bgm.cpp:21-22`; `RiptOPL/src/core/bgm.cpp:116-120`
- Problem: The 'Theme Sound Effects' banner (:21-22) heads nothing: sound effects moved to sfx.cpp. :116-120 describes bytesPerMs_ and roomTarget_, which now live in include/bgmplayer.h:46-47, and sits above an unrelated function. Nothing in bgm.cpp calls a pad.h function, and the only timer-like call, SetAlarm, is a kernel.h syscall that opl.h:5 already brings in (INFERRED for timer.h).
- Fix: Delete the banner, move or delete the orphaned comment, and drop the timer.h and pad.h includes.
- Benefit: ~9 lines removed; no binary effect.
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-016: GPT driver re-checks a sector size that bdm_try_mount already guarantees to be 512

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/modules/bdm/src/part_driver_gpt.c:69-74`; `RiptOPL/modules/bdm/src/bdm.c:169-180`
- Problem: part_connect_gpt is reached only through g_gpt_fs.connect_bd, which bdm_try_mount calls only after refusing every device whose sectorSize != 512, so entriesPerSector is always 4 and the entriesPerSector <= 0 branch (added in commit 5fe47865, not SDK code) cannot run.
- Fix: Delete the check (lines 69-74).
- Benefit: 6 lines; a few instructions in bdm.irx (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-017: ilink_bd_assault build leftovers: redundant -DREQ_CHECK_CONSOLE_VERSION, empty IOP_LIB, cdvdman.h includes

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/modules/bdmassault/ilink_bd_assault/Makefile:10`; `RiptOPL/modules/bdmassault/ilink_bd_assault/Makefile:16-21`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/include/iLink_internal.h:16`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/iLinkman/src/iLink_internal.c:15`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/irx_imports.h:20`; `RiptOPL/modules/bdmassault/ilink_bd_assault/src/irx_imports.h:32-34`
- Problem: REQ_CHECK_CONSOLE_VERSION is already #defined unconditionally in iLink_internal.h:16 (included by iLink.c before its use), so the Makefile -D only duplicates it; IOP_LIB is never set and IOP_INC_DIR names src/include/, which does not exist; iLink_internal.c and the live src/irx_imports.h still include cdvdman.h although nothing uses cdvdman, and the SIF_CALLBACKS_12_13 guard in src/irx_imports.h is never defined.
- Fix: Drop the -DREQ_CHECK_CONSOLE_VERSION flag, the $(IOP_LIB) references and IOP_INC_DIR, the cdvdman.h includes in iLink_internal.c and src/irx_imports.h, and the SIF_CALLBACKS_12_13 guard in src/irx_imports.h.
- Benefit: ~6 lines; no binary change expected (INFERRED; the CI byte check in flavours.yml:376-391 would confirm).
- Effort: S. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-018: cheatman.h: CHEAT_VERSION, CR and SPACE are never used

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/cheatman.h:37`; `RiptOPL/include/cheatman.h:48-49`
- Problem: No C/C++ source uses CHEAT_VERSION, CR or SPACE from this header; NUL and LF (:46-47) are the only character constants the cheat code uses. CHEAT_VERSION's only other mention is a text anchor in .github/scripts/test_cheat_capacity.py:24.
- Fix: Delete the three definitions and move the test's slice anchor to the next line (for example 'constexpr int MAX_HOOKS').
- Benefit: 3 lines removed; no ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-019: parse_buf computes each cheat's Select-mode default that cheatConfigLoadSelections always recomputes

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/core/cheatman.cpp:27`; `RiptOPL/src/core/cheatman.cpp:367-369`; `RiptOPL/src/core/cheatconfig.cpp:71-73`
- Problem: parse_buf sets enabled = (gCheatMode == 0) || cheatConfigIsMasterCode(name) for every cheat. In Select mode (load returns 1) both load paths call sbCheatsSelect (supportbase.cpp:1785,1840), whose cheatConfigLoadSelections first resets every entry to cheatConfigIsMasterCode(name) (cheatconfig.cpp:72-73). The parser's master-code test is therefore overwritten every time, and it is the only reason cheatman.cpp includes cheatconfig.h.
- Fix: Set enabled = 1 in parse_buf (Auto mode runs every cheat; Select mode is reset by cheatConfigLoadSelections), drop the cheatconfig.h include from cheatman.cpp, and drop or rewrite the text check in .github/scripts/test_neutrino_cheats.py:335 that requires the old line.
- Benefit: ~3 lines and one header dependency removed; saves one name normalisation (one 129-byte stack buffer and a few strcmp) per parsed cheat (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-020: Dead setting key CONFIG_OPL_ENABLE_ANALOG_NAV and its dialog id UICFG_ENABLE_ANALOG_NAV

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/config.h:114`; `RiptOPL/include/dialogs.h:27`
- Problem: The Left Stick Navigation option was removed (dialogs.cpp:2263-2268 says it is never coming back), but its config key and its UI id remain. Neither name, nor the key string enable_analog_nav, is used anywhere else.
- Fix: Delete both lines.
- Benefit: 2 lines removed; no ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-021: config.h comments point to files that no longer exist (src/libview.c, opl.c, src/config.c)

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/include/config.h:125`; `RiptOPL/include/config.h:168`; `RiptOPL/include/config.h:311`
- Problem: The comments name libViewStoreToConfig in src/libview.c, readConfigPathRedirect() in opl.c and src/config.c; the code now lives in src/ui/libview.cpp:351, src/app/settingsio.cpp:296 and src/core/config.cpp, and none of the named .c files exists.
- Fix: Correct the three paths.
- Benefit: Cosmetic; 0 lines removed.
- Effort: S. Risk: Low. Confidence: High (READ)

### Not yet verified (raised by a reader, second check still pending)

#### RIPTOPL-022: Boot logo animation frames LOGO1..LOGO6 and the single LOGO_PICTURE are decoded on every theme load but never drawn

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/src/ui/guisplash.cpp:70-73`; `RiptOPL/src/gfx/themes.cpp:1683-1697`; `RiptOPL/include/themes.h:281`; `RiptOPL/include/textures.h:102-112`; `RiptOPL/src/gfx/textures.cpp:120-124`; `RiptOPL/src/gfx/textures.cpp:291-301`; `RiptOPL/tools/assets.py:53-54`; `RiptOPL/gfx/logo.png`; `RiptOPL/gfx/logo1.png`; `RiptOPL/gfx/logo2.png`; `RiptOPL/gfx/logo3.png`; `RiptOPL/gfx/logo4.png`; `RiptOPL/gfx/logo5.png`; `RiptOPL/gfx/logo6.png`
- Problem: The splash no longer animates: guisplash.cpp:73 draws only LOGO0_PICTURE (LOGO_PICTURE only when logoFrameCount is 0, which cannot happen because logo0 is always embedded and loaded with texLoadInternal). Yet themes.cpp:1684 and 1691-1697 decode LOGO_PICTURE and all seven LOGO0..LOGO6 frames on every theme load and keep them until the theme is freed. No other code references LOGO1..LOGO6_PICTURE or logoFrameCount. gfx/logo4..6.png are not embedded at all (assets.py:54 lists only logo0..3; textures.cpp:299-301 aliases them).
- Fix: Keep one logo: map LOGO_PICTURE to the logo0 image (the one drawn today) and delete LOGO0..LOGO6_PICTURE from textures.h, their table rows and extern declarations in textures.cpp, the frame loop and logoFrameCount in themes.cpp/themes.h, the ternary in guisplash.cpp, and the files gfx/logo1..6.png plus the old gfx/logo.png (renaming logo0.png to logo.png) and their assets.py entries.
- Benefit: EE heap: 6 unused T8 512x128 frames (65536 B + 1 KB CLUT each, textures.cpp:1093-1095) + LOGO_PICTURE T8 512x256 (128 KB + 1 KB) = ~520 KB held for the theme lifetime (INFERRED arithmetic). ELF: logo.png+logo1..3.png = 32,964 B embedded (MEASURED file sizes). 7 fewer PNG decodes per theme load.
- Effort: S. Risk: Med. Confidence: High (INFERRED)

#### RIPTOPL-023: thmLoad decodes and keeps seven boot-logo textures that are never drawn (LOGO_PICTURE, LOGO1..LOGO6) in every loaded theme

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/src/gfx/themes.cpp:1683-1697`; `RiptOPL/src/ui/guisplash.cpp:70-73`; `RiptOPL/src/gfx/textures.cpp:291-301`; `RiptOPL/src/gfx/textures.cpp:120-124`
- Problem: Every thmLoad (boot, each theme switch, each hold-L3 view switch, Widescreen/alt/wallpaper/grid/language reloads) runs texLoadInternal for LOGO_PICTURE, TECHWRITESCODE_LOGO and LOGO0..LOGO6, and the textures stay allocated until the theme is freed. The only reader is the boot splash, which since the animation was dropped draws LOGO0 alone (LOGO_PICTURE only when logoFrameCount is 0, i.e. LOGO0 failed). LOGO1..LOGO6 and, normally, LOGO_PICTURE are never drawn; LOGO0 and the credit mark are not drawn after the intro either.
- Fix: Delete the LOGO1..LOGO6 loop and load LOGO0 only (LOGO_PICTURE only if LOGO0 failed, or drop that fallback), keeping the enum ids since texture ids are positional. Point the LOGO1..LOGO6 table entries at NULL and drop logo1..logo3 (and logo, if the fallback goes) from the embedded asset list. Optional second step: skip LOGO0 and TECHWRITESCODE_LOGO for loads after the intro has ended.
- Benefit: ~519 KB EE RAM per loaded theme, ~1 MB while old and new theme coexist (INFERRED: logo.png 512x256 T8 = 129 KB, logo1-6 6 x 512x128 T8 = 390 KB); 7 PNG decodes less per load; up to 29,811 B ELF (MEASURED raw/riptopl-per-object.tsv: logo 9954, logo1-3 19,857)
- Effort: S. Risk: Low. Confidence: High (INFERRED)

#### RIPTOPL-024: Legacy HTTP SEND_GET_REQ path (EE HttpSendGetRequest + IOP HttpGetResponse/HttpSendGetRequest) has no caller since the compatibility updater is gone

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/platform/httpclient.cpp:61-93`; `RiptOPL/src/platform/httpclient.cpp:157-161`; `RiptOPL/modules/network/httpclient/main.c:24`; `RiptOPL/modules/network/httpclient/main.c:44-63`; `RiptOPL/modules/network/httpclient/httpclient.c:103-250`; `RiptOPL/modules/network/httpclient/httpclient.c:322-383`; `RiptOPL/modules/network/httpclient/exports.tab:9`; `RiptOPL/modules/network/common/httpclient.h:12-20`; `RiptOPL/modules/network/common/httpclient.h:34-37`; `RiptOPL/modules/network/common/httpclient.h:48-55`; `RiptOPL/modules/network/common/httpclient.h:118-137`; `RiptOPL/modules/network/common/httpclient.h:145`; `RiptOPL/tools/pc/http/tests/test_stream.c:295-310`
- Problem: No EE code calls HttpSendGetRequest or HttpStreamEnd (grep: only their definitions in httpclient.cpp and host test_stream.c); opl.map drops both (400 + 8 bytes). The IOP SEND_GET_REQ case, its 512-byte DmaBuffer and the IOP HttpSendGetRequest with its private helpers GetData, HttpParseEntityLine, HttpGetResponse and GetDayInWeek therefore serve only a host test. The 'compatibility updater' named in httpclient.h:36,55 no longer exists (only guiGameCompatUpdater, a dialog callback). All live HTTP goes through STREAM_BEGIN/READ.
- Fix: Delete the EE HttpSendGetRequest and HttpStreamEnd wrappers, the IOP SEND_GET_REQ case and DmaBuffer, IOP HttpSendGetRequest, HttpGetResponse, HttpParseEntityLine, GetData, GetDayInWeek and the work-buffer statics, the HttpClientSendGetArgs/Result structs, the mtime doc, prototype, I_ macro and stale 'compatibility updater' comments, and test 20 in test_stream.c. Remove the enum value on both sides together (EE and IRX are built as one) and keep the export ordinals by replacing the export line with _retonly or renumbering the I_ macros.
- Benefit: ~310 source lines removed. EE: no ELF change (already gc'd, MEASURED raw/riptopl-discarded-firstparty.txt). IOP: httpclient.irx (10,917 bytes, MEASURED raw/irx-modules.tsv) is linked without --gc-sections, so roughly 2-3 KB of .text plus 512 B .bss leaves the embedded IRX (INFERRED from function sizes).
- Effort: M. Risk: Low. Confidence: High (READ)

#### RIPTOPL-025: GSHandler delay-slot emulation: B_taken zero-extends backward branch offsets and B_com1 masks the REGIMM field with 0x3F

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/ee_core/src/gsm_engine.S:1128-1134`; `RiptOPL/ee_core/src/gsm_engine.S:1061-1064`; `RiptOPL/ee_core/src/gsm_engine.S:119-127`; `RiptOPL/ee_core/src/main.c:127-152`
- Problem: When a trapped GS access is in a branch delay slot, GSHandler computes the resume PC itself. B_taken takes the offset with and 0xFFFF then sll 2, so a negative offset is zero-extended: a taken backward branch resumes at EPC+4+~0x3FFxx (about 256 KB ahead) instead of the loop head. B_com1 masks the REGIMM rt field with 0x3F, which also takes bit 21 (low bit of rs), so BLTZ/BGEZ/... on an odd register (v1, a1, a3, t1, s1, sp, ra...) indexes past the 32-word BD_t2 table and jumps to whatever word follows it. Either way the game jumps into arbitrary code.
- Fix: In B_taken replace the li/and/sll triple with a sign-extending pair (shift the instruction left 16, then arithmetic shift right 14). In B_com1 change the mask from 0x3F to 0x1F (its own comment already says range 0x00-0x1F).
- Benefit: Fixes a crash/freeze whenever a trapped GS access sits in a backward-branch delay slot (GSM: CSR polling loops with the ld in a beql slot; IGR menu tracker: GS register stores in loops) or an odd-rs REGIMM slot (INFERRED frequency); net -1 instruction
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-026: Atlas node tree held in nested raii::UniquePtr makes GCC unroll the recursive destructor into ~14 KB of .text

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/include/atlas.h:42-52`; `RiptOPL/src/gfx/atlas.cpp:42-47`; `RiptOPL/src/gfx/atlas.cpp:54-60`; `RiptOPL/src/gfx/atlas.cpp:117-123`; `RiptOPL/include/raii.h:83-95`
- Problem: Atlas::Node owns its children as raii::UniquePtr<Node>, so destroying the tree is a recursive chain of inline templates (unique_ptr dtor -> DestroyFree -> ~Node -> two unique_ptr dtors). At -O2 GCC inlines that recursion several levels deep: nm of the shipped ELF shows Atlas::~Atlas at 0x1958 (6,488 B) and std::unique_ptr<Atlas::Node, raii::DestroyFree>::~unique_ptr at 0x1d10 (7,440 B) plus a 168 B DestroyFree part, i.e. 14,096 of atlas.o's 15,448 text bytes for freeing a binary tree that upstream OPL freed with a ten-line recursive function.
- Fix: Stop the recursive inlining: either give Node plain Node* children and free the tree with one small static recursive (or iterative) free function, as upstream OPL's atlas.c did, or keep UniquePtr and declare Node's destructor out of line with __attribute__((noinline)). Confirm with nm that Atlas::~Atlas and the unique_ptr<Atlas::Node> destructor drop to a few hundred bytes.
- Justification: The fix replaces the template destructor chain with one explicit free routine (or one attribute); source size stays about the same while ~13.8 KB of loaded .text goes away.
- Benefit: ~13.8 KB .text: MEASURED 6,488 + 7,440 + 168 = 14,096 B now (riptopl-nm-size-sorted.full.txt:132-133,153-154,3181); a non-inlined free is ~200-300 B (INFERRED). ELF is packed, so on-disk saving is smaller (INFERRED); RAM saving is the full amount.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-027: ArtIndex allocates hashes[4096] and pics[4096] (48 KB) per indexed directory regardless of its size and keeps them for the session

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/include/artindexint.h:29-34`; `RiptOPL/include/artindexint.h:40-44`; `RiptOPL/src/gfx/artindex.cpp:191-201`; `RiptOPL/src/gfx/artindex.cpp:281-286`
- Problem: ArtIndex::build mallocs kMaxEntries*sizeof(unsigned int) = 16,384 bytes for hashes and kMaxEntries*sizeof(Pic) = 32,768 bytes for pics before the sweep and publishes those same buffers into the slot, never shrinking them to the n / picCount entries actually found. Slots are freed only when round-robin reuses them (release() via slotFor; invalidate() frees nothing), so with kSlots = 3 up to 147,456 bytes stay allocated for the whole run even when each ART folder holds a few hundred files. The index is live: artload.cpp:777 registers the art worker as owner and textures.cpp:1345,1357,1431 query it.
- Fix: After the sweep succeeds, shrink hashes to n entries and pics to picCount entries with realloc (skip the shrink when the count is 0), before publishing them into the slot.
- Justification: Two realloc calls are the smallest change that returns the unused tail of each array to the heap; the alternatives (a counting pre-pass or growable arrays) add more code. Concrete gain: about 46 KB per slot for a 200-file ART folder, which is >= 32 KB of EE RAM that is otherwise held for the session.
- Benefit: INFERRED: per slot 49,152 B allocated vs 12*n B needed (n=200: 2,400 B), so ~46 KB freed per slot, up to ~140 KB with all three slots in use. sizeof(Pic)=8 from the two 4-byte members at artindexint.h:40-44.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-028: bgm.cpp: 768 KB static bgmBuffer ring is reserved for the whole run although BGM is off by default

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/src/core/bgm.cpp:29-30`; `RiptOPL/src/core/bgm.cpp:72`; `RiptOPL/src/core/bgm.cpp:720-734`; `RiptOPL/src/core/settings.cpp:176`
- Problem: bgmBuffer[192][4096] is file-static .bss (786,432 bytes, the largest symbol in the ELF) and is only touched by the two BGM threads, yet CONFIG_OPL_BGM defaults to 0 (settings.cpp:176) and BGM also stays silent when no bgm.ogg is found. In the default setup the 768 KB is EE RAM the menu can never use.
- Fix: Allocate the ring with malloc in BgmPlayer::start() after load() succeeds (fail like a missing file when it returns NULL) and free it in teardown(); keep it when stop() abandons threads that did not exit. Use the pointer in threadMain/ioThreadMain instead of the static array.
- Justification: Adds about six lines (malloc, NULL check, free, pointer member). The concrete gain is 768 KB of EE RAM back for every user without theme music, which is the default; the same lazy pattern was already applied to the 1 MB cheat table (cheatman.cpp:448-465) for the same reason.
- Benefit: 786,432 bytes of .bss removed (MEASURED: evidence/RiptOPL/BUILD-EVIDENCE.md:95 and :249, nm of the unstripped ELF); RAM is then held only while music plays.
- Effort: S. Risk: Med. Confidence: High (READ)

#### RIPTOPL-029: httpParseCatalog allocates 2048-row arrays (~890 KB) for every catalog and keeps them for the whole HTTP listing

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/src/sources/httpsupport.cpp:165-166`; `RiptOPL/src/sources/httpsupport.cpp:241-243`; `RiptOPL/src/sources/httpsupport.cpp:604-610`; `RiptOPL/src/sources/httpsupport.cpp:443-455`; `RiptOPL/include/httpsupport.h:19`
- Problem: Both row arrays are sized HTTP_CATALOG_ROWS_MAX (2048) whatever the row count: base_game_info_t is 188 bytes (161+13+5 chars, 3 u8, padding, u32) and http_catalog_extra_t 257 bytes, so 445 B x 2048 = 911,360 bytes are held while the HTTP source is listed. A refresh peaks near 2 MB, because update() parses the new pair before setCatalog frees the old one, on top of the up-to-256 KB fetch buffer. A 100-row catalog needs about 44 KB.
- Fix: At the end of httpParseCatalog, shrink both arrays to `rows` entries (at least one) with realloc before publishing them, keeping the original pointers if realloc returns NULL.
- Justification: Adds about 6 lines. It returns roughly 850 KB of EE heap for a typical catalog for the whole time the HTTP source is active, and the same on the test-server copy. The parse contract the host fixtures check does not change: the harness compiles the extracted function against libc, which has realloc.
- Benefit: ~867 KB EE heap freed for a 100-row catalog and the refresh peak lowered by the same (INFERRED: (2048-100) x 445 B). Only while the HTTP source is in use, so rated 4, not 5.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-030: LAUNCH_DIAG UDPBD triage markers are hard-wired off (#define LAUNCH_DIAG 0) yet keep ~250 lines across 13 files

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/platform/launchdiag.cpp`; `RiptOPL/include/launchdiag.h`; `RiptOPL/src/platform/elfldr_noreset.cpp:19`; `RiptOPL/src/platform/elfldr_noreset.cpp:125-126`; `RiptOPL/src/platform/elfldr_noreset.cpp:139-146`; `RiptOPL/src/platform/elfldr_noreset.cpp:156`; `RiptOPL/src/platform/elfldr_noreset.cpp:176-183`; `RiptOPL/src/platform/elfldr_noreset.cpp:202-203`; `RiptOPL/src/platform/elfldr_noreset.cpp:222-223`; `RiptOPL/src/platform/elfldr_noreset.cpp:243-244`; `RiptOPL/src/app/opl.cpp:1302-1303`; `RiptOPL/src/app/opl.cpp:1338-1339`; `RiptOPL/src/app/opl.cpp:1357-1358`; `RiptOPL/src/app/opl.cpp:1369-1370`; `RiptOPL/src/app/opl.cpp:1396-1397`; `RiptOPL/src/app/opl.cpp:1432-1433`; `RiptOPL/src/sources/bdmlaunch.cpp:358`; `RiptOPL/src/sources/bdmlaunch.cpp:393-394`; `RiptOPL/src/sources/bdmlaunch.cpp:450-451`; `RiptOPL/src/sources/bdmlaunch.cpp:474-475`; `RiptOPL/src/sources/bdmlaunch.cpp:486-487`; `RiptOPL/src/platform/sysneutrino.cpp:1144-1145`; `RiptOPL/src/platform/sysneutrino.cpp:1152-1153`; `RiptOPL/src/platform/sysneutrino.cpp:1393-1394`; `RiptOPL/src/platform/sysneutrino.cpp:1408-1409`; `RiptOPL/elfldr/loader.c:140-155`; `RiptOPL/elfldr/loader.c:396`; `RiptOPL/elfldr/loader.c:455`; `RiptOPL/elfldr/loader.c:469`; `RiptOPL/src/ui/menuitems.cpp:11`; `RiptOPL/src/sources/appscan.cpp:12`; `RiptOPL/src/platform/sysvmc.cpp:28`; `RiptOPL/src/platform/system.cpp:31`; `RiptOPL/src/platform/sysdisc.cpp:30`; `RiptOPL/src/platform/sysneutrino.cpp:29`; `RiptOPL/src/app/settingsio.cpp:13`; `RiptOPL/src/app/opl.cpp:15`; `RiptOPL/src/app/lwnbdctl.cpp:12`; `RiptOPL/src/app/autolaunch.cpp:11`; `RiptOPL/.github/scripts/test_handoff_probe.py:78-91`
- Problem: launchdiag.h:41 is a plain #define LAUNCH_DIAG 0 (no build can override it; commit be17a194 'LAUNCH_DIAG off for merge'), so launchdiag.cpp compiles to nothing, gLaunchDiag is the constant 0 and every marker, hold and refusal call is a no-op stub. The machinery is a finished one-off triage build for a single UDPBD hang (#755) that now costs a source file, a 92-line header, call sites in 6 files, a child-side copy in elfldr/loader.c and stale includes in 9 files (menuitems, appscan, sysvmc, system, sysdisc, settingsio, lwnbdctl, autolaunch use no launchDiag symbol).
- Fix: Delete src/platform/launchdiag.cpp (and its ps2.yaml:641 source entry), include/launchdiag.h, every launchDiag*/gLaunchDiag call and include, the diag parameter of probeTarget/sysLoadELFCommon, the LAUNCHDIAG_* block in elfldr/loader.c, and the LAUNCH_DIAG checks in test_handoff_probe.py and the stubs in test_neutrino_argv_budget.py. Keep the LOG lines. Git history keeps the triage build.
- Benefit: ~250 source lines removed (86 + 92 + ~70 call sites/includes); no ELF change because LAUNCH_DIAG 0 already compiles it all out (INFERRED from the #if at launchdiag.cpp:5 and the inline stubs at launchdiag.h:72-88).
- Effort: M. Risk: Low. Confidence: High (READ)

#### RIPTOPL-031: Ranet, RaBadge, RaHash and RaWatch are single-instance classes wrapped by a full layer of free forwarder functions

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/include/ranet.h:11-15`; `RiptOPL/include/ranet.h:100-102`; `RiptOPL/src/sources/ranet.cpp:148-209`; `RiptOPL/include/rabadge.h:15-43`; `RiptOPL/src/sources/rabadge.cpp:50-60`; `RiptOPL/src/sources/rahash.cpp:66-153`; `RiptOPL/include/rawatch.h:7-11`; `RiptOPL/include/rawatch.h:49-55`; `RiptOPL/src/sources/rawatch.cpp:32-72`
- Problem: Each class has exactly one object (gRanet, gRaBadge, static gRaHash, gRaWatch) and every product caller uses the C-style free functions, which only forward to that object (13 in ranet.cpp, 2 in rabadge.cpp, 6 plus disc_read_at in rahash.cpp, 8 in rawatch.cpp). The extern instance declarations in the headers are used by no other file. Two layers do the job of one; RaHash's comment even admits the class only exists in the .cpp because a test strips includes.
- Fix: Keep the free API that callers use. Turn each class into file-static state plus static functions in its .cpp, delete the class declarations, the extern instance lines and the forwarders, and name the former methods after the free functions (e.g. RaHash::hashDisc becomes raHashDisc, discReadAt becomes disc_read_at). Update the host tests that stub Ranet:: and RaWatch:: members by name to stub the static functions instead.
- Benefit: ~200 lines removed (INFERRED: ~60 forwarder lines in ranet.cpp, ~70 in rahash.cpp class+forwarders, ~40 in rawatch.cpp, ~25 in rabadge, header decls). ~30 forwarders of ~8-16 bytes each, ~0.3 KB in RA ELFs (INFERRED, not measured).
- Effort: M. Risk: Med. Confidence: High (READ)

#### RIPTOPL-032: Renderer class is a single-instance, single-file object reached only through 34 one-line rm* forwarders

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/gfx/renderman.cpp:817-965`; `RiptOPL/src/gfx/renderman.cpp:21`; `RiptOPL/include/renderer.h`
- Problem: include/renderer.h declares class Renderer; its only instance is `static Renderer gRenderer` (renderman.cpp:21) and no other source or header includes renderer.h or names Renderer. Every rm* function (renderman.cpp:819-965) only forwards to the matching method, and scaleXi/scaleYi exist only to wrap the private scaleX/scaleY template for rmScaleX/rmScaleY. The layer adds a second name for every entry point and a 96-line header without any second implementation or caller.
- Fix: Make the Renderer methods the rm* functions themselves, with the member variables as file-static state in renderman.cpp (keep scaleX/scaleY as static inline helpers), and delete include/renderer.h and the forwarder block. Update the four host tests that extract `Renderer::` function text (.github/scripts/test_vmode_safe_area.py, test_hires_line_blend.py, test_hires_thread_priority.py, test_theme_render_queue.py) to extract the rm* functions instead.
- Benefit: ~170 source lines (149-line forwarder block plus most of the 96-line header, minus ~25 lines of statics). No ELF change: MEASURED, riptopl-discarded-firstparty.txt shows the out-of-line Renderer:: copies (1,504 bytes) already discarded by --gc-sections because they are inlined into the forwarders.
- Effort: M. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-033: Second renderString body and bidi helpers under __RTL are superseded by txtShape, which already lays out Hebrew/Arabic in every build

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/gfx/fntsys.cpp:709`; `RiptOPL/src/gfx/fntsys.cpp:809-962`; `RiptOPL/src/gfx/fntsys.cpp:1217-1223`; `RiptOPL/include/fontsystem.h:116-118`; `RiptOPL/src/ui/gui.cpp:157-159`; `RiptOPL/tools/build_configuration.py:37-38`
- Problem: The extras/extras_ds5 configurations define __RTL (build_configuration.py:38), which swaps in a 154-line second copy of renderString (its first 30 lines repeat 710-740) plus isRTL/isWeak/renderSubRTL, and turns OFF txtShape in drawing and measuring. txtShape (textshape.cpp:283-287, 381) already reorders right-to-left lines for U+0590..08FF, i.e. Hebrew and Arabic, and also joins Arabic letters, so the __RTL build keeps a cruder duplicate bidi path; isRTL repeats txtRtlChar's ranges.
- Fix: Delete the #else __RTL branch (isRTL, isWeak, renderSubRTL and the second renderString), the #ifndef __RTL/#endif lines at 709 and 1217/1223, the renderSubRTL declaration, and the RTL tag in the About box; drop __RTL from the extras configuration. Update the source-text assertion in .github/scripts/test_ripps2_text_port.py:72.
- Benefit: ~160 source lines removed; the extras build gets the same (better) RTL layout as standard builds and one drawing path to maintain. ELF size of the extras build shrinks by the RTL code (INFERRED, not measured).
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-034: __DECI2_DEBUG debug path is never built (no ps2build configuration defines it), plus always-true _DTL_T10000 guards in debug.cpp

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/platform/debug.cpp:17-35`; `RiptOPL/src/platform/debug.cpp:61`; `RiptOPL/src/platform/debug.cpp:70-73`; `RiptOPL/src/platform/system.cpp:9-11`; `RiptOPL/src/platform/system.cpp:316-322`; `RiptOPL/src/platform/system.cpp:993-1000`; `RiptOPL/src/platform/system.cpp:1025-1037`; `RiptOPL/src/platform/system.cpp:1042-1046`; `RiptOPL/src/platform/system.cpp:1068-1122`; `RiptOPL/src/platform/system.cpp:1191-1193`; `RiptOPL/src/platform/system.cpp:1400-1407`; `RiptOPL/src/platform/sysdisc.cpp:9-11`; `RiptOPL/src/platform/sysneutrino.cpp:7-9`; `RiptOPL/src/platform/sysvmc.cpp:7-9`; `RiptOPL/src/app/opl.cpp:2056-2058`; `RiptOPL/include/system.h:122-124`; `RiptOPL/include/extern_irx.h:36`; `RiptOPL/include/extern_irx.h:170`; `RiptOPL/ee_core/src/iopmgr.c:183-186`; `RiptOPL/tools/syntax_sweep.py:23`; `RiptOPL/modules/debug/deci2.img`; `RiptOPL/modules/debug/drvtif.irx`; `RiptOPL/modules/debug/drvtif-ingame.irx`; `RiptOPL/modules/debug/tifinet.irx`; `RiptOPL/modules/debug/tifinet-ingame.irx`
- Problem: __DECI2_DEBUG is set only by the legacy Makefile (DECI2_DEBUG ?= 0, Makefile:84,339-343) and tools/syntax_sweep.py; tools/build_configuration.py never adds it, and no ps2.yaml target embeds drvtif/tifinet or deci2.img, so every DECI2 branch is dead and the five modules/debug blobs (92,197 bytes) are referenced only by the Makefile. In debug.cpp the _DTL_T10000 guards are also always true: debug.cpp is added only by the debug configurations (build_configuration.py:79-83) and configurations are exclusive, so none of them has _DTL_T10000 (only 'dtl', :41-43).
- Fix: Delete every #ifdef __DECI2_DEBUG block (keeping the #else/#elif branch where there is one), the drvtif/tifinet IMPORT_BIN2C lines, the DECI2 set in syntax_sweep.py and the modules/debug DECI2 blobs; in debug.cpp drop the #ifndef _DTL_T10000 wrappers and turn the TTY_UDP || __DECI2_DEBUG test into #ifdef TTY_UDP.
- Benefit: ~130 source lines and 92,197 bytes of committed binaries (MEASURED inventory/RiptOPL.tsv) removed; no shipped-ELF change since no build compiles these branches (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-035: DECI2 debug mode (__DECI2_DEBUG) is built by no ps2build configuration: declarations, code paths and 91.6 KB of blobs are dead

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/extern_irx.h:34-38`; `RiptOPL/include/extern_irx.h:170-172`; `RiptOPL/src/platform/system.cpp:9-11`; `RiptOPL/src/platform/system.cpp:316-319`; `RiptOPL/src/platform/system.cpp:993-999`; `RiptOPL/src/platform/system.cpp:1025-1037`; `RiptOPL/src/platform/system.cpp:1042-1046`; `RiptOPL/src/platform/system.cpp:1068-1122`; `RiptOPL/src/platform/system.cpp:1191-1193`; `RiptOPL/src/platform/system.cpp:1400-1401`; `RiptOPL/src/platform/debug.cpp:20-33`; `RiptOPL/src/app/opl.cpp:2056-2058`; `RiptOPL/include/system.h:122-124`; `RiptOPL/ee_core/src/iopmgr.c:183-185`; `RiptOPL/modules/debug/deci2.img`; `RiptOPL/modules/debug/drvtif.irx`; `RiptOPL/modules/debug/drvtif-ingame.irx`; `RiptOPL/modules/debug/tifinet.irx`; `RiptOPL/modules/debug/tifinet-ingame.irx`
- Problem: __DECI2_DEBUG is defined only by the legacy Makefile (Makefile:84,309-342) and ee_core/Makefile:43-44, and by the -D set in tools/syntax_sweep.py:23. No ps2.yaml target, tools/build_configuration.py or tools/build_edition.py defines it, and deci2/drvtif/tifinet are in no embed_irx list (ps2.yaml:652, build_configuration.py:84-102), so the DECI2 declarations in extern_irx.h, every #ifdef __DECI2_DEBUG block and the five prebuilt blobs are never built or linked.
- Fix: Delete the five extern_irx.h declarations, the __DECI2_DEBUG blocks in system.cpp, debug.cpp, opl.cpp, system.h and ee_core/src/iopmgr.c (keeping the TTY_UDP/TTY_PPC branches), CORE_IRX_DECI2 if nothing else remains, the DECI2 set in tools/syntax_sweep.py, and the five modules/debug blobs.
- Benefit: ~110 source lines (sum of cited ranges) and 91,597 bytes of committed blobs (MEASURED: ls -l modules/debug, 73625+5985+5985+3301+3301); no shipped ELF change since no build compiles it (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-036: __DECI2_DEBUG code in system.cpp (and its satellites) is compiled by no ps2build configuration

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/platform/system.cpp:9-11`; `RiptOPL/src/platform/system.cpp:316-322`; `RiptOPL/src/platform/system.cpp:993-1000`; `RiptOPL/src/platform/system.cpp:1025-1037`; `RiptOPL/src/platform/system.cpp:1042-1046`; `RiptOPL/src/platform/system.cpp:1068-1122`; `RiptOPL/src/platform/system.cpp:1191-1193`; `RiptOPL/src/platform/system.cpp:1400-1402`; `RiptOPL/src/platform/sysvmc.cpp:7-9`; `RiptOPL/src/platform/sysneutrino.cpp:7-9`; `RiptOPL/src/platform/sysdisc.cpp:9-10`; `RiptOPL/include/system.h:122-123`; `RiptOPL/src/app/opl.cpp:2056-2057`; `RiptOPL/src/platform/debug.cpp:20-25`; `RiptOPL/ee_core/src/iopmgr.c:183`
- Problem: The only places that define __DECI2_DEBUG are the legacy top-level Makefile:340 and ee_core/Makefile:44 (not the build) and a syntax-sweep set in tools/syntax_sweep.py:23; ps2.yaml, tools/build_*.py and the workflows never set it. So the DECI2 reboot buffer, the DECI2 in-game module pair, the kernel-RAM UDNL copy, ResetDECI2/sysInitDECI2 and their prototypes/callers are dead in every shipped or CI-built configuration.
- Fix: Delete every #ifdef __DECI2_DEBUG block listed (keep the #else side where there is one: the plain SifIopReset, the loop from i = 0, the TTY_UDP/PPC branches as #if/#elif), the sysInitDECI2 prototype and call, and the iopcontrol_special.h includes. The CORE_IRX_DECI2 masks at system.cpp:829 and 1411 then become no-ops and can go too.
- Benefit: ~95 lines in system.cpp plus ~15 in the satellites removed; no ELF change because the code is preprocessed out today (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-037: ee_core/Makefile is a stale legacy build: ps2build builds eecore from ps2.yaml, and this file no longer matches it

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/ee_core/Makefile:1-101`; `RiptOPL/Makefile:572-574`; `RiptOPL/docs/GSM.md:213`; `RiptOPL/docs/RETROACHIEVEMENTS-INTEGRATION-PLAN.md:259`
- Problem: The real eecore target is ps2.yaml:604-613 plus the overlays in tools/build_edition.py/build_configuration.py. ee_core/Makefile is reached only from the legacy top-level Makefile:572-574 (no workflow runs it) and has drifted: no IGR_MENU define or igrmenu.o/igrmbox.o (ps2.yaml:606,609), RA adds ra.o ra_overlay.o but not ra_pulse.o although ra_overlay.c:378 calls RA_PulseOnVblank (ra_pulse.c:73), and no freeze_diag/igrovl objects. clean and clean_all (95-99) are identical.
- Fix: Delete ee_core/Makefile together with the legacy top-level Makefile, and drop the two doc mentions. If the top-level Makefile stays for now, at least delete this file's dead DECI2/TTY_PPC knobs; it already cannot reproduce a shipped build.
- Benefit: 101 lines removed (MEASURED: file length); no ELF change, the file is not used by ps2build.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-038: LinkImage applies the PS2RD .img cheat image before the game is loaded, so the patches never land

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/ee_core/src/cheat_api.c:34-71`; `RiptOPL/ee_core/src/main.c:116-117`; `RiptOPL/ee_core/src/main.c:178-181`; `RiptOPL/ee_core/src/syshook.c:116-126`; `RiptOPL/src/app/host.cpp:161-163`; `RiptOPL/src/core/cheatman.cpp:39`; `RiptOPL/src/core/cheatman.cpp:111-125`; `RiptOPL/src/sources/supportbase.cpp:1701-1718`
- Problem: LinkImage is called from eecoreInit (main.c:116-117), i.e. on the first ee_core run, before LoadExecPS2 (main.c:181) restarts ee_core, which then wipes user memory (syshook.c:117, plus the kernel wipe from ModStorageEnd) and loads the game ELF (syshook.c:126). At LinkImage time the game is not in memory: the sanity gate reads OPL's leftover memory at Offset0 and fails, and any word it did write is wiped and overwritten by the game load. The $EnableImage option therefore never patches a game.
- Fix: Delete the .img feature (LinkImage, config->gImage, LoadImage/GetImage/gImage, sbLoadImage and its call sites, the $EnableImage row and strings). If the owner wants to keep it, the apply has to move to sysLoadElf after LoadElf/apply_patches, with the words kept somewhere the wipe does not reach (ee_core has no room for the 4 KB, cheat_api.c:36-38).
- Benefit: Removes an option that silently does nothing; ~100 lines across ee_core and the menu (INFERRED from the cited ranges plus prototypes/UI rows) and 4 KB of main-ELF .bss (static u32 gImage[MAX_IMAGEWORDS=1024] = 4096 bytes, INFERRED arithmetic).
- Effort: M. Risk: Low. Confidence: High (INFERRED)

#### RIPTOPL-039: One-line free forwarders duplicate EthSupport/FavSupport methods and netsupport (21 wrappers)

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/sources/ethsupport.cpp:207-210`; `RiptOPL/src/sources/ethsupport.cpp:349-352`; `RiptOPL/src/sources/ethsupport.cpp:359-362`; `RiptOPL/src/sources/ethsupport.cpp:405-408`; `RiptOPL/src/sources/ethsupport.cpp:424-437`; `RiptOPL/src/sources/ethsupport.cpp:681-684`; `RiptOPL/src/sources/ethsupport.cpp:711-714`; `RiptOPL/src/sources/ethsupport.cpp:889-892`; `RiptOPL/src/sources/ethsupport.cpp:1646-1654`; `RiptOPL/include/ethsupport.h:24-61`; `RiptOPL/include/favsupport.h:139-179`; `RiptOPL/src/sources/favsupport.cpp:1216-1219`; `RiptOPL/src/sources/favsupport.cpp:1409-1412`; `RiptOPL/src/sources/favsupport.cpp:1445-1460`; `RiptOPL/src/sources/favsupport.cpp:1472-1480`; `RiptOPL/src/sources/favsupport.cpp:1778-1791`
- Problem: Every public EthSupport/FavSupport operation exists twice: as a class method and as a free function whose whole body is `return gXxxSupport.method(...)` (eth: ethIsNfs, ethShareChosen, ethIsSMBShareConnected, ethEnsureSMBShareConnected, ethRequestCheckedRefresh, ethRequestReconnect, ethResolveIsoFavourite; fav: favGetItemSourceMode, favGetFlags, favGetItemKind, favGetArtMode, favRemoveByIndex, addFavouriteItem, removeFavouriteByIdAndText, favGetItemDisplay, favGetItemStarred, favToggleByIndex). Four more eth functions only forward to netsupport (ethApplyConfig, ethGetModulesLoaded, ethGetNetConfig, ethGetDHCPStatus). gEthSupport/gFavSupport are already extern in the same headers.
- Fix: Pick one API. Simplest: callers use gEthSupport.x()/gFavSupport.x() (and netApplyConfig/netGetModulesLoaded/netGetConfig/netGetDHCPStatus) directly; delete the 21 forwarders and their prototypes, moving the useful prototype comments onto the methods. Update the source-text tests that stub or scrape these names.
- Benefit: ~21 forwarders x 4 lines + ~40 prototype/comment lines in the two headers = ~110 lines removed, ~50 call sites renamed (INFERRED from cited ranges). ELF effect small: forwarders are non-static, no section GC assumed (INFERRED).
- Effort: M. Risk: Low. Confidence: High (READ)

#### RIPTOPL-040: Stateless single-instance classes BdmBoot, BdmLauncher, NeutrinoLauncher with private headers and free forwarders

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/include/bdmboot.h:1-41`; `RiptOPL/src/sources/bdmboot.cpp:41`; `RiptOPL/src/sources/bdmboot.cpp:98-112`; `RiptOPL/src/sources/bdmboot.cpp:488-496`; `RiptOPL/include/bdmlauncher.h:1-37`; `RiptOPL/include/neutrinolauncher.h:1-43`; `RiptOPL/src/sources/bdmlaunch.cpp:29-30`; `RiptOPL/src/sources/bdmlaunch.cpp:46-47`; `RiptOPL/src/sources/bdmlaunch.cpp:120-123`; `RiptOPL/src/sources/bdmlaunch.cpp:255-258`
- Problem: None of the three classes has a data member; each has exactly one global instance (gBdmBoot, gBdmLauncher, gNeutrinoLauncher) and each header is included only by its own .cpp. The public entry points are free functions that just forward to the instance, and BdmBoot even needs public static shims slotIdentity/elfOnSlot (bdmboot.h:22-24) so the file's own free function bootElfProbe can reach its private statics.
- Fix: Delete the three headers and the globals; make the member functions file-static functions in their .cpp, and let bdmResolveBootDir/bdmResolveBootDirBootstrap/bdmLaunchCue/bdmLaunchVcd be the functions themselves instead of forwarders. Update the source-scraping tests that look up 'BdmLauncher::'/'NeutrinoLauncher::' names.
- Benefit: ~100 lines removed (121 header lines, ~17 forwarder/global lines, minus comments worth keeping); no ELF change expected (INFERRED: no state, calls already resolve statically)
- Effort: M. Risk: Low. Confidence: High (READ)

#### RIPTOPL-041: In-game DECI2 debug mode (__DECI2_DEBUG) is dead: no ps2build configuration defines it and its IRX blobs are never embedded

- Category: 1 (Dead code)
- Location(s): `RiptOPL/ee_core/include/modules.h:33-34`; `RiptOPL/ee_core/src/iopmgr.c:183-185`; `RiptOPL/ee_core/Makefile:43-45`; `RiptOPL/src/platform/system.cpp:9-11`; `RiptOPL/src/platform/system.cpp:316-318`; `RiptOPL/src/platform/system.cpp:679`; `RiptOPL/src/platform/system.cpp:993-999`; `RiptOPL/src/platform/system.cpp:1025-1037`; `RiptOPL/src/platform/system.cpp:1042-1046`; `RiptOPL/src/platform/system.cpp:1068-1122`; `RiptOPL/src/platform/system.cpp:1191-1193`; `RiptOPL/src/platform/system.cpp:1400-1401`; `RiptOPL/include/system.h:122-123`; `RiptOPL/src/app/opl.cpp:2056-2057`; `RiptOPL/Makefile:340-342`; `RiptOPL/modules/debug/deci2.img`; `RiptOPL/modules/debug/drvtif.irx`; `RiptOPL/modules/debug/drvtif-ingame.irx`; `RiptOPL/modules/debug/tifinet.irx`; `RiptOPL/modules/debug/tifinet-ingame.irx`
- Problem: __DECI2_DEBUG is set only by the legacy make files (Makefile:340, ee_core/Makefile:44); tools/build_configuration.py (debug configs at :79-100) never adds it and ps2.yaml/tools never embed drvtif/tifinet/deci2. So OPL_MODULE_ID_DRVTIF/TIFINET, the iopmgr.c load branch and all menu-side DECI2 code (ResetDECI2, sysInitDECI2, CORE_IRX_DECI2) cannot be built by the real build; the 5 blobs (drvtif.irx and drvtif-ingame.irx are byte-identical) serve only the legacy deci2_debug target.
- Fix: Delete the __DECI2_DEBUG branches (keep the #elif TTY_UDP/TTY_PPC bodies as the #if), CORE_IRX_DECI2 and its masks, sysInitDECI2/ResetDECI2, and modules/debug/deci2.img, drvtif*.irx, tifinet*.irx. Leave OPL_MODULE_ID_DRVTIF/TIFINET in modules.h only if renumbering the later IDs is unwanted; otherwise delete them too (menu and core share the header and rebuild together).
- Benefit: ~100 source lines (INFERRED from the cited #ifdef ranges) and 92,197 bytes of committed blobs (MEASURED: ls -la modules/debug: 73625 + 2x5985 + 2x3301). No shipped ELF change: nothing here is compiled today.
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-042: vmc_groups.cpp: 97 count_ constants and the titleCount field can go if the arrays are NULL-terminated

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/core/vmc_groups.cpp:5-10`; `RiptOPL/src/core/vmc_groups.cpp:1153`; `RiptOPL/src/core/vmc_groups.cpp:1158`; `RiptOPL/src/core/vmc_groups.cpp:1172`; `RiptOPL/src/core/vmc_groups.cpp:1180`; `RiptOPL/src/core/vmc_groups.cpp:1344`; `RiptOPL/src/core/vmc_groups.cpp:1352-1451`; `RiptOPL/src/core/vmc_groups.cpp:1463`
- Problem: Each of the 97 groups has a hand-written count_X = sizeof(titles_X)/sizeof(titles_X[0]) line (1344 is one of them) feeding a titleCount field. The pattern has already broken: the comments at 1153, 1158, 1172, 1180 record four copy-pasted numerators that overstated the count and walked past the array.
- Fix: End each titles_ array with NULL on its closing line, drop the titleCount field and all 97 count_ lines (with the four 'was sizeof' history comments), and loop while titleIds[j] != NULL. Update tests/host/test_vmc_groups.cpp:25-26, which reads titleCount.
- Benefit: ~98 source lines removed (97 count lines + 1 field). ELF data about equal: -97 x 4-byte titleCount, +97 x 4-byte NULL (INFERRED). Removes the class of out-of-bounds bug the comments describe.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-043: deinit() is a line-for-line copy of deinitEx() with modeSelected2 = -1

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/app/opl.cpp:1373-1456`; `RiptOPL/src/app/opl.cpp:1279-1371`; `RiptOPL/src/app/opl.cpp:1254-1256`; `RiptOPL/src/app/opl.cpp:1310-1318`; `RiptOPL/src/app/opl.cpp:1404-1412`
- Problem: deinit (1373-1456) repeats deinitEx (1279-1371) statement for statement, including its 36 comment lines. The only differences: deinit calls deinitAllSupport where deinitEx runs the same loop with a spared mode (with modeSelected2 = -1 the loop is exactly deinitAllSupport, 608-614), and deinitEx adds launchDiagMark(7)/(8), which are gated on gLaunchDiag (compiled to 0 without LAUNCH_DIAG, armed only by udp-block Neutrino launches). Both copies also carry a stale paragraph saying 'deinitAllSupport below' (1310-1318, wrong in deinitEx) and the deinitEx header comment (1254-1256) is separated from its function by the drain-ticks block.
- Fix: Delete the body of deinit and make it call deinitEx(exception, modeSelected, -1). Keep one copy of the comments, drop the stale first 'JOIN the art worker' paragraph (the join is described again at cacheEnd), and move the deinitEx header comment next to its function. Adjust .github/scripts/test_menu_crash_catcher.py:63-64, which greps both bodies for menuWatchStop().
- Benefit: ~80 source lines removed (84-line function becomes a 3-line forwarder); one copy of the teardown code leaves the ELF, a few hundred bytes (INFERRED, not measured); future teardown fixes land once instead of twice.
- Effort: S. Risk: Med. Confidence: High (READ)

#### RIPTOPL-044: sysneutrino.cpp: file read, write-with-restore and two 8 KB static buffer pairs duplicated in the udpfs ip sync and SMB/NFS preflight

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/platform/sysneutrino.cpp:384-416`; `RiptOPL/src/platform/sysneutrino.cpp:445-465`; `RiptOPL/src/platform/sysneutrino.cpp:639-681`; `RiptOPL/src/platform/sysneutrino.cpp:732-749`; `RiptOPL/src/platform/sysneutrino.cpp:980-991`; `RiptOPL/src/platform/sysneutrino.cpp:1068-1079`
- Problem: sysSyncNeutrinoUdpfsToml and sysNeutrinoSmbPreflight each carry the same accumulate-read loop (403-409 vs 670-676) and the same O_TRUNC write plus restore-original-on-short-write block (446-465 vs 733-749), and each owns static toml[4096] plus updated[4116] / updated[4608]. Both run only on the GUI thread before teardown and never nest, so the second pair of buffers is never live at the same time as the first. sysWriteNeutrinoCheats (980-991) repeats the open/write/close/unlink-on-short-write that sysNeutrinoRAWrite (1068-1079) already implements.
- Fix: Move the two buffers to file scope once (toml[4096], updated[4608]) and use them from both functions; replace the duplicated read loop and write-restore block with two small static helpers (read whole file into toml, write updated or restore toml). Take sysNeutrinoRAWrite out of the RETROACHIEVEMENTS block and call it from sysWriteNeutrinoCheats.
- Justification: The helpers replace two copies each of the same 7-line read loop and 15-line write/restore block plus one 10-line write/unlink copy, so code shrinks, and sharing the buffers removes a whole duplicate 8 KB .bss pair.
- Benefit: 8,212 B .bss (INFERRED: 4096+4116+4096+4608 = 16,916 B now vs 4096+4608 = 8,704 B shared) and about 25 lines.
- Effort: S. Risk: Med. Confidence: High (READ)

#### RIPTOPL-045: InGameScreenshot spends roughly 8-15 s in decorative busy-waits and writes its text file one byte per RPC

- Category: 5 (Performance)
- Location(s): `RiptOPL/ee_core/src/igs_api.c:30-39`; `RiptOPL/ee_core/src/igs_api.c:61`; `RiptOPL/ee_core/src/igs_api.c:171`; `RiptOPL/ee_core/src/igs_api.c:303-304`; `RiptOPL/ee_core/src/igs_api.c:340-341`; `RiptOPL/ee_core/src/igs_api.c:356-357`; `RiptOPL/ee_core/src/igs_api.c:370-371`; `RiptOPL/ee_core/src/igs_api.c:379`; `RiptOPL/ee_core/src/igs_api.c:549-551`; `RiptOPL/ee_core/src/igs_api.c:556`; `RiptOPL/ee_core/src/igs_api.c:564`; `RiptOPL/ee_core/src/igs_api.c:673`; `RiptOPL/ee_core/src/igs_api.c:762`; `RiptOPL/ee_core/src/igs_api.c:787`
- Problem: On the IGR thread, between the game's shutdown and IGR_Exit, the screenshot code waits only to animate the background colour: FastDelay(1) (a 768-pass nop loop) after every pixel in the three ConvertColors loops, delay(1) (16.7M-pass nop loop, util.c:170-179) after each 128 KB DMA slice and six more times, and two success blinks (BlinkColour, util.c:184-198) worth 10 more delay units. SaveTextFile then sends its ~700-byte report with one fioPutc (one fio RPC) per byte.
- Fix: Delete FastDelay and its three calls with the per-pixel fade writes, the six standalone delay(1) calls and the success blinks at lines 61 and 787. Keep the per-slice delay(1) only if a hardware test shows the empirical slicing needs it. Replace the fioPutc loop with one fioWrite of the built string.
- Benefit: INFERRED: delay(1) = 16.7M passes x 4-7 cycles at 294.9 MHz = 0.23-0.40 s; 16 fixed units = 3.6-6.4 s, 8 slices at 640x448x32 = 1.8-3.2 s; FastDelay 768 x 4-7 cycles x 286720 px = 3.0-5.2 s. ~700 RPCs become 1. ~24 lines removed.
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-046: bdmLoadBlockDeviceModules sleeps up to 4.2 s on the IO worker at boot, ahead of every page list and of deferredInit (splash end)

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/sources/bdmsupport.cpp:1470-1485`; `RiptOPL/src/sources/bdmsupport.cpp:1415-1427`; `RiptOPL/src/sources/bdmsupport.cpp:1462-1468`; `RiptOPL/src/sources/bdmsupport.cpp:1549-1560`; `RiptOPL/src/app/opl.cpp:538-542`; `RiptOPL/src/app/opl.cpp:1751-1759`; `RiptOPL/src/app/opl.cpp:2116`; `RiptOPL/src/ui/guisplash.cpp:191-210`
- Problem: The first bdmInit queues bdmLoadBlockDeviceModules (1559) before each slot's IO_MENU_UPDATE_DEFFERED (opl.cpp 538-542), and deferredInit, which posts GUI_INIT_DONE, is queued after init() (opl.cpp 2116); the single FIFO IO worker runs them in order. The pass ends with one-shot DelayThread settles: 600 ms whenever USB is enabled (1465), 600 ms when MX4SIO is enabled but no card answers (1422), and a 12 x 250 ms poll when iLink is enabled but no iLink root appears (1480-1481). Every page list, and the splash fade (guisplash.cpp 191-210 waits on guiInitComplete), waits behind those sleeps.
- Fix: Delete the iLink poll loop at 1480-1483 and keep its single bdmForceDeviceRefresh, as the MX4SIO branch does: bdmevent is armed before any transport loads (1513-1526), so a late iLink mount already bumps BdmGeneration, which docs/ILINK-DISCOVERY-PARITY-2026-10-18.md already relies on after the deadline. For the USB and MX4SIO settles, either drop the DelayThread and keep the probe plus refresh, or queue the one-shot settle tail as its own IO request after deferredInit so GUI_INIT_DONE and the first lists are not behind it.
- Justification: Only the second USB/MX4SIO option adds anything (one function split out of bdmLoadBlockDeviceModules and one ioPutRequest). It removes a fixed 0.6-1.2 s from the boot critical path without changing the settle the comments at 1429-1461 defend; the iLink part is a pure deletion.
- Benefit: Up to 3.0 s (iLink on, no device) + 0.6 s (MX4SIO on, no card) + 0.6 s (USB on) less wait before the first lists and the splash fade (INFERRED: sum of the DelayThread arguments; overlap with other boot work not measured); ~6 lines removed for the iLink loop
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-047: delay() is a CPU-burning NOP spin (~0.25 s per tick) that starves lower-priority threads, and lwnbdctl's '~5 s' pad retry is really ~20 minutes

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/platform/util.cpp:941-950`; `RiptOPL/src/app/lwnbdctl.cpp:194-201`; `RiptOPL/src/app/lwnbdctl.cpp:228-237`; `RiptOPL/src/ui/guimodal.cpp:287`
- Problem: delay(n) spins 0x01000000 x 4 nops per tick; the repo's own notes put a tick at about 0.25 s (opl.cpp:1269-1272, autolaunch.cpp:204, settingsio.cpp:1419). It runs on the main thread at priority 31, so while it spins the IO worker (32), the art worker (72) and BGM I/O (31) cannot run; guiWarning spins delay(count) for 2 to 25 ticks. lwnbdctl retries startPads 100 times with delay(50) and its comment calls that ~5 s; at 0.25 s per tick it is about 12 s per retry and ~20 minutes in total, the very freeze #307 tried to bound.
- Fix: Make delay() sleep instead of spin: DelayThread(250000) per tick, which keeps the tick length every caller was tuned to. In lwnbdctl.cpp replace the two delay(50) retry waits with a 50 ms DelayThread so the 100-try bound is the ~5 s its comment states.
- Benefit: Frees the CPU for the IO, art and BGM threads during every guiWarning and boot/autolaunch wait; bounds the NBD pad retry at ~5 s instead of ~20 min (INFERRED tick length from repo comments).
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-048: IoQueue::blockOps polls the drain with delay(1), a ~0.25 s NOP spin, so drains finish late and every bound is in idle-CPU ticks (NBD bound ~4 min)

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/platform/ioman.cpp:374-386`; `RiptOPL/src/platform/util.cpp:941-949`; `RiptOPL/src/app/opl.cpp:1269-1277`; `RiptOPL/src/app/lwnbdctl.cpp:78`
- Problem: blockOps lowers the caller to priority 90 and waits with delay(1), a 0x01000000-iteration NOP spin (util.cpp:941-949; opl.cpp:1269-1271 says about a quarter second per tick). A drain that ends mid-spin is noticed only when the spin ends, so each launch, exit, NBD start and HDD rename/delete with work queued waits up to ~0.25 s more. The spin advances only while every higher-priority thread is blocked, so the timeout counts idle CPU, not time. lwnbdctl.cpp:78 still passes 1000 ticks (~4 min) as its bound; opl.cpp rescaled its own constants but NBD was missed.
- Fix: In IoQueue::blockOps replace delay(1) with DelayThread(10 * 1000) and count the budget in those 10 ms steps. Restate the budgets in that unit: EXIT_IO_DRAIN_TICKS 4 -> 100, LAUNCH_IO_DRAIN_TICKS 40 -> 1000, and LWNBD_IO_DRAIN_TICKS to the bound it was meant to be. menusys.cpp:248 passes 0 and is unaffected.
- Benefit: Drain completion seen within 10 ms instead of up to ~0.25 s per call (INFERRED: 16.7M-iteration spin, repo comment opl.cpp:1269-1271); the NBD bound drops from ~250 s (1000 x ~0.25 s, INFERRED) to a real wall-clock limit; no lines added.
- Effort: S. Risk: Med. Confidence: High (INFERRED)

#### RIPTOPL-049: NIBBLE2CHAR emits 'k'..'p' for hex digits 10-15, so the SOS/Disaster Report IREMSND patch gets a wrong module ID

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/ee_core/src/patches.c:160`; `RiptOPL/ee_core/src/patches.c:565-570`; `RiptOPL/modules/iopcore/patches/iremsndpatch/main.c:65-76`
- Problem: NIBBLE2CHAR(n) is 'a' + n for n > 9, giving 'k' for 10 instead of 'a'. SOS_SifLoadModuleHook formats the IREMSND module ID with it and iremsndpatch parses it with strtol(..., 16), which stops at the bad character: an ID such as 0x1A becomes 1, so the patch searches for the wrong module (ID 1 exists) or none.
- Fix: Change the macro to 'a' + (n) - 10.
- Benefit: Fixes the IREMSND patch for module IDs whose hex form contains a-f (6 of 16 low nibbles). No size change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-050: PS1 favourites resolve and star by name, but addItem/removeByIdAndText require the stored id, so un-favouriting fails once the PS1 list reorders

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/sources/favsupport.cpp:616-619`; `RiptOPL/src/sources/favsupport.cpp:1022-1029`; `RiptOPL/src/sources/favsupport.cpp:1065-1076`; `RiptOPL/src/sources/favsupport.cpp:1765-1776`; `RiptOPL/src/sources/favsupport.cpp:1736-1745`; `RiptOPL/src/sources/favsupport.cpp:594-596`; `RiptOPL/src/sources/favsupport.cpp:700-728`; `RiptOPL/src/ui/menuitems.cpp:459-465`; `RiptOPL/src/sources/vcdsupport.cpp:737-738`; `RiptOPL/src/sources/cuesupport.cpp:1105-1106`
- Problem: VCD/CUE favourites ignore the stored id when resolving (700-728), lighting device-page stars (594-596) and All Games stars (1740), but the add-time duplicate check (1025) and removeByIdAndText (1071) require recs[i].id == id unless mode is APP (616-619). PS1 lists are qsorted (vcdsupport.cpp:737-738, cuesupport.cpp:1105-1106), so adding/removing a PS1 game or toggling autosort shifts the index. R3 on the lit star (menuitems.cpp:459-465, toggleByIndex 1770) then matches nothing, rewrites the file unchanged, returns 1 ('Removed'), and the star returns on the next rebuild; the next R3 appends a duplicate record.
- Fix: Make favIdsMatchForMode kind-aware: ignore the id for FAV_KIND_VCD and FAV_KIND_CUE exactly as it already does for APP_MODE, and pass the kind from addItem and removeByIdAndText. favRecIsStarred can then call the same helper instead of its own favKindIsNameAddressed special case.
- Benefit: Fixes un-favouriting of PS1 games after any PS1 list change and stops duplicate PS1 records in favourites.bin; net ~0 lines (one extra parameter, one special case folded in).
- Effort: S. Risk: Med. Confidence: High (READ)

#### RIPTOPL-051: RA badge cache keeps 168 bytes per list row for the whole session although it is only read during the same list rebuild

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/src/sources/rabadge.cpp:173-199`; `RiptOPL/src/sources/rabadge.cpp:202-218`; `RiptOPL/include/rabadge.h:8-13`; `RiptOPL/src/app/opl.cpp:797-805`; `RiptOPL/src/ui/cfgtitle.cpp:147-158`
- Problem: refresh() mallocs count * RA_BADGE_TEXT (ISO_GAME_NAME_MAX 160 + 8 = 168 bytes) per device list and fills every row, mostly with empty strings, and keeps it until the next rebuild of that list. The only readers are raBadgeText at opl.cpp:800 (which immediately strdup's the text into the row) and cfgtitle.cpp:149 (uses it at once), both inside the same updateMenuFromGameList pass on the I/O thread, so the per-row strings are never needed afterwards and badged names exist twice.
- Fix: Store one byte per row in the slot (badged or not) instead of the formatted string, and let text() format prefix/suffix plus the row name into one static RA_BADGE_TEXT buffer on demand; both callers already copy or consume the result before the next call.
- Benefit: INFERRED: 168 B/row -> 1 B/row of EE heap held per list, e.g. a 500-row USB list 84,000 -> 500 bytes, 1,000 rows ~167 KB -> 1 KB; RA builds with telemetry and badges on only. Source size about equal (the snprintf moves into text()).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-052: cacheLoadImage retries every non-ERR_BAD_FILE failure, so an undecodable art file is re-read continuously while it is on screen

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/gfx/artload.cpp:743-751`; `RiptOPL/src/gfx/texcache.cpp:372-378`; `RiptOPL/src/gfx/textures.cpp:723-726`; `RiptOPL/src/gfx/textures.cpp:1035-1042`; `RiptOPL/src/gfx/textures.cpp:1328-1334`
- Problem: The transient branch fires for any result other than ERR_BAD_FILE, including deterministic format failures: ERR_SET_JMP (truncated/corrupt PNG, or any PNG over the 1024x1024 user limit), ERR_BAD_DEPTH (16-bit PNG), ERR_BAD_DIMENSION, and ERR_FILE_IO for a zero-byte file. The slot is cleared, the row re-requests on its next draw, and the 'settle gate' the comment relies on only exists for SIO2, so on USB/HDD/SMB the art worker re-opens and re-reads that file back to back (twice per try, since texLoadArtFile retries a bad .png as a JPG) for as long as the row or selected BG is shown, keeping cacheHasPendingArt() true.
- Fix: Keep the retry lane only for real I/O and allocation failures: retry when the result is ERR_FILE_IO, ERR_READ_STRUCT or ERR_INFO_STRUCT, and park every other negative result exactly like ERR_BAD_FILE (it is re-asked after the next fail-epoch bump). Make the zero-byte case at textures.cpp:723-726 return a format error rather than ERR_FILE_IO so it parks too.
- Benefit: Stops an unbounded read loop (one full file read, often two, per art-worker cycle) for every broken art file on screen; no lines added, one condition changed (INFERRED from the code path; no hardware count).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-053: cheat_entry_t.codes[] is sized in u32 words (510) but holds pairs: the 1.05 MB cheat table is twice what any consumer can use

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/include/cheatman.h:39-41`; `RiptOPL/include/cheatman.h:64-69`; `RiptOPL/src/core/cheatman.cpp:373-374`; `RiptOPL/src/core/cheatman.cpp:454-465`; `RiptOPL/src/core/cheatman.cpp:522-525`; `RiptOPL/src/core/cheatman.cpp:570`; `RiptOPL/src/core/cheatman.cpp:623`; `RiptOPL/src/core/cheatman.cpp:660`
- Problem: MAX_CHEATLIST (510) is the word count of gCheatList (5 hook pairs + 250 code pairs), but cheat_entry_t uses it as the number of code_t PAIRS per cheat. No reader can use more than 255 pairs of one cheat: set_cheats_list caps the whole list at 510 words, igrRowFits needs n <= 250, set_cheats_list_live takes at most 5 hooks plus 250 codes. Half of every 4216-byte entry is unreachable.
- Fix: Declare codes[MAX_CHEATLIST / 2] and bound the parser store (cheatman.cpp:373) and the four codes[] walks (524, 570, 623, 660) by that array size instead of MAX_CHEATLIST. Update the ~1.03 MB comments.
- Benefit: sizeof(cheat_entry_t) 132+4080+4 = 4216 -> 132+2040+4 = 2176; x250 = 1,054,000 -> 544,000 bytes, 510,000 bytes less heap per cheats-on launch and a smaller single malloc that is less likely to fail (INFERRED arithmetic).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-054: diaShowKeyb writes one byte past its pre[maxLen] VLA when strlen(text) == maxLen; File Manager rename passes FM_NAME - 1 with names up to 127 bytes

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/ui/diaeditors.cpp:104-109`; `RiptOPL/src/ui/diaeditors.cpp:215-238`; `RiptOPL/src/ui/guifilemgr.cpp:518-523`; `RiptOPL/src/ui/guifilemgr.cpp:535-539`; `RiptOPL/src/core/filemgr.cpp:208-210`
- Problem: diaKeybTextWidth copies text[from..to) into scratch and writes scratch[to - from] = 0; diaShowKeyb passes the stack VLA pre[maxLen] with from=view(0) and to=n=len every frame, so len == maxLen writes pre[maxLen]. fmRenameEntry seeds name from a list entry (fmList keeps names up to 127 bytes, filemgr.cpp:208-210) and passes maxLen FM_NAME - 1 = 127, so renaming a file whose name is 127 bytes long overwrites one stack byte per frame while the keyboard is open. Every other caller passes the buffer size.
- Fix: Pass sizeof(name) (FM_NAME) instead of FM_NAME - 1 at guifilemgr.cpp:523 and :539, matching every other diaShowKeyb caller and fmNameOk's 1..FM_NAME-1 limit (filemgr.cpp:151).
- Benefit: Closes a reachable one-byte stack overwrite in the main thread (consequence depends on what lies above the VLA in the frame: INFERRED); no size change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-055: lwnbdctl.cpp: NBD start/stop 'bounded' waits treat delay() ticks as milliseconds (IO drain ~4 min, pad retries up to ~20 min)

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/app/lwnbdctl.cpp:77-78`; `RiptOPL/src/app/lwnbdctl.cpp:150-154`; `RiptOPL/src/app/lwnbdctl.cpp:195-203`; `RiptOPL/src/app/lwnbdctl.cpp:228-239`; `RiptOPL/src/platform/ioman.cpp:380-386`; `RiptOPL/src/platform/util.cpp:941-950`
- Problem: delay() spins 0x01000000 x 4 nops per tick, ~0.25 s (util.cpp:941-950; the repo records this at opl.cpp:1269-1277). ioBlockOpsTimed spends one delay(1) per tick (ioman.cpp:380-386), so LWNBD_IO_DRAIN_TICKS = 1000, meant to fail a stuck request quickly, spins the GUI thread for ~250 s behind the frozen 'Starting NBD' screen. Both pad-restart loops wait delay(50) (~12.5 s) per failed startPads(), up to 100 times (~20 min), where the comment at 228-231 promises ~5 s. LWNBD_ART_DRAIN_TICKS = 500 next to it is milliseconds (artload.cpp:426-449), so the two *_TICKS constants differ in unit by ~250x.
- Fix: Set LWNBD_IO_DRAIN_TICKS to about 4 (~1 s, the same value opl.cpp:1276 already uses for EXIT_IO_DRAIN_TICKS) and note the unit. In both pad loops replace delay(50) with DelayThread(50000) so 100 tries are the documented ~5 s (move the delaythread.h include out of the __OPLDIAG block), or keep delay() and use delay(1) with 20 tries.
- Benefit: On NBD start/stop failure paths the screen freezes ~1 s / ~5 s as documented instead of ~250 s / up to ~20 min. Timing INFERRED: 16.8M loop iterations per tick at 294.9 MHz is ~0.25 s, the figure opl.cpp:1269-1272 and artload.cpp:426-427 state.
- Effort: S. Risk: Med. Confidence: High (INFERRED)

#### RIPTOPL-056: vmc_groups: 1046 per-title pointers cost 4184 B of .data; fixed-width char arrays make them unnecessary

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/src/core/vmc_groups.cpp:5-10`; `RiptOPL/src/core/vmc_groups.cpp:14`; `RiptOPL/src/core/vmc_groups.cpp:1346`
- Problem: Each of the 97 groups is a static const char *titles_*[] array (first at line 14, last at 1346), so every title costs a 4-byte pointer in .data on top of its 12-byte string: vmc_groups.o has 4184 B of .data = 1046 x 4. Every ID is 11 characters (once SSCES_540.95 at 614 is fixed, finding 6), so the pointers carry no information.
- Fix: Declare each group as static const char titles_*[][12] with the same initialiser lists and make MemoryCardGroup.titleIds a const char (*)[12]; the count expressions, the strcmp lookup and the host test keep working. A mechanical edit of 97 declarations and one struct field.
- Benefit: ~4 KB of ELF and RAM (MEASURED 4184 B .data in riptopl-per-object.tsv; strings move from .rodata.str into the arrays at the same 12 B, minus ~144 B of string merging lost for 12 repeated IDs, INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-057: Glyph cache pages of 256 entries (9 KB each) make CJK text cost ~0.7 MB of mostly empty cache per font

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/include/fontsystem.h:22`; `RiptOPL/include/fontsystem.h:43-59`; `RiptOPL/src/gfx/fntsys.cpp:187-221`; `RiptOPL/src/gfx/fntsys.cpp:593-602`
- Problem: The cache is paged by codepoint/256, and each page is malloc'd whole: 256 x 36-byte entries = 9,216 B. CJK ideographs are spread over U+4E00-9FFF, so a few hundred distinct characters touch dozens of pages, each almost empty. Pages are freed only when the font is flushed. Counting the codepoints in lng/fork/Japanese, SChinese and TChinese.yml gives 74, 76 and 81 distinct pages: 666-729 KB per font once those strings have been drawn, for 578-746 used entries.
- Fix: Lower GLYPH_CACHE_PAGE_SIZE to 32. The code already uses only the constant (/ and %). Optionally also narrow the entry's int fields to short.
- Benefit: INFERRED from the lang files: at 32 entries a page is 1,152 B, so 239-340 pages = 269-382 KB instead of 666-729 KB per CJK font (~350-400 KB saved per font in the worst case). Latin fonts save ~10-18 KB each. The pointer array grows to at most 8 KB.
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-058: NIBBLE2CHAR emits 'k'..'p' for 10..15, so the SOS/Disaster Report hook can patch the wrong IOP module

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/ee_core/src/patches.c:160`; `RiptOPL/ee_core/src/patches.c:565-570`; `RiptOPL/modules/iopcore/patches/iremsndpatch/main.c:65-76`; `RiptOPL/modules/iopcore/patches/iremsndpatch/main.c:87-108`
- Problem: NIBBLE2CHAR returns 'a' + n for n >= 10, i.e. 'k'..'p' instead of 'a'..'f'. SOS_SifLoadModuleHook uses it to pass IREMSND's IOP module ID as two hex digits (patches.c:565-570), and iremsndpatch parses the argument with strtol(argv[1], NULL, 16) (main.c:65). For an ID like 0x2A the argument is "2k", strtol returns 2, and iremsndpatch writes its JAL/JMP words at fixed offsets into module 2's text (main.c:87-108). With a high nibble of A-F the parse gives 0 and the fix is silently skipped.
- Fix: Change the second branch of NIBBLE2CHAR to 'a' + (n) - 10.
- Benefit: Fixes IOP code corruption (crash or hang) in Disaster Report and Zettai Zetsumei Toshi whenever IREMSND's module ID has a low nibble of A-F; one character, no size change (INFERRED)
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-059: fmCopy / fmCopyThenDelete clean up an FM_ERR_EXISTS failure by deleting the file or folder that was already at the target

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/core/filemgr.cpp:453-466`; `RiptOPL/src/core/filemgr.cpp:564-573`; `RiptOPL/src/core/filemgr.cpp:332-338`; `RiptOPL/src/core/filemgr.cpp:400-401`; `RiptOPL/src/core/filemgr.cpp:156-172`; `RiptOPL/src/ui/guifilemgr.cpp:454-458`
- Problem: A failed copy is undone with fmDeleteTree(dst) whatever the error. FM_ERR_EXISTS means the target's create was refused because something is already there (O_EXCL open 332-338, mkdir 400-401), i.e. fmTarget's fmExists (156-172, one try) wrongly said 'not there'; the cleanup then removes that pre-existing file or folder tree, the case O_EXCL was added to stop. The file's own header (11-12) says the first card access after a card is detected answers 'changed card', and for a file the opendir fallback cannot succeed. Trigger: copy a file, swap the memory card in that slot (listing not refreshed), paste where the new card has the same name: open fails once, O_EXCL fails, fmExists now says yes, remove(dst) deletes the card's file.
- Fix: In fmCopy and fmCopyThenDelete, skip the fmDeleteTree(dst) cleanup when fmErr is FM_ERR_EXISTS; keep it for every other error.
- Justification: One condition in two places. It is the smallest change that stops a refused create from deleting the user's existing file or save folder; an FM_ERR_EXISTS can only arise where this call made nothing at that path (a nested one needs two source names the target treats as one, where keeping the partial copy is the safe side).
- Benefit: Closes a memory-card data-loss path (an existing save file or folder deleted on paste). No size change.
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-060: Every config entry is a fixed 308-byte node (48-byte key + 256-byte value) regardless of content

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/include/configset.h:13-20`; `RiptOPL/include/config.h:268-269`; `RiptOPL/src/core/config.cpp:229-241`
- Problem: allocConfigItem mallocs sizeof(config_value_t) = 48 + 256 + 4 = 308 bytes (plus malloc header) for every key, although typical keys are under 30 bytes and most values are a few digits. The five global sets (settings, last, apps, network, game) stay loaded for the whole menu session; include/config.h defines 137 CONFIG_OPL_* and 47 CONFIG_ITEM_* key names, so the resident sets hold on the order of 150-250 nodes.
- Fix: Allocate each node with its key and value at their real lengths (one malloc: header + key + NUL + value + NUL) and reallocate the node only when setStr stores a longer value. Callers that keep a getStr() pointer across a setStr of the same key must be checked first, since such a pointer would then dangle.
- Benefit: INFERRED: ~200 resident nodes x ~280 unused bytes = ~55 KB EE heap returned; per-game sets loaded at launch shrink the same way. Not measured.
- Effort: M. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-061: HDD exact archive and generic sweep share one TAR_KIND_ART slot: mixed Favourites/Apps lists reload the whole index per cover

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/platform/tar.cpp:389-405`; `RiptOPL/src/platform/tar.cpp:407-413`; `RiptOPL/src/platform/tar.cpp:467-477`; `RiptOPL/src/platform/tar.cpp:504-514`; `RiptOPL/src/gfx/artload.cpp:122-137`; `RiptOPL/src/gfx/texcache.cpp:187-207`
- Problem: cacheResolveArtArchive picks the HDD archive per row (mode 1) for HDD-origin Favourites/Apps rows and the generic sweep (mode 0) for others. artTarLoadImageExact calls tarLoadFile, which drops a generic index. artTarLoadImage calls tarFindPrefix, which drops any exact index (s_isExact) and runs tarLoadFromAnyDevice from gDevices[0]. So in a list that alternates HDD and USB/SMB/MMCE games, each change of source frees the index, opens device archives again and re-reads the whole sidecar (64 bytes per member) or rescans the tar, all on the art worker.
- Fix: Give the HDD exact archive its own kind slot (one more TarKind with ART's entry size and cache name, used only by artTarLoadImageExact) so both indexes stay resident. Then delete the s_isExact close-and-reload branches in tarLoadFromAnyDevice, tarFind and tarFindPrefix.
- Justification: The extra slot is the cause-level fix: the reloads exist only because two archives share one slot. Cost is the second index's RAM (64 bytes per member, held only when an HDD archive exists). It removes about as many lines of s_isExact handling as it adds.
- Benefit: Removes a full index reload plus up to 12 device open() probes per HDD/non-HDD alternation (INFERRED; a 3000-member pack is a 192 KB sidecar read; artload.cpp:44 notes a missed open can cost ~4.3 s)
- Effort: M. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-062: readFileBuffer hands lang.cpp a pointer into its own buffer for an empty line; the buffer is freed and the pointer is later passed to free()

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/platform/util.cpp:614-615`; `RiptOPL/src/platform/util.cpp:631-634`; `RiptOPL/src/core/lang.cpp:163-188`; `RiptOPL/src/core/lang.cpp:42-53`
- Problem: With allocResult set, readFileBuffer only mallocs a copy when lineSize is non-zero; an empty line (or a lone CR) returns 1 with *outBuf = lastPtr, an address inside fileBuffer->buffer. lngLoadFromFile (the only allocResult=1 caller) stores it in newL[], calls closeFileBuffer (frees the buffer), then reads it in lngFormatFits and frees it at lang.cpp:183 or in lngFreeFromFile (language switch, and lngEnd in deinitEx before every launch). One blank line in a lang_*.lng is a use-after-free plus an invalid free of an interior pointer: newlib heap corruption. The copy's malloc is also unchecked (memcpy to NULL on OOM).
- Fix: In readFileBuffer drop the lineSize test so every returned line is a heap copy when allocResult is set, and return 0 if that malloc fails.
- Justification: The added NULL check stops a NULL memcpy on allocation failure; dropping the lineSize test removes code. Together they close a heap corruption reachable from a user-supplied language file.
- Benefit: Fixes heap corruption (invalid free at language switch or game-launch teardown) triggered by any blank line in a .lng file; net about +1 line.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-063: findDuplicate shares the first COV cache whatever slot count the later element needs: synthesized Coverflow/Grid/PSBBN views can get a 2-slot cache

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/gfx/themeelems.cpp:633-649`; `RiptOPL/src/gfx/themeelems.cpp:837-855`; `RiptOPL/src/gfx/themeelems.cpp:871-891`; `RiptOPL/src/gfx/themes.cpp:700-702`; `RiptOPL/src/gfx/themes.cpp:1480-1491`; `RiptOPL/src/gfx/themeviews.cpp:513-528`; `RiptOPL/src/gfx/coverflow.cpp:354-357`; `RiptOPL/src/gfx/grid.cpp:663-671`; `RiptOPL/src/gfx/texcache.cpp:396-407`
- Problem: initMutableImage links any earlier element's cache with the same suffix (findDuplicate) and only creates its own cache when none was linked, so the FIRST COV element's slot count wins. A GameImage with pattern=COV gets count 1 floored to 2. For a disk theme without views=, thmViewSynthesize appends the template presenter after the theme's last main block, so a Coverflow (wants 20), PSBBN flow or Grid (wants 2 x cells, up to 48) built after such a GameImage shares its 2-slot cache. The eviction scan never takes an entry used this frame, so only 2 covers of the carousel/grid ever load; the rest stay on the default picture.
- Fix: In findDuplicate, share a cache only when its count() is at least the slot count the new element asked for (pass cacheCount in from initMutableImage); otherwise let the element create its own cache as it already does when nothing matches.
- Justification: Adds one parameter and one condition. The cache has no resize, so refusing an undersized share is the smallest change that gives presenters the slots their own code computes (grid.cpp:663-671, coverflow COVER_CACHE_SLOTS); the cost is at most a duplicate decode of the selected cover in the small GameImage cache.
- Benefit: Fixes permanently missing covers (all but 2) in synthesized Coverflow/Grid/PSBBN views of disk themes that declare a GameImage pattern=COV before the presenter. Theme population affected is INFERRED.
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-064: vmcsaves: names read from a VMC image are not validated before vsWriteFolder joins them into a path, so an export can write outside SAVES/

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/core/vmcsaves.cpp:453`; `RiptOPL/src/core/vmcsaves.cpp:469`; `RiptOPL/src/core/vmcsaves.cpp:727`; `RiptOPL/src/core/vmcsaves.cpp:739-740`; `RiptOPL/src/core/vmcsaves.cpp:867-868`
- Problem: vsLoadVmc copies the save name (453) and every file name (469) straight from the card's directory entries, and vsWriteFolder joins them into parent + save + '/' + file (727, 739) and opens that with O_CREAT|O_TRUNC (740). vsLoadPsu checks the same names with vsNameOk (796, 822) and vsWritePsu checks the save name (867), but the VMC-to-folder export (vmcSavesExport, guigame.cpp:442) checks nothing. A crafted VMC whose save holds a file named like '../../../X' makes the export create or truncate X outside the save folder on a device whose path layer resolves '..'.
- Fix: Check the names where they enter, as vsLoadPsu does: in vsLoadVmc fail with VMCSAVES_ERR_NAME when vsNameOk(s->name) or vsNameOk(f->name) is false. The save-name check in vsWritePsu (867-868) then covers every source (folder and PSU sources are already checked) and can be deleted.
- Justification: Adds two checks with an existing helper: they close a path traversal from untrusted VMC images (shared save cards) into the rest of the drive, and the now-redundant check in vsWritePsu goes, so the net change is about one line.
- Benefit: Closes an overwrite-outside-the-folder path on VMC export (HDD/SMB/NFS); net about +1 line (INFERRED: +3 in vsLoadVmc, -2 in vsWritePsu).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-065: Xerabora cache keeps a fixed 1024-entry achievement_entry_t array (232 KB) for the rest of the session

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/src/sources/achievements.cpp:144-146`; `RiptOPL/src/sources/achievements.cpp:356`; `RiptOPL/src/sources/achievements.cpp:383-385`; `RiptOPL/include/achievements.h:17-21`
- Problem: achievementsParseXerabora mallocs XERA_LIST_MAX (1024) entries of achievement_entry_t for every parse and keeps the block as xeraList; nothing frees it (achievementsForget only marks it stale). A game set is typically tens of entries and a library a few hundred, so most of the 237,568-byte block is held unused for the whole menu session, and each refetch briefly holds the old list, the new one and the 512 KB document together.
- Fix: After the parse loop, shrink the list to count entries with realloc (keep the original block if realloc fails or count is 0).
- Justification: Two added lines return roughly 200 KB of EE heap that is otherwise pinned for the session after the browser is first opened in Xerabora mode; no new state or option.
- Benefit: INFERRED: sizeof(achievement_entry_t) = 5 ints (20) + 33+57+101+20 chars = 231, padded 232; 1024 x 232 = 237,568 B held; a 100-entry set needs 23,200 B, so ~210 KB freed (RA editions only).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-066: ArtIndex::build allocates 4096-entry hash and picture tables plus a 16 KB name buffer per directory and never trims them

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/src/gfx/artindex.cpp:191-201`; `RiptOPL/src/gfx/artindex.cpp:275-286`; `RiptOPL/src/gfx/artindex.cpp:421-435`; `RiptOPL/include/artindexint.h:29-38`
- Problem: Every indexed directory gets hashes = 4096 x 4 B = 16 KB and pics = 4096 x 8 B = 32 KB at full kMaxEntries size, and names starts at 16 KB; the slot keeps these exact sizes until the round-robin rebuilds that slot (invalidate() only marks slots unusable). A 200-entry POPS or ART folder needs about 7 KB but holds 64 KB; a 2000-picture ART folder needs ~72 KB but holds ~112 KB. With three slots that is roughly 120-170 KB of EE heap held for nothing.
- Fix: After the sort and before publishing (lines 275-286), shrink each buffer to what the sweep used: realloc hashes to n entries, pics to picCount entries and names to namesUsed bytes, keeping the original pointer when realloc returns NULL or the count is zero.
- Justification: Adds three or four realloc lines; the concrete problem is 40-57 KB of heap per held directory slot that no code ever reads, and shrinking in place is the smallest change that returns it.
- Benefit: ~40-57 KB heap per valid slot, ~120-170 KB with three slots (INFERRED: 16 KB + 32 KB fixed tables and 16 KB name floor vs 12 B per entry + name bytes actually used).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-067: Undecodable art files are retried back-to-back forever: decode failures are reported as transient to the art cache

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/gfx/textures.cpp:1023-1031`; `RiptOPL/src/gfx/textures.cpp:1035-1042`; `RiptOPL/src/gfx/textures.cpp:1108-1111`; `RiptOPL/src/gfx/textures.cpp:477-483`; `RiptOPL/src/gfx/textures.cpp:1328-1334`; `RiptOPL/src/gfx/textures.cpp:1397-1409`; `RiptOPL/src/gfx/artload.cpp:746-751`; `RiptOPL/src/gfx/texcache.cpp:372-378`
- Problem: A present art file that cannot be decoded returns ERR_SET_JMP (corrupt PNG, PNG over 1024 px via png_set_user_limits, neither PNG nor JPG), ERR_BAD_DIMENSION (e.g. an 800x600 RGBA PNG over maxSize) or ERR_BAD_DEPTH (16-bit PNG). texDiscoverArt passes these up unchanged and artload.cpp:746 treats every result other than ERR_BAD_FILE as transient: it clears the slot, and texcache re-claims it on the next frame (only SIO2 devices have an idle gate). So the art worker reloads the same file in a loop for as long as the game is on screen, each cycle a full open and read (two of each for a .png name, finding 2).
- Fix: Treat decode failures of a file that was read completely as absent: in texDiscoverArt return ERR_BAD_FILE for ERR_SET_JMP, ERR_BAD_DIMENSION and ERR_BAD_DEPTH (or let artload.cpp:746 retry only ERR_FILE_IO and the libpng-struct OOM codes). So a mid-read network error on the streaming reader stays transient, have texReadFileFunction mark its reader when read() fails and make the setjmp handler return ERR_FILE_IO in that case.
- Justification: Adds a flag in tex_file_reader_t and one mapping (about 4 lines). It fixes a real endless reload loop that occupies the art worker and the device bus (shared with BGM on USB, with the pad on MMCE/MX4SIO). It is the smallest change that keeps the issue #388 rule that only real I/O failures retry.
- Benefit: Ends an indefinite reload loop per broken art file on screen. INFERRED: a 2 MB 1280x720 PNG background read twice per cycle on USB at about 1 MB/s is about 4 s of device time per cycle, repeated while selected, delaying every other cover behind it.
- Effort: S. Risk: Med. Confidence: Med (READ)

#### RIPTOPL-068: tarInvalidate(TAR_KIND_ART) frees the art index from the GUI/IO thread while the art worker is using it

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/platform/tar.cpp:609-613`; `RiptOPL/src/platform/tar.cpp:92-103`; `RiptOPL/src/platform/tar.cpp:304-317`; `RiptOPL/src/platform/tar.cpp:367-370`; `RiptOPL/src/platform/tar.cpp:437-443`; `RiptOPL/src/platform/tar.cpp:591-604`; `RiptOPL/src/app/settingsio.cpp:2479-2483`; `RiptOPL/src/ui/guisettings.cpp:1801-1804`; `RiptOPL/src/gfx/artload.cpp:98-136`
- Problem: The ART index (s_index/s_count/s_cap/s_dev) has no lock. All lookups, parses and reads run on the art worker (priority 72, artload.cpp:659-666), but applyConfig(...,0) from many settings pages and the Artwork toggle call tarInvalidate on the GUI thread (31), which preempts the worker at every wake. If it lands while the worker is blocked in a header read inside tarParseFile, the partial index is freed, parsing continues from mid-archive, and tarWriteCache stores that truncated index with the full tar size, so every later boot accepts it. s_dev is also cleared, so every tarRead fails for the session. If it lands inside artTarDecode, tarRead reads entry->offset and rawSize from freed memory.
- Fix: Do not free from the foreign thread: tarInvalidate only sets a per-kind volatile flag; tarFind, tarFindPrefix, tarLoadFile and tarLoadFromAnyDevice (all called on the art worker) check it at entry, clear it, call tarCloseInternal and clear s_inactive. Nothing else changes.
- Justification: Adds one flag and a 3-line check. It removes a real use-after-free and a corrupt sidecar that persists across boots. A semaphore like the THM one would make the GUI thread wait out multi-second device reads, so the deferred flag is the smallest safe change.
- Benefit: Removes a use-after-free and a truncated art_cache.bin that hides covers until the archive changes size (paths READ; the race timing is INFERRED; needs gEnableArtTar on)
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-069: Hold-L3 view switch runs a full thmLoad (cfg, font files, up to 47 PNG probes, two art drains) synchronously on the GUI thread

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/gfx/themes.cpp:2186-2208`; `RiptOPL/src/ui/menusys.cpp:2773-2776`; `RiptOPL/src/gfx/themes.cpp:1412-1419`; `RiptOPL/src/gfx/themes.cpp:1573-1576`; `RiptOPL/src/gfx/themes.cpp:1699-1750`; `RiptOPL/src/gfx/themes.cpp:1817-1825`
- Problem: menuHandleInputMain (GUI thread, screen input dispatch) calls thmCycleView on a 500 ms L3 hold; it reloads the whole theme through setGuiValueLocked -> thmLoad between frames, so no frame is drawn until it returns. For a disk theme that re-reads conf_theme.cfg, every font file (thmLoadFonts, whole TTF reads) and probes/decodes up to 47 icon PNGs from the theme device (SMB, USB, MMCE), plus up to 3 x 500 ms art drains (DelayThread(1000) loops, artload.cpp:442-452); the built-in theme decodes about 100 embedded PNGs. Only the element layout differs between views.
- Fix: Run the hold-L3 switch through guiHandleDeferedIO(..., IO_CUSTOM_SIMPLEACTION, worker, 0) like the other blocking actions (menusys.cpp:983, guisettings.cpp:953), so the IO worker does the load while the GUI keeps drawing a busy message; thmLoad already publishes under the GUI lock for IO-thread loads (themes.cpp:1828-1844).
- Justification: Adds a two-line void wrapper and one call. Problem: a frozen frame for the whole reload on every hold-L3 (seconds for a disk theme on a network share, INFERRED). Reusing the existing deferred-IO wait is the smallest change that keeps the UI responsive without restructuring thmLoad.
- Benefit: Removes a frozen frame on each view switch: INFERRED hundreds of ms (built-in theme, ~100 PNG decodes) to seconds (disk theme over SMB: cfg + fonts + up to 47 file probes); not measured on hardware
- Effort: M. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-070: hiddenLists reserves 512 name slots for each of 15 modes: 40,920 bytes of .bss for lists that are usually empty

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/src/sources/gamemanage.cpp:33-41`; `RiptOPL/src/sources/gamemanage.cpp:70-88`; `RiptOPL/include/gamemanage.h:9-10`
- Problem: hidden_list_t embeds names[HIDDEN_MAX] pointers and ps1[HIDDEN_MAX] flags (gamemanage.cpp:33-39) and hiddenLists has one per MODE_COUNT (15) mode (41): MEASURED 40,920 bytes of .bss. APP, FAV and HTTP can never hold a list (hiddenPrefix 60-62), and a device's hidden list is normally a handful of names.
- Fix: Replace the two fixed arrays with one heap array of {name, ps1} entries per list, grown with realloc in hiddenAppend and freed in hiddenClear; keep HIDDEN_MAX as the cap and keep prefix/loaded/count static.
- Justification: About 38 KB of EE RAM is reserved for the whole run by a feature that is usually unused; a growable array is about 6 extra lines.
- Benefit: MEASURED 40,920 B .bss (BUILD-EVIDENCE.md:113) -> about 2.6 KB (15 x 176 B) plus heap per hidden game; ~38 KB RAM saved.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-071: Custom Settings Path is ignored at boot whenever the boot folder still holds a settings file

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/app/settingsio.cpp:1557-1563`; `RiptOPL/src/app/settingsio.cpp:962-984`; `RiptOPL/src/app/settingsio.cpp:2397-2402`; `RiptOPL/src/app/opl.cpp:1885-1895`
- Problem: config.path is read only inside tryAlternateDevice, and _loadConfig and miniInit call that only when the boot home has no master file. Choosing a Custom Settings Path writes the settings only to the new folder and leaves the old settings_riptopl.cfg beside the ELF as it was. The next boot loads that stale file (custom path empty) and never reads config.path, so every change saved since appears lost, and the next save goes back to the boot folder.
- Fix: Check the redirect before trusting the boot home: move the readConfigPathRedirect / prepareCustomSettingsPath block out of tryAlternateDevice into the first read in _loadConfig and miniInit, and fall back to the boot home when the redirect target has no master.
- Justification: The setting is documented as a folder to read and save settings in instead of the default, but it fails for every user who saved settings before choosing a path. Moving the existing check costs one small open() of config.path per boot and adds no new mechanism.
- Benefit: Fixes a settings-revert bug for Custom Settings Path users who already had settings in the boot folder; costs one extra small file open per boot (INFERRED)
- Effort: M. Risk: High. Confidence: Med (INFERRED)

#### RIPTOPL-072: R3 handlers keep pointers into gFavSupport's array and the menu row across blocking card I/O while the higher-priority IO worker can free both

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/sources/favsupport.cpp:1765-1776`; `RiptOPL/src/sources/favsupport.cpp:1079-1099`; `RiptOPL/src/sources/favsupport.cpp:1491-1507`; `RiptOPL/src/sources/favsupport.cpp:1181-1186`; `RiptOPL/src/sources/favsupport.cpp:789-800`; `RiptOPL/src/ui/menuitems.cpp:416-424`; `RiptOPL/src/ui/menuitems.cpp:92-95`; `RiptOPL/src/ui/gui.cpp:368-374`; `RiptOPL/src/app/opl.cpp:712`; `RiptOPL/src/app/opl.cpp:908-909`
- Problem: itemExecFav runs on the GUI thread (prio 31) after endFrame drops the GUI lock (gui.cpp:368-374). toggleByIndex writes r->starred through r = &array_[i] after addItem/removeByIdAndText card I/O (1769-1774); removeByIndex reuses the array text after I/O (1086-1095); recentRecord copies fav->text (menuitems.cpp:93-95) only after favReadFile (1492-1507); itemExecFav writes it->favourited after I/O (menuitems.cpp:418-423). While the GUI waits on the card RPC, the prio-30 IO worker can run a queued FAV rebuild: clearMenuGameList frees the rows (opl.cpp:712) and update() calls clear(), freeing array_ and texts (1183, 789-800). Every other list rebuild queues one (opl.cpp:908-909).
- Fix: In toggleByIndex and removeByIndex copy mode, id, kind, starred and the text into locals before the I/O, and afterwards touch array_ only if validIndex(favIndex) still holds and that entry's text equals the copy. In itemExecFav re-read curMenu->current after the call instead of writing through the old it, and in itemRecordRecent pass a local copy of fav->text.
- Justification: Closes a write to freed heap reachable from a button press (for example R3 on All Games while the apps scan it arms at 1630-1632, a boot device scan or a hotplug list lands). Local copies and one re-check (~8 lines) are the smallest fix; no new lock is needed.
- Benefit: Removes a use-after-free write that can corrupt the EE heap or star the wrong row; about +8 lines (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-073: bdmUpdateDeviceData fast reappearance path republishes the old list for whatever volume now holds the slot

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/sources/bdmdevices.cpp:286-309`; `RiptOPL/src/sources/bdmdevices.cpp:437-449`; `RiptOPL/src/sources/bdmdevices.cpp:373-390`; `RiptOPL/src/sources/supportbase.cpp:707-714`; `RiptOPL/src/sources/bdmsupport.cpp:1814-1818`; `RiptOPL/src/sources/bdmsupport.cpp:1725-1734`
- Problem: A confirmed disconnect (bdmdevices 437-449) hides the page but keeps root/driver/type, and the rescan it triggers keeps the old list because sbReadList retains it when the device is gone (supportbase 707-714). Any later mount in that slot is treated as 'reappeared intact' (286-309): page shown, return 0, so needsUpdate stops at 1817 before the CD/DVD mtime and ul.cfg checks. Swap stick A for stick B and B's tab shows A's games and art until a manual refresh. The path also skips the publish gates at 373-390: a page of a disabled transport is re-shown on every generation bump and hidden again on the next poll (bdmsupport 1725-1734), and BDM=Off is not honoured.
- Fix: In the fast path, return without showing the page when bdmTransportEnabled(type)==0 or the explicit BDM-Off gate applies. Then let BdmSource::needsUpdate still run its cheap CD/DVD mtime and ul.cfg size comparison for a restored page (for example a distinct return value that skips the connect sound and folder creation). A stall on the same volume costs two stats, and a different volume gets a normal rebuild.
- Justification: Adds about 8 lines. It fixes a wrong game list (and launches of titles that are not on the inserted device) without bringing back the full rescan on every transient stall, which is what the fast path was added to avoid. Reusing the existing mtime/size checks is the smallest change that can tell two volumes apart.
- Benefit: Correct list after a device swap; no tab flash or FAV rebuild per hotplug for a disabled transport (INFERRED from the traced call chain).
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-074: Four virtual host:: service interfaces have one implementation each, and most members are pure forwarders

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/include/host.h:26-131`; `RiptOPL/src/app/host.cpp:23-109`; `RiptOPL/src/app/host.cpp:279-293`; `RiptOPL/.github/scripts/hostmap.py:1-55`; `RiptOPL/tests/host/host_env.h:34-64`
- Problem: UiServices, AppServices, LibraryServices and LaunchServices are abstract classes with exactly one product implementation each (OplUi, OplApp, OplLibrary, OplLaunch in host.cpp); OplUi/OplApp/OplLibrary only forward to existing free functions. The only substitute is a no-op UiServices in tests/host/host_env.h, and hostmap.py exists just to rewrite host:: calls back into the old function names so source-scraping tests still work. The cost is 4 vtables plus RTTI and ~45 forwarder thunks in the shipped ELF and an indirect call per use.
- Fix: Replace the classes with a plain header of free-function prototypes (the decoupling the header was written for only needs declarations, not virtual dispatch) and call guiMsgBox/libViewActive/... directly; keep OplLaunch's real logic as free functions. Tests substitute by linking their own definitions, as host_env.h already does for ziso_init/ziso_read_sector. Then delete hostmap.py and its imports.
- Benefit: MEASURED: the 79 host-layer symbols (vtables, typeinfo, forwarders, accessors) total 1863 B in riptopl-nm-size-sorted.full.txt; about 1.3 KB of that is vtable/RTTI/forwarder overhead (INFERRED, OplLaunch logic ~0.5 KB stays). ~150 lines (host.cpp forwarders, class boilerplate, hostmap.py).
- Effort: L. Risk: Med. Confidence: Med (READ)

#### RIPTOPL-075: pad.h and sound.h sfx* functions are pure forwarders to a single static PadManager / SoundEffects instance

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/core/pad.cpp:1240-1322`; `RiptOPL/include/padmanager.h:36-59`; `RiptOPL/src/core/pad.cpp:1127-1131`; `RiptOPL/src/core/sfx.cpp:417-439`; `RiptOPL/include/soundeffects.h:19-30`
- Problem: Sixteen pad.h functions (pad.cpp:1244-1322) and four sfx* functions (sfx.cpp:421-439) only call the same-named method on one file-static object (gPads, gSfx); no other instance or caller of the classes exists. unloadPads goes through two forwarders (unloadPads -> PadManager::unload -> unloadEx(0)). The build already inlines the members into the wrappers: the linker map lists the out-of-line PadManager copies as discarded, so the layer is source weight only.
- Fix: Keep one API: either make the methods the pad.h/sound.h functions over file-static state (the shape before the class conversion), or let the few callers use the object directly. At minimum delete PadManager::unload and call unloadEx(0) from unloadPads.
- Benefit: ~100 source lines (wrappers ~105, public class declarations ~35, offset by moving state to file statics); no ELF change, MEASURED: riptopl-discarded-firstparty.txt shows the out-of-line PadManager members already removed by --gc-sections
- Effort: M. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-076: Include blocks of autolaunch.cpp and lwnbdctl.cpp are copied from opl.cpp; most headers and their comments do not apply

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/app/autolaunch.cpp:7-66`; `RiptOPL/src/app/lwnbdctl.cpp:7-67`; `RiptOPL/src/sources/appscan.cpp:7-68`
- Problem: Both files carry the ~60-line opl.cpp include list verbatim (dupes-blocks.tsv: autolaunch.cpp:9-65 == lwnbdctl.cpp:10-66, also matching opl.cpp, appscan.cpp, settingsio.cpp, menuitems.cpp). autolaunch.cpp uses only oplint/opl/ioman/config/util/supportbase/bdmsupport/hddsupport/menusys and fileXio_rpc; lwnbdctl.cpp uses oplint/oplog/opl/ioman/gui/lang/texcache/pad/system/util/extern_irx/ethsupport/sound. The per-include comments are wrong for these files: lwnbdctl.cpp says fileXio is 'used by BDM autolaunch' and both justify sys/stat.h by an mkdir neither file calls; libds34bt/usb, tar.h, rabadge.h, cheatman.h, xparam.h etc. are unused.
- Fix: Delete the unused includes and their comments from autolaunch.cpp and lwnbdctl.cpp, keeping only the headers listed above; let the compiler confirm. Apply the same trim to the other split-out copies when their units are touched.
- Benefit: ~95 source lines removed from the two files (60+61 lines down to ~13+14); faster compiles; no ELF change (INFERRED: headers only).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-077: gui.cpp and three split-off files carry the same ~25 unused #include lines with comments naming code that is not in them

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/ui/gui.cpp:19`; `RiptOPL/src/ui/gui.cpp:26`; `RiptOPL/src/ui/gui.cpp:33-46`; `RiptOPL/src/ui/gui.cpp:48-52`; `RiptOPL/src/ui/gui.cpp:55-56`; `RiptOPL/src/ui/guiwait.cpp:13-45`; `RiptOPL/src/ui/guimodal.cpp:14-46`; `RiptOPL/src/ui/guigameid.cpp:26-58`
- Problem: The include block of the pre-split gui.cpp was copied verbatim into guiwait.cpp, guimodal.cpp and guigameid.cpp. In gui.cpp none of the symbols the comments name (httpTestServer, bdmIsUDPBDLoaded, tarInvalidate, appGetObject, libViewActive, gFAVStartMode, gLastDeferredTimedOut, sfxGetPlayDiag) are used, nor anything from mmce/eth/net/udpfs/http/artindex/bdm/hdd/vcd/libview/pggsm/cheatman/guigame/appsupport/tar headers, <limits.h>, <math.h> or <time.h>; sound.h is included twice. The same comment-tagged headers are unused in the three copies.
- Fix: Delete the unused includes (and the duplicate sound.h) from gui.cpp, guiwait.cpp, guimodal.cpp and guigameid.cpp, keeping only headers whose symbols each file uses; let the compiler confirm.
- Benefit: ~80 include lines removed across 4 files (gui.cpp ~23 verified, ~19 per copy INFERRED); faster rebuilds, no ELF change
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-078: Stale history narratives in texcache.h/texcache.cpp (wrong priorities, removed depth cap, old .c file names)

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/include/texcache.h:26-31`; `RiptOPL/include/texcache.h:35-47`; `RiptOPL/include/texcache.h:149`; `RiptOPL/include/texcache.h:188-190`; `RiptOPL/include/texcache.h:219-232`; `RiptOPL/src/gfx/texcache.cpp:362-367`; `RiptOPL/src/gfx/texcache.cpp:409-412`; `RiptOPL/src/gfx/texcache.cpp:482-512`; `RiptOPL/src/gfx/texcache.cpp:522-524`
- Problem: Large comments record past releases instead of current behaviour, and several are now wrong: texcache.cpp:365 says the art thread runs at priority 64 and :490-496 explains a race through the IO worker at priority 32, but artload.cpp:31 sets ART_THREAD_PRIORITY = 72 on its own thread; texcache.cpp:409-412 describes a backpressure refusal that the next paragraph (:413-419) says was removed; texcache.h:188-190 documents a 'depth cap' counter that nothing increments; texcache.h:219-232 lists call sites in themes.c/opl.c (now .cpp) and a stub history; :149 and :26-31 are rebuild-number history. texcache.cpp:522 names 'artPush' for gArtQ.push.
- Fix: Delete the historical paragraphs and the wrong priority/backpressure statements; keep one or two sentences per site that state the current rule (e.g. 'stamp lastUsed at enqueue so the 20-frame cancellation measures frames since last draw').
- Benefit: ~70 comment lines removed; no binary effect (INFERRED, comments only). Removes three statements that contradict the code.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-079: SupportBase class and include/supportbaseint.h are a single-instance wrapper used only by supportbase.cpp, reached through thin forwarders

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/include/supportbaseint.h`; `RiptOPL/src/sources/supportbase.cpp:61-63`; `RiptOPL/src/sources/supportbase.cpp:930-935`; `RiptOPL/src/sources/supportbase.cpp:1302-1321`; `RiptOPL/src/sources/supportbase.cpp:2827-2834`; `RiptOPL/src/sources/supportbase.cpp:2854-2859`; `RiptOPL/src/sources/supportbase.cpp:2868-2873`; `RiptOPL/src/sources/supportbase.cpp:2882-2887`
- Problem: The class holds three values (the size-stat owner thread, the IGR-USB asked latch, the RA hash-log FILE*), has one instance gSupportBase, and every member is reached only through a free forwarder in the same file (sbEnsureIgrUsbDrivers, sbSetConfigStatSize, sbConfigStatSizeEnabled, raHashLogOpen/Step/Add/Close). Nothing outside supportbase.cpp includes supportbaseint.h or names gSupportBase.
- Fix: Delete include/supportbaseint.h and the class. Make the three fields file-scope statics in supportbase.cpp, move each member body into its free function, and delete the forwarders and the gSupportBase instance. Update the stub in .github/scripts/test_info_size_ps1.py (lines 52, 112-113), which matches the SupportBase:: signatures.
- Benefit: ~70 lines removed (43-line header, 6 forwarders of 4 lines, the instance and its comment); ELF effect negligible (INFERRED: same-TU forwarders, one small global object replaced by statics).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-080: guiGameHandleDeferedIO is never called; its dialog wait path makes DeferredIoWait a two-mode class with one live mode

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/ui/guiwait.cpp:115-159`; `RiptOPL/include/guiwait.h:12-47`; `RiptOPL/include/gui.h:197`
- Problem: guiGameHandleDeferedIO (guiwait.cpp:155) appears only at its definition and its prototype gui.h:197. It is the sole user of the UIItem constructor, ui_ field and runWithDialog (115-139); with it gone DeferredIoWait has one constructor, one path and one caller (guiHandleDeferedIO:149-153), so the class, its run() dispatch and the public header declaration are a wrapper around a single function body.
- Fix: Delete guiGameHandleDeferedIO and its prototype, runWithDialog, run() and the class; move the runWithMessage body into guiHandleDeferedIO taking its parameters directly. guiwait.h then keeps only the two progress-bar externs.
- Benefit: ~70 lines removed (guiwait.cpp 115-159 minus a 5-line signature, guiwait.h 12-47, gui.h:197); runWithDialog/class code is compiled into the ELF today (non-static members), so a few hundred bytes (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-081: NetSupport class in netsupport.h is private state behind a public class plus nine one-line forwarders (and two more in ethsupport.cpp)

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/include/netsupport.h:61-106`; `RiptOPL/src/sources/netsupport.cpp:25-28`; `RiptOPL/src/sources/netsupport.cpp:41-44`; `RiptOPL/src/sources/netsupport.cpp:99-107`; `RiptOPL/src/sources/netsupport.cpp:160-168`; `RiptOPL/src/sources/netsupport.cpp:244-247`; `RiptOPL/src/sources/netsupport.cpp:307-310`; `RiptOPL/src/sources/netsupport.cpp:330-333`; `RiptOPL/src/sources/netsupport.cpp:349-352`; `RiptOPL/src/sources/netsupport.cpp:399-402`; `RiptOPL/src/sources/netsupport.cpp:497-500`; `RiptOPL/src/sources/ethsupport.cpp:1646-1654`
- Problem: gNetSupport and class NetSupport are used only inside netsupport.cpp; every caller uses the netXxx free functions, each a one-line forward to a method. waitValidLinkState/waitValidDhcpState are one-line wrappers, the NetStatusCheckCb forward declaration precedes a definition that already comes before its use, and ethGetNetConfig/ethGetDHCPStatus forward again to netGetConfig/netGetDHCPStatus, so a caller goes through three layers.
- Fix: Delete the class from netsupport.h; keep the members as file-static variables in netsupport.cpp and turn each method into the netXxx function (or a static helper) directly. Delete the two waitValid wrappers and the forward declaration. Optionally point ethGetNetConfig/ethGetDHCPStatus callers at netGetConfig/netGetDHCPStatus and delete those two. Update the source-text tests that pin the method names (.github/scripts/test_pccf.py:38, test_smb2_menu_wait_bounds.py:95-146).
- Benefit: ~70 source lines removed (46-line class in the header, ~40 lines of forwarders, ~9 lines of wrappers, offset by ~8 lines of file statics); no ELF change worth stating, forwarders are tiny (INFERRED).
- Effort: M. Risk: Low. Confidence: High (READ)

#### RIPTOPL-082: Worst-open latch (texDebugWorstOpen, gTexWorstOpen*) and gBgmInRead exist only for a finished art-open investigation

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/gfx/textures.cpp:528-584`; `RiptOPL/src/gfx/textures.cpp:929-943`; `RiptOPL/src/gfx/textures.cpp:7`; `RiptOPL/include/textures.h:231-234`; `RiptOPL/src/ui/guioverlay.cpp:193-194`; `RiptOPL/src/ui/guioverlay.cpp:211-213`; `RiptOPL/src/core/bgm.cpp:76`; `RiptOPL/src/core/bgm.cpp:240`; `RiptOPL/src/core/bgm.cpp:276`; `RiptOPL/include/sound.h:46`
- Problem: Every staged art open samples two ioman queue depths, gBgmInRead and clock() to keep a session-worst record (5 statics, a 13-line getter, ~40 lines of investigation narrative incl. a quoted user report). The comment says the measurement exists to decide whether an in-RAM art directory index is worth building; that index now exists (artindex.cpp, IX field in the same HUD line), so the question is closed. gBgmInRead's only reader is textures.cpp:933.
- Fix: Delete gTexWorstOpen* and texDebugWorstOpen, the pendAtOpen/menuAtOpen/bgmAtOpen sampling and latch update in texLoadAll, the W field in the HUD format string, gBgmInRead (definition, two writes, extern) and the sound.h include. Cut the 528-565 narrative to one line describing O:<hit>/<miss> if that field is kept.
- Benefit: ~70 source lines removed (35 latch+getter, ~12 sampling, 4 header, 4 gBgmInRead, ~17 comment); a few hundred bytes of .text/.data, two queue reads per staged open (INFERRED)
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-083: system.cpp include prelude and disc-probe constants copied verbatim into sysneutrino.cpp, sysvmc.cpp and sysdisc.cpp at the split

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/platform/sysneutrino.cpp:7-65`; `RiptOPL/src/platform/sysvmc.cpp:7-64`; `RiptOPL/src/platform/sysdisc.cpp:9-66`; `RiptOPL/src/platform/system.cpp:9-67`
- Problem: When system.cpp was split, its whole prelude (DECI2 include, delaythread/time, the SYS_DISC_* constexprs and their 6-line comment, ~25 project headers, PADEMU libds34, fileXio/usbhdfsd-common/ps2sdkapi_compat 'VMC contiguity probe') was pasted into each new file. SYS_DISC_DETECT/REDETECT/NODISC_MS are used only in sysdisc.cpp:161,185,186; the copies at sysneutrino.cpp:15-23, sysvmc.cpp:15-23 and system.cpp:17-25 are dead. sysneutrino.cpp uses no DelayThread, clock, fileXio, USBMASS ioctl, ps2sdk_get_iop_fd, ds34, cheat, xparam, pggsm, OSDHistory, ioprp or extern_irx symbol; sysdisc.cpp likewise uses none of the VMC/PADEMU/fileXio/RA headers.
- Fix: Delete the SYS_DISC_* constants and their comment from sysneutrino.cpp, sysvmc.cpp and system.cpp (keep sysdisc.cpp's). Delete delaythread/time from sysneutrino.cpp and sysvmc.cpp, and trim each split file's include list to the headers it uses (for sysneutrino.cpp at least lines 7-23, 57-65, 39, 42, 44, 48, 50-55; for sysdisc.cpp at least 9-11, 39-52 except mmcesupport, 53-66), letting the compiler confirm the minimal set.
- Benefit: About 70 source lines and the misleading 'disc wait' / 'VMC contiguity probe' comments in files that do neither; no ELF change (constexpr ints and unused headers emit nothing), faster compiles (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-084: Legacy sub-page buttons (VCD_BDMA, MMCE_COMM, MMCE_PATH, LAUNCH_NEUTRINO_DEFAULTS) never render; composeInto's skip logic exists only to hide them

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/ui/dialogs.cpp:654-659`; `RiptOPL/src/ui/dialogs.cpp:835-843`; `RiptOPL/src/ui/dialogs.cpp:1232-1234`; `RiptOPL/include/dialogs.h:330-336`; `RiptOPL/src/ui/guisettings.cpp:360`; `RiptOPL/src/ui/guisettings.cpp:2040-2049`; `RiptOPL/src/ui/guisettings.cpp:2051-2061`; `RiptOPL/src/ui/guisettings.cpp:2085-2099`; `RiptOPL/src/ui/guisettings.cpp:2799`; `RiptOPL/src/ui/guisettings.cpp:2904`; `RiptOPL/include/settingshell.h:44-48`; `RiptOPL/lng/tmpl/_base.yml:1720-1721`; `RiptOPL/lng/tmpl/_base.yml:1728-1731`; `RiptOPL/lng/tmpl/_base.yml:1786-1787`
- Problem: diaVcdConfig, diaMmceConfig and diaLaunchConfig are only ever used as compose parts (guisettings.cpp:359, 2798, 2903), and every compose call skips exactly these four button ids, so the rows plus their label/spacer/break lines never render; the comments calling them 'callable outside the Settings shell' are false. guiSettingsSkipID and the orphaned-label removal at 2085-2099 serve only these rows, skipPart is -1 at all 7 calls, and suppressSecondaryHeaders is 0 only on a 1-part page where it has no effect.
- Fix: Delete the four button rows (with their label/spacer/break lines and comments) from the templates, the four enum ids, the three skipIDs arrays, guiSettingsSkipID and the skip block in composeInto, and drop the skipIDs/skipCount/skipPart/suppressSecondaryHeaders parameters from compose/composeInto (always suppress). Optionally drop the HINT_BDMA_BUTTON, HINT_MMCE_COMM_BUTTON, HINT_MMCE_PATH_BUTTON and HINT_NEUTRINO_DEFAULTS_BUTTON strings from _base.yml and the lng/fork translations.
- Benefit: ~65 code lines (dialogs.cpp 18, dialogs.h 4, guisettings.cpp ~35, _base.yml 8) plus ~130 translation lines; ~270 bytes of English hint text and 4 table rows in the ELF (INFERRED from string lengths).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-085: The twelve element families are listed by hand eleven times in themes.cpp (and once more in themeelems.cpp)

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/gfx/themes.cpp:196-198`; `RiptOPL/src/gfx/themes.cpp:306-317`; `RiptOPL/src/gfx/themes.cpp:334-336`; `RiptOPL/src/gfx/themes.cpp:544-551`; `RiptOPL/src/gfx/themes.cpp:561-572`; `RiptOPL/src/gfx/themes.cpp:576-587`; `RiptOPL/src/gfx/themes.cpp:610-621`; `RiptOPL/src/gfx/themes.cpp:624-635`; `RiptOPL/src/gfx/themes.cpp:957-968`; `RiptOPL/src/gfx/themes.cpp:1122-1124`; `RiptOPL/src/gfx/themes.cpp:1855-1858`; `RiptOPL/src/gfx/themeelems.cpp:871-882`
- Problem: theme_t's 12 theme_elems_t members are not contiguous (include/themes.h:200-234), so every pass over all families spells them out: seven 12-line call blocks (306-317, 561-572, 576-587, 610-621, 624-635, 957-968, themeelems.cpp:871-882), an 8-line block (544-551) and four hand-written pointer arrays (196-198, 334-336, 1122-1124, 1855-1858). Adding a family means editing ~12 places.
- Fix: Add one static helper that fills a theme_elems_t *[12] for a theme (exported through themeint.h for themeelems.cpp) and turn each block into a loop; merge the coverage and text-room passes (561-587) into one loop. Keep the 8-family subset at 544-551 as a loop that skips thmIsFavKindFamily.
- Justification: The helper (about 8 lines) replaces about 75 lines of repeated per-family calls and removes the risk of one list missing a family when the next one is added.
- Benefit: ~65 source lines net (INFERRED: 7 x 12-line blocks become 2-line loops = -70, 4 arrays shrink to 1 line = -6, 8-line block -5, helper +8); negligible ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-086: FolderBrowse class and folderbrowseint.h exist only to be wrapped by six one-line folder* forwarders

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/include/folderbrowseint.h`; `RiptOPL/src/sources/folderbrowse.cpp:7`; `RiptOPL/src/sources/folderbrowse.cpp:13`; `RiptOPL/src/sources/folderbrowse.cpp:23-51`; `RiptOPL/src/sources/folderbrowse.cpp:55`; `RiptOPL/src/sources/folderbrowse.cpp:62`; `RiptOPL/src/sources/folderbrowse.cpp:69`; `RiptOPL/src/sources/folderbrowse.cpp:95`
- Problem: FolderBrowse has one instance (gFolderBrowse) and no user outside folderbrowse.cpp: every caller in the repo uses the folder* C functions, which only forward to the methods. folderbrowseint.h (38 lines, repeating the doc comments of folderbrowse.h) is included only by folderbrowse.cpp. The four guards also test mode < 0 || mode >= MODE_COUNT next to folderModeSupported(mode), which already rejects every out-of-range mode.
- Fix: Delete folderbrowseint.h and the class; keep the three state arrays as file-static in folderbrowse.cpp and move each method body into its folder* function. Drop the redundant range test from the four guards (keep it in folderReset/folderConsumeDirty, which do not call folderModeSupported). Update .github/scripts/test_libview_l3_rescan.py, which extracts FolderBrowse::resetToRoot by name.
- Benefit: ~60 lines removed (38-line header + ~29 lines of forwarders, minus 3 lines of static arrays); no ELF change expected, the forwarders are tiny (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-087: Stale, orphaned and repeated comments in the art pipeline after the texcache split and the move off the ioman worker

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/gfx/artload.cpp:38-39`; `RiptOPL/src/gfx/artload.cpp:58-75`; `RiptOPL/src/gfx/artload.cpp:150-162`; `RiptOPL/src/gfx/artload.cpp:330-349`; `RiptOPL/src/gfx/artload.cpp:445-448`; `RiptOPL/src/gfx/artload.cpp:457-459`; `RiptOPL/src/gfx/artload.cpp:528-541`; `RiptOPL/src/gfx/artload.cpp:680-681`; `RiptOPL/include/artworker.h:4`; `RiptOPL/include/artworker.h:49-55`; `RiptOPL/include/artworker.h:73-88`; `RiptOPL/include/artqueue.h:30-38`; `RiptOPL/include/artqueue.h:59-61`; `RiptOPL/src/gfx/coverflow.cpp:101-103`
- Problem: Comments describe code that is gone: artload 38-39 is an orphan copy of artworker.h:15-16; 58-75 says cacheLoadImage is an ioman handler at priority 30 (it runs on the art thread at 72, lines 31 and 510); 150-162 describe counters that now live in ArtQueue; 445 says priority 64; 457-459 says five call sites and MMCE (14 sites, SIO2); 528-541 cite texcache.c line numbers and ioPutRequest; 680 says the flag is read by nobody (read at 688); artworker.h cites texcache.cpp, gui.c, gArtRefused/gArtDone, the IO worker and the shared ioman queue (77-88, contradicted by artload.cpp:817-822); artqueue.h:60 points to src/artqueue.cpp for a rationale that sits in artload.cpp:330-349; the MX4SIO note is repeated at artqueue.h:34-38 and artqueue.cpp:80-84.
- Fix: Delete the orphan blocks (artload 38-39, 150-162), move the ART QUEUE rationale (artload 330-349) to artqueue.cpp and drop the repeated copy in artqueue.h, and rewrite the false sentences to one line each (or delete them).
- Benefit: ~60 comment lines removed; no binary change (READ: comments only)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-088: igs_api.c SaveTextFile: 37 hand-unrolled label/format/strcat triples, DISPFB/DISPLAY blocks written twice

- Category: 2 (Duplication)
- Location(s): `RiptOPL/ee_core/src/igs_api.c:415-547`
- Problem: SaveTextFile builds its report with about 37 repetitions of _strcat(label); u16todecstr(field); _strcat(value), and the DISPFB2/DISPLAY2 block (506-544) is a line-for-line copy of the DISPFB1/DISPLAY1 block (467-505) with only the variable changed.
- Fix: Add one static helper that appends a label and a formatted field, call it once per field, and emit the DISPFB/DISPLAY pair in a two-iteration loop over (dispfb1, display1) and (dispfb2, display2).
- Justification: The helper replaces about 110 lines of repeated three-line triples with about 40 one-line calls plus an 8-line helper, and the loop removes the duplicated 39-line block; net code shrinks.
- Benefit: ~60 lines removed net (INFERRED: 111 triple lines -> ~37 calls + 8-line helper, minus the duplicated block); smaller .text in the extras eecore
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-089: sysLaunchDisc prelude (SYS_DISC_* budgets, time.h, delaythread.h) copied unused into system.cpp, sysvmc.cpp and sysneutrino.cpp

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/platform/system.cpp:14-25`; `RiptOPL/src/platform/sysvmc.cpp:11-23`; `RiptOPL/src/platform/sysneutrino.cpp:11-23`; `RiptOPL/src/platform/sysvmc.cpp:24-59`
- Problem: SYS_DISC_DETECT_MS/REDETECT_MS/NODISC_MS, clock() and DelayThread are used only in sysdisc.cpp (134-206), yet the same 12-13 line block with its long comment sits in three other files where nothing reads it (system.cpp still needs delaythread.h for line 504). sysvmc.cpp also kept system.cpp's whole include list (gui, lang, launchdiag, eth/hdd/bdm/mmce support, pad, OSDHistory, renderman, extern_irx, ee_core modules/coreconfig, pggsm, sema_lock, cheatman, xparam, rawatch, ds34) although its code only uses system.h, ioman.h (LOG), fileXio, usbhdfsd-common, ps2sdkapi_compat and genvmc.h.
- Fix: Delete the SYS_DISC_* constants and time.h include from system.cpp, sysvmc.cpp and sysneutrino.cpp, and delaythread.h from sysvmc.cpp and sysneutrino.cpp; keep them only in sysdisc.cpp. Trim sysvmc.cpp's include block to the headers its code uses.
- Benefit: ~38 lines of copied constants/comments removed, ~25 more include lines in sysvmc.cpp; constexpr ints emit no code, so no ELF change (INFERRED); slightly faster compiles
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-090: Two ISO9660 SYSTEM.CNF readers for PS1 VCDs: retrogem.cpp and rahash.cpp

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/retrogem.cpp:177-388`; `RiptOPL/src/sources/rahash.cpp:204-208`; `RiptOPL/src/sources/rahash.cpp:270-303`; `RiptOPL/src/sources/rahash.cpp:403-501`; `RiptOPL/src/sources/rahash.cpp:663-763`; `RiptOPL/src/sources/rahash.cpp:826-846`
- Problem: Both files read a VCD past its 1 MiB header as raw 2352-byte sectors, locate the PVD, walk the root directory with case-insensitive ';'-tolerant name matching, read SYSTEM.CNF and parse its BOOT line (retrogem: readSector/FindPvd/IsoNameMatches/ParseReader/ParseSystemCnf; rahash: vcd_read_at/readRoot/findEntry/findPath/parse_boot_psx/vcdBootName), each with its own le32 reader. retrogem is always built; rahash is RA-only.
- Fix: Keep one ISO9660 directory walker (rahash's findEntry/findPath already take a sector-source abstraction) and have the other use it; at minimum share the le32 reader and the directory-record walk. rahash must keep its rcheevos-exact BOOT parsing (BOOT2 ignored), so only the walker is shared.
- Benefit: ~60-90 source lines removed in an RA build (INFERRED: walker and name match are ~100 lines in each file); no base-build saving unless retrogem adopts the shared walker
- Effort: M. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-091: class Gui has one instance and is used only through one-line C wrappers: two layers for one API

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/ui/gui.cpp:476-498`; `RiptOPL/include/guiruntime.h:8-69`; `RiptOPL/src/ui/gui.cpp:299-302`; `RiptOPL/src/ui/gui.cpp:376-378`
- Problem: Every public Gui method is reached through a guiXxx forwarder in gui.cpp:477-498; outside gui.cpp the class is touched once (guisplash.cpp:229, gGui.runFrameHook). handleOpThunk exists only to turn the member back into a plain function for the queue, mainLoop repeats runFrameHook inline, and queueSema_ is a member though only init() needs it.
- Fix: Make the Gui members file-static state in gui.cpp and turn the methods into the guiXxx functions themselves; delete guiruntime.h, the forwarders and handleOpThunk. Smaller step: at least call runFrameHook in mainLoop and make queueSema_ a local in init().
- Benefit: ~60 lines removed (INFERRED: 72-line guiruntime.h and 22 forwarders replaced by ~15 lines of static state); no ELF change expected
- Effort: M. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-092: Composed-only settings tables keep never-shown legacy button rows, plus the skip-ID plumbing that exists only to hide them

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/ui/dialogs.cpp:654-659`; `RiptOPL/src/ui/dialogs.cpp:835-843`; `RiptOPL/src/ui/dialogs.cpp:1232-1234`; `RiptOPL/src/ui/dialogs.cpp:677-679`; `RiptOPL/src/ui/dialogs.cpp:852-857`; `RiptOPL/src/ui/dialogs.cpp:924-929`; `RiptOPL/src/ui/dialogs.cpp:1292-1294`; `RiptOPL/src/ui/dialogs.cpp:1318-1319`; `RiptOPL/include/dialogs.h:330-336`; `RiptOPL/src/ui/guisettings.cpp:360`; `RiptOPL/src/ui/guisettings.cpp:1103`; `RiptOPL/src/ui/guisettings.cpp:2040-2049`; `RiptOPL/src/ui/guisettings.cpp:2085-2099`; `RiptOPL/src/ui/guisettings.cpp:2799`; `RiptOPL/src/ui/guisettings.cpp:2904`
- Problem: diaMmceConfig, diaLaunchConfig and diaVcdConfig are only ever used as parts of SettingsShell::compose/composeInto, and every such call passes the rows' ids in skipIDs, so the MMCE_COMM_BUTTON, MMCE_PATH_BUTTON, LAUNCH_NEUTRINO_DEFAULTS_BUTTON and VCD_BDMA_BUTTON rows are never rendered and no code handles them. The comments claiming standalone 'legacy callers/editors' (BDMA, MMCE comm/path, Security password, Advanced) are false: those tables are only composed, and their UI_HEADER rows are dropped by suppressSecondaryHeaders=1 in every multi-part compose call.
- Fix: Delete the four LABEL/SPACER/BUTTON/BREAK groups and their enum values; then delete the skipIDs arrays, guiSettingsSkipID, the orphan-label/skip block in composeInto and the skipIDs/skipCount/skipPart parameters (skipPart is -1 at every call). Delete or correct the stale 'legacy caller' comments, and make guiBdmaUpdater use activeDialog() without the diaBdmaConfig fallback.
- Benefit: ~55 source lines removed (18 table/comment lines in dialogs.cpp, 4 enum lines, ~30 in guisettings.cpp); a few UIItem rows of .data (INFERRED, not measured)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-093: Three Start-menu submenu screens duplicate their background, layout and up/down wrap code

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/ui/menusys.cpp:2177-2212`; `RiptOPL/src/ui/menusys.cpp:2924-2980`; `RiptOPL/src/ui/menusys.cpp:3293-3344`; `RiptOPL/src/ui/menusys.cpp:2322-2340`; `RiptOPL/src/ui/menusys.cpp:3151-3169`; `RiptOPL/src/ui/menusys.cpp:3360-3378`
- Problem: menuRenderMenu, menuRenderGameMenu and menuRenderAppMenu each repeat the same 6-line settings-background block (with the same 4-line comment), the same 'count items / find selected index / spacing 25 / centre y' block (~13 lines) and the same centred draw loop; menuHandleInputMenu, menuHandleInputGameMenu and menuHandleInputAppMenu repeat the same 19-line 'default current, Up wraps to last, Down wraps to first' block on mainMenu/gameMenu/appMenu.
- Fix: Keep one static helper that moves a (head, current) pair on Up/Down and one that draws the background and the centred item list (with an optional extra-gap predicate for the main and game menus); call them from the three screens.
- Justification: The helpers replace three verbatim copies each, so the file gets shorter overall; no behaviour changes.
- Benefit: ~55 lines removed (3x19 nav lines -> one ~17-line helper; 3x~25 render lines -> one ~25-line helper) (INFERRED from cited ranges); negligible ELF change
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-094: vcdCanonDisplayId is a no-op on retrogem output: retrogem only ever returns the canonical AAAA_NNN.NN (or LSP) shape

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/sources/vcdsupport.cpp:358-409`; `RiptOPL/src/sources/vcdsupport.cpp:452-459`; `RiptOPL/src/sources/retrogem.cpp:78-104`; `RiptOPL/src/sources/retrogem.cpp:154-172`
- Problem: Its only caller (vcdsupport.cpp:457) feeds it ids from retrogemGetVcdGameID/retrogemGetPs1ImageGameID. Every retrogem success path writes an uppercased AAAA_NNN.NN (retrogem.cpp:103, 157-159, 167-170) or LSPxxxxx.xx (90-92); a dash form can never come out (the comment at vcdsupport.cpp:361-363 claiming AAAA-NNNNN survives retrogemCleanTitleID is wrong: the dash branch at 164-171 re-renders it). So the canonical branch copies the input unchanged, the dash branch at 394-406 is unreachable, and LSP ids return 0 and keep discId anyway.
- Fix: Delete vcdCanonDisplayId and its comment block, and store discId directly at vcdsupport.cpp:456-459 (drop the canon buffer).
- Benefit: ~55 source lines removed (52 for the function and comment, 3 at the call site); static function, a few hundred bytes of .text (INFERRED, not measured)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-095: GSM 'fix' toggles are compile-time constants written at runtime; the engine tests them on every trapped GS write

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/ee_core/src/gsm_api.c:58-74`; `RiptOPL/ee_core/src/gsm_api.c:104-117`; `RiptOPL/ee_core/src/gsm_engine.S:596-601`; `RiptOPL/ee_core/src/gsm_engine.S:624-626`; `RiptOPL/ee_core/src/gsm_engine.S:678-680`; `RiptOPL/ee_core/src/gsm_engine.S:697-699`; `RiptOPL/ee_core/src/gsm_engine.S:715-717`; `RiptOPL/ee_core/src/gsm_engine.S:937-939`; `RiptOPL/ee_core/src/gsm_engine_adv.S:307-312`; `RiptOPL/ee_core/src/gsm_engine_adv.S:345-350`; `RiptOPL/ee_core/src/gsm_engine_adv.S:369-374`; `RiptOPL/ee_core/src/gsm_engine_adv.S:390-395`; `RiptOPL/ee_core/src/gsm_engine_adv.S:415-420`; `RiptOPL/ee_core/include/gsm_defines.h:76-84`
- Problem: UpdateGSMParams is the only writer of GSMFlags and always stores the same values: ADAPTATION/SMODE2/DISPFB/DISPLAY_fix = 1, PMODE/SMODE1/SRFSH/SYNCH/SYNCV_fix = 0. GSHandler still loads and branches on each flag per trapped write, so the PMODE/SMODE1/SRFSH/SYNCH1/SYNCH2/SYNCV 'forced' paths and the five 'fix disabled' paths are unreachable. Side effect: GSMDestGSRegs.syncv (gsm_api.c:104, plumbed from the launcher) is only read at gsm_engine_adv.S:418, behind SYNCV_fix=0, so it never takes effect.
- Fix: Delete the nine constant flag bytes and their assignments; in the engine keep only the path each constant selects (pass-through store for PMODE/SMODE1/SRFSH/SYNCH1/SYNCH2/SYNCV, forced/adapted path for SMODE2/DISPFB/DISPLAY/adaptation). Drop the syncv store and its parameter if nothing else needs it. Keep FIELD_fix, gs576P_param and track_only (set from config). Upstream GSM (doctorxyz) code: a trim, not a rewrite.
- Benefit: ~55 source lines; INFERRED ~45 instructions (~180 bytes) of ee_core ram84 text: 6 handlers x ~6 dead instructions + 5 sites x 3 flag-test instructions; one lb+beql less per trapped GS write
- Effort: M. Risk: Med. Confidence: High (READ)

#### RIPTOPL-096: ThemeCatalog is a single-instance class reached only through one-line thm* forwarders; its count() accessor and two includes of its header are unused

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/include/themecatalog.h:15-49`; `RiptOPL/src/gfx/themes.cpp:54`; `RiptOPL/src/gfx/themes.cpp:1870-1873`; `RiptOPL/src/gfx/themes.cpp:2111-2129`; `RiptOPL/src/gfx/themes.cpp:2210-2218`; `RiptOPL/src/gfx/themes.cpp:2233-2241`; `RiptOPL/src/gfx/coverflow.cpp:5`; `RiptOPL/src/gfx/themeelems.cpp:5`; `RiptOPL/.github/scripts/test_psbbn_flow.py:265-272`
- Problem: The only instance is static gCatalog in themes.cpp; every external caller uses the C API (thmAddElements, thmReinit, thmGetValue, thmSetGuiValue, thmGetGuiValue, thmFindGuiID, thmGetGuiList, thmGetFilePath), each a one-line forwarder, and readEntryCallback is a trampoline back into gCatalog. ThemeCatalog::count() has no caller. coverflow.cpp and themeelems.cpp include themecatalog.h but use nothing from it (both include themes.h directly).
- Fix: Drop the class: make themeId_, count_, themes_ and names_ file-static in themes.cpp, implement the thm* functions directly, pass readEntry to listDir, delete count(), delete themecatalog.h and its two unused includes, and adjust the source-text test that extracts the class.
- Benefit: ~55 source lines (INFERRED: header 35 lines -> 0, 8 forwarders and the trampoline ~35 lines -> 0, file-static state +5); no ELF change expected beyond inlined forwarders.
- Effort: M. Risk: Low. Confidence: High (READ)

#### RIPTOPL-097: 35-line include block copy-pasted from gui.cpp into guigameid.cpp and guimodal.cpp, with comments naming symbols these files never use

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/ui/guigameid.cpp:22-58`; `RiptOPL/src/ui/guimodal.cpp:9-46`; `RiptOPL/src/ui/guiwait.cpp:9-45`
- Problem: Both files include ~35 headers (sound.h twice) annotated with gFAVStartMode, httpTestServer, bdmIsUDPBDLoaded, appGetObject, tarInvalidate, gLastDeferredTimedOut, sfxGetPlayDiag: none of these appear in either file. guigameid.cpp only uses opl.h (gApplyGameID), guiint.h (screen size, frame calls, guiDrawBusy), renderman.h and guigameid.h. guiwait.cpp carries the same block.
- Fix: Delete the unused includes: guigameid.cpp keeps opl.h, guiint.h, renderman.h, guigameid.h; guimodal.cpp keeps cheatconfig.h, cheatman.h, opl.h, gui.h, guiint.h, renderman.h, fntsys.h, lang.h, themes.h, pad.h, sound.h, util.h, config.h, guigame.h, guimodal.h (confirm with one compile).
- Benefit: ~55 lines removed in these two files (37-4 in guigameid, 38-15 in guimodal) plus ~25 in guiwait.cpp; faster compile, no ELF change (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-098: Copy-pasted include blocks with stale per-include comments in guiwait.cpp and menuitems.cpp (and the other files split from gui.cpp/opl.cpp)

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/ui/guiwait.cpp:9-45`; `RiptOPL/src/ui/menuitems.cpp:7-68`; `RiptOPL/src/ui/guimodal.cpp:9-46`; `RiptOPL/src/app/autolaunch.cpp:7-66`
- Problem: Both files carry the full include list of the file they were split from (guiwait.cpp:9-45 matches guimodal.cpp:10-46; menuitems.cpp:7-68 matches autolaunch.cpp:7-66; the duplicate report also pairs them with gui.cpp, guisettings.cpp, guigameid.cpp, opl.cpp, lwnbdctl.cpp, appscan.cpp, settingsio.cpp). guiwait.cpp includes sound.h twice and about 24 headers it does not use (plasma, lang, favsupport, mmce/eth/net/udpfs/http/bdm/hdd/vcd support, libview, pggsm, cheatman, guigame, appsupport, tar, limits, stdlib...). In menuitems.cpp the comments name symbols the file never uses: mkdir (sys/stat.h, 61-64), fileXioDopen (59), mmceLoadModules (43), tarInvalidate (44); libds34bt/usb, xparam, cheatman, extern_irx, OPLDIAG fntsys/delaythread are also unused.
- Fix: Trim each file's includes to the headers whose symbols it uses and drop the stale comments; do the same in the sibling split files when their readers confirm.
- Benefit: ~25 lines in guiwait.cpp and ~30 in menuitems.cpp; no ELF change (headers only), shorter compile and fewer false dependencies (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-099: fntFitString duplicates fntWrapText's word wrap; theme text could use fntWrapText

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/gfx/fntsys.cpp:1135-1195`; `RiptOPL/src/gfx/fntsys.cpp:1049-1133`; `RiptOPL/include/fntsys.h:65-68`; `RiptOPL/src/gfx/themeelems.cpp:281-282`; `RiptOPL/src/gfx/themeelems.cpp:573-582`
- Problem: Two word-wrap implementations live side by side: fntFitString (in place, used only by themeelems.cpp:282 and :579, where 579 already wraps a private copy) and fntWrapText (out of place, used by dia.cpp and guimodal.cpp, handles hard breaks, over-wide words and CJK/Thai/Lao). The theme path lacks the CJK handling the newer helper has.
- Fix: Delete fntFitString and its prototype; at the two themeelems.cpp sites wrap with fntWrapText into a buffer sized for the inserted breaks (e.g. 2x the source length), keeping the existing private-copy ownership at 579. Update .github/scripts/test_font_fit_bounds.py.
- Benefit: ~55 net lines removed (61-line function and 4-line prototype minus a few lines at the call sites); one wrap algorithm for all text (INFERRED).
- Effort: S. Risk: Med. Confidence: Med (READ)

#### RIPTOPL-100: NIC-reserved predicate, peek-then-recv rule, hostent and deadline check written up to three times in nettime, ratls_ps2 and ranet

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/nettime.cpp:30-38`; `RiptOPL/src/sources/nettime.cpp:72-76`; `RiptOPL/src/sources/nettime.cpp:108-120`; `RiptOPL/src/sources/nettime.cpp:138-140`; `RiptOPL/src/sources/ratls_ps2.cpp:39-48`; `RiptOPL/src/sources/ratls_ps2.cpp:137-172`; `RiptOPL/src/sources/ratls_ps2.cpp:260-264`; `RiptOPL/src/sources/ranet.cpp:171-174`; `RiptOPL/src/sources/ranet.cpp:206-209`; `RiptOPL/src/sources/ranet.cpp:227-231`; `RiptOPL/src/sources/ranet.cpp:1072-1080`; `RiptOPL/src/sources/ranet.cpp:1110`
- Problem: The 'UDP transport owns or is chosen for the NIC' expression exists in ranet.cpp:229-230, ratls_ps2.cpp:151-152 and nettime.cpp:138-139; the peek/shorten-to-48-byte-tail recv in ranet.cpp:1074-1079, ratls_ps2.cpp:158-163 and inline in nettime.cpp:110-119; struct hostent twice; the elapsed-clock check three times. ratls_ps2 carries an #ifdef split (137-165) only because ranet.cpp is RA-only, and ratlsNicBusy -> ratlsNicReserved -> raNetNicReserved -> Ranet::nicReserved is four hops.
- Fix: Define netNicReserved(), netTcpRecv() and the hostent struct once in an always-compiled network file (nettime.cpp or ethsupport.cpp, with prototypes in its header). Make Ranet::nicReserved use it, delete Ranet::tcpRecv, raNetTcpRecv, raNetNicReserved, the ratls_ps2 #ifdef block and ratlsNicBusy (selfupdate.cpp calls netNicReserved), and use netTcpRecv in nettime's loop.
- Justification: The same three helpers are already written two or three times; moving one copy to a file every build compiles deletes the others and the RA/non-RA #ifdef split, so code shrinks overall.
- Benefit: ~50 lines removed (INFERRED: ratls_ps2 137-165 and 260-264, one hostent, ranet forwarders and tcpRecv, nettime inline recv). Small .text saving in RA/updater ELFs (INFERRED, not measured).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-101: Nine hdd* free functions only forward to identical HddSupport methods used nowhere else

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/sources/hddsupport.cpp:3232-3240`; `RiptOPL/src/sources/hddsupport.cpp:3255-3273`; `RiptOPL/src/sources/hddsupport.cpp:3285-3293`; `RiptOPL/src/sources/hddsupport.cpp:3300-3303`; `RiptOPL/include/hddint.h:38-39`; `RiptOPL/include/hddint.h:50-55`; `RiptOPL/include/hddint.h:61`
- Problem: hddLoadModules, hddDiagLoadModulesReady, hddVcdInvalidateCache, hddGetLiveOplHomeSelection, hddGetOplHomeSelection, hddResolveLiveOplDataPath, hddOplHomeSelectionNeedsTargetSave, hddMountSelectedOplHome and hddCommitOplHomeSelection are one-line forwarders to methods of the single gHddSupport instance. HddSupport (include/hddint.h) is included only by hddsupport.cpp, all its data members are public, and the hddint.h comments ('was hddXxx') show the methods are the old free functions moved into the class, so each operation now exists twice. The file even mixes both spellings (hddGetLiveOplHomeSelection() at 2085/2098, gHddSupport.getLiveOplHomeSelection() at 2114).
- Fix: Put each body back into its exported hdd* function (prefixing member accesses with gHddSupport.) and delete the matching method declaration and definition, keeping methods only where there is no exported twin. Update the .github/scripts source-scraping tests that stub these methods.
- Benefit: ~9 forwarders x 4-5 lines + 9 declarations = ~50 source lines and one indirection layer; ELF change small (INFERRED: forwarders may already be inlined)
- Effort: M. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-102: Kerning code in renderString, renderSubRTL and calcDimensionsLocked never runs: `previous` starts at 0 and is only set inside `if (... && previous)`

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/gfx/fntsys.cpp:753-755`; `RiptOPL/src/gfx/fntsys.cpp:775-783`; `RiptOPL/src/gfx/fntsys.cpp:837-844`; `RiptOPL/src/gfx/fntsys.cpp:887-889`; `RiptOPL/src/gfx/fntsys.cpp:907-916`; `RiptOPL/src/gfx/fntsys.cpp:946-948`; `RiptOPL/src/gfx/fntsys.cpp:1227-1229`; `RiptOPL/src/gfx/fntsys.cpp:1244-1252`; `RiptOPL/include/fontsystem.h:97-99`
- Problem: Each loop sets previous = 0 and guards the kerning block with `use_kerning && previous`, and the only assignment to previous is inside that block, so the condition is always false and FT_Get_Kerning is never called. Kerning was never applied (an inherited OPL bug). Draw and measure agree only because neither one kerns. The built-in PoeVeticaNew.ttf has no 'kern' table anyway.
- Fix: Delete the kerning blocks, the use_kerning/previous/glyph_index/delta state (GlyphRun fields and calcDimensionsLocked locals) and delta_x in the RTL renderString, plus the 'so kerning stays exact' remark at fntsys.cpp:1085. This keeps the current rendering exactly.
- Benefit: ~45 lines removed; no rendering change, because the code never executes (READ). FT_Get_Kerning may be dropped from the link (INFERRED, small).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-103: Ember launch preflight copied in four launchCue legs (ETH, BDM, MMCE, UDPFS) plus an HDD variant

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/ethsupport.cpp:1058-1077`; `RiptOPL/src/sources/bdmlaunch.cpp:67-100`; `RiptOPL/src/sources/mmcesupport.cpp:1195-1231`; `RiptOPL/src/sources/udpfssupport.cpp:837-857`; `RiptOPL/src/sources/hddsupport.cpp:2199-2222`
- Problem: Each leg repeats the same sequence with the same messages: cueNameLaunchable -> _STR_EMBER_BAD_NAME, cueResolveEmber -> _STR_EMBER_NOT_FOUND, cueResolveEmberBios -> _STR_EMBER_BIOS_MISSING, cuePrepareLaunch, cueApplySettings, cueGameHasImage -> _STR_EMBER_NO_DISC, then cuePrepareRetroGemBarcode. Only the prefix and (for MMCE/BDM-SDC) an SIO2 quiesce differ; HDD has its own error cleanup.
- Fix: Add one cuesupport function (e.g. cueLaunchPreflight(prefix, name, configSet, emberElf, launchName)) that performs the check sequence with its messages and returns 1/0; call it from the ETH, BDM, MMCE and UDPFS legs, keeping their device-specific steps around it. Leave HDD as is or have it call the helper and do its restore on 0. Update test_ember_disc_selection.py:186-204, which asserts the order inside each leg.
- Justification: The same ~18-line sequence is written four times; one ~22-line helper replaces ~70 lines, and a future change to the launch checks (as the BIOS check and image probe were added) happens once instead of five times.
- Benefit: ~4 x 18 lines replaced by 4 calls + ~22-line helper = ~45 lines net (INFERRED from cited ranges).
- Effort: M. Risk: Med. Confidence: High (READ)

#### RIPTOPL-104: ArtIndex: single-instance class in a private header behind five C forwarders, with its docs repeated in two headers

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/gfx/artindex.cpp:23-48`; `RiptOPL/include/artindexint.h:1-97`; `RiptOPL/include/artindex.h:33-63`
- Problem: gArtIndex is the only ArtIndex; artindexint.h is included only by artindex.cpp (and named by a source-text test), and the five artIndex* functions only forward to it. The API is documented twice (artindex.h and artindexint.h:12-23).
- Fix: Keep the C API in artindex.h and make the state file-static in artindex.cpp (functions operate on it directly); delete artindexint.h and the forwarders, and update .github/scripts/test_ripps2_art_port.py:37.
- Benefit: ~45 lines (98-line header and 24 lines of forwarders become ~75 lines of file-static state); no ELF change expected (INFERRED)
- Effort: S. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-105: appscan.cpp carries ~45 unused includes copied from opl.cpp, with comments for code that is not in this file

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/sources/appscan.cpp:7-68`; `RiptOPL/src/ui/menuitems.cpp:43`; `RiptOPL/src/ui/menuitems.cpp:61`; `RiptOPL/src/app/lwnbdctl.cpp:44`; `RiptOPL/src/app/lwnbdctl.cpp:60`; `RiptOPL/src/app/autolaunch.cpp:43`; `RiptOPL/src/app/autolaunch.cpp:59`
- Problem: appscan.cpp (180 lines of code) includes 46 headers: gui, renderman, themes, textures, pad, dialogs, menusys, every device support header, tar, cheatman, sound, xparam, fileXio_rpc, sys/stat, libds34bt/usb and more. Its code needs only opl.h, oplint.h, sourceregistry.h, gamesource.h, ioman.h (LOG), config.h, util.h, appsupport.h and unistd. The comments claim uses that do not exist here (mkdir for mc?:/OPL, fileXioDopen for BDM autolaunch, mmceLoadModules for resolveBootDirToMass). The same copied block with the same stale comments sits in menuitems.cpp and lwnbdctl.cpp, which also contain no such calls.
- Fix: Delete the include lines appscan.cpp does not need and the stale per-include comments; keep the eight or so headers listed. Do the same trim in the other split-out files that kept the opl.cpp block (each in its own unit).
- Benefit: ~45 lines removed from appscan.cpp, less rebuild coupling (every header change recompiles it); no ELF change (INFERRED)
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-106: FontSystem class has a single static instance and the fnt* API is ten one-line forwarders to it

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/gfx/fntsys.cpp:1260-1324`; `RiptOPL/include/fontsystem.h:24-41`
- Problem: Only fntsys.cpp includes fontsystem.h; the one object is static FontSystem gFonts (1262), and fntInit, fntEnd, fntLoadFile, fntLoadFallback, fntLoadDefault, fntRelease, fntSetWeight, fntUpdateAspectRatio, fntRenderString and fntCalcDimensions only call the matching method. The class adds a public-method layer, copy-deletes and a constexpr constructor for an object nothing else can see.
- Fix: Make the FontSystem members file-static state and the methods static functions in fntsys.cpp named as the public fnt* functions, deleting the forwarders and the public part of the class; keep fontsystem.h only if the private types are wanted in a header (or fold them into fntsys.cpp). Update test_cjk_font_fallback.py, which reads fontsystem.h.
- Benefit: ~45 lines removed (forwarders and public declarations), INFERRED; no ELF change expected (calls are already direct after inlining or nearly so).
- Effort: M. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-107: VcdSupport class is a single-instance wrapper behind a parallel set of forwarding free functions

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/include/vcdsupport.h:309-364`; `RiptOPL/src/sources/vcdsupport.cpp:353-356`; `RiptOPL/src/sources/vcdsupport.cpp:499-502`; `RiptOPL/src/sources/vcdsupport.cpp:583-591`; `RiptOPL/src/sources/vcdsupport.cpp:616-619`; `RiptOPL/src/sources/vcdsupport.cpp:1674-1682`; `RiptOPL/src/sources/cuesupport.cpp:1090`
- Problem: Every external caller uses the free functions (vcdInvalidateGameIds, vcdRequestDisplayId, ps1RowIdRequest, ps1RowIdCached, vcdEquipBdma, vcdEquipBdmaFromPops) that only forward to the one global gVcdSupport; the only outside use of the class is cuesupport.cpp:1090 noteScanDir. The class adds a 56-line header block, a static thunk for ioPutRequest and six one-line forwarders, and exposes private state (memo head, request slot, custom-path cache) in a public header.
- Fix: Delete the class from vcdsupport.h. Move its fields into vcdsupport.cpp as file-static variables, make the methods file-static functions (resolveQueuedDisplayId can then be passed to ioPutRequest directly, no thunk), let the existing free functions carry the bodies, and add one free vcdNoteScanDir prototype for cuesupport.cpp. Update the source-text tests that name gVcdSupport/VcdSupport::equipBdma.
- Benefit: ~45 net source lines removed (56-line header block and ~10 forwarder/thunk lines, minus ~8 lines of statics and one prototype); no ELF change expected beyond the 12-byte thunk (INFERRED).
- Effort: M. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-108: igs_api.c: five number formatters, three decimal and two hex copies with identical bodies

- Category: 2 (Duplication)
- Location(s): `RiptOPL/ee_core/src/igs_api.c:79-152`
- Problem: u8todecstr, u16todecstr and u32todecstr (79-122) are the same 14-line body with only the parameter type changed; u32tohexstr and u64tohexstr (124-152) likewise. Every u8/u16 call promotes cleanly to u32, and the single u32tohexstr call (546) to u64.
- Fix: Keep one decimal formatter taking u32 and one hex formatter taking u64; delete the other three and point the calls at the kept ones.
- Benefit: ~44 lines removed; three small static functions dropped from the extras eecore (INFERRED, a few hundred bytes at most)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-109: thmLoad re-zeroes about 40 fields right after memset(newT, 0), and sets loadingIconCount only to overwrite it

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/gfx/themes.cpp:1366`; `RiptOPL/src/gfx/themes.cpp:1371-1396`; `RiptOPL/src/gfx/themes.cpp:1399-1410`; `RiptOPL/src/gfx/themes.cpp:1679-1681`; `RiptOPL/src/gfx/themes.cpp:1690`; `RiptOPL/src/gfx/themes.cpp:1712`; `RiptOPL/src/gfx/themes.cpp:1768`; `RiptOPL/src/gfx/themes.cpp:1770`
- Problem: newT is memset to 0 at 1366, then 24 first/last pointers, parsingFavKindFamily, gameCacheCount, wallpaper, five items-list pointers, coverflow/psbbnFlow/grid, coverflowCoverOffset, loadingIcon, every textures[].Mem (1680-1681), logoFrameCount and hintsHide/hintsColumn are stored as 0/NULL again. loadingIconCount at 1410 is overwritten unconditionally at 1712 and nothing reads it in between (only reader is guioverlay.cpp:60).
- Fix: Delete the zero/NULL stores at 1371-1396, 1399-1409, 1679-1681, 1690, 1768, 1770 and the dead store at 1410; keep the non-zero defaults (useDefault, usedHeight, artGeneration, wallpaperWanted, hintsLabels, hintsSpacing).
- Benefit: ~44 source lines removed (counted from the cited ranges); a few dozen bytes of .text (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-110: CueSupport and DiscSupport are wrapper classes around free functions; DiscSupport::launch is declared but never defined

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/sources/cuesupport.cpp:33-35`; `RiptOPL/src/sources/cuesupport.cpp:912-924`; `RiptOPL/src/sources/cuesupport.cpp:956-959`; `RiptOPL/include/cuesupport.h:220-235`; `RiptOPL/src/sources/discsupport.cpp:24-41`; `RiptOPL/src/sources/discsupport.cpp:80-82`; `RiptOPL/src/sources/discsupport.cpp:138-151`
- Problem: CueSupport is stateless with one method, gameHasImage, reached only through the cueGameHasImage forwarder; gCueSupport has no other use. DiscSupport holds one volatile int, its methods are reached only through discCheckBusy/discCheckSupportDeferred and a discCheckWorker trampoline, and it declares launch() (line 34) which has no definition and no caller (discLaunch is a free function).
- Fix: Put the gameHasImage body into cueGameHasImage and delete CueSupport, gCueSupport and the header comment/class. In discsupport.cpp replace the class with a file-static `volatile int discBusy` and plain functions (discCheckBusy, discCheckSupportDeferred, static discCheckWorker), deleting the forwarders and the undefined launch() declaration.
- Benefit: ~22 lines in cuesupport + ~20 in discsupport removed (INFERRED from cited ranges); no measurable ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-111: SourceRegistry class and two forwarders wrap one plain array

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/include/sourceregistry.h:1-28`; `RiptOPL/src/app/opl.cpp:128-157`; `RiptOPL/src/sources/appscan.cpp:76-82`
- Problem: The page table is a class with one instance (gSourceRegistry) whose only members return &modules_[mode], the array base, and appImageByMode. opl.cpp then re-exposes it as a list_support pointer (132), forwards module() through oplGetModule (138-141), and appscan.cpp forwards oplGetAppImageByMode to the member (79-82) whose body lives back in opl.cpp (145-157). Its only callers are appsupport.cpp:1389,1391. Nothing else uses SourceRegistry or gSourceRegistry.
- Fix: Delete include/sourceregistry.h and the class. In opl.cpp declare a plain static opl_io_module_t list_support[MODE_COUNT] (the name test_io_queue_trims.py needs), let oplGetModule return &list_support[mode], and make the appImageByMode body the definition of oplGetAppImageByMode in opl.cpp. Delete the forwarder and the include in appscan.cpp. Update docs/CPP-CONVERSION-PLAN.md section 22.
- Benefit: ~40 source lines and one header removed (header 28 lines, opl.cpp ~6, appscan.cpp ~7); one call level gone on the per-cover app art lookup; no measurable ELF change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-112: RaWatch class with one instance plus eight free forwarders: two APIs for one global

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/sources/rawatch.cpp:32-72`; `RiptOPL/include/rawatch.h:7-63`
- Problem: gRaWatch is referenced only inside rawatch.cpp (grep: rawatch.h:49 extern, rawatch.cpp:32-71); every other file uses the eight free functions LoadWatchListFile..ClearWatchList, each a one-line forward (rawatch.h:9-10 says they exist so callers do not change). The class adds inline accessors that the forwarders merely wrap.
- Fix: Keep one API: either make the data file-static and turn the free functions into the implementations (delete the class and gRaWatch), or keep the class and call gRaWatch directly from the few callers. Update test_ra_runtime_budget.py, which instantiates RaWatch.
- Benefit: ~40 source lines removed (39 lines of forwarders or the class declaration); calls are tiny non-static functions, ELF change a few hundred bytes at most (INFERRED)
- Effort: M. Risk: Low. Confidence: High (READ)

#### RIPTOPL-113: Freeze report has two hand-written formatter sets (put_* for the card file, ln_* for the TV page) and the overrun threshold twice

- Category: 2 (Duplication)
- Location(s): `RiptOPL/ee_core/src/freeze_diag_format.c:16-49`; `RiptOPL/ee_core/src/freeze_diag_format.c:303-325`; `RiptOPL/ee_core/src/freeze_diag_screen.c:36-94`; `RiptOPL/ee_core/include/freeze_diag_screen.h:14-15`
- Problem: freeze_diag_screen.c re-implements put_str/put_dec/put_hex/put_where of freeze_diag_format.c as ln_str/ln_dec/ln_hex/ln_where (same digit loop, same module-name+offset search, only the buffer struct and the hex width differ). HEAP_DIAG_OVERRUN_MIN (format.c:325) and FDS_OVERRUN_MIN (screen.h:15) are the same 64-byte rule, whose comment says they must agree. The two files go into different binaries (freezediag.irx and ee_core), so the saving is source only.
- Fix: Move the buffer helpers (with a hex width argument, 0 = minimal, no prefix) and one overrun constant into a header-only freeze_diag_report.h section used by both files; delete the ln_* copies and the second constant. Both host tests (ee_core/tests) cover the result.
- Benefit: ~40 source lines net; no binary change in either ELF/IRX (INFERRED: each binary keeps one copy)
- Effort: M. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-114: pad.cpp in-file duplication: two identical deadzone switches, the actAlign setup twice, and two copies of the pulse-arm assignments in rumble()

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/core/pad.cpp:317-349`; `RiptOPL/src/core/pad.cpp:279-284`; `RiptOPL/src/core/pad.cpp:613-618`; `RiptOPL/src/core/pad.cpp:736-752`
- Problem: readLeftJoy has two 16-line switches mapping gXSensitivity and gYSensitivity to the same {128,100,80,60} deadzones (default 100). initialize() and rumbleRealign() fill actAlign[0..5] with the same six assignments. rumble() sets rumbleStartTicks_, rumbleDurTicks_ and rumbleLarge_ in both the active and the new-pulse branch.
- Fix: Use one static const deadzone table indexed by the clamped setting for both axes; let initialize() set the alignment through the same few lines rumbleRealign uses (or a 6-byte static const array copied with memcpy); in rumble() assign start, duration and level once and keep only the branch-specific repeat/active updates.
- Benefit: ~38 lines removed (switches 33 -> ~4, actAlign 6, rumble ~5); negligible .text change (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-115: sendIrxKernelRAM repeats the two-line module-table append 37 times, and the UDPFS chains duplicate smap+ministack

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/platform/system.cpp:857-1013`; `RiptOPL/src/platform/system.cpp:917-934`
- Problem: Every module goes into irxptr_tab with the same pair 'irxptr_tab[modcount].info = size_X | SET_OPL_MOD_ID(ID); irxptr_tab[modcount++].ptr = (void *)&X;' (37 occurrences). The BDM_UFS and UDPFS_FILES branches each append the same udpfs_smap and udpfs_ministack pair before their one differing module.
- Fix: Add one function-local macro (or static inline) that appends {size, id, ptr} and use it for every row; merge the two UDPFS branches into one block guarded by (CORE_IRX_UDPFS | CORE_IRX_UDPFS_FILES) that appends smap+ministack and then udpfs_bd or udpfs_fhi.
- Justification: The helper is one line and replaces 37 two-line pairs with one-line calls, a net removal of ~35 lines, and removes the chance of a pair whose info and ptr name different modules.
- Benefit: ~38 lines removed net; code size roughly unchanged (INFERRED)
- Effort: S. Risk: Med. Confidence: Med (READ)

#### RIPTOPL-116: Neutrino fragBudgetOk isUdp parameter is always 0, so _STR_NEUTRINO_TOO_FRAGMENTED_NET and its 34 language entries are dead

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/sources/bdmlaunch.cpp:271`; `RiptOPL/src/sources/bdmlaunch.cpp:344-347`; `RiptOPL/src/sources/bdmlaunch.cpp:441-444`; `RiptOPL/include/neutrinolauncher.h:33`; `RiptOPL/lng/tmpl/_base.yml:988-989`
- Problem: The only caller passes 0 (bdmlaunch.cpp:444; the comment at 441-443 says udp deliberately takes the local message now), so the isUdp ? _STR_NEUTRINO_TOO_FRAGMENTED_NET branch at 347 never runs. That string id has no other user in the code, yet it is carried in _base.yml and 33 lng/fork language files.
- Fix: Drop the isUdp parameter and always use _STR_NEUTRINO_TOO_FRAGMENTED; delete the NEUTRINO_TOO_FRAGMENTED_NET label from lng/tmpl/_base.yml and the lng/fork/*.yml files; update test_issues_830_831.py's budget() helper.
- Benefit: ~37 lines removed (2 code, 2 in _base.yml, 33 fork lang lines) and one string out of every .lng and the internal language table (INFERRED, ~110 bytes of English text)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-117: GameIdBarcode class and private header for one call site; show() repeats its frame loop twice; serial length capped twice

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/include/guigameid.h:1-27`; `RiptOPL/src/ui/guigameid.cpp:73-74`; `RiptOPL/src/ui/guigameid.cpp:96-98`; `RiptOPL/src/ui/guigameid.cpp:116-132`; `RiptOPL/src/ui/guigameid.cpp:157-163`; `RiptOPL/src/ui/guigameid.cpp:197-212`
- Problem: GameIdBarcode (constructor, valid, draw, show, two statics) is used only by guiShowGameID and guigameid.h is included only by guigameid.cpp. show() has two loops that differ only by the draw() call. buildPacket re-caps the length at 11 that cleanSerial already capped (88-89), and cleanSerial's NULL check is unreachable because guiShowGameID returns on NULL/empty first (207).
- Fix: Make cleanSerial/buildPacket/draw file-static functions working on a local u8[32], delete guigameid.h and the class, merge the two loops into one of GAMEID_HOLD_FRAMES + 2 frames that calls draw only while frame < GAMEID_HOLD_FRAMES, and drop the redundant cap and NULL check.
- Benefit: ~35 lines removed (27-line header, class glue, one loop, two checks); output frames identical; negligible ELF change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-118: Kerning code in renderString/calcDimensionsLocked can never run: previous starts at 0 and is only assigned inside if (previous)

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/gfx/fntsys.cpp:753-755`; `RiptOPL/src/gfx/fntsys.cpp:775-783`; `RiptOPL/src/gfx/fntsys.cpp:837-844`; `RiptOPL/src/gfx/fntsys.cpp:887-889`; `RiptOPL/src/gfx/fntsys.cpp:907-916`; `RiptOPL/src/gfx/fntsys.cpp:1227-1229`; `RiptOPL/src/gfx/fntsys.cpp:1244-1252`; `RiptOPL/include/fontsystem.h:97-99`
- Problem: Each loop sets previous = 0 (755, 889, 1228) and the only assignment previous = glyph_index sits inside if (use_kerning && previous) (782, 843, 915, 1251), so the condition is always false: FT_Get_Kerning is never called and kerning is never applied. The blocks and the GlyphRun fields use_kerning, glyph_index, previous and delta are dead (inherited from upstream OPL).
- Fix: Delete the kerning blocks, the use_kerning/previous setup and the unused GlyphRun fields (and delta_x in the __RTL body if finding 1 is not taken). Rendering is unchanged because the code never runs.
- Benefit: ~35 lines removed (INFERRED from cited ranges); small code-size drop per loop, not measured.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-119: SourceRegistry is a class wrapper around one array, reached through two forwarders; its 'no dynamic init' claim is false

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/include/sourceregistry.h:1-28`; `RiptOPL/src/app/opl.cpp:128-157`; `RiptOPL/src/sources/appscan.cpp:10`; `RiptOPL/src/sources/appscan.cpp:76-82`; `RiptOPL/include/opl.h:555-557`
- Problem: SourceRegistry only holds opl_io_module_t modules_[MODE_COUNT] with module()/table() accessors; opl.cpp then re-exposes it as a list_support pointer and an oplGetModule forwarder, and appscan.cpp's oplGetAppImageByMode only forwards to SourceRegistry::appImageByMode in opl.cpp. Only opl.cpp and appscan.cpp include the header. The comments (sourceregistry.h:8-10, opl.cpp:128-131) say it is constant-initialized, but nm shows _GLOBAL__sub_I_gSourceRegistry: list_support = gSourceRegistry.table() runs as a dynamic initializer. opl.h:555-556 still talks of a file-static list_support[] and favsupport.c.
- Fix: Make it 'static opl_io_module_t list_support[MODE_COUNT];' in opl.cpp (menuDeferredUpdate's text, which test_io_queue_trims.py extracts, keeps saying list_support[i]); have oplGetModule return &list_support[mode]; move the appImageByMode body into oplGetAppImageByMode in opl.cpp and delete the appscan.cpp forwarder and include/sourceregistry.h; fix the opl.h comment.
- Benefit: ~35 lines and one header removed; drops a 20 B static initializer (MEASURED: _GLOBAL__sub_I_gSourceRegistry 0x14 in nm) and a call hop per art lookup.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-120: Stale comments in textures.cpp contradict the code they describe (absence rule, SEEK_END ban, OE meaning)

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/gfx/textures.cpp:495-525`; `RiptOPL/src/gfx/textures.cpp:661-667`; `RiptOPL/include/textures.h:222-223`; `RiptOPL/src/ui/guioverlay.cpp:179-182`
- Problem: The 31-line block before gTexStagedOpenNonEnoent says the absence check returns 1 unconditionally and should 'match master' if OE climbs, but texStagedOpenIsAbsence (586-610) already returns err==ENOENT plus FatFs 4/5/6 for mass*, and counts non-absence on every device, not 'non-MMCE'. The block at 661-667 says 'Do NOT size the file with lseek(SEEK_END) first' directly under the fast path (639-641) that does exactly that for non-MMCE devices. The header and HUD comments repeat the outdated OE meaning.
- Fix: Delete 495-525 except one line stating what gTexStagedOpenNonEnoent counts; reduce 661-667 to 'MMCE cannot SEEK_END: grow the buffer while reading'; reword textures.h:222-223 and guioverlay.cpp:179-182 to the current rule.
- Benefit: ~35 comment lines removed; no ELF change (MEASURED from cited ranges: 31 + 7 lines, minus ~3 replacement lines)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-121: Three copies of the alloc/SIF-DMA/LoadModuleBuffer/free block for IOP patch modules in patches.c

- Category: 2 (Duplication)
- Location(s): `RiptOPL/ee_core/src/patches.c:553-575`; `RiptOPL/ee_core/src/patches.c:674-694`; `RiptOPL/ee_core/src/patches.c:836-856`; `RiptOPL/ee_core/src/util.c:132-149`
- Problem: SOS_SifLoadModuleHook, UltProPinball_SifLoadModuleHook and ShadowMan2_SifLoadModuleHook each repeat the same ~20 lines: GetOPLModInfo, game AllocIopHeap, a hand-built SifDmaTransfer_t retried until SetDma succeeds, LoadModuleBuffer retried until >= 0, FreeIopHeap, break on alloc failure. The DMA part re-implements CopyToIop (util.c:132), which also waits for the DMA to finish before the module is loaded; these copies do not wait.
- Fix: Add one static helper in patches.c that takes the game's alloc/free/loadbuffer pointers plus the argument string, uses CopyToIop for the transfer, and returns the load result; call it from the three hooks and drop their local sifdma/dma_id variables.
- Justification: The same block exists three times; one helper of ~15 lines replaces ~60 and reuses the existing CopyToIop instead of a third and fourth DMA loop.
- Benefit: ~35 lines removed net; eecore links without --gc-sections, so the shared code shrinks .text by roughly two block copies, ~150 bytes (INFERRED).
- Effort: S. Risk: Med. Confidence: High (READ)

#### RIPTOPL-122: checkMC always opens four directories even when the first answers the question

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/platform/util.cpp:55-110`
- Problem: checkMC opendirs mc0:/, mc1:/, mc0:OPL/ and mc1:OPL/ unconditionally, stores four flags and only then picks in the order mc0:OPL, mc1:OPL, mc0, mc1. Each opendir is a card access on the IO path, and while no card is found mcID stays -1 so every later call (openFile on an mc? path, checkMCFolder) repeats all four.
- Fix: Probe in priority order and return on the first hit: mc0:OPL/, mc1:OPL/, mc0:/, mc1:/, using one small static exists test instead of four copied opendir/closedir blocks.
- Benefit: ~35 lines removed (56-line function to about 20); up to three fewer memory-card opendirs on the first probe (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-123: malloc followed by field-by-field zeroing instead of calloc in four init functions

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/gfx/themeelems.cpp:211-217`; `RiptOPL/src/gfx/themeelems.cpp:810-829`; `RiptOPL/src/gfx/themeelems.cpp:1608-1622`; `RiptOPL/src/gfx/themeelems.cpp:2123-2141`; `RiptOPL/src/gfx/themeelems.cpp:703-704`; `RiptOPL/src/gfx/themeelems.cpp:767-768`
- Problem: initMutableText, initMutableImage, initBasic and initItemsList malloc a struct and then zero or NULL most fields one by one (initBasic even comments that it must, 1614); initImageTexture/initImageInternalTexture set name=NULL right after calloc. About 33 of these lines only write 0/NULL.
- Fix: Use calloc in the four init functions and delete every explicit 0/NULL store; keep only the non-zero defaults (-1 cache ids, currentCount -1, showRun 1, endElem). Delete the two 'texture->name = NULL' lines after calloc.
- Benefit: ~33 source lines removed and fewer stores in the generated code (~100 bytes .text, INFERRED); new fields can no longer be left uninitialised by accident.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-124: ioGetPendingRequestCount/IoQueue::pendingCount and IoQueue::total/totalByType_ are never used

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/ioman.h:42-43`; `RiptOPL/include/ioman.h:48-52`; `RiptOPL/src/platform/ioman.cpp:55-58`; `RiptOPL/src/platform/ioman.cpp:282`; `RiptOPL/src/platform/ioman.cpp:342-358`; `RiptOPL/src/platform/ioman.cpp:432-435`; `RiptOPL/include/ioqueue.h:30`; `RiptOPL/include/ioqueue.h:33`; `RiptOPL/include/ioqueue.h:85`; `RiptOPL/.github/scripts/test_io_menu_lifetime.py:16`
- Problem: ioGetPendingRequestCount (and the IoQueue::pendingCount it wraps) has no caller; comments at ioman.cpp:30 and artload.cpp:151 record that its one user was removed because it froze the menu. IoQueue::total() has no caller and totalByType_ is only incremented (ioman.cpp:282); only ioGetPending (per-type pending) feeds the HUD. ioman.h:48-52 still advertises a 'running total'.
- Fix: Delete ioGetPendingRequestCount, IoQueue::pendingCount, IoQueue::total, totalByType_ and its increment, their declarations, and the 'running total' wording in ioman.h; drop the pendingCount half of test_io_menu_lifetime.py.
- Benefit: ~32 source lines; 232 bytes of .text already dropped by --gc-sections (MEASURED raw/riptopl-discarded-firstparty.txt: 96+96+40), so ELF change is only the 16-byte totalByType_ array (INFERRED: IO_REQ_TYPE_COUNT 4 x 4 bytes).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-125: retrogemCleanTitleID legacy serial parser only adds acceptance of trailing junk after the serial

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/sources/retrogem.cpp:131-132`; `RiptOPL/src/sources/retrogem.cpp:142-172`
- Problem: Every token the legacy branches accept (AAAA_NNN.NN or AAAA-NNNNN at the start of p) is already accepted by retrogemNormalizeGameID at 133-140 when the token ends there, since '_', '.', '-' are stripped and nine alnum characters remain. The legacy code therefore only changes the result for a serial followed by characters outside the strcspn stop set (for example SLUS_123.45X or a token of 32+ characters).
- Fix: Delete lines 142-172 and return 0 after the normalize attempt; drop the 'legacy prefix parser' wording at 131-132.
- Benefit: ~31 source lines removed; static code in the base ELF, roughly 300-400 bytes of .text (INFERRED, not measured)
- Effort: S. Risk: Med. Confidence: Med (READ)

#### RIPTOPL-126: COP0 Count reader and the 294.912 MHz tick constant are copied in five ee_core files

- Category: 2 (Duplication)
- Location(s): `RiptOPL/ee_core/src/freeze_diag.c:36-37`; `RiptOPL/ee_core/src/freeze_diag.c:53-60`; `RiptOPL/ee_core/src/freeze_diag_screen.c:268-276`; `RiptOPL/ee_core/src/igrmenu.c:698-707`; `RiptOPL/ee_core/src/igrmbox.c:24`; `RiptOPL/ee_core/src/igrmbox.c:36-42`; `RiptOPL/ee_core/src/ra.c:54-61`; `RiptOPL/ee_core/src/padhook.c:126`; `RiptOPL/ee_core/src/padhook.c:197-202`
- Problem: ee_ticks, fds_ticks, igrmCount, mbCount and ra_ticks are the same 'mfc0 %0, $9' static inline under five names, padhook.c inlines the asm twice more, and the clock constant appears four times (EE_TICKS_PER_MS twice in the two freeze_diag files that are always built together, IGRM_SECOND, MB_SECOND, IGR_SECOND).
- Fix: Put one static inline count reader and one ticks-per-second constant in an ee_core header every user already includes (for example util.h or ee_core.h; it must stay header-only because igrmenu.c/igrmbox.c are also built into igrovl.elf), and delete the copies.
- Benefit: ~30 source lines net; no ELF change (all copies are static inline, INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-127: COP0 Count reader and the 294912000 ticks-per-second constant are copied into six ee_core files

- Category: 2 (Duplication)
- Location(s): `RiptOPL/ee_core/src/igrmbox.c:24`; `RiptOPL/ee_core/src/igrmbox.c:36-42`; `RiptOPL/ee_core/src/igrmenu.c:698`; `RiptOPL/ee_core/src/igrmenu.c:701-707`; `RiptOPL/ee_core/src/padhook.c:126`; `RiptOPL/ee_core/src/padhook.c:197-202`; `RiptOPL/ee_core/src/freeze_diag.c:53-60`; `RiptOPL/ee_core/src/freeze_diag_screen.c:268-276`; `RiptOPL/ee_core/src/ra.c:54-61`
- Problem: The same static inline 'mfc0 %0, $9' wrapper exists as mbCount (igrmbox.c), igrmCount (igrmenu.c), ee_ticks (freeze_diag.c), fds_ticks (freeze_diag_screen.c), ra_ticks (ra.c) and twice open-coded in padhook.c IGR_WaitIdle; the clock constant is defined three times as MB_SECOND, IGRM_SECOND and IGR_SECOND (plus EE_TICKS_PER_MS). igrmbox.c and igrmenu.c are even linked into the same overlay (build_edition.py:161).
- Fix: Put one static inline Count reader and one ticks-per-second define in ee_core/include/ee_core.h (every one of these files already includes it) and delete the six local copies and the three local constants.
- Benefit: ~30 source lines net (five 7-8 line copies plus two open-coded reads and three constants, minus one 7-line helper); no ELF change, all copies are inlined (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-128: Concrete mc0:/mc1: boot handling in preferredMcSlot, tryMissingConfigPathRecovery and the tryAlternateDevice legacy fallback is unreachable

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/app/settingsio.cpp:239-249`; `RiptOPL/src/app/settingsio.cpp:255`; `RiptOPL/src/app/settingsio.cpp:856-865`; `RiptOPL/src/app/settingsio.cpp:1045-1052`; `RiptOPL/src/app/settingsio.cpp:1070-1075`
- Problem: tryAlternateDevice returns at 1012-1019 for every concrete MC boot, so tryMissingConfigPathRecovery (only caller 1023) and the legacy block at 1044-1081 never see gBootDir starting mc0:/mc1:; nothing in between rewrites gBootDir. preferredMcSlot's other caller, mcConfigPathRedirect, is only reached under gBootHomeDeferred (311, 360), which is set only for smb/udpfs boots (1398-1400). So preferredMcSlot always returns sysCheckMC(), the mc1-first swap is dead, and the concreteMcHome branch and the sameConcreteMcSlot(gBootDir, concreteMcHome) branch never run.
- Fix: Delete preferredMcSlot and call sysCheckMC() in mcConfigPathRedirect; use the fixed {0,1} slot order in tryMissingConfigPathRecovery and drop its stale comment; in the legacy fallback call configSetMove(NULL) directly and delete the concreteMcHome buffer and the sameConcreteMcSlot else-if (keep the gBootHomeDeferred / configSetMove(gBootDir) pair).
- Benefit: ~30 source lines and a few hundred bytes of live code removed (INFERRED: the branches sit inside reachable functions, so the compiler cannot drop them); the remaining recovery logic reads as what it does.
- Effort: S. Risk: Med. Confidence: High (READ)

#### RIPTOPL-129: Copy-paste per slot/group in validateBackgroundElems, isDecoratorCoverImage and the addGUIElem ItemsList branches

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/gfx/themes.cpp:100-118`; `RiptOPL/src/gfx/themes.cpp:221-243`; `RiptOPL/src/gfx/themes.cpp:731-753`
- Problem: validateBackgroundElems has two identical 7-line blocks differing only in mainElems/infoElems; isDecoratorCoverImage repeats one 5-line check for gamesItemsList, appsItemsList, favsItemsList and vcdItemsList; addGUIElem has four identical initBasic(42,42,400,360)+initItemsList pairs (filtered, apps, favs, vcd) that differ only in which slot pointer receives elem.
- Fix: validateBackgroundElems: one small static that prepends a default background to a family, called for main and (when non-empty) info. isDecoratorCoverImage: loop over a 4-entry array of the slot pointers. addGUIElem: pick the target slot (NULL for filtered or fav-kind lists, games keeps its own 0,0,DIM_UNDEF defaults) and build the list once.
- Benefit: ~30 source lines removed (INFERRED: 7 + 15 + 9); small .text saving.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-130: Dead and duplicated bits in patches.c: SOS PAL entry is a no-op, commented-out pSifLoadModule, identical DotHack and Eutechnyx branches

- Category: 1 (Dead code)
- Location(s): `RiptOPL/ee_core/src/patches.c:114`; `RiptOPL/ee_core/src/patches.c:535-540`; `RiptOPL/ee_core/src/patches.c:592-594`; `RiptOPL/ee_core/src/patches.c:798`; `RiptOPL/ee_core/src/patches.c:810`; `RiptOPL/ee_core/src/patches.c:816`; `RiptOPL/ee_core/src/patches.c:822`; `RiptOPL/ee_core/src/patches.c:828`; `RiptOPL/ee_core/src/patches.c:438-445`; `RiptOPL/ee_core/src/patches.c:728-758`
- Problem: SOSPatch case 2 (PAL) has its hook install commented out, so the SLES_513.01 table entry only sets g_mode and SOS_SifLoadModuleHook's case 2 can never run. ShadowMan2_SifLoadModuleHook carries five commented-out pSifLoadModule lines. DotHack_patches has two byte-identical branches for SLES_524.68 and SLES_524.69. EutechnyxWakeupTIDPatch repeats the same check-v1-then-v2 block for SLUS_215.80 and SLUS_205.82, differing only in the v2 address.
- Fix: Delete the SLES_513.01 entry, SOSPatch case 2 and the hook's case 2; delete the commented-out lines; merge the two DotHack branches with ||; pick the v2 address by GameID and keep one Eutechnyx block.
- Benefit: ~30 lines removed; a few dozen bytes of eecore .text/.rodata (no --gc-sections) (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-131: HttpSource overrides prefix, deleteGame and checkVmc with bodies identical to the GameSource defaults

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/sources/httpsupport.cpp:518`; `RiptOPL/src/sources/httpsupport.cpp:529`; `RiptOPL/src/sources/httpsupport.cpp:536`; `RiptOPL/src/sources/httpsupport.cpp:539-545`; `RiptOPL/src/sources/httpsupport.cpp:660-664`; `RiptOPL/src/sources/httpsupport.cpp:898-912`; `RiptOPL/src/sources/gamesource.cpp:24-27`
- Problem: GameSource::prefix returns NULL, GameSource::deleteGame is empty and GameSource::checkVmc returns -1 (gamesource.cpp:24,25,27; virtual, not pure, gamesource.h:35,44,52). HttpSource's overrides do exactly the same thing, so the three overrides and their declarations add code and vtable entries for no behaviour.
- Fix: Delete the three override declarations and the three definitions (keep rename, which is pure virtual). Move the one useful sentence of the checkVmc comment (no VMC over HTTP) next to the class if wanted.
- Benefit: ~30 source lines removed; three small functions dropped from the ELF (they are vtable-referenced so --gc-sections keeps them today), well under 100 bytes (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-132: On-screen keyboard repeats insert, backspace, enter and mode-switch code; numpad repeats backspace

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/ui/diaeditors.cpp:29`; `RiptOPL/src/ui/diaeditors.cpp:346-429`; `RiptOPL/src/ui/diaeditors.cpp:524-528`; `RiptOPL/src/ui/diaeditors.cpp:547-551`
- Problem: In diaShowKeyb the caret insert (memmove, store, mask update, len++, caret++) is written three times (351-359, 372-380, 407-415), backspace twice (362-368, 396-403), ENTER with free(mask_buffer) twice plus once more at the exit (383-386, 418-421, 438-439) and the mode switch twice through a selkeyb counter and KEYB_MODE (389-393, 423-428). diaShowNumPad's '<' key repeats its Square backspace.
- Fix: Use two local lambdas (insert a char at the caret, delete before the caret) and leave the loop through one exit that frees mask_buffer; toggle the layout with keyb = (keyb == keyb0) ? keyb1 : keyb0 and delete selkeyb and KEYB_MODE; share the numpad backspace branch.
- Benefit: ~30 lines removed (INFERRED from the cited ranges)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-133: UDPBD/UDPFS network block chain load is written twice (ensureNetworkSourceModules and bdmLoadBlockDeviceModules)

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/bdmsupport.cpp:781-807`; `RiptOPL/src/sources/bdmsupport.cpp:1326-1369`
- Problem: Both blocks do the same sequence: sysInitDev9, build the 'ip=' arg from ps2_ip, load UDPFS_SMAP -> UDPFS_MINISTACK(args) -> UDPFS_BD or SMAP_UDPBD(args), set udpbdNicClaimed/udpbdModLoaded/loaded protocol, and call sysShutdownDev9 when the NIC was not claimed. Inside each copy the NicClaimed + protocol latch is also set twice on the UDPFS success path (789-792 and 801-804; 1340-1343 and 1348-1352).
- Fix: Keep one static helper (caller holds bdmLoadModuleLock) that takes the protocol, performs the DEV9 init, the chain load, the latches and the DEV9 release, and returns the result; call it from ensureNetworkSourceModules and from bdmLoadBlockDeviceModules. Drop the duplicated second latch assignment inside the UDPFS branch.
- Justification: The helper replaces two existing ~30-line copies of the same load order and latch rules (which must stay in sync for the NIC interlock), so it removes code overall.
- Benefit: ~30 source lines removed (two 27-44 line copies become one ~25-line helper plus two calls) (INFERRED); a few hundred bytes of .text (INFERRED, duplicated call sequences).
- Effort: S. Risk: Med. Confidence: High (READ)

#### RIPTOPL-134: rahash.cpp: the MD5(name || extent) chunk loop is written three times

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/rahash.cpp:525-547`; `RiptOPL/src/sources/rahash.cpp:634-649`; `RiptOPL/src/sources/rahash.cpp:799-818`
- Problem: hashBootExec and hashPsx carry the same 20-line block (md5_init, append name, read RA_HASH_CHUNK pieces through readAt into g_chunk, md5_finish, md5_hex); hashDisc repeats it a third time over a plain fd, which is exactly what readAt does for an RA_SRC_ISO source (lseek64 + read).
- Fix: Keep one private helper md5Extent(src, name, off, size, out33) with the loop, call it from hashBootExec and hashPsx, and have hashDisc call it with RaSrc{fd, RA_SRC_ISO} at offset 0.
- Justification: Three copies of the same loop exist; one helper replaces them and removes code overall.
- Benefit: ~30 lines removed (INFERRED: three ~20-line blocks become one helper plus three calls). A few hundred bytes of .text in RA ELFs (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-135: tarFind/tarFindExact and tarFindPrefix/tarFindPrefixExact duplicate their search loops; the ART/CFG/CHT prefix strip is written three times

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/platform/tar.cpp:479-501`; `RiptOPL/src/platform/tar.cpp:538-555`; `RiptOPL/src/platform/tar.cpp:516-531`; `RiptOPL/src/platform/tar.cpp:562-573`; `RiptOPL/src/platform/tar.cpp:349-352`
- Problem: The Exact variants differ from the plain ones only in their entry guard (s_isExact required instead of cleared + lazy device sweep); the name loop (479-501 vs 538-555) and the prefix loop (516-531 vs 562-573) are identical, and the './' + 'ART/'|'CFG/'|'CHT/' strip appears at 349-352, 480-483 and 539-542.
- Fix: Move the two loops into static searchName(kind, name) and searchPrefix(kind, prefix) and the strip into one static stripKindPrefix(); the four public functions keep only their guard and call the helper.
- Benefit: ~30 lines removed; a few hundred bytes of .text (INFERRED: two duplicated loops of string compares)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-136: raVcdBootName / RaHash::vcdBootName has no product caller (test-only)

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/rahash.h:22`; `RiptOPL/src/sources/rahash.cpp:89`; `RiptOPL/src/sources/rahash.cpp:142-145`; `RiptOPL/src/sources/rahash.cpp:826-846`
- Problem: raVcdBootName is declared, defined and forwarded, but the only caller in the repo is .github/scripts/test_ra_ps1.py:139. Its PSX.EXE fallback (839-842) is a second copy of the one in hashPsx (773-780); hashVcd already returns the boot name through its boot argument.
- Fix: Delete raVcdBootName, RaHash::vcdBootName and the prototype; let test_ra_ps1.py check the boot name that raHashVcd fills in.
- Benefit: ~27 lines removed. Non-static and unreferenced, so --gc-sections (ps2.yaml:649) already drops it: no ELF change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-137: initImageInternalTexture duplicates initImageTexture's themePath==NULL branch and makes a failed internal load run twice

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/gfx/themeelems.cpp:762-784`; `RiptOPL/src/gfx/themeelems.cpp:893-898`; `RiptOPL/src/gfx/themeelems.cpp:712-719`
- Problem: For a built-in theme (themePath NULL) initMutableImage first calls initImageInternalTexture, which does texLookupInternalTexId + texLoadInternal; if that fails it calls initImageTexture(NULL, ...), whose themePath==NULL branch (712-719) does the same lookup and load again. texLoadInternal(-1) already returns ERR_BAD_FILE (textures.cpp:1007), so initImageTexture alone gives the identical result. initImageInternalTexture also ignores its themeConfig parameter.
- Fix: Delete initImageInternalTexture and the 'if (!themePath)' block at 893-895; the following initImageTexture call already handles a NULL themePath.
- Benefit: 26 source lines removed; one static function (~150 bytes of .text, INFERRED) and a repeated failed lookup per missing built-in default texture.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-138: xparam.h: 18 PARAM_* constants unused and included by 8 files that use nothing from it

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/xparam.h:10-31`; `RiptOPL/src/app/autolaunch.cpp:54`; `RiptOPL/src/app/lwnbdctl.cpp:55`; `RiptOPL/src/app/settingsio.cpp:58`; `RiptOPL/src/platform/sysdisc.cpp:52`; `RiptOPL/src/platform/sysneutrino.cpp:51`; `RiptOPL/src/platform/sysvmc.cpp:50`; `RiptOPL/src/sources/appscan.cpp:55`; `RiptOPL/src/ui/menuitems.cpp:55`
- Problem: PARAM_MDEC_DELAY_CYCLE..PARAM_CACHE_FLASH_CHANNELS (xparam.h:12-29) are referenced only in comments in xparam.cpp:146-163 and in docs/xparam/*.c, which no build compiles. GM_IOP_TYPE and GM_IF are used only in xparam.cpp. Of the 11 files that include xparam.h, only opl.cpp (ResetDeckardXParams, :2069) and system.cpp (ApplyDeckardXParam, :1511) use it.
- Fix: Delete the 18 PARAM_* lines, move GM_IOP_TYPE and GM_IF into xparam.cpp, leaving the header with the two prototypes (add an include guard while there), and delete the 8 unneeded #include lines.
- Benefit: ~26 source lines; no ELF change (constexpr ints emit nothing) (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-139: Duplicated vcdPreparePopstarterSmbLaunch result-to-message mapping in ethsupport.cpp and appsupport.cpp

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/ethsupport.cpp:1013-1033`; `RiptOPL/src/sources/appsupport.cpp:1148-1166`; `RiptOPL/include/vcdsupport.h:282-294`; `RiptOPL/src/sources/vcdsupport.cpp:2994-3025`
- Problem: Both callers of vcdPreparePopstarterSmbLaunch translate its vcd_popsnet_ensure_t result into the same five-branch chain (CANCELLED -> return, SMB_MISSING/NEED_STATIC/IO_ERROR/INVALID -> the same _STR_POPSTARTER_* msgBox and return). The function already uses host::ui().msgBox itself (vcdsupport.cpp:3022), so the messages can live there once.
- Fix: Show the matching message inside vcdPreparePopstarterSmbLaunch and make it return 1 (ready) or 0 (stop); replace each caller's block with a single 'if (!vcdPreparePopstarterSmbLaunch(...)) return;'. The public vcd_popsnet_ensure_t enum can then become file-local to vcdsupport.cpp. The stub enum in .github/scripts/test_app_launch_args.py needs the matching edit.
- Benefit: ~38 caller lines become 2 and ~10 lines move into the callee: about 25 lines net (INFERRED from the cited ranges); one mapping instead of two that can drift.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-140: igs_api.h: unused register macros, a dead duplicate set of GS register numbers and a BMP hex-dump comment

- Category: 1 (Dead code)
- Location(s): `RiptOPL/ee_core/include/igs_api.h:35-39`; `RiptOPL/ee_core/include/igs_api.h:60-62`; `RiptOPL/ee_core/include/igs_api.h:100`; `RiptOPL/ee_core/include/igs_api.h:103`; `RiptOPL/ee_core/include/igs_api.h:105-107`; `RiptOPL/ee_core/include/igs_api.h:112-113`; `RiptOPL/ee_core/include/igs_api.h:148`; `RiptOPL/ee_core/include/igs_api.h:168-176`; `RiptOPL/ee_core/src/igrmenu.c:687-690`
- Problem: GS_BITBLTBUF/TRXPOS/TRXREG/TRXDIR/FINISH (35-39) are never used: igs_api.c uses the identical GS_GSBITBLTBUF..GS_GSTRXDIR at 91-95, and igrmenu.c (which does not include igs_api.h) defines its own copies at 687-690. GS_D1_TADR/ASR0/ASR1, GS_IMR, GET_PMODE_EN1, GET_SMODE2_INT/FFMD/DPMS, GET_DISPFB_DBX/DBY have no use anywhere. 148 and 168-176 are a sample BMP pixel dump in comments.
- Fix: Delete lines 35-39, 60-62, 100, 103, 105-107, 112-113, 148 and 168-176 of igs_api.h.
- Benefit: ~25 lines removed; macros only, no ELF change (header compiled in IGS extras builds via build_configuration.py:40 and padhook.c:39).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-141: ratls_ps2.cpp: raHttpsGet and raHttpsDownload repeat the same clock/NIC/DNS/connect/deadline preamble; bio expiry ternary written three times

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/ratls_ps2.cpp:223-251`; `RiptOPL/src/sources/ratls_ps2.cpp:275-303`; `RiptOPL/src/sources/ratls_ps2.cpp:177-185`; `RiptOPL/src/sources/ratls_ps2.cpp:198`; `RiptOPL/src/sources/nettime.cpp:88-101`
- Problem: Lines 228-251 and 280-303 are the same 24 lines (raTlsClockOk, NIC check, gethostbyname, sockaddr fill for port 443, socket, connect, budget check); nettime.cpp:88-101 is a third copy of the DNS+connect part for port 80. bioSend and bioRecv each spell out 'bio->idle ? ratlsExpired(start, IDLE) : ratlsExpired(start)'.
- Fix: Move the preamble into one static ratlsConnect(host, port, &bio) returning 0 or the RATLS_ERR_* code and call it from both entry points (nettime can share the DNS+connect part if finding 4's shared file hosts it). Fold the idle ternary into a one-line bioExpired(bio).
- Justification: The block exists twice in the file (three times counting nettime); one helper removes code overall.
- Benefit: ~25 lines removed (INFERRED: one 24-line copy becomes a call, three ternaries become calls). Small .text saving in RA+updater builds (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-142: sbResolveCustomLoaderPath: mc/mmce alias blocks and pfs/rom literal-or-unit-0 blocks are near-copies

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/supportbase.cpp:2261-2306`; `RiptOPL/src/sources/supportbase.cpp:2366-2375`; `RiptOPL/src/sources/supportbase.cpp:2430-2441`
- Problem: The mc and mmce blocks (21 and 22 lines) are the same two-slot probe, differing only in the stem length (2 vs 4), the root format and the mmceLoadModules() call. The pfs and rom tails (10 lines each) are identical except for the unit-0 root string.
- Fix: Fold mc/mmce into one static helper taking the stem length and root format (call mmceLoadModules before it for mmce), and pfs/rom into one helper taking the unit-0 root.
- Benefit: ~25 lines removed net (INFERRED: 43+20 duplicated lines become two helpers of ~20+8 lines plus 4 call lines).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-143: Cover-art lookup and frame-overlay drawing copied in grid.cpp, psbbnflow.cpp and coverflow.cpp

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/gfx/grid.cpp:308-333`; `RiptOPL/src/gfx/psbbnflow.cpp:155-177`; `RiptOPL/src/gfx/coverflow.cpp:292-297`; `RiptOPL/src/gfx/coverflow.cpp:337-347`
- Problem: All three views resolve a cover texture the same way (getGameImageTexture, else the image's default texture, else COVER_DEFAULT, then a Mem check) and then draw it with the same block: if overlayTexture, rmDrawOverlayPixmap with all eight inlay corners scaled by drawn size over the cover element's size, then overlayTexture2; else rmDrawPixmap. About 12-14 matching lines per copy.
- Fix: Move the lookup and the overlay-or-plain draw into one static helper in the theme layer (cover element, image, texture, x, y, w, h, colour, reflection) and call it from the three views.
- Justification: The same 12-14 line block exists in three files; one helper of about 15 lines replaces about 40 lines, a net reduction.
- Benefit: ~25 net lines removed (3 copies x ~13 lines minus a ~15-line helper), INFERRED; no measurable ELF change.
- Effort: S. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-144: crt0.S calls weak _init/_fini that ee_core never defines, and stores/reloads the exit code around them

- Category: 1 (Dead code)
- Location(s): `RiptOPL/ee_core/src/crt0.S:14-18`; `RiptOPL/ee_core/src/crt0.S:85-93`; `RiptOPL/ee_core/src/crt0.S:114-128`; `RiptOPL/ee_core/src/crt0.S:145-146`
- Problem: ee_core is linked with -nostartfiles -nostdlib (ps2.yaml:613), is all C (no constructors/destructors anywhere in ee_core), and no ee_core source defines _init or _fini, so both weak symbols resolve to 0 and the ctors/dtors blocks always branch past. _retval exists only to keep the exit code across the _fini call.
- Fix: Delete the .weak _init/_fini declarations, the ctors and dtors blocks and _retval; _exit becomes 'Exit(retval)' directly (a0 already holds it).
- Benefit: ~25 lines; INFERRED 16 instructions (64 bytes) of ram84 text and 4 bytes .bss: 2x(la=2 + beqz/nop/jalr/nop) + 2x(la+sw/lw)
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-145: BDMA launch card checks open the POPSTARTER folder and marker repeatedly; vcdGetFileSize duplicates vcdSourceFileSize

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/vcdsupport.cpp:1720-1730`; `RiptOPL/src/sources/vcdsupport.cpp:1734-1754`; `RiptOPL/src/sources/vcdsupport.cpp:1769-1798`; `RiptOPL/src/sources/vcdsupport.cpp:1801-1826`; `RiptOPL/src/sources/vcdsupport.cpp:1383-1390`
- Problem: vcdBdmaEnvironmentValid and vcdBdmaManualPairMatches are always evaluated together on the launch fast path (1837, 1848, 1899) and each repeats the same block: vcdFindPopstarterMc (opendir), build the two module paths, read both sizes, compare with vcdBdmaPairMatches. EnvironmentValid also calls vcdReadBdmaMode, which runs vcdFindPopstarterMc and opens the marker again, and ManualPairMatches opens the marker a third time. vcdGetFileSize is vcdSourceFileSize without the MMCE read-through fallback; on mc0:/mc1: they return the same value.
- Fix: Fold the two checks into one function that finds the folder once, reads the two sizes once and the marker once, and returns ready when the sizes match and the marker is absent or names the mode (keeping the FAT32 'no pair' rule). Replace vcdGetFileSize with vcdSourceFileSize. Update test_bdma_shipped_pairs.py and test_mc_folders.py, which extract these functions by name.
- Benefit: ~25 source lines removed; per VCD launch check ~2 fewer opendir and ~2 fewer marker opens on the memory card (INFERRED from the call chain 1837 -> 1775/1794->1386/1807/1811).
- Effort: M. Risk: Med. Confidence: Med (READ)

#### RIPTOPL-146: ListModules (EESIO debug) has no caller

- Category: 1 (Dead code)
- Location(s): `RiptOPL/ee_core/src/modmgr.c:265-285`; `RiptOPL/ee_core/include/modmgr.h:94-96`
- Problem: ListModules is defined under __EESIO_DEBUG and prototyped in modmgr.h, but nothing calls it: the only matches in the repo are the definition and the prototype.
- Fix: Delete the function (with its banner and #ifdef) and its prototype.
- Benefit: 24 lines removed; only compiled in the unshipped eesio_debug configuration (ee_core links without --gc-sections, so that build carries the dead code, INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-147: ListModules in modmgr.c has no caller in any build

- Category: 1 (Dead code)
- Location(s): `RiptOPL/ee_core/src/modmgr.c:265-285`; `RiptOPL/ee_core/include/modmgr.h:94-96`
- Problem: ListModules (guarded by __EESIO_DEBUG) is only defined and prototyped: no source, assembly, script or test calls it, so even the EESIO debug configuration compiles it for nothing.
- Fix: Delete the function, its banner and the guarded prototype.
- Benefit: ~24 lines removed; no release ELF change (only built with __EESIO_DEBUG, and eecore links with --gc-sections per BUILD-EVIDENCE.md:147)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-148: Copy-paste in texcache.cpp: FAV/APP proxy-mode resolution twice and in-flight request adoption twice

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/gfx/texcache.cpp:151-162`; `RiptOPL/src/gfx/texcache.cpp:191-202`; `RiptOPL/src/gfx/texcache.cpp:263-273`; `RiptOPL/src/gfx/texcache.cpp:318-328`; `RiptOPL/src/gfx/texcache.cpp:525`
- Problem: cacheRequestIsSIO2 (151-162) and cacheResolveArtArchive (191-202) carry the same 12 lines resolving FAV_MODE then APP_MODE through favGetArtMode/appGetArtMode, and cacheGetTextureEx calls both for the same request (355 and 430), so a FAV/APP enqueue looks the value up twice. The pending-request touch (stamp focusEpoch, promote when isPriority && !(req->sio2 && gArtQ.navActive())) is pasted with the same 5-line comment at 263-273 and 318-328, inside an `if (req != NULL)` that is always true because entry->qr was just tested non-NULL; the same promotion condition recurs at 525.
- Fix: Add one static helper returning the effective art mode (resolving FAV and APP) and call it once in cacheGetTextureEx, passing the mode to both checks; add one static helper for the pending-request touch and use it at both sites, dropping the always-true NULL test and the duplicate comment.
- Justification: The two helpers replace four pasted blocks (about 40 lines) with two definitions of about 15 lines, and halve the favGetArtMode/appGetArtMode calls per FAV/APP enqueue.
- Benefit: ~22 lines removed net (INFERRED: 2x12 + 2x11 pasted lines -> ~25 lines of helpers and calls); one fewer FAV/APP store lookup per enqueue.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-149: Four long-path keyboard handlers repeat the body of NEUTRINO_ARG_HANDLER

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/ui/guisettings.cpp:138-144`; `RiptOPL/src/ui/guisettings.cpp:807-813`; `RiptOPL/src/ui/guisettings.cpp:1083-1097`; `RiptOPL/src/ui/guisettings.cpp:1705-1721`
- Problem: guiExitPathHandler, guiNfsExportHandler, guiNeutrinoPathHandler and guiPopstarterPathHandler are each the same 7 lines (guiShowKeyboard on a backing buffer, snprintf into text) that the NEUTRINO_ARG_HANDLER macro already generates for the 7 Neutrino-args handlers; they differ only in function name and buffer.
- Fix: Generalise the macro to take the function name and the buffer expression, define all 11 handlers with it, and delete the four hand-written copies.
- Benefit: ~22 lines removed (4 x 7 lines -> 4 macro lines, minus small macro change); no ELF change (same code) (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-150: Install_PadOpen_Hook: the J/JAL patch loop and the pointer patch loop are near copies

- Category: 2 (Duplication)
- Location(s): `RiptOPL/ee_core/src/padhook.c:1006-1037`; `RiptOPL/ee_core/src/padhook.c:1040-1079`
- Problem: The two search-and-patch loops repeat the same find_pattern_with_mask walk, the same libpad/libpad2 hook selection (1022-1028 and 1063-1069), the same DPRINTFs and the same Pad_Data.libpad/libversion stores (1034-1035, 1075-1076); they differ only in the pattern/mask and in whether the written word is a jump encoding or the raw hook address.
- Fix: Pick the hook address once per pattern before the loops, and run one search-and-patch loop twice (first with the J/JAL pattern and the jump encoding, then, if nothing was patched, with the address pattern and the raw address), setting libpad/libversion once after a patch.
- Benefit: ~22 lines removed in upstream-derived OPL code; slightly smaller eecore .text (INFERRED)
- Effort: S. Risk: Med. Confidence: High (READ)

#### RIPTOPL-151: PS2-logo probe (open, CheckPS2Logo, close) copied five times; udpfsLaunchOplCore opens the ISO twice and launch() builds partname twice

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/udpfssupport.cpp:940-954`; `RiptOPL/src/sources/udpfssupport.cpp:1123-1131`; `RiptOPL/src/sources/udpfssupport.cpp:1150`; `RiptOPL/src/sources/ethsupport.cpp:1242-1249`; `RiptOPL/src/sources/ethsupport.cpp:1479-1485`; `RiptOPL/src/sources/mmcesupport.cpp:1503-1509`
- Problem: The block 'if (gPS2Logo) { fd = open(partname); if (fd >= 0) { EnablePS2Logo = CheckPS2Logo(fd, 0); close(fd); } }' appears five times. In udpfsLaunchOplCore the ISO is opened once for its size (941-947) and then reopened for the logo (948-954), although CheckPS2Logo seeks to 0 itself (util.cpp:796); on UDPFS each open/close is a server round trip. UdpfsSource::launch calls sbCreatePath for the same partname at 1125 and again at 1150.
- Fix: In udpfsLaunchOplCore run CheckPS2Logo on the fd opened for the size before closing it. In launch() build partname once before the logo check and drop the second sbCreatePath. Optionally add one helper next to CheckPS2Logo (int ps2LogoForFile(const char *path): returns 0 when gPS2Logo is off, else opens, checks, closes) and call it from the other four sites.
- Justification: The helper replaces five identical 7-line blocks with one-line calls; an 8-line helper leaves about 20 lines net removed, and the udpfs merge saves one network open/close per OPL-core launch.
- Benefit: ~20-25 lines net (INFERRED: 5 x 7-line blocks -> 5 calls + ~8-line helper, plus 5 lines in udpfsLaunchOplCore); one fewer UDPFS open/close round trip per OPL-core launch.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-152: Remember-last-played block copy-pasted in nine launch paths across the game sources

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/hddsupport.cpp:2389-2392`; `RiptOPL/src/sources/hddsupport.cpp:2720-2723`; `RiptOPL/src/sources/bdmlaunch.cpp:453-456`; `RiptOPL/src/sources/bdmlaunch.cpp:898-901`; `RiptOPL/src/sources/ethsupport.cpp:1224-1227`; `RiptOPL/src/sources/ethsupport.cpp:1375-1378`; `RiptOPL/src/sources/httpsupport.cpp:799-802`; `RiptOPL/src/sources/mmcesupport.cpp:1384-1387`; `RiptOPL/src/sources/udpfssupport.cpp:1070-1073`
- Problem: The same four lines (if gRememberLastPlayed: configSetStr(CONFIG_LAST, "last_played", startup); saveConfig(CONFIG_LAST, 0)) appear nine times; no helper exists (grep for LastPlayed\w*\( finds none). In HDD the Neutrino attempt saves at 2389 and, when a preflight falls back to the native core, the native path saves again at 2720.
- Fix: Add one small helper in supportbase (e.g. sbRememberLastPlayed(startup)) and replace the nine blocks with calls. In HDD, the Neutrino-fallback double save then also becomes obvious and can be dropped by moving the call after the preflights.
- Justification: Adds a ~5-line helper but removes 9 x 4 = 36 duplicated lines (net ~22) and gives one place for the card-write policy.
- Benefit: ~22 net lines removed; a few dozen bytes of code (INFERRED); possibly one redundant CONFIG_LAST save on HDD Neutrino fallback (INFERRED, depends on saveConfig skipping unchanged data)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-153: Remember-last-played save block copied in 9 launch paths

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/bdmlaunch.cpp:453-456`; `RiptOPL/src/sources/bdmlaunch.cpp:898-901`; `RiptOPL/src/sources/ethsupport.cpp:1224-1227`; `RiptOPL/src/sources/ethsupport.cpp:1375-1378`; `RiptOPL/src/sources/hddsupport.cpp:2389-2392`; `RiptOPL/src/sources/hddsupport.cpp:2720-2723`; `RiptOPL/src/sources/httpsupport.cpp:799-802`; `RiptOPL/src/sources/mmcesupport.cpp:1384-1387`; `RiptOPL/src/sources/udpfssupport.cpp:1070-1073`
- Problem: The same four lines (if gRememberLastPlayed: configSetStr(CONFIG_LAST, "last_played", startup); saveConfig(CONFIG_LAST, 0)) appear verbatim in nine launch functions, with the key spelled as a bare string literal each time (also read in opl.cpp:733).
- Fix: Add one small helper in supportbase (e.g. sbRememberLastPlayed(startup)) and replace the nine blocks with a call.
- Justification: Nine identical copies: a 5-line helper plus nine 1-line calls replaces 36 lines, and the key literal lives in one place.
- Benefit: ~22 lines removed net (36 -> 9 calls + 5-line helper); a few hundred bytes of repeated call sequences (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-154: Small duplicates inside cuesupport.cpp: settings value tables twice, extension loop twice, nine free() pairs

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/cuesupport.cpp:798-801`; `RiptOPL/src/sources/cuesupport.cpp:836-839`; `RiptOPL/src/sources/cuesupport.cpp:353-364`; `RiptOPL/src/sources/cuesupport.cpp:895-906`; `RiptOPL/src/sources/cuesupport.cpp:698-794`
- Problem: cueSaveGameSettings and cueApplySettings each declare the same four static value tables (timing, dither, shading, controller); cueResolveGameImage re-implements cueNameIsDiscImage's extension loop instead of reusing it with an index result; cueRewriteSettingsFile frees before/after on each of nine exits (18 lines).
- Fix: Move the value tables to file scope once; make cueNameIsDiscImage return the matched extension index (or -1) and use it in cueResolveGameImage; in cueRewriteSettingsFile use one allocation of two halves or one cleanup label so the buffers are freed in one place.
- Benefit: ~22 lines removed (INFERRED: 4 table lines, ~6 loop lines, ~12 free lines); duplicate tables are already merged by the compiler only if identical-data folding applies, so possibly ~60 bytes .rodata (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-155: Write-only font-system state: GSCLUT fields, glyph shy, and fntCacheFlushPage clearing pages that are freed next

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/renderman.h:37-44`; `RiptOPL/include/fontsystem.h:122`; `RiptOPL/src/gfx/fntsys.cpp:225-228`; `RiptOPL/include/fontsystem.h:51`; `RiptOPL/src/gfx/fntsys.cpp:631`; `RiptOPL/src/gfx/fntsys.cpp:151-161`; `RiptOPL/src/gfx/fntsys.cpp:169`
- Problem: GSCLUT is used only for FontSystem::clut_; its PSM, ClutPSM and VramClut are written in prepareCLUT and never read (newAtlas sets the surface's ClutPSM itself and reads only clut_.Clut). The glyph field shy is written at 631 and never read. fntCacheFlushPage clears every entry of a page that cacheFlush frees on the very next line, its only call site.
- Fix: Replace GSCLUT clut_ with a plain u32 *clut_ and delete the GSCLUT typedef; delete the shy field and its store; delete fntCacheFlushPage and its call.
- Benefit: ~22 lines removed (8-line typedef, 3 stores, shy, 11-line function and call), INFERRED; a few bytes of .bss and code.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-156: config.cpp: configSetMove duplicates configInit; configMove and configBuildLegacyOplPath are one-line forwarders

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/core/config.cpp:630-647`; `RiptOPL/src/core/config.cpp:660-680`; `RiptOPL/src/core/config.cpp:22-23`; `RiptOPL/src/core/config.cpp:702-705`; `RiptOPL/src/core/config.cpp:1228-1234`; `RiptOPL/src/core/config.cpp:1464`
- Problem: configInit and configSetMove are the same 18 lines (NULL->gBaseMCDir, legacy IPCONFIG path, build each file path, set cfgHomePath, configPrepareNotifications) except for one call per file: configAlloc versus configMove. configMove (plus its forward declaration and comment) only forwards to ConfigSet::rename and has two callers (672, 1446). configBuildLegacyOplPath only forwards to configBuildSiblingPath with a constant and has one caller (1464).
- Fix: Make one static function that takes the prefix and a move flag, and let configInit/configSetMove call it. Replace configMove(x, p) with x->rename(p) and delete configMove and its forward declaration. Call configBuildSiblingPath(..., CONFIG_OPL_FILENAME_LEGACY, ...) at 1464 and delete configBuildLegacyOplPath.
- Benefit: ~22 source lines removed (INFERRED: 18-line duplicate body less ~6 lines of new helper/call sites, plus 6 and 7 lines of forwarders); static functions, so ELF change is a few dozen bytes at most
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-157: AppSource::launch re-implements the mass:/mass?: slot search that appResolveLegacyMassStartup already does

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/appsupport.cpp:1090-1116`; `RiptOPL/src/sources/appsupport.cpp:438-464`
- Problem: launch() rewrites 'mass:' to 'mass?:' with memmove and then probes mass0..massN by poking filename[4] and opening the file (27 lines). appResolveLegacyMassStartup (same file) accepts both 'mass:' and 'mass?:' and returns the first massN: path that opens, or the input unchanged, which leads to the same open failure and error dialog.
- Fix: Replace lines 1090-1116 with one call to appResolveLegacyMassStartup(filename) and copy its result back into filename (or open the returned pointer).
- Benefit: ~22 lines removed (INFERRED from the cited ranges); no behaviour change except the diagnostic dialog shows the original path on failure
- Effort: S. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-158: Remember-last-played save block copied 9 times across sources

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/ethsupport.cpp:1224-1227`; `RiptOPL/src/sources/ethsupport.cpp:1375-1378`; `RiptOPL/src/sources/bdmlaunch.cpp:453-456`; `RiptOPL/src/sources/bdmlaunch.cpp:898-901`; `RiptOPL/src/sources/hddsupport.cpp:2389-2392`; `RiptOPL/src/sources/hddsupport.cpp:2720-2723`; `RiptOPL/src/sources/mmcesupport.cpp:1384-1387`; `RiptOPL/src/sources/udpfssupport.cpp:1070-1073`; `RiptOPL/src/sources/httpsupport.cpp:799-802`
- Problem: The identical 4-line block `if (gRememberLastPlayed) { configSetStr(configGetByType(CONFIG_LAST), "last_played", startup); host::app().saveConfig(CONFIG_LAST, 0); }` appears nine times; only the startup expression differs (game->startup or startup).
- Fix: Add one supportbase function sbRememberLastPlayed(const char *startup) holding the block and call it from the nine launch paths.
- Justification: Nine copies of the same 4 lines; a 6-line helper replaces 36 lines with 9 calls, net ~21 lines, and the CONFIG_LAST key/save policy lives in one place.
- Benefit: 36 lines -> 9 calls + 6-line helper = ~21 lines net (INFERRED from cited ranges).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-159: OSD ConfigParam copied field by field three times instead of by struct assignment

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/ee_core/src/syshook.c:194-201`; `RiptOPL/ee_core/src/syshook.c:209-216`; `RiptOPL/src/platform/system.cpp:1591-1598`
- Problem: hook_SetOsdConfigParam, hook_GetOsdConfigParam and the launcher copy all eight bitfields of ConfigParam one at a time between two objects of the same type (coreconfig.h:64 declares CustomOSDConfigParam as ConfigParam; system.cpp:1518 declares PARAM as ConfigParam). The eight fields make up the whole 32-bit struct, so each block is one assignment.
- Fix: Replace each eight-line block with a single struct assignment.
- Benefit: 21 lines removed; eight bitfield extract/insert pairs become one word copy in each eecore hook, likely ~100 bytes of eecore .text (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-160: Global/per-game source-switch block copied in four dialog updaters

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/ui/guigame.cpp:1029-1037`; `RiptOPL/src/ui/guigame.cpp:1184-1192`; `RiptOPL/src/ui/guigame.cpp:1424-1432`; `RiptOPL/src/ui/guigame.cpp:2390-2398`; `RiptOPL/src/ui/guigame.cpp:880-901`
- Problem: Cheats, PADEMU, pad macro and OSD updaters each carry the same 9-line block: if the source row changed to Global, gameMenuLoadConfig(dia), configRemoveKey(source key), reload; if changed to Per-game, configSetInt(source key), reload. They differ only by dialog, key, source value and load function. The GSM updater (880-901) is the same with a third arm.
- Fix: Add one static helper taking (previous, now, dialog, source key, load function) and call it from the four updaters; leave the GSM three-way arm as it is or route its two common arms through the helper.
- Justification: The same block exists four times (36 lines); a ~12-line helper plus four one-line calls removes about 20 lines net.
- Benefit: ~20 source lines net (INFERRED: 4 x 9 lines replaced by 4 calls + ~12-line helper); a few hundred bytes of .text (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-161: One RA event buffer and one event-intake routine would do: ra_overlay.c and ra_pulse.c each keep a 128-byte buffer and the same torn-copy check

- Category: 2 (Duplication)
- Location(s): `RiptOPL/ee_core/src/ra_overlay.c:61`; `RiptOPL/ee_core/src/ra_overlay.c:364-393`; `RiptOPL/ee_core/src/ra_pulse.c:42-60`; `RiptOPL/ee_core/src/ra_pulse.c:75-99`
- Problem: RA_OverlayEventBuffer hands raudp either ra_ovl_event or ra_pulse_event, chosen by raClientMode and ra_pkt, which are fixed before the IOP modules load (main.c:122-123, iopmgr.c:354), so only one 128-byte aligned buffer is ever used per launch. Both OnVblank paths then repeat the same magic/seq/commit copy-and-recheck and RESET handling, and both buffer getters repeat the same five-field clear.
- Fix: Keep a single struct ra_event buffer and one static intake function (copy, validate, update seen, handle RESET, return the kind) used by both the card and the pulse paths; delete RA_PulseEventBuffer's separate buffer and clear.
- Benefit: 128 bytes of ee_core .bss (INFERRED: struct ra_event is 128 bytes per ra_overlay.h:8); per ra.c:72-77 ee_core's stack is what is left above .bss. ~20 lines removed. RA builds only.
- Effort: S. Risk: Med. Confidence: High (READ)

#### RIPTOPL-162: POPSTARTER SMB preparation result -> message mapping duplicated in ETH launchVcd and APPS

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/ethsupport.cpp:1013-1033`; `RiptOPL/src/sources/appsupport.cpp:1147-1167`
- Problem: Both call vcdPreparePopstarterSmbLaunch and then map its five vcd_popsnet_ensure_t results to the same five msgBox strings with the same returns (20 identical lines).
- Fix: Do the mapping once: let vcdsupport show the message (e.g. a vcdPreparePopstarterSmbLaunchUi wrapper returning 1 only for READY) and have both callers return on 0.
- Justification: Two identical 20-line blocks; one ~16-line function replaces both, net ~20 lines, and a new result code cannot be handled in one caller and missed in the other.
- Benefit: ~40 lines -> 2 calls + ~16-line function = ~20 lines net (INFERRED from cited ranges).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-163: Repeated list-reset, list-free and guard blocks in ethsupport.cpp; update() re-implements count()

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/ethsupport.cpp:611-618`; `RiptOPL/src/sources/ethsupport.cpp:729-735`; `RiptOPL/src/sources/ethsupport.cpp:1308-1312`; `RiptOPL/src/sources/ethsupport.cpp:1590-1595`; `RiptOPL/src/sources/ethsupport.cpp:1611-1616`; `RiptOPL/src/sources/ethsupport.cpp:636-640`; `RiptOPL/src/sources/ethsupport.cpp:653-657`; `RiptOPL/src/sources/ethsupport.cpp:868-872`; `RiptOPL/src/sources/ethsupport.cpp:927-932`; `RiptOPL/src/sources/ethsupport.cpp:1298`
- Problem: The scan-state reset (ulSizePrev=-2, mtimes, counts, retry flags) is written three times; cleanUp and shutdown free and zero both lists with the same 6 lines; checkedRefreshSmb repeats its 5-line ownership guard; the last 5 lines of update() are the body of count(); and launch() re-tests game != NULL at 1298 after returning on NULL at 1291.
- Fix: Give EthSupport one resetScan() and one freeLists() method and use them at the cited places; make the guard a local lambda or bool helper; end update() with `return count(itemList);`; drop the redundant game != NULL test.
- Justification: Two small methods replace five copies of 5-8 lines each; net reduction about 20 lines and the reset fields cannot drift apart between init, reconnect and share selection.
- Benefit: ~20 lines net (INFERRED: 3 reset copies ~20 lines, 2 free copies 12 lines, guard 5, count tail 5, minus ~16 lines of helpers).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-164: SettingsShell::shellActive_ guards are constant: every page runs inside the shell, no standalone callers remain

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/ui/guisettings.cpp:401-406`; `RiptOPL/src/ui/guisettings.cpp:1250-1254`; `RiptOPL/src/ui/guisettings.cpp:2212-2213`; `RiptOPL/src/ui/guisettings.cpp:3262`; `RiptOPL/src/ui/guisettings.cpp:3267`; `RiptOPL/src/ui/guisettings.cpp:3304`; `RiptOPL/src/ui/guisettings.cpp:3312`; `RiptOPL/src/ui/guisettings.cpp:3322`; `RiptOPL/include/settingshell.h:51-52`; `RiptOPL/include/settingshell.h:59`; `RiptOPL/include/settingshell.h:75`
- Problem: shellActive_ is true for the whole of showFromPage, and every reader runs only from there: guiShowMmceConfig (static, sole caller 2517), guiShowVcdListConfig (static, sole caller 2952) and beginDialog (called only by the nine pages dispatched at 3274-3301). So the '!shellActive()' legacy apply at 401-406 never runs, the 'if (shellActive())' at 1253 is always true, and beginDialog's early return never fires.
- Fix: Delete the dead branch at 401-406, make 1253-1254 an unconditional markSavePending(), drop the guard in beginDialog, then delete shellActive_, shellActive() and the five assignments in showFromPage, and the 'legacy standalone callers' header comment.
- Benefit: ~20 lines removed; a bool field and a few branches (INFERRED, tens of bytes).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-165: Small copy-paste in mmcesupport.cpp: slot-candidate loop, card-ready tail, list frees, constant temporaries

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/mmcesupport.cpp:176-185`; `RiptOPL/src/sources/mmcesupport.cpp:562-571`; `RiptOPL/src/sources/mmcesupport.cpp:259-265`; `RiptOPL/src/sources/mmcesupport.cpp:280-286`; `RiptOPL/src/sources/mmcesupport.cpp:1037-1044`; `RiptOPL/src/sources/mmcesupport.cpp:1796-1799`; `RiptOPL/src/sources/mmcesupport.cpp:1812-1815`; `RiptOPL/src/sources/mmcesupport.cpp:1776-1788`
- Problem: sendGameID and resetCards open-code the same 'configured slot, then mmce0, then mmce1, skip tried' loop, parsing the slot by hand (strlen < 5, [4]-'0', 0..1) although mmceSlotOf (98-103) does exactly that. Inside sendGameID the 'waitCardReady, <0 return -1, ==0 break, log, return 1' tail appears twice. update(), cleanUp() and shutdown() each free and NULL both game lists (update with redundant NULL checks); shutdown's body equals cleanUp's. textId/iconId copy a constant into a local before returning it.
- Fix: Use mmceSlotOf in both candidate loops; let the silent-swap branch fall into the shared card-ready tail (set a flag, or goto); make shutdown call cleanUp; drop the NULL checks before free; return the constants directly.
- Benefit: ~20 source lines removed; no measurable ELF change (INFERRED).
- Effort: S. Risk: Med. Confidence: High (READ)

#### RIPTOPL-166: autoLaunchBDMGame repeats its free/NULL/miniDeinit cleanup five times and the slot-record block twice

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/app/autolaunch.cpp:155-158`; `RiptOPL/src/app/autolaunch.cpp:184-187`; `RiptOPL/src/app/autolaunch.cpp:254-262`; `RiptOPL/src/app/autolaunch.cpp:278-285`; `RiptOPL/src/app/autolaunch.cpp:298-305`; `RiptOPL/src/app/autolaunch.cpp:225-239`
- Problem: Every exit frees gAutoLaunchDeviceData and gAutoLaunchBDMGame, NULLs them and calls miniDeinit with an inline copy (free(NULL) is safe, so one sequence fits all five). In the mass-slot loop the first-slot branch (225-230) and the ata branch (232-238) assign the same driver, index and prefix; the strlen(detectedDriver) == 3 test is implied by strcmp(detectedDriver, "ata") == 0.
- Fix: Add one static helper in autolaunch.cpp that frees both globals, NULLs them and calls miniDeinit(configSet), and use it at the five exits. Record the slot under one 'if (selectedMassSlot < 0 || isAta)' and keep only bdmResolveLBA_UDMA and break in the ata branch; drop the strlen test.
- Justification: The helper replaces five copies of the same 4-6 line sequence in one file, so it removes code overall.
- Benefit: ~20 lines removed; a few hundred bytes of repeated free/miniDeinit call sequences (INFERRED).
- Effort: S. Risk: Med. Confidence: High (READ)

#### RIPTOPL-167: bdmDeviceIsATA/bdmGetATADeviceId re-implement the identity and by-type lookup helpers

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/bdmsupport.cpp:2244-2274`; `RiptOPL/src/sources/bdmsupport.cpp:2285-2287`; `RiptOPL/src/sources/bdmsupport.cpp:2322`
- Problem: bdmDeviceIsATA opens mass<i>:/ and reads the driver name into a whole bdm_device_data_t on the stack (a ~190-byte struct for a 32-byte field), with a redundant strlen()==3 after strcmp("ata"). bdmGetATADeviceId loops all slots with it, but both callers only test the result for >= 0; bdmGetDeviceRootByType(BDM_TYPE_ATA, ...) at 495-520 already answers that, and bdmReadDeviceIdentity + bdmDriverIsATA cover the single-slot check.
- Fix: Delete bdmGetATADeviceId and use bdmGetDeviceRootByType(BDM_TYPE_ATA, root, sizeof(root)) at 2285 and 2322. Reduce bdmDeviceIsATA to a char[32] read via bdmReadDeviceIdentity and bdmDriverIsATA, or test the waited slot with bdmGetDeviceSlotsByType.
- Benefit: ~20 source lines removed (INFERRED); ~160 bytes less stack per call (INFERRED from bdm_device_data_t fields at bdmsupport.h:37-74).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-168: diaExecuteDialog: nine exits each restore pad settings through one-line wrappers, and L1/R1 and cancel handling are written twice

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/ui/dia.cpp:1088-1098`; `RiptOPL/src/ui/dia.cpp:1154`; `RiptOPL/src/ui/dia.cpp:1199-1217`; `RiptOPL/src/ui/dia.cpp:1281-1292`; `RiptOPL/src/ui/dia.cpp:1303-1327`
- Problem: diaStoreScrollSpeed/diaRestoreScrollSpeed only forward to padStoreSettings/padRestoreSettings, and diaRestoreScrollSpeed is repeated before each of nine returns (the comment at 1320-1323 records a bug from missing one). The Settings-shell L1 and R1 blocks (1199-1209) repeat the list-dialog L1/R1 block (1213-1217), and the two cancel blocks (1281-1292) differ only in the value returned.
- Fix: Leave the loop with a result variable and one exit that calls padRestoreSettings once; call padStoreSettings directly and delete the two wrappers; merge the L1/R1 checks into one (settingsShell || ui == diaListUI) test and the two cancels into one returning settingsShell ? DIA_RESULT_INDEX : UIID_BTN_CANCEL.
- Benefit: ~20 lines removed; removes the class of 'forgot to restore on this exit' bugs (INFERRED)
- Effort: S. Risk: Med. Confidence: High (READ)

#### RIPTOPL-169: menuRenderMain and menuRenderInfo are the same five-branch dispatcher and re-call the family getter

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/ui/menusys.cpp:2557-2589`; `RiptOPL/src/ui/menusys.cpp:2864-2887`
- Problem: Both functions compute kindElems with menuGetMainElems/menuGetInfoElems and then, in every one of the four non-Favourites branches, call the same getter again and pass the result to menuRenderElements, so the render call is duplicated 5 times per function and the getter runs twice per frame. The branch tests mirror the getter's own tests, and the slot fallbacks (vcd/favs/apps ItemsList or gamesItemsList) are written out twice, once per function.
- Fix: Call menuRenderElements(kindElems) once at the top of each function, then pick the items list; better, fold both into one static helper taking the getter's result plus the info/main family pointers so the slot-fallback chain is written once.
- Benefit: ~20 lines removed and one redundant family lookup per frame (INFERRED from cited ranges)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-170: oplGetLegacyAppsConfig and oplGetLegacyAppsInfo repeat the same device-prefix loop and open/close existence probe

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/appscan.cpp:179-216`; `RiptOPL/src/sources/appscan.cpp:218-248`
- Problem: Both functions walk the modules from MODE_COUNT-1 down, skip disabled or prefix-less sources, build a path, test it with openFile+close and configAlloc it on success, else configAlloc the last tried path. They differ only in the file name format, the config type (CONFIG_APPS vs 0), the mc?:OPL first probe and the empty-path fallback. The open/close/configAlloc probe appears three times (187-192, 202-207, 233-238).
- Fix: Merge them into one static lookup taking the per-device name format and config type (the ETH backslash only matters for the CFG case), with the two public functions as one-line callers; or at least fold the repeated probe into one static helper.
- Benefit: ~20 lines removed (INFERRED from the two 30-38 line bodies sharing about 25 lines); no measurable ELF change
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-171: pggsm.h: GSM_ARGS, makeSMODE1, makeSYNCH1 and makeSYNCH2 are never used

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/pggsm.h:20`; `RiptOPL/include/pggsm.h:23-41`
- Problem: These four constexpr definitions have no reference anywhere in the repo besides their definitions; only makeSYNCV and makeDISPLAY are used (src/core/gsm.cpp).
- Fix: Delete GSM_ARGS and the makeSMODE1, makeSYNCH1 and makeSYNCH2 functions.
- Benefit: 20 source lines; constexpr helpers emit no code, so no ELF change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-172: vcdDisplayName(mode, text) has no caller; three includes still cite it

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/sources/vcdsupport.cpp:173-188`; `RiptOPL/include/vcdsupport.h:102-105`; `RiptOPL/src/gfx/coverflow.cpp:17`; `RiptOPL/src/gfx/themes.cpp:18`; `RiptOPL/src/gfx/themeelems.cpp:19`
- Problem: The mode-based vcdDisplayName was superseded by the row-aware vcdDisplayNameForRow (vcdsupport.cpp:190, used by cfgtitle.cpp, themeelems.cpp, rabadge.cpp). The only remaining mentions of vcdDisplayName are its definition, its prototype, comments, and the '// vcdDisplayName' notes on three #include lines; no code and no test calls it.
- Fix: Delete vcdDisplayName and its prototype/comment block. Fix the include comments; coverflow.cpp and themes.cpp appear to use nothing else from vcdsupport.h, so their #include can probably go too (check the build).
- Benefit: ~20 source lines removed; no ELF change, the main ELF links with --gc-sections and -ffunction-sections (ps2.yaml:645,649) (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-173: vmc_groups.cpp: two whole groups (XEBP_100.72, XEBP_000.12) and five duplicate entries are unreachable

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/core/vmc_groups.cpp:1149-1153`; `RiptOPL/src/core/vmc_groups.cpp:1424`; `RiptOPL/src/core/vmc_groups.cpp:1271-1276`; `RiptOPL/src/core/vmc_groups.cpp:1439`; `RiptOPL/src/core/vmc_groups.cpp:668`; `RiptOPL/src/core/vmc_groups.cpp:676`; `RiptOPL/src/core/vmc_groups.cpp:1029`; `RiptOPL/src/core/vmc_groups.cpp:1057`; `RiptOPL/src/core/vmc_groups.cpp:1226`
- Problem: The lookup returns the first group in table order (vmc_groups.cpp:1459-1469). XEBP_100.72 (1149-1152) repeats XEBP_100.71's three ids exactly (1144-1146) and every XEBP_000.12 id (1272-1275) is already in XEBP_100.53 (936-939), so both groups can never be returned. Lines 668, 676, 1029, 1057, 1226 repeat an id already listed earlier in the same group.
- Fix: Delete the titles_XEBP_100_72 and titles_XEBP_000_12 arrays with their count lines and table rows, and the five in-group duplicate lines. Behaviour is unchanged. XEBP_100.72 looks like a copy-paste of 100.71 (the history comment at 1153 says the same), so whoever owns the data may want to look up its real titles instead.
- Benefit: ~20 source lines; ~12 dead entries x (12-byte string + 4-byte pointer) + 2 x 12-byte rows = ~215 bytes of ELF data (INFERRED arithmetic).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-174: Drive enumeration and size formatting are implemented twice (Drive Status vs File Manager)

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/ui/drivestatus.cpp:86-140`; `RiptOPL/src/ui/guifilemgr.cpp:174-217`; `RiptOPL/src/ui/drivestatus.cpp:35-45`; `RiptOPL/src/core/filemgr.cpp:627-636`
- Problem: fmScanDrives repeats dsScan's BDM loop (fileXioDopen, bdmReadDriverName, dsDriverLabel, exFAT-HDD fallback), the mmce0/1 opendir probe, the mcGetInfo+mcSync loop with _STR_DRIVE_STATUS_MC and the hddGetSpace/dsSizeText HDD line; about 25 lines are the same logic. fmSizeText is dsSizeText with a bytes-to-KB rounding and a decimal MB branch.
- Fix: Have fmSizeText round bytes to KB and call dsSizeText (accepting one MB format), and move the shared probes into one drive walker used by both pages, each keeping only its own line formatting.
- Justification: Same logic in two files; one walker plus a callback or flag removes more lines than it adds and keeps the two drive lists from drifting apart (they already differ on the mc free-cluster check).
- Benefit: ~20 lines net removed (INFERRED: ~25 duplicated scan lines and 9 formatter lines, minus a ~12-line shared walker); no measurable ELF change
- Effort: M. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-175: diaLayoutBottom repeats diaRenderUI's whole row-layout walk

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/ui/dia.cpp:566-610`; `RiptOPL/src/ui/dia.cpp:650-728`
- Problem: diaLayoutBottom walks the items 'exactly as diaRenderUI lays them out': the row-collapse, break-before/after, lead, spacer advance and hmax/y bookkeeping are the same statements in both loops (about 25 lines), so any layout change must be made twice.
- Fix: Let diaRenderUI's pass use one shared walker (diaLayoutBottom with an optional draw step, or a per-item callback) so the bookkeeping exists once.
- Benefit: ~20 lines removed and one sync hazard gone (INFERRED)
- Effort: M. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-176: udpfsDiscoverThemes and udpfsDiscoverLanguages are the same any-case root scan, and with udpfsDiscoverArtFolder list the root three times per poll

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/udpfssupport.cpp:382-414`; `RiptOPL/src/sources/udpfssupport.cpp:419-455`; `RiptOPL/src/sources/udpfssupport.cpp:492-522`; `RiptOPL/src/sources/udpfssupport.cpp:575-583`
- Problem: udpfsDiscoverThemes (382-414) and udpfsDiscoverLanguages (419-455) differ only in the folder name (THM/LNG), the register call (thmAddElements/lngAddLanguages), the latch, and the fact that only the LNG copy handles readdir errors and unopenable entries. udpfsDiscoverArtFolder opens and lists the same root again (503-520). needsUpdate calls all three on every poll until each latches, so an unlatched session lists udpfs:/ over the network up to three times per poll.
- Fix: Merge the THM and LNG functions into one helper that takes the folder name and a register callback (keeping the LNG copy's errno/failed handling for both), and let one root listing per poll feed THM, LNG and ART together. test_udpfs_themes.py and test_udpfs_art.py extract these functions by exact signature and must be updated with the change.
- Benefit: ~20 lines removed (INFERRED: the two 33-37 line functions share ~25 lines); up to two fewer network directory listings per background poll before discovery latches.
- Effort: M. Risk: Med. Confidence: Med (READ)

#### RIPTOPL-177: PS2-logo probe (open, CheckPS2Logo, close) copied 5 times in ETH, MMCE and UDPFS launch paths

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/ethsupport.cpp:1242-1249`; `RiptOPL/src/sources/ethsupport.cpp:1479-1485`; `RiptOPL/src/sources/mmcesupport.cpp:1503-1508`; `RiptOPL/src/sources/udpfssupport.cpp:948-953`; `RiptOPL/src/sources/udpfssupport.cpp:1124-1130`
- Problem: Each copy is `if (gPS2Logo) { fd = open(path, O_RDONLY, 0666); if (fd >= 0) { EnablePS2Logo = CheckPS2Logo(fd, 0); close(fd); } }`, identical except for the path variable.
- Fix: Add one helper next to CheckPS2Logo in src/platform/util.cpp (e.g. CheckPS2LogoPath(const char *path), returning 0 when gPS2Logo is off or the open fails) and replace the five blocks with one-line calls.
- Justification: Five identical 6-7 line blocks; an ~9-line helper replaces ~33 lines with five calls, net ~19 lines.
- Benefit: ~33 lines -> 5 calls + ~9-line helper = ~19 lines net (INFERRED from cited ranges).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-178: guiDrawIconAndText duplicates guiDrawIconAndLiteralText except for the _l() lookup

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/gfx/uidraw.cpp:12-34`; `RiptOPL/src/gfx/uidraw.cpp:36-57`
- Problem: The two functions are line-for-line identical (icon width, VCENTER offset, rmDrawPixmap, x advance, fntRenderString) except that the first renders _l(textId) and the second a caller string.
- Fix: Make guiDrawIconAndText a one-line call: return guiDrawIconAndLiteralText(iconId, _l(textId), font, x, y, color).
- Benefit: ~19 source lines removed; one copy of the draw code (~100-150 bytes .text, INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-179: vcdDisplayName(mode, text) is never called; only vcdDisplayNameForRow is used

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/sources/vcdsupport.cpp:178-188`; `RiptOPL/include/vcdsupport.h:101-105`; `RiptOPL/src/sources/vcdsupport.cpp:978-981`; `RiptOPL/src/gfx/coverflow.cpp:17`; `RiptOPL/src/gfx/themeelems.cpp:19`; `RiptOPL/src/gfx/themes.cpp:18`
- Problem: The repo-wide matches for 'vcdDisplayName(' are its definition (vcdsupport.cpp:178) and prototype (vcdsupport.h:105); all display callers use vcdDisplayNameForRow. The section banner at vcdsupport.cpp:978-981 still calls it 'the only piece that stayed' and places it 'below', and three include comments name it as the reason for including vcdsupport.h.
- Fix: Delete vcdDisplayName and its prototype/comment block, delete the stale banner at vcdsupport.cpp:978-981, and correct the three include comments (or drop the includes if nothing else from vcdsupport.h is used there).
- Benefit: ~19 source lines removed; the 144-byte function is already dropped by --gc-sections, so no ELF change (MEASURED: evidence/RiptOPL/raw/riptopl-discarded-firstparty.txt lists .text._Z14vcdDisplayNameiPKc 144).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-180: Copy-paste slot blocks: checkLoadConfigMMCE and trySaveConfigMMCE repeat the mmce0/mmce1 body; writeConfigPathRedirect repeats its write body

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/app/settingsio.cpp:88-118`; `RiptOPL/src/app/settingsio.cpp:1984-2001`; `RiptOPL/src/app/settingsio.cpp:348-354`; `RiptOPL/src/app/settingsio.cpp:368-372`
- Problem: checkLoadConfigMMCE has two identical 12-line blocks differing only in "mmce0:" vs "mmce1:", and trySaveConfigMMCE has two identical 6-line blocks the same way. writeConfigPathRedirect writes path plus newline and checks both write counts twice (348-354 and 368-372).
- Fix: Loop over the two MMCE roots in both functions; fold the two redirect writes into one small static write-path-and-newline function used by both the CWD copy and the mc mirror.
- Justification: The small write function replaces two identical 5-line bodies, so it removes code overall.
- Benefit: ~18 source lines removed (INFERRED: 31->17 and 18->12 lines for the MMCE pair, ~4 for the redirect write).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-181: Four module-load wrappers where two would do (bdmLoadOptionalModule/Args, bdmDiagLoadOptionalModule/Args)

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/sources/bdmsupport.cpp:586-610`; `RiptOPL/src/sources/bdmsupport.cpp:711-729`
- Problem: bdmLoadOptionalModuleArgs is bdmLoadOptionalModule with arglen/args passed to sysLoadModuleBuffer instead of 0/NULL, and bdmDiagLoadOptionalModuleArgs is likewise a copy of bdmDiagLoadOptionalModule. Each pair is identical apart from those two arguments.
- Fix: Delete bdmLoadOptionalModuleArgs and bdmDiagLoadOptionalModuleArgs and give the remaining two functions arglen/args parameters; pass 0, NULL at the no-arg call sites (bdmsupport.cpp, bdmdevices.cpp:508-520, prototype bdmint.h:24, stub in .github/scripts/test_pccf.py:79).
- Benefit: ~20 source lines removed (two 11- and 9-line functions) (INFERRED); no ELF change worth stating.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-182: MmceSource::launch carries dead autolaunch branches, a commented-out block and other commented-out code

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/sources/mmcesupport.cpp:1304-1310`; `RiptOPL/src/sources/mmcesupport.cpp:1314`; `RiptOPL/src/sources/mmcesupport.cpp:1330-1339`; `RiptOPL/src/sources/mmcesupport.cpp:1731-1741`; `RiptOPL/src/sources/mmcesupport.cpp:1801-1802`; `RiptOPL/src/sources/mmcesupport.cpp:1818-1819`; `RiptOPL/src/sources/mmcesupport.cpp:28`
- Problem: gAutoLaunchBDMGame is only set by autoLaunchBDMGame (autolaunch.cpp:138), which runs miniInit(BDM_MODE) and launches through bdmLaunchGame (autolaunch.cpp:292); it never reaches MmceSource::launch, so the gAutoLaunchBDMGame tests are always false/true and the 'No autolaunch yet' block at 1735-1741 is commented out. With game always from activeGame (never NULL, returns &emptyGame), the game != NULL tests at 1314/1330/1333/1339 are always true. cleanUp/shutdown hold commented-out code, and <usbhdfsd-common.h> is included only for the commented USBMASS_DEVCTL_STOP_ALL line.
- Fix: Delete the gAutoLaunchBDMGame branches (game = activeGame(...) unconditionally; call deinit unconditionally), the commented-out blocks at 1735-1741, 1801-1802, 1818-1819, the always-true game != NULL tests, and the usbhdfsd-common.h include.
- Benefit: ~18 source lines removed; no ELF change beyond a few compares (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-183: RA_PROBE lab probe ladder: a compile-time knob that every build leaves at 0

- Category: 8 (Half-finished or redundant features)
- Location(s): `RiptOPL/ee_core/src/iopmgr.c:238-247`; `RiptOPL/ee_core/src/iopmgr.c:342`; `RiptOPL/ee_core/src/ra.c:242-244`; `RiptOPL/ee_core/src/ra.c:290`; `RiptOPL/ee_core/src/ra.c:368-370`; `RiptOPL/ee_core/src/ra.c:393-395`; `RiptOPL/ee_core/include/ra.h:7-11`
- Problem: RA_PROBE is defined only as a default 0 in ra.h:9-10 ('0 in every real build'); no ps2.yaml, tools/*.py or workflow defines it. All tests on it (iopmgr.c:244,246,342; ra.c:244,290,369,395) are constant, plus comments describing a 12.09 lab hunt.
- Fix: Delete the RA_PROBE define and its comments, and drop every RA_PROBE term from the conditions (the SMSTCPIP/SMAP loads, the raudp load, ra_snap_send's loops, the early return and the RA_OnVblank test become unconditional as at RA_PROBE 0). Drop the '#define RA_PROBE 0' stubs in .github/scripts/test_ra_launch_resources.py:40 and test_ra_runtime_budget.py:55.
- Benefit: ~18 lines of conditions and lab comments removed; no ELF change, the compiler already folds RA_PROBE == 0 (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-184: achievements.cpp jsonPutUtf8 re-implements textshape txtPut

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/achievements.cpp:158-178`; `RiptOPL/src/gfx/textshape.cpp:36-65`
- Problem: jsonPutUtf8 (21 lines) encodes a BMP code point as UTF-8 into a bounded buffer with the same fit rule as txtPut (at+n must leave room for the terminator); txtPut is always linked (src/gfx is in the main ELF) and handles 4-byte sequences too. The only difference is the failure value (unchanged at vs -1).
- Fix: Delete jsonPutUtf8 and at its single call site (achievements.cpp:219) call txtPut, keeping at when it returns -1.
- Benefit: ~18 lines net removed in the RetroAchievements build; ~100 bytes .text (INFERRED, small static function)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-185: elfldr/loader.c: duplicated launch tail, repeated error cleanup and an always-true argc test

- Category: 2 (Duplication)
- Location(s): `RiptOPL/elfldr/loader.c:447-457`; `RiptOPL/elfldr/loader.c:461-471`; `RiptOPL/elfldr/loader.c:266-304`; `RiptOPL/elfldr/loader.c:398-417`
- Problem: main() repeats the same 9-line hand-off (cleanupHddForLaunch, resetIOP, SifExitRpc, two FlushCache, paint, ExecPS2) for the LOADFILE and fileXio paths, differing only in where entry/gp come from. loadElfViaFileXio repeats fileXioClose/fileXioExit/return -1 four times. After 'if (argc < 2) return' at 398, 'if (argc > 1)' at 401 is always true.
- Fix: Set entry/gp from elfdata on LOADFILE success, else from loadElfViaFileXio, and run one shared hand-off tail; use a single fail label for the close/exit/return in loadElfViaFileXio; drop the redundant if (argc > 1) wrapper.
- Benefit: ~18 source lines; a few dozen bytes off elfldr.elf, which is embedded in the main ELF (INFERRED, not measured).
- Effort: S. Risk: Med. Confidence: High (READ)

#### RIPTOPL-186: httpTestServer keeps a static 2 KB probe buffer and repeats httpProbeRead's read loop

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/src/sources/httpsupport.cpp:474-497`; `RiptOPL/src/sources/httpsupport.cpp:676-697`
- Problem: The Test button's range probe reads into `static char probe[2048]`, 2048 bytes of .bss held for the whole session, through a copy of httpProbeRead's loop with the same got == length && hasTotal test. The function runs on the IO worker (guisettings.cpp:952-953 through guiHandleDeferedIO), whose stack is 96 KB, so the buffer can live on the stack.
- Fix: Move httpProbeRead above httpTestServer and replace lines 474-497 with one httpProbeRead(&testEndpoint, probePath, 0, 2048, buf, &total) call into a local 2 KB buffer. Delete the static array.
- Benefit: 2,048 bytes .bss (MEASURED: evidence/RiptOPL/raw/riptopl-nm-size-sorted.full.txt:362, size 0x800) and ~18 source lines
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-187: httpTestServer re-implements httpProbeRead for its 2 KB range probe

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/httpsupport.cpp:429-430`; `RiptOPL/src/sources/httpsupport.cpp:469-498`; `RiptOPL/src/sources/httpsupport.cpp:676-697`
- Problem: The Test button's probe (openRequest with a range, read loop until 2048 bytes or EOF, close, then check got == 2048 && hasTotal) is the same logic as httpProbeRead, which does openRequest, the same read loop, close, and returns got == length && hasTotal. httpProbeRead is defined later in the file, which is probably why it was copied.
- Fix: Move httpProbeRead (and httpProbeImage if preferred) above httpTestServer and replace lines 474-497 with one call: httpProbeRead(&testEndpoint, probePath, 0, 2048, probe, &total), showing _STR_HTTP_ERR_RANGE on 0. Drop the now unused locals contentLen, hasTotal, sock, r and the extra brace blocks.
- Benefit: ~18 source lines removed; a few dozen bytes of code (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-188: rawatch: header and node-tail validation written twice (setWatchList vs loadWatchList) and repeated again in takeNodes

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/rawatch.cpp:164-174`; `RiptOPL/src/sources/rawatch.cpp:252-274`; `RiptOPL/src/sources/rawatch.cpp:288-290`; `RiptOPL/src/sources/rawatch.cpp:184-186`; `RiptOPL/src/sources/rawatch.cpp:296-297`; `RiptOPL/src/sources/rawatch.cpp:83-84`
- Problem: The magic/version/count/bytes checks on struct ra_watch_file appear in setWatchList (164-174) and again, with LOG lines, in loadWatchList (252-274); the commit of count/bytes/startup is duplicated (184-186 and 288-290). loadWatchList also re-checks nf.count == 0 and the node magic (296-297) although takeNodes rejects both at 83-84; only the count > RA_NODE_MAX bound is needed there before the read into nodeList_.
- Fix: Move the four header checks into one static function returning the same -3..-6 codes (one LOG with the code is enough) and call it from both loaders; drop the nf.count == 0 and magic tests at 296-297 and let takeNodes reject them.
- Justification: The helper replaces two copies of the same four checks; net code goes down (about 30 lines become about 12).
- Benefit: ~18 net source lines removed (INFERRED from the cited ranges); RA overlay only
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-189: tlb.c: mutable TLBInfo struct only re-wraps constants; write-only NumWiredEntries, no-op kprintf, unused returns

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/ee_core/src/tlb.c:2-5`; `RiptOPL/ee_core/src/tlb.c:48-57`; `RiptOPL/ee_core/src/tlb.c:113`; `RiptOPL/ee_core/src/tlb.c:120`; `RiptOPL/ee_core/src/tlb.c:133-141`
- Problem: struct TLBInfo (a static, writable .data object) holds the three TLB_NUM_* constants and pointers to the three const tables, all used only by InitTLB32MB in the same file. NumWiredEntries is written at line 133 and never read; kprintf is defined empty and only feeds line 120; stdio.h is not needed; the NumExtendedTLBEntries > 0 test is always true; InitTLB32MB's and SetTLBEntry's return values are ignored by every caller.
- Fix: Delete struct TLBInfo and its instance, loop over kernelTLB/defaultTLB/extendTLB with the TLB_NUM_* macros directly, drop kprintf, line 120, stdio.h, the always-true if, and make both functions void.
- Benefit: ~18 lines removed; 28 bytes of .data (7 words) plus the loads through it (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-190: hddUpdateGameListCache detects only partition-name changes, so games.bin keeps stale start_sector/name/size and the next boot launches from it

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/sources/hddsupport.cpp:3155-3179`; `RiptOPL/src/sources/hddsupport.cpp:1860-1866`; `RiptOPL/src/sources/hddsupport.cpp:2779`
- Problem: The rewrite test only asks whether every cached partition_name still exists and whether the counts match. A game reinstalled under the same partition label (new start_sector/size) or retitled in its HDL header leaves games.bin untouched. At the next boot update() publishes the cache without any APA scan (forceUpdate is 0), and the native launch writes the stale game->start_sector into cdvdman's lba_start. The game then fails to boot until the user refreshes. The log labels are also swapped: a cached game missing from the scan is logged as "game added".
- Fix: Replace the nested partition-name loops with one test: rewrite when the counts differ or memcmp of the two arrays differs. Both arrays are byte-deterministic because hddGetHDLGamelist memsets the records and walks the APA table in a fixed order.
- Benefit: Fixes a stale-LBA boot failure on the opt-in HDD game list cache. Removes about 18 lines and the O(n*m) strncmp loop (INFERRED from 3155-3176).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-191: sbPopulateConfigIn re-implements sbCreatePath_name's path composition for its size stat (and differs from it)

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/supportbase.cpp:1450-1481`; `RiptOPL/src/sources/supportbase.cpp:1210-1238`
- Problem: The size-stat block rebuilds the subseg string (1453-1457, identical to 1218-1222) and the ISO, OLD_ISO and USBLD part paths (1462-1479) that sbCreatePath_name already composes. The copy inserts an extra separator after the prefix ("%s%s%s..." prefix, sep, CD/DVD at 1463/1467) where sbCreatePath does not, so the stat path is e.g. udpfs://CD/x.iso while the launch path is udpfs:/CD/x.iso.
- Fix: Replace 1453-1480 with one loop over the game's parts that calls sbCreatePath(game, gamepath, prefix, sep, part) and adds sbFileSizeMB(gamepath); keep the isPs1/sizeMB/gate condition.
- Benefit: ~18 lines removed and one path rule instead of two (INFERRED: 31-line block becomes ~10 lines).
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-192: Eight hand-unrolled overlay corner key reads in initImageTexture

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/gfx/themeelems.cpp:726-753`
- Problem: The eight <name>_overlay_ulx..lry keys are read with eight copies of snprintf + configGetInt + assign (24 lines). configGetInt leaves the output untouched when the key is absent (config.cpp:420-428) and the texture is calloc'd, so each can read straight into the field.
- Fix: Loop over a small static table of the eight key suffixes paired with the eight field pointers (or make the corners an int[8]) and call configGetInt(themeConfig, prop, field) once in the loop.
- Justification: Replaces 24 repeated lines with a ~7-line loop and table; no behaviour change.
- Benefit: ~17 source lines removed; seven fewer inlined snprintf/configGetInt sequences (~300 bytes .text, INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-193: guiShowNetConfig pre-seeds enabled/visible states that netConfigUpdater(1) overwrites before the first frame

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/ui/guisettings.cpp:854-855`; `RiptOPL/src/ui/guisettings.cpp:859-860`; `RiptOPL/src/ui/guisettings.cpp:864-869`; `RiptOPL/src/ui/guisettings.cpp:877-878`; `RiptOPL/src/ui/guisettings.cpp:920-923`; `RiptOPL/src/ui/guisettings.cpp:696`
- Problem: guiShowNetConfig calls netConfigUpdater(1) at 928 before diaExecuteDialog, and the updater recomputes every one of these: ETHOPMODE/SHARE_PORT enabled (730-731), SHARE_NB_ADDR visible (712), the PS2 IP/mask/gateway/DNS enabled and share-IP visible loops (719-728), UDPFSMODE and SMBDIALECT enabled (755-765). The comment at 920-921 ('renders the FIRST frame before it calls netConfigUpdater') is stale for that reason. Line 860 is diaSetInt on a UI_STRING row, which diaSetInt ignores (diaitems.cpp:104). Inside the updater, 696 is overwritten by 712.
- Fix: Delete the listed setEnabled/setVisible lines (keep the diaSetInt value seeding in the loops), the no-op diaSetInt at 860, the stale comment, and line 696.
- Benefit: ~17 lines removed and a few dozen redundant dialog lookups per page open (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-194: 13 dialogs.h item ids are never used, including two declared 'unused placeholder' and a 'retired' reserved slot

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/dialogs.h:27`; `RiptOPL/include/dialogs.h:51`; `RiptOPL/include/dialogs.h:73-74`; `RiptOPL/include/dialogs.h:112`; `RiptOPL/include/dialogs.h:244`; `RiptOPL/include/dialogs.h:250`; `RiptOPL/include/dialogs.h:278-279`; `RiptOPL/include/dialogs.h:342-345`; `RiptOPL/include/dialogs.h:364`; `RiptOPL/include/dialogs.h:403-407`; `RiptOPL/src/ui/dialogs.cpp:494-495`; `RiptOPL/.github/scripts/test_settings_back_keeps_edits.py:422-423`
- Problem: UICFG_ENABLE_ANALOG_NAV, CFG_ETHMODE, CFG_ENABLEUDPBD, CFG_NETBOOTPROTOCOL, CFG_POPSTARTER_DEVICE, CHTCFG_CHEATCFG, GSMCFG_GSCONFIG, COMPAT_RETIRED_DOWNLOAD_DEFAULTS, SECURITY_PARENTAL_BUTTON, ADV_PREFIX_BUTTON, ADV_STORAGE_BUTTON, CFG_HDDOPLPART and PADCFG_PADEMU_CONFIG appear nowhere outside dialogs.h (one comment and one source-text test). The 'preserve IDs' reasoning does not hold: the auto run before COMPAT_DMA = 100 already reaches 242 (251 with RA), and before COMPAT_MODE_BASE = 250 reaches 277 with PADEMU, so the explicit values reserve nothing.
- Fix: Delete the 13 enumerators and the comments that defend them (dialogs.h:73-74 notes, :278, :407 note, dialogs.cpp:494-495 sentence), and drop the test assertion that requires COMPAT_RETIRED_DOWNLOAD_DEFAULTS.
- Benefit: ~16 source lines; no ELF change (enumerators emit no code) (INFERRED)
- Effort: S. Risk: Low. Confidence: High (INFERRED)

#### RIPTOPL-195: Linked-list push in addAppsLegacyList and appScanCallback: both if/else branches do the same thing, twice

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/appsupport.cpp:677-689`; `RiptOPL/src/sources/appsupport.cpp:736-748`
- Problem: Each block mallocs a node and, in the NULL-head branch, sets next = NULL and head = app; in the other branch next = head and head = app. Since head is NULL in the first branch, both branches are 'app->next = *list; *list = app'. The 13-line block is copied verbatim into two functions.
- Fix: Collapse each block to one malloc plus 'if (app) { app->next = *appsLinkedList; *appsLinkedList = app; }' (or one static push helper used by both).
- Benefit: ~16 lines removed (2 x 13 lines -> 2 x 5 lines)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-196: Grid navigation repeats the count/index walk three times and the select-by-index tail twice

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/ui/menusys.cpp:2599-2605`; `RiptOPL/src/ui/menusys.cpp:2616-2622`; `RiptOPL/src/ui/menusys.cpp:2639-2645`; `RiptOPL/src/ui/menusys.cpp:2656-2662`; `RiptOPL/src/ui/menusys.cpp:2675-2681`
- Problem: menuGridRow, menuGridPage and menuGridAtRowStart each open with the same 7-line loop that counts the submenu and finds the selected index; menuGridRow and menuGridPage end with the same 7-line 'walk to target, set current and pagestart, play cursor sound' tail.
- Fix: Add one static function returning count and index, and one that selects row N; call them from the three functions.
- Justification: Replaces three and two verbatim copies; net removal after the helpers is positive.
- Benefit: ~15 lines removed (3x7 + 2x7 = 35 duplicated lines -> ~20 with two small helpers) (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-197: Lab-only RA_PROBE ladder is 0 in every build but still threads through ra.c and iopmgr.c

- Category: 1 (Dead code)
- Location(s): `RiptOPL/ee_core/src/ra.c:242-244`; `RiptOPL/ee_core/src/ra.c:290`; `RiptOPL/ee_core/src/ra.c:368-370`; `RiptOPL/ee_core/src/ra.c:393-395`; `RiptOPL/ee_core/include/ra.h:7-11`; `RiptOPL/ee_core/src/iopmgr.c:238-247`; `RiptOPL/ee_core/src/iopmgr.c:342`
- Problem: ra.h says RA_PROBE is a lab probe ladder, 0 in every real build. No ps2.yaml, tools/*.py or workflow defines it; only two host tests set it to 0. All RA_PROBE tests are constant-false or constant-true in shipped builds and exist only for a past X-Men Origins investigation.
- Fix: Delete the RA_PROBE default in ra.h and every RA_PROBE condition and its comment, keeping the RA_PROBE == 0 behaviour; drop the defines from the two host tests.
- Benefit: ~15 lines removed; constant-folded today, so no ELF change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-198: Three hand-written copies of the Neutrino argument token walk (neutrinoArgHasActiveFlag, neutrinoEffectiveBsdfs, neutrinoArgActiveValue)

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/platform/sysneutrino.cpp:199-219`; `RiptOPL/src/platform/sysneutrino.cpp:228-261`; `RiptOPL/src/platform/sysneutrino.cpp:881-904`
- Problem: Each function re-implements the same skip-blanks / find-token-end / stop-at --b walk over gNeutrinoArgs and extraArgs. neutrinoEffectiveBsdfs (234-254) is exactly neutrinoArgActiveValue(extraArgs, "-bsdfs=", ...) followed by a name lookup: both scan global then per-game args, require a non-empty value, and let the last token win.
- Fix: Move neutrinoArgActiveValue above neutrinoEffectiveBsdfs and replace the 21-line loop in neutrinoEffectiveBsdfs with one call into a 16-byte buffer plus the names[] comparison.
- Benefit: About 15 lines and one fewer copy of tokenizer rules that must agree with Neutrino's parser; a few hundred bytes of .text at most (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-199: guigame.cpp redeclares genvmc.h's status struct and uses raw devctl codes; system.cpp keeps an unused stale createVMCparam_t copy

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/ui/guigame.cpp:128-134`; `RiptOPL/src/ui/guigame.cpp:228`; `RiptOPL/src/ui/guigame.cpp:611`; `RiptOPL/src/ui/guigame.cpp:707`; `RiptOPL/src/ui/guigame.cpp:736-740`; `RiptOPL/src/platform/system.cpp:69-76`; `RiptOPL/src/platform/sysvmc.cpp:272`; `RiptOPL/modules/vmc/genvmc/genvmc.h:11-42`
- Problem: statusVMCparam_t at guigame.cpp:128-134 is a hand copy of genvmc.h:36-42, and the GENVMC_DEVCTL_STATUS/ABORT codes are written as 0xC0DE0003/0xC0DE0002 literals; EE code already includes modules/vmc/genvmc/genvmc.h (sysvmc.cpp:65, vmcrebuild.cpp:19). system.cpp:69-76 defines an older 5-field createVMCparam_t that nothing in system.cpp uses (the real one with VMC_flags is genvmc.h:26-34).
- Fix: Include modules/vmc/genvmc/genvmc.h in guigame.cpp, delete the local typedef and use GENVMC_DEVCTL_STATUS, GENVMC_DEVCTL_ABORT and GENVMC_STAT_AVAIL; use GENVMC_DEVCTL_CREATE_VMC at sysvmc.cpp:272. Delete the typedef at system.cpp:69-76.
- Benefit: ~15 source lines removed; one struct definition instead of three, so a layout change in genvmc.h cannot silently desync the EE side; no ELF change (INFERRED: typedefs emit no code)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-200: sysLastPathSeparator is a byte-identical copy of supportbase.cpp's sbLastPathSeparator

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/platform/sysneutrino.cpp:279-291`; `RiptOPL/src/sources/supportbase.cpp:1955-1967`; `RiptOPL/src/platform/util.cpp:288-292`
- Problem: The 13-line static helper exists twice with only the name changed, and util.cpp:288-292 open-codes the same last-of-slash-or-backslash logic inline.
- Fix: Keep one non-static copy (for example in util.cpp with a prototype in util.h), delete the other, and use it at util.cpp:288-292.
- Justification: Adding one prototype lets a 13-line duplicate and a 4-line inline copy go, a net reduction.
- Benefit: About 15 lines; tens of bytes of .text (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-201: bdmLoadBlockDeviceModules re-implements bdmEnsureTransportLoaded for USB, iLink, MX4SIO and ATA

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/bdmsupport.cpp:1275-1297`; `RiptOPL/src/sources/bdmsupport.cpp:1301-1320`; `RiptOPL/src/sources/bdmdevices.cpp:504-532`
- Problem: bdmEnsureTransportLoaded (bdmdevices.cpp:504-532) already loads each local transport with the same latches (iUSBModLoaded, iLinkManModLoaded/ieee1394ModLoaded/iLinkModLoaded, mx4sioModLoaded with the host: gate, hddModLoaded). bdmLoadBlockDeviceModules repeats the iLink block line for line, the MX4SIO gate and the ATA load, differing only in gEnable* gating and diag labels. It also sets the USB boot status at 1276 although bdmLoadUsbMassBd sets the same string again at 1176.
- Fix: In bdmLoadBlockDeviceModules call bdmEnsureTransportLoaded(BDM_TYPE_USB/ILINK/SDC/ATA) under the existing gEnable* checks, keeping only the ATA post-settle devctl probe locally. Delete the duplicate bootStatusSticky at 1276.
- Benefit: ~15 source lines removed (iLink 9 lines, MX4SIO 4, USB status 1, ATA partly) (INFERRED). The diag-only BEGIN/END stage labels would be lost or need moving into bdmEnsureTransportLoaded.
- Effort: S. Risk: Med. Confidence: Med (READ)

#### RIPTOPL-202: sysCheckVMCEx reads and validates the VMC superblock with two copies of the same loop

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/platform/sysvmc.cpp:160-185`; `RiptOPL/src/platform/sysvmc.cpp:187-216`
- Problem: When the caller passes no superblock and the size is whole-MB, lines 168-185 seek, loop-read a vmc_superblock_t into a local and check magic/type/geometry; lines 188-216 repeat the same seek, loop-read and checks into vmc_superblock. The function already redirects a NULL pointer to a local (autoSuperblock, 161-163) for the format probe.
- Fix: Always point vmc_superblock at the local autoSuperblock when the caller passed NULL, read it once with the second loop, and keep the first block only as the early-return decision based on that single validation result.
- Benefit: ~15 lines removed and one fewer seek+read of 512+ bytes per probe on a whole-MB card (INFERRED)
- Effort: S. Risk: Med. Confidence: Med (READ)

#### RIPTOPL-203: texLoadArtFile opens and reads a .png art file twice when its bytes are not a PNG (JPG-in-.png, corrupt PNG)

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/gfx/textures.cpp:1328-1334`; `RiptOPL/src/gfx/textures.cpp:1269-1288`
- Problem: For a .png name, texLoad -> texLoadAll opens the file and (on mmce/mass) stages it whole, then libpng fails on the signature and the buffer is freed. texLoadArtFile then calls texLoadJpgFile, which opens the same path again (another FAT directory walk, which the comments at 533-546 measure in seconds on big ART folders) and reads the whole file again just to test for a JPG. This happens on every load of a JPG saved as .png (a case the comment at 1144-1146 says is supported) and on every failed PNG, doubling the cost of finding 1's retries.
- Fix: In texLoadArtFile read the non-tar file into RAM once (the open + texStageExternalFileIntoMemory that texLoadJpgFile already does) and decode it with texLoadArtFromMemory, which already tries PNG and then JPG from one buffer. Delete texLoadJpgFile and the extension branch. This moves HDD/SMB PNG art from the 4 KB streaming reader to one staged read, as .jpg art already does.
- Benefit: One open and one read per art file instead of two for JPG-named-.png and broken files; about 15 lines removed (texLoadJpgFile 22 lines and the extension branch, replaced by about 8). INFERRED.
- Effort: S. Risk: Med. Confidence: Med (READ)

#### RIPTOPL-204: favPrepareSave re-reads and re-parses the primary file that favReadFile just validated on every favourite/recent save

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/sources/favsupport.cpp:517-545`; `RiptOPL/src/sources/favsupport.cpp:554`; `RiptOPL/src/sources/favsupport.cpp:395-423`; `RiptOPL/src/sources/favsupport.cpp:1011-1016`; `RiptOPL/src/sources/favsupport.cpp:1058-1063`; `RiptOPL/src/sources/favsupport.cpp:1491-1496`
- Problem: All three writers (addItem, removeByIdAndText, recentRecord) call favReadFile with editable and refuse to write unless it authorises the write; favWriteFile then runs favPrepareSave, which loads and parses the same primary (and staged) file again and recomputes the same decision (primary complete wins; else staged complete and primary missing or recoverable; else both missing). The only extra work it does is copying a complete .new back over a torn primary. recentRecord runs on every game launch, so each launch pays an extra open/read/parse of the primary before the two write+readback cycles.
- Fix: Let favReadFile report which copy was authoritative (primary, staged, or none) and pass that to favWriteFile; restore the primary from the staged image only in the staged case and drop favImageValid and the primary re-read in favPrepareSave.
- Benefit: ~15 lines removed and one fewer whole-file read (open, 2 lseek, read, tail read, close, about 6 IOP RPCs) of the memory-card file per favourite toggle and per launch (INFERRED from favLoadImage 344-373)
- Effort: M. Risk: High. Confidence: Med (INFERRED)

#### RIPTOPL-205: gNetBootProtocol is a derived shadow of gNetworkProtocol recomputed in four places

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/include/opl.h:337-347`; `RiptOPL/src/app/settingsio.cpp:1778`; `RiptOPL/src/app/settingsio.cpp:1829`; `RiptOPL/src/core/riptlite.cpp:84`; `RiptOPL/src/ui/guisettings.cpp:259`; `RiptOPL/src/sources/bdmsupport.cpp:739`; `RiptOPL/src/sources/bdmsupport.cpp:1328-1336`
- Problem: opl.h says the NET_BOOT_* shadows are kept 'so downstream consumers can migrate incrementally'. gNetBootProtocol is always set to (gNetworkProtocol == NET_PROTO_UDPFSBD ? NET_BOOT_UDPFS : NET_BOOT_UDPBD) at four sites, and its only runtime readers are three lines in bdmsupport.cpp plus the Settings snapshot (guisettings.cpp:3023,3076). Its one real job is carrying the legacy config key into the migration at settingsio.cpp:1759.
- Fix: Have the three bdmsupport.cpp readers test gNetworkProtocol == NET_PROTO_UDPFSBD directly, read the legacy key into a local in the settingsio.cpp migration, and delete the global, its four re-derivations, the snapshot field and the NET_BOOT_* enum. gEnableUDPBD follows the same pattern and can be folded the same way.
- Benefit: ~15 lines and one global removed, and one fewer state that can drift from gNetworkProtocol; a few dozen bytes (INFERRED).
- Effort: M. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-206: PrepareGSM: unused cmdline parameter, always-true config check, and the caller copies an identical struct field by field

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/core/gsm.cpp:95`; `RiptOPL/src/core/gsm.cpp:199-211`; `RiptOPL/include/pggsm.h:64`; `RiptOPL/src/app/host.cpp:202-217`
- Problem: PrepareGSM never reads its cmdline argument, and its only caller (host.cpp:206) passes NULL for it and a stack struct for config, so the if (config) guard at gsm.cpp:199 is always true. host.cpp:207-217 then copies all 11 fields of that local GsmConfig_t into config->GsmConfig, which is the same struct GsmConfig_t (ee_core/include/coreconfig.h:14-27, member at :73, not packed).
- Fix: Drop the cmdline parameter (definition and pggsm.h prototype) and the if (config) wrapper, and call PrepareGSM(&config->GsmConfig) directly in host.cpp, deleting the local gsm_config and the 11 copy lines.
- Benefit: ~14 source lines removed and one 48-byte stack struct plus 11 copies per launch gone (INFERRED from the cited lines); negligible ELF change
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-207: ethReadOnlyCfgDir duplicates the read-only CFG folder block in BdmSource::config

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/ethsupport.cpp:1528-1546`; `RiptOPL/src/sources/bdmsupport.cpp:2052-2068`
- Problem: Both build `<home>[/]CFG/`, keep a static madeCfgDir[256] and mkdir the folder once per path, with the same comments; ~14 lines are the same. They differ only in the 'home is on this read-only device' test (nfs: prefix vs bdmIsReadOnlyPath) and ETH also accepting EEXIST.
- Fix: Move one sbReadOnlyCfgDir(cfgDir, size, excludeHome) into supportbase.cpp (taking the home-exclusion result as a parameter, and accepting EEXIST for both) and call it from both sources.
- Justification: Removes one of two near-identical copies (~14 lines net) and one of two 256-byte static madeCfgDir buffers; the EEXIST difference means BDM currently retries mkdir on every selection when the folder already exists.
- Benefit: ~14 lines and 256 bytes .bss removed (INFERRED: one static char madeCfgDir[256] dropped).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-208: sfxGetDropDiag is a stub that always reports 0/0; the debug HUD's SX field and six include comments describe counters that no longer exist

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/core/sfx.cpp:305-311`; `RiptOPL/include/sound.h:26-28`; `RiptOPL/src/ui/guioverlay.cpp:196-203`; `RiptOPL/src/ui/guioverlay.cpp:211-213`; `RiptOPL/src/ui/gui.cpp:29`; `RiptOPL/src/ui/guioverlay.cpp:19`; `RiptOPL/src/ui/guigameid.cpp:35`; `RiptOPL/src/ui/guimodal.cpp:23`; `RiptOPL/src/ui/guiwait.cpp:22`; `RiptOPL/src/ui/guisettings.cpp:33`
- Problem: sfxGetDropDiag writes 0 to both outputs; the dispatch ring and stale/full counters it describes (sound.h:26-27, 'see the counters' comment in sound.c') are gone. Its only caller is guioverlay.cpp:203, which prints the zeros as 'SX%u/%u' and documents them at :196-201. Six files carry an include comment naming the drop counters; only guioverlay.cpp calls either diag.
- Fix: Delete sfxGetDropDiag and its prototype, drop sxStale/sxFull and the SX%u/%u field from the HUD string and its comment lines, and shorten the six include comments to what is actually used (sfxPlay etc.).
- Benefit: ~14 lines removed and a misleading always-zero HUD value gone; a few dozen bytes of .text and format string (INFERRED: the stub is called, so it is linked)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-209: diaRenderItem re-implements diaItemHeight's height rules and the fixed-size percent arithmetic appears four times

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/ui/dia.cpp:251-255`; `RiptOPL/src/ui/dia.cpp:262-291`; `RiptOPL/src/ui/dia.cpp:315`; `RiptOPL/src/ui/dia.cpp:345`; `RiptOPL/src/ui/dia.cpp:381`; `RiptOPL/src/ui/dia.cpp:441`; `RiptOPL/src/ui/dia.cpp:457-475`
- Problem: diaItemHeight is documented as 'kept byte-for-byte in step with diaRenderItem's *h logic', i.e. the same rules (spacer 0, colour 17, wrapped lines, fixedHeight override) are written twice and must be kept in sync by hand. The 'negative = percent of screen' conversion is written out at 252, 286, 459-462 and 469-472.
- Fix: In diaRenderItem set *h once at the end from diaItemHeight(item, spacingH, (lines - 1) * MENU_ITEM_HEIGHT) and drop its own height assignments and fixedHeight block; replace the four percent conversions with one small static function (e.g. diaFixedSize(value, screenDim)).
- Benefit: ~14 lines removed and one sync hazard gone; static functions, so code size roughly unchanged (INFERRED)
- Effort: S. Risk: Med. Confidence: Med (READ)

#### RIPTOPL-210: Dialog footer drawn by two identical branches, and the editors re-spell guiCancelKey/guiConfirmIcon/guiCancelIcon inline

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/ui/dia.cpp:792-835`; `RiptOPL/src/ui/diaeditors.cpp:293`; `RiptOPL/src/ui/diaeditors.cpp:431`; `RiptOPL/src/ui/diaeditors.cpp:503`; `RiptOPL/src/ui/diaeditors.cpp:561`; `RiptOPL/src/ui/diaeditors.cpp:626-627`; `RiptOPL/src/ui/diaeditors.cpp:649`; `RiptOPL/src/ui/guimodal.cpp:192-210`
- Problem: In diaRenderUI the non-Settings branch (822-835) draws gSelectButton==KEY_CIRCLE ? CIRCLE : CROSS and the reverse, which is exactly guiConfirmIcon()/guiCancelIcon() that the Settings branch (793-821) already uses; the two branches differ only by the settingsShell L1/R1 indicator. diaeditors.cpp writes the guiCancelKey()/guiCancelIcon()/guiConfirmIcon() ternaries out by hand seven times.
- Fix: Keep one footer block (hints/icons from guiConfirmIcon/guiCancelIcon) with the settingsShell indicator as the only conditional part; replace the inline ternaries in diaeditors.cpp with guiCancelKey(), guiConfirmIcon() and guiCancelIcon().
- Benefit: ~13 lines removed in dia.cpp; editors become one-call expressions (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-211: vmc_groups: eight malformed title IDs never match, and groups XEBP_100.72 and XEBP_000.12 are unreachable duplicates

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/core/vmc_groups.cpp:443-444`; `RiptOPL/src/core/vmc_groups.cpp:464-465`; `RiptOPL/src/core/vmc_groups.cpp:472-473`; `RiptOPL/src/core/vmc_groups.cpp:498`; `RiptOPL/src/core/vmc_groups.cpp:614`; `RiptOPL/src/core/vmc_groups.cpp:1149-1153`; `RiptOPL/src/core/vmc_groups.cpp:1271-1276`; `RiptOPL/src/core/vmc_groups.cpp:936-939`
- Problem: getGroupIdForTitleId compares exact strings (1465) with IDs like SLUS_207.54 passed by guigame.cpp:493, 529 and 702, but lines 443-444, 464-465, 472-473, 498 and 614 hold SLUS_20754, SLUS_20824, SCES_50422, SLES_50422, SCES_51154, SLES_51154, SLES-5350 and SSCES_540.95, so those games get their own VMC name instead of their series card. Group 100.72 (1149-1152) repeats 100.71 (1143-1146), and all four titles of 000.12 (1272-1275) are also in 100.53 (936-939); the first match wins, so 100.72 and 000.12 are never returned.
- Fix: Correct the eight strings to the 11-character form (SLUS_207.54, SLUS_208.24, SCES_504.22, SLES_504.22, SCES_511.54, SLES_511.54, most likely SLES_535.06, SCES_540.95). Delete group 100.72 (1149-1153 and its row at 1424), and either delete 000.12 (1271-1276 and row 1439) or move its four titles out of 100.53, whichever upstream intended. The table is upstream Open PS2 Loader data, so the fix belongs upstream too.
- Benefit: Eight title IDs regain their series VMC name in the VMC dialog and auto-VMC; 13 lines removed with the two unreachable groups (no behaviour change for those).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-212: The twelve element families are enumerated by hand five times (12 findDuplicate calls plus four arrays in themes.cpp)

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/gfx/themeelems.cpp:871-882`; `RiptOPL/src/gfx/themes.cpp:196-198`; `RiptOPL/src/gfx/themes.cpp:334-336`; `RiptOPL/src/gfx/themes.cpp:1122-1124`; `RiptOPL/src/gfx/themes.cpp:1855-1858`; `RiptOPL/include/themes.h:200-234`
- Problem: initMutableImage calls findDuplicate twelve times, once per family; themes.cpp spells the same twelve-pointer list as a local array in four more functions (one with the first entry dropped). Adding a family means editing all of them.
- Fix: Keep one list: either make the families an indexed array in theme_t (named indices) or add one small function in themes.cpp that fills theme_elems_t *[12] for a theme (declared in themeint.h), and loop over it in initMutableImage and the four themes.cpp sites.
- Justification: The same 12-entry enumeration appears five times; one 5-line accessor and short loops remove roughly 12 lines and the risk of a family missing from one copy.
- Benefit: ~12 source lines net; 11 fewer inlined 7-argument calls in initMutableImage (~250 bytes .text, INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-213: inMenu parameter of diaExecuteDialog/diaRenderUI is never read; guiScreenInMenu and ScreenManager::inMenu exist only to feed it

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/dia.h:124-125`; `RiptOPL/src/ui/dia.cpp:628`; `RiptOPL/src/ui/dia.cpp:1127`; `RiptOPL/src/ui/dia.cpp:1175`; `RiptOPL/src/ui/guiscreens.cpp:18-25`; `RiptOPL/src/ui/guiscreens.cpp:36-39`; `RiptOPL/src/ui/guiscreens.cpp:100-104`; `RiptOPL/include/guiscreens.h:14-18`; `RiptOPL/include/guiint.h:17`; `RiptOPL/src/ui/guiwait.cpp:134`; `RiptOPL/src/ui/guimodal.cpp:141`
- Problem: diaRenderUI (dia.cpp:628-836) never uses its inMenu argument and diaExecuteDialog only forwards it there (dia.cpp:1175). All ~40 diaExecuteDialog callers pass the constant 1; the two diaRenderUI callers pass guiScreenInMenu(), whose only callers are those two lines. ScreenManager::inMenu() is called only by guiScreenInMenu, and Handler::inMenu is read only by ScreenManager::inMenu.
- Fix: Delete the inMenu parameter from diaExecuteDialog and diaRenderUI (and the 1 argument at every call), delete guiScreenInMenu and its prototype, ScreenManager::inMenu, and the Handler::inMenu field with its column in screenHandlers.
- Benefit: ~12 lines removed plus one argument at ~42 call sites; a few dozen bytes of code and 2 bytes per screenHandlers entry (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-214: Overlay-or-plain image draw block copied three times (StaticImage, GameImage, AttributeImage)

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/gfx/themeelems.cpp:931-938`; `RiptOPL/src/gfx/themeelems.cpp:1307-1314`; `RiptOPL/src/gfx/themeelems.cpp:1546-1553`
- Problem: Each draw function repeats the same 8 lines: rmDrawOverlayPixmap with the element geometry and the overlay's eight corner fields, then rmDrawPixmap of overlayTexture2, else a plain rmDrawPixmap. Only the inlay texture and the element differ (in drawStaticImage the overlay inlay is defaultTexture, which equals bgPicture there because a Background never reads an overlay key, line 860).
- Fix: Add one static helper (element, mutable_image_t, inlay GSTEXTURE) holding this block and call it from the three places.
- Justification: The same 8-line block exists three times in one file; one ~10-line helper plus three one-line calls removes about 11 lines and three copies of a long 18-argument call.
- Benefit: ~11 source lines net; two inlined copies of the rmDrawOverlayPixmap argument setup (~200 bytes .text, INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-215: Element init chain carries parameters that are unused or always passed the same constant

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/gfx/themeelems.cpp:209`; `RiptOPL/src/gfx/themeelems.cpp:469`; `RiptOPL/src/gfx/themeelems.cpp:622`; `RiptOPL/src/gfx/themeelems.cpp:633`; `RiptOPL/src/gfx/themeelems.cpp:657-666`; `RiptOPL/include/themeint.h:104`; `RiptOPL/src/gfx/themeelems.cpp:808`; `RiptOPL/src/gfx/themeelems.cpp:941-943`; `RiptOPL/src/gfx/themeelems.cpp:1337-1339`; `RiptOPL/src/gfx/themeelems.cpp:1727-1729`; `RiptOPL/src/gfx/themeelems.cpp:2119-2138`
- Problem: initMutableText ignores themePath and theme, and both callers pass alias=NULL, DISPLAY_ALWAYS, SIZING_NONE. findDuplicate's overlayTexture is discarded with (void) and the function is exported in themeint.h though only this file calls it. initMutableImage's defaultTexture/overlayTexture, initGameImage's texture/overlay, initStaticImage's imageName, initBackground's texture and initItemsList's decorator are NULL at every call site; each is only a seed for a configGetStr.
- Fix: Drop these parameters and use locals initialised to the constant; make findDuplicate static without overlayTexture, delete its themeint.h prototype and shorten the 657-666 comment to one line; in initItemsList read straight into itemsList->decorator.
- Benefit: ~10 source lines removed and 13 fewer arguments across 8 signatures and ~20 call sites; no ELF change expected (INFERRED, -O2 already propagates constants for static callees).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-216: The mc0:/mc1: path prefix test is written out in six places (suIsMc, fmIsMcPath, fmIsMc, menuwatch writeReport, settingsio twice)

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/ui/selfupdate.cpp:117-120`; `RiptOPL/src/ui/menuwatch.cpp:284`; `RiptOPL/src/ui/guifilemgr.cpp:69-72`; `RiptOPL/src/core/filemgr.cpp:82-85`; `RiptOPL/src/app/settingsio.cpp:690-696`; `RiptOPL/src/app/settingsio.cpp:700-704`
- Problem: The same expression (strncmp(p, "mc", 2) == 0 && (p[2] == '0' || p[2] == '1') && p[3] == ':') appears as three identical static one-line functions (selfupdate.cpp:117, guifilemgr.cpp:69, filemgr.cpp:82), inline in menuwatch.cpp:284, and twice more in settingsio.cpp (bootHomeIsConcreteMc on gBootDir, and inside sameConcreteMcSlot). No shared helper exists in include/.
- Fix: Keep one static inline isMcPath() in a header the files already include (include/opl.h) and delete suIsMc, fmIsMcPath, fmIsMc and bootHomeIsConcreteMc, calling the shared one instead; use it in menuwatch.cpp:284 and for the first operand in sameConcreteMcSlot.
- Justification: The helper replaces six copies of the same test already in the tree, so it removes code overall (four function bodies and two inline expressions become one four-line inline).
- Benefit: ~10 source lines net (INFERRED: 4 static helpers of 4-5 lines plus 2 inline expressions replaced by one 4-line inline); no ELF change, the compiler already inlines these static one-liners.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-217: Modal screens render the whole menu (guiShow) and then paint an opaque black rect over it every frame

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/ui/guimodal.cpp:226-228`; `RiptOPL/src/ui/guimodal.cpp:274-278`; `RiptOPL/src/ui/guimodal.cpp:306-309`; `RiptOPL/src/ui/guimodal.cpp:396-400`; `RiptOPL/src/ui/guiwait.cpp:204-214`
- Problem: RebootIopPrompt::draw, guiWarning, RemoveSettingsPrompt::draw, CheatScreen::draw (and guiRenderTextScreen) call guiShow() and then cover the full screen with gColBlack (alpha 0x80, solid: renderman.cpp:122), so the menu render is invisible wasted frame time. guiConfirmVideoMode already dropped this exact call for that reason (guisettings.cpp:3457-3462), and the comments at guimodal.cpp:308 and 398 still cite guiConfirmVideoMode as having guiShow as backdrop, which is no longer true.
- Fix: Delete the guiShow() call (and its stale 'same reason as guiConfirmVideoMode' comment) in the four guimodal.cpp draws and in guiRenderTextScreen.
- Benefit: One full menu-screen render per frame saved on every opaque modal and status screen (INFERRED from the repo's own note at guisettings.cpp:3457-3461 that this render cost visible frame time); ~8 lines removed.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-218: cacheAbortMmceImageLoadsTimed called right before cacheCancelPendingImageLoadsTimed at six sites is redundant and can lengthen the drain

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/gfx/artload.cpp:465-508`; `RiptOPL/src/gfx/artload.cpp:428-455`; `RiptOPL/src/app/lwnbdctl.cpp:141-142`; `RiptOPL/src/ui/guiwait.cpp:67-69`; `RiptOPL/src/gfx/themes.cpp:1820-1821`; `RiptOPL/src/sources/appsupport.cpp:789-790`; `RiptOPL/src/sources/hddsupport.cpp:2178-2179`; `RiptOPL/src/sources/hddsupport.cpp:2267-2268`
- Problem: Cancel is a strict superset of Abort: it flags the in-flight request whatever its bus, frees the whole queue, and waits for queued==0 && active==0. Abort first frees only SIO2 requests, RE-PUSHES the rest and wakes the worker, then waits for an SIO2 read; once it sleeps the worker can pop a re-pushed non-SIO2 request and start a new device read that the following Cancel must then wait out. The pair costs up to 2x the timeout instead of 1x.
- Fix: At the six paired sites delete the cacheAbortMmceImageLoadsTimed call and keep only cacheCancelPendingImageLoadsTimed (fold guiwait's abortOk into cancelOk). Keep Abort where it is used alone (mmcesupport, bdmlaunch, bdmsupport, menusys, themes.cpp:1415).
- Benefit: ~8 call-site lines removed; worst-case drain halved (500-2000 ms less when an SIO2 read is in flight); one detach/re-push pass and a possible extra device read avoided (INFERRED from the code paths)
- Effort: S. Risk: Med. Confidence: High (READ)

#### RIPTOPL-219: bgmStart/bgmStop block the GUI thread: up to 500 ms prebuffer wait in start(), 200 ms minimum alarm slice in stop()

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/core/bgm.cpp:798-816`; `RiptOPL/src/core/bgm.cpp:860-870`; `RiptOPL/src/core/bgm.cpp:736-739`; `RiptOPL/src/ui/gui.cpp:332-333`; `RiptOPL/src/ui/guisettings.cpp:1957-1958`; `RiptOPL/src/ui/guisettings.cpp:2741-2742`; `RiptOPL/src/ui/guisettings.cpp:2766-2767`
- Problem: start() runs on the GUI thread and spins DelayThread(5 ms) up to 100 times until 48 chunks are decoded, so no frame is drawn for up to 500 ms at main-loop entry, on Audio settings accept and after a theme change. stop() waits in SetAlarm/SleepThread slices of 3200 HSYNC (~200 ms); the I/O thread has priority 31 like the GUI thread (opl.cpp:2064, bgm.cpp:696), so it cannot exit before the first sleep and every stop costs at least ~200 ms. A theme change does stop then start: roughly 0.2-0.7 s of frozen screen.
- Fix: Move the prebuffer wait to the top of threadMain (the playback thread waits for the cushion before its first queueChunk; discretionaryIoAllowed already holds art I/O until the ring is primed) and drop the else-branch at 810-816, since the playback thread already exits on the I/O thread's terminate signal. In stop(), poll with DelayThread(10 ms) over the same 3.2 s budget and delete bgmShutdownDelayCallback.
- Benefit: Removes up to ~500 ms (start) and ~190 ms (stop) of GUI-thread stall per call (INFERRED: 100 x 5 ms loop bound; 3200 HSYNC slice vs a 10 ms poll); about 6 lines removed (callback, else-branch).
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-220: RA notice timer uses an absolute clock() deadline, the wrap-unsafe form the class header warns against

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/ui/guinotify.cpp:171-198`; `RiptOPL/include/guinotify.h:33-35`; `RiptOPL/include/guinotify.h:45-46`
- Problem: renderRA arms raNoticeTimer_ = clock() + 8 s and expires on clock() >= raNoticeTimer_, while guinotify.h:33-35 (and guiwait.cpp:97-98) state that this form breaks across the 32-bit clock wrap; popup and toast in the same class use the elapsed (clock() - start) form. A notice armed within 8 s of the wrap gets a wrapped deadline and is dropped after one frame. renderRA's only caller is drawRA, always with y=10, yadd=35 (gui.cpp:360).
- Fix: Store the start tick and expire on (clock() - start) >= 8 s like popupStart_/toastStart_ (a separate armed flag replaces the 0 sentinel), and fold renderRA into drawRA.
- Benefit: Removes a rare dropped-notice bug in RETROACHIEVEMENTS builds and one wrapper function; ~5 lines removed.
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-221: vmcSavesDelete frees at most 64 file chains, leaking card space for bigger saves; its files[] buffer is not needed

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/core/vmcsaves.cpp:1181-1182`; `RiptOPL/src/core/vmcsaves.cpp:1192-1195`; `RiptOPL/src/core/vmcsaves.cpp:1202-1204`
- Problem: vmcSavesDelete collects file chains into u32_t files[SAVE_FILES] and stops at 64 (1181-1182, 1193); files past the 64th, and sub-folders inside the save (only MC_FILE is taken, 1194), are never freed, so once the root entry is hidden (1197-1198) their clusters stay marked used with nothing naming them. The buffer exists to free chains after the entry is hidden, but vsFreeChain and vsSetFat (300-304, 406-415) only change the in-memory FAT until vsFlushFat (1205), so freeing inside the scan keeps the same on-disk order.
- Fix: Call vsFreeChain(&v, e.cluster) inside the scan loop for every existing file with length > 0 (keep the length test: an empty file's cluster field is 0, vsWriteVmc line 585), and delete files[], nfiles, the 64 cap and the second loop. Sub-folders inside a save stay unsupported, as they already are for export.
- Benefit: Deleting a save with more than 64 files no longer loses VMC space until a reformat; ~5 lines and a 256 B stack array removed (INFERRED).
- Effort: S. Risk: Med. Confidence: Med (READ)

#### RIPTOPL-222: Native MMCE launch switches the card (Game ID) and writes the OSD history before the cheats prompt that can cancel the launch

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/sources/mmcesupport.cpp:1399-1437`; `RiptOPL/src/sources/mmcesupport.cpp:1540-1550`; `RiptOPL/src/sources/supportbase.cpp:1690-1697`; `RiptOPL/include/mmcesupport.h:40-42`
- Problem: The OPL-core leg sends the 0x8 Game ID switch (1410), waits the swap out and writes the OSD history record (1426) before sbLoadCheats. When per-game cheats are on and the .cht is missing or broken, sbCheatsMissingContinue asks, and a cancel returns to the menu (1543-1548). The card is then left on the game's per-game folder, or switching to it through its own history tracker, with a history entry for a game that never started. Nothing resets the card on that path (mmceReset serves non-game launches only), so later menu writes to that slot's mcN: go to the game's card (INFERRED).
- Fix: Move the cheats load and its prompt (1540-1550, with the RA watch-list call above it if wanted) to just after `settings` is set at 1379, before the native Game ID block. The Neutrino leg already loads cheats before its own send. The two VMC fd closes in that bail then go away, because no fd exists yet.
- Benefit: A cancelled launch leaves the card folder and OSD history untouched and skips a pointless swap wait of up to 180 s; 4 lines removed (INFERRED)
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-223: Tray-open check compares sceCdStatus() with an error code (SCECdErOPENS), so it never fires

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/platform/sysdisc.cpp:158-159`; `RiptOPL/src/platform/system.cpp:498-499`
- Problem: sceCdStatus() returns the drive status register (bit 0 = tray open, value SCECdStatShellOpen; see the repo's own emulation in modules/iopcore/cdvdman/scmd.c:60-68,115-119), but both functions compare it with SCECdErOPENS, a member of the SCECdEr* error-code family returned by sceCdGetError. An open tray therefore never takes the documented 'return -1' exit. In sysGetDiscBootPath the open tray reads as NODISC and the code goes on to send a tray-close request (sysdisc.cpp:168-175) and re-detect for up to 2-4 s; in sysGetDiscID the NODISC test at system.cpp:506 returns -1 anyway.
- Fix: Delete the two dead checks. They never fire today, and an open tray already ends in the NODISC handling in both functions. If an open tray should fail fast in sysGetDiscBootPath, test sceCdStatus() & SCECdStatShellOpen there instead.
- Benefit: 4 lines removed (INFERRED); the code then does what it actually does, with no misleading guard. No ELF size change worth stating.
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-224: CNFGetKey leaves BOOT2 unterminated at 64 characters; GetStartupExecName's 64-byte buffer is then scanned past its end

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/platform/ps2cnf.cpp:54-67`; `RiptOPL/src/sources/supportbase.cpp:116-144`
- Problem: CNFGetKey copies up to CNF_PATH_LEN_MAX (64) characters and writes '\0' only when it meets whitespace. A BOOT2 value of 64+ printable characters fills all of GetStartupExecName's char ps2disc_boot[CNF_PATH_LEN_MAX] (supportbase.cpp:116) with no terminator, and the ':' and ';' scans at supportbase.cpp:127-144 then read past it on the IO worker stack. The input is SYSTEM.CNF inside any user ISO read as iso:/SYSTEM.CNF;1 during the ISO scan (supportbase.cpp:450). The '\0' branch at ps2cnf.cpp:62-64 is unreachable under the loop condition.
- Fix: Bound the copy loop to CNF_PATH_LEN_MAX - 1 characters and write the terminator once after the loop; delete the unreachable '\0' branch at lines 62-64.
- Justification: Adds one terminator write after the loop: the only caller's buffer is exactly CNF_PATH_LEN_MAX bytes and the parser can fill it from ISO content, so this closes a stack over-read; deleting the dead branch makes the change net smaller.
- Benefit: Closes a stack over-read on untrusted ISO content; ~2 lines net removed (3-line dead branch out, 1 line in) (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-225: Two Neutrino TOML preflights keep separate static 4 KB in/out buffer pairs (16.9 KB .bss) though they never run together

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/src/platform/sysneutrino.cpp:384-385`; `RiptOPL/src/platform/sysneutrino.cpp:639-640`
- Problem: sysSyncNeutrinoUdpfsToml has static toml[4096] and updated[4116]; sysNeutrinoSmbPreflight has static toml[4096] and updated[4608]. That is 16,916 bytes of .bss held for the whole run. The two are only called one after the other (the udpfs sync from sysNeutrinoPreflight:836-837 for udpfs/udpfsbd/udpbd only; the SMB/NFS preflight only from ethsupport.cpp:1216), neither calls the other, and neither keeps data between calls, so one pair is enough.
- Fix: Replace the four function-local statics with one file-scope pair (4096 in, 4096 + 512 out) used by both functions.
- Benefit: 8,212 bytes .bss saved (MEASURED: nm gives 0x1000+0x1014 and 0x1000+0x1200 = 16,916 B; one 4096+4608 pair keeps 8,704). No code growth.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-226: tarParseFile cold scan: an extra lseek RPC per member and +64-entry linear index growth

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/platform/tar.cpp:293-295`; `RiptOPL/src/platform/tar.cpp:304-311`; `RiptOPL/src/platform/tar.cpp:363-364`
- Problem: For every member the scan does read(512), lseek64(fd, 0, SEEK_CUR) only to learn the offset it already knows (header end), and lseek64(paddedSize, SEEK_CUR). Each is a fileXio RPC on the art worker, and the scan contends with BGM I/O on the same channel. The index also grows by 64 entries per realloc. For N members that copies O(N^2) bytes when realloc moves the block (6000 members: about 18 MB copied) and leaves heap fragments.
- Fix: Keep the file position in a local u64 (add 512 after each header, paddedSize after each member). Store it as dataOffset, and skip with one lseek64(pos, SEEK_SET). Delete the SEEK_CUR query. Double the capacity instead of adding 64.
- Benefit: One fewer IOP RPC per member on each cold scan (N members = N RPCs) and realloc copying drops from ~18 MB to under 1 MB for 6000 members (INFERRED arithmetic: 4096 x 94 x 95 / 2 bytes vs < 2 x 384 KB)
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-227: Ranet receive buffer rx_ is 2048 bytes but no read ever exceeds 992 + NUL; netTestLink keeps a second static rx[256]

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/include/ranet.h:88-89`; `RiptOPL/src/sources/ranet.cpp:313-317`; `RiptOPL/src/sources/ranet.cpp:855-859`; `RiptOPL/src/sources/ranet.cpp:1112`
- Problem: ask() clamps every read to RA_RECV_MAX = 992 bytes (ranet.cpp:313-317, also for the mstack: path) and httpGet reads into rx_ at most 992 (ranet.cpp:1112), so at most 993 bytes of rx_ are ever written. netTestLink uses its own static rx[256] although it runs on the same thread and never overlaps a use of rx_.
- Fix: Shrink rx_ to 1024 bytes (still 64-byte aligned) and let netTestLink use rx_ instead of its static rx[256].
- Benefit: INFERRED: 2048 -> 1024 bytes for rx_ plus 256 bytes for netTestLink's buffer = 1,280 bytes of .bss, RA builds only.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-228: Two separate static int sel[FM_MAX] buffers in guifilemgr.cpp

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/src/ui/guifilemgr.cpp:374`; `RiptOPL/src/ui/guifilemgr.cpp:489`
- Problem: fmToClipboard and fmDeleteSelection each own a static 512-entry int selection buffer; the two never run at the same time (both are called from fmActions, one per dialog result), so one buffer is enough.
- Fix: Use one file-scope selection buffer for both functions (or allocate it with the other File Manager buffers in guiShowFileManager).
- Benefit: 2 KB .bss saved (INFERRED: 512 x 4 bytes); about 1 line
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-229: ThemeCatalog::reinit can leave themeId_ pointing at a removed or different theme (failed default load; 64-byte name copy)

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/gfx/themes.cpp:1972-1977`; `RiptOPL/src/gfx/themes.cpp:2002-2012`; `RiptOPL/src/gfx/themes.cpp:2020-2023`; `RiptOPL/src/sources/ethsupport.cpp:601-611`; `RiptOPL/src/sources/udpfssupport.cpp:323-324`
- Problem: When the active theme lives on the device being removed (SMB reconnect, UDPFS re-init) and thmLoad(NULL) returns -1 (MX4SIO art drain timeout at 1415, or OOM at 1361), themeId_ keeps the old index although the swap-remove at 1985-1997 just deleted that entry: the id now names another theme or lies past count_, so thmGetValue reads names_[count_+1] (NULL) or past the array, and picking the theme that moved into that slot is skipped as 'already loaded' (2034). Separately, activeName[64] truncates a theme name of 64+ chars, findGuiID misses, and themeId_ becomes 0 while the disk theme stays on screen.
- Fix: Keep a pointer to the active entry's name instead of the 64-byte copy (only removed entries are freed; moved entries keep their name pointers), and when the active entry was removed set themeId_ to 0 whether or not thmLoad(NULL) succeeded, since the old index no longer exists.
- Benefit: Fixes a wrong theme id (possible out-of-bounds read of names_) after a failed fallback load, and the id loss for long theme names; removes a 64-byte stack copy and its snprintf
- Effort: S. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-230: init() starts the pads twice at every boot: startPads() result at line 1686 is discarded and the wait loop calls it again

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/app/opl.cpp:1668`; `RiptOPL/src/app/opl.cpp:1686`; `RiptOPL/src/app/opl.cpp:1699-1708`; `RiptOPL/src/core/pad.cpp:1155-1191`; `RiptOPL/src/core/pad.cpp:1136-1151`; `RiptOPL/src/core/pad.cpp:171-186`; `RiptOPL/src/core/pad.cpp:263-294`
- Problem: padStatus is initialised to 0 (1668) and the startPads() at 1686 ignores its return, so the while (!padStatus) loop always runs startPads() a second time even when the first call opened every port. PadManager::start() resets padCount_ and, for every port/slot, calls padPortOpen again without a close and re-runs initialize(): mode-table RPCs, padSetMainMode, press mode, actuator align, and up to six waitReady() polls of 25 x 1 ms each. All of that is repeated on the boot path before the config even loads.
- Fix: Keep the result of the first call: make line 1686 assign padStatus = startPads() (and drop the separate = 0 initialisation at 1668), so the loop only retries when no port opened.
- Benefit: One full pad bring-up (padPortOpen + initialize) per port saved on every boot; initialize can wait up to 6 x 25 ms per DualShock and ~25 ms per empty port, so roughly 50-300 ms of boot time (INFERRED from PAD_WAIT_POLLS=25, PAD_WAIT_POLL_US=1000, not measured)
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-231: 12 gfx/ PNGs that no build embeds: Device_1..6, Device_all, no_Device, no_Rating, logo4..6

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/gfx/Device_1.png`; `RiptOPL/gfx/Device_2.png`; `RiptOPL/gfx/Device_3.png`; `RiptOPL/gfx/Device_4.png`; `RiptOPL/gfx/Device_5.png`; `RiptOPL/gfx/Device_6.png`; `RiptOPL/gfx/Device_all.png`; `RiptOPL/gfx/no_Device.png`; `RiptOPL/gfx/no_Rating.png`; `RiptOPL/gfx/logo4.png`; `RiptOPL/gfx/logo5.png`; `RiptOPL/gfx/logo6.png`; `RiptOPL/src/gfx/textures.cpp:241-243`; `RiptOPL/src/gfx/textures.cpp:258-266`; `RiptOPL/src/gfx/textures.cpp:297-301`
- Problem: textures.cpp maps these textures to NULL or to a shared blob (Rating_0, logo3/2/1), and tools/assets.py PNG_ASSETS (46-57, plus the Korium additions at 163) does not list them, so the source PNGs are never bin2c'd. logo4/5/6 and no_Rating are byte-identical to logo3/2/1 and Rating_0 (cmp). No other file references the file names.
- Fix: Delete the 12 PNG files from gfx/. Keep the table rows: their names are still theme lookup keys (misc/*.cfg use default=no_Rating).
- Benefit: 29,792 bytes of tracked repo files removed (MEASURED: inventory/RiptOPL.tsv sizes); no ELF change
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-232: AppSource::launch resets MMCE cards before checks that can still cancel the launch

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/sources/appsupport.cpp:1174-1187`; `RiptOPL/src/sources/appsupport.cpp:1264-1289`; `RiptOPL/src/sources/mmcesupport.cpp:547-590`
- Problem: mmceReset() runs at 1181, right after the Reboot-IOP answer. The path normalisation (1184-1187), the HDD partition and handoff-path checks (1265-1273) and the 256-byte argument-pool check (1286-1289) can then still return to the menu. resetCards loads the MMCE modules if they are not loaded and sends the 0x9 reset to every present MMCE card (mmcesupport.cpp:554-585). An over-long path or argv1 therefore leaves OPL in the menu with the card switched and mmceman resident. appLaunchElfPath does the same reset only after all its checks (1068-1072).
- Fix: Move the mmceReset() call to just before host::app().deinit at line 1295, after the argBytes check. Keep a call in the OPLDIAG Ember block if that probe needs it.
- Benefit: An aborted APPS launch no longer resets SD2PSX/MemCard PRO cards or loads mmceman; no line change (moved call).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-233: Every save with a Custom Settings Path rewrites config.path, including each game launch's CONFIG_LAST save

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/app/settingsio.cpp:2419-2424`; `RiptOPL/src/app/settingsio.cpp:348-381`; `RiptOPL/src/sources/bdmlaunch.cpp:453-456`; `RiptOPL/src/ui/guigame.cpp:2117`
- Problem: _saveConfig calls writeConfigPathRedirect after every successful explicit-path save, whatever lscstatus holds. Launch paths call saveConfig(CONFIG_LAST, 0) when Remember Last Played is on, and per-game resets call saveConfig(CONFIG_GAME, 0). Each of these truncates and rewrites config.path in the boot folder (often a memory card) and, on a network boot, the mc?:/OPL mirror too, although the path has not changed. configWriteChecked returns a positive value even when no set was dirty, so the rewrite happens even when nothing else was written.
- Fix: Rewrite the redirect only when the save includes CONFIG_OPL, the set that holds custom_settings_path: add (lscstatus & CONFIG_OPL) to the condition at line 2420.
- Justification: The condition removes one or two file rewrites (open with O_TRUNC, two writes, close) per game launch for Custom Settings Path users. The path can only change in a CONFIG_OPL save, so no state is lost.
- Benefit: Saves 1 file rewrite per launch (2 on network boots: boot-folder copy plus mc mirror) for Custom Settings Path users, usually on a memory card (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-234: IGS halts the console forever when mc1 is missing or full, or after the 255th screenshot, instead of finishing the exit

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/ee_core/src/igs_api.c:411-413`; `RiptOPL/ee_core/src/igs_api.c:598-600`; `RiptOPL/ee_core/src/igs_api.c:608-626`; `RiptOPL/ee_core/src/igs_api.c:646-648`; `RiptOPL/ee_core/src/igs_api.c:659-665`; `RiptOPL/ee_core/src/padhook.c:646-659`
- Problem: The output path is hard-coded to mc1:. A failed fioOpen (no card in slot 2), any short fioWrite (card full: a 640x448x32 BMP is ~1.1 MB), and a 255th existing file all call DBGCOL_BLNK(..., true), which is BlinkColour with forever set (util.c:184-198), so the IGR thread never reaches IGR_Exit (padhook.c:659). The game is already shut down and the IOP reset, so the user is left on a blinking screen and must power-cycle; a truncated BMP stays on the card.
- Fix: On an open or write failure (and at the 255 limit) close any open handle and return, passing false to the blink, so InGameScreenshot falls through to IGR_Exit like a normal exit.
- Benefit: Removes a hang that needs a power cycle on realistic card conditions (empty slot 2, full card); no size change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-235: Pad fault-discriminator runs (notReadyRun_/emptyRun_) are one counter updated once per pad, so an empty second port doubles them and logs false misses

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/core/pad.cpp:484-501`; `RiptOPL/include/padmanager.h:80-82`; `RiptOPL/src/core/pad.cpp:1170-1187`
- Problem: readOne() advances the shared run counters per pad, not per poll. start() keeps an empty port in padData_ (initialize() returns 1 on every path), and a DISCONN port reads nothing, so each poll adds 1 to notReadyRun_. With one pad and one empty port, a k-poll miss on the real pad becomes a run of 2k: the debug HUD, DIAG.TXT and menuwatch (guioverlay.cpp:159, diagsave.cpp:33, menuwatch.cpp:192) report doubled maxima, maxNotReadyRun_ is never below 1, and a 2-poll miss writes 'pad: 4 polls in a row could not be read' into the 16 KB diag ring. With two pads, emptyRun_ interleaves the same way.
- Fix: Decide the runs once per poll in read(): have readOne() report whether it attempted a native or ds34 read and whether any delivered, then after the loop count a not-ready poll when some pad attempted and none delivered, and an empty poll when one delivered and paddata_ is 0. Delete the per-pad block at 484-501.
- Benefit: Correct fault counters and no false miss lines in DIAG.TXT on the diagnostic path the code was written to chase hardware faults with; net lines about 0 (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-236: Static tables larger than needed: crctab[0x400] uses 256 entries; s_zeroBlock is a 512-byte table for one memcmp

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/src/platform/system.cpp:465-484`; `RiptOPL/src/platform/tar.cpp:73`; `RiptOPL/src/platform/tar.cpp:87-90`
- Problem: USBA_crc32 declares crctab[0x400] (4096 bytes .bss) but only fills and indexes crctab[0..255] (index is byte ^ (crc>>24 & 0xFF)). It also rebuilds the table on every call. tar.cpp keeps a 512-byte all-zero .rodata block whose only use is memcmp in isZeroBlock.
- Fix: Declare crctab[256]. Replace s_zeroBlock with a loop in isZeroBlock that checks the 64-byte-aligned header word by word, and delete the table.
- Benefit: 3072 bytes .bss and 512 bytes ELF .rodata (MEASURED: nm riptopl-nm-size-sorted.full.txt shows _ZL6crctab 0x1000 and _ZL11s_zeroBlock 0x200)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-237: USBA_crc32 table crctab[0x400] is four times the 256 entries it uses

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/src/platform/system.cpp:465-488`
- Problem: The table loop fills indices 0..255 and the lookup index is byte ^ (crc>>24 & 0xFF), at most 255, so entries 256..1023 of the static unsigned int crctab[0x400] are never touched.
- Fix: Declare crctab[256] (upstream-derived code; behaviour unchanged).
- Benefit: 768 x 4 = 3072 bytes of .bss saved (INFERRED from the declaration)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-238: cheatConfigSaveSelections marks the game CFG modified even when the picks did not change, so every Select-mode launch rewrites it

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/core/cheatconfig.cpp:100-110`; `RiptOPL/src/ui/guimodal.cpp:439-441`; `RiptOPL/src/core/config.cpp:447-458`; `RiptOPL/src/core/config.cpp:1519`
- Problem: The save first removes every $CheatSel000-249 key and then re-appends the enabled picks. removeKey sets modified_ when a key exists and append sets it again, so with any remembered pick the set is always dirty and CheatScreen::finish's configWrite rewrites the game's CFG on its device at each Select-mode launch, even when the user pressed START without changing anything. configWrite would otherwise skip a clean set (config.cpp:1511-1519).
- Fix: Write the picks first with configSetStr (a no-op when the value is equal, config.cpp:371-377), then remove only the keys from index n upward; an unchanged pick list leaves the set clean and configWrite does nothing.
- Benefit: One CFG file write saved per Select-mode launch with unchanged picks (game device, may be MMCE/USB/HDD); removeKey calls drop from 250 to 250-n. No size change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-239: set_ipconfig indexes SmapLinkModeArgs with an unchecked eth_linkmode from conf_network.cfg

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/ee_core/src/util.c:74-78`; `RiptOPL/ee_core/src/util.c:96-100`; `RiptOPL/src/app/settingsio.cpp:1924`; `RiptOPL/src/platform/system.cpp:1561`; `RiptOPL/src/sources/netsupport.cpp:408-423`
- Problem: The link mode is read from conf_network.cfg with configGetInt and never range-checked (settingsio.cpp:1924), copied into the unsigned char g_ps2_ETHOpMode (system.cpp:1561), and set_ipconfig indexes the four-entry SmapLinkModeArgs with ETHOpMode - 1 for any non-zero value (util.c:96-100). A value of 5..255 (hand-edited or damaged file; -1 becomes 255) reads a pointer past the array and strncpy's 6 bytes from it: a garbage SMAP argument or an address/TLB exception at ee_core start. The menu's own switch treats such values as auto (netsupport.cpp:408-423).
- Fix: In set_ipconfig, add the -no_auto arguments only when ETHOpMode is below ETH_OP_MODE_COUNT, as the menu does.
- Justification: Adds one comparison: a config-file value currently reaches an array index unchecked; defaulting to auto matches what the menu already does with the same value.
- Benefit: Closes an out-of-bounds read at game launch from a malformed network config; no net line change (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-240: settingshell.h: mmceDialog_ reserves 256 UIItems (10 KB .bss) for a page of at most ~45 rows

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/include/settingshell.h:70-73`; `RiptOPL/src/ui/guisettings.cpp:377`; `RiptOPL/src/ui/guisettings.cpp:2117-2129`
- Problem: SettingsShell holds two UIItem[SETTINGS_DIALOG_CAPACITY=256] arrays. mmceDialog_ is used only by guiShowMmceConfig (guisettings.cpp:377), which composes diaMmceConfig + diaMmceCommConfig + diaMmcePathConfig (dialogs.cpp:816-942, 43 source rows before skips). sizeof(struct UIItem) is 40 bytes (dia.h:72-121), so mmceDialog_ is 10,240 bytes of .bss of which ~8 KB is never used.
- Fix: Size mmceDialog_ to 64 entries and let composeInto bound by the destination size (pass the capacity instead of using SETTINGS_DIALOG_CAPACITY at guisettings.cpp:2117 and 2125).
- Justification: The capacity parameter is the only addition: without it composeInto would check a 64-entry buffer against 256 and could overflow. It costs one argument and saves ~7.7 KB of EE .bss.
- Benefit: ~7.7 KB .bss: (256 - 64) x 40 B = 7,680 B. MEASURED total: gSettingsShell is 0x50bc = 20,668 B in evidence/RiptOPL/raw/riptopl-nm-size-sorted.full.txt:45; the 40 B UIItem size is INFERRED from dia.h.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-241: utf8.h defines its 400-byte DFA table static in a header, so the ELF carries two copies

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/include/utf8.h:21-49`; `RiptOPL/src/gfx/fntsys.cpp:12`; `RiptOPL/src/gfx/textshape.cpp:12`; `POPSLoader/src/include/utf8.h:21-48`
- Problem: utf8d[] is a 'static const' array and utf8Decode a static function in the header; fntsys.cpp and textshape.cpp both include and use it, so each object gets its own table. nm shows two _ZL5utf8d symbols of 0x190 bytes. (POPSLoader carries the same upstream Hoehrmann table; that is cross-repo and not shareable.)
- Fix: Make the table a single definition: declare it 'inline constexpr' (C++17, the GCC 15 default) or move the table and utf8Decode into one .cpp with a prototype in utf8.h.
- Benefit: 400 B of .rodata (MEASURED: two 'r _ZL5utf8d' entries of 0x190 at 005dc7d0 and 005dceb8 in riptopl-nm-size-sorted.full.txt).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-242: utf8d decoder table is static in utf8.h and linked twice (textshape.cpp and fntsys.cpp)

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/include/utf8.h:21-49`; `RiptOPL/src/gfx/textshape.cpp:12`; `RiptOPL/src/gfx/fntsys.cpp:12`
- Problem: utf8.h defines the 400-byte DFA table as static const and utf8Decode as static, so each translation unit that includes it gets its own copy. The shipped ELF has two identical 400-byte _ZL5utf8d objects, one from each includer.
- Fix: Give the table one definition: make it inline constexpr in the header (C++17 inline variable) and utf8Decode inline, or define both once in textshape.cpp and declare them in utf8.h.
- Benefit: 400 bytes of .rodata removed. MEASURED: raw/riptopl-nm-size-sorted.full.txt lines 1757-1758, two _ZL5utf8d of 0x190 bytes.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-243: vmcsaves holds 4.9 KB of .bss for the whole run: vsCopy's static vs_save_t and vsWritePsu's static pad

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/src/core/vmcsaves.cpp:1123`; `RiptOPL/src/core/vmcsaves.cpp:862`
- Problem: vsCopy's static vs_save_t s (1123; 3896 B, not the ~2.5 KB its comment says) and vsWritePsu's static pad[1024] (862) are used only during one copy but stay in .bss for the whole run; vmcsaves.o has 4924 B of .bss. Both run on GUI code paths whose thread stack is 128 KB.
- Fix: Drop the two static keywords so both live on the stack during the copy (the memsets at 1127 and 881 already initialise them) and correct the size in the comment.
- Benefit: 4920 B of RAM freed (MEASURED: riptopl-per-object.tsv vmcsaves.o bss 4924; nm sizes vsCopy::s 0xf38, vsWritePsu::pad 0x400); peak stack grows by 4.9 KB of 128 KB.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-244: A torn first save (favourites.bin.new without favourites.bin) or a zero-length store file blocks every later favourite/recent save

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/sources/favsupport.cpp:226-234`; `RiptOPL/src/sources/favsupport.cpp:410-422`; `RiptOPL/src/sources/favsupport.cpp:537-541`; `RiptOPL/src/sources/favsupport.cpp:553-560`
- Problem: With the primary missing and an incomplete but recognised .new, favReadFile returns editable = missing && stagedMissing = 0 (420-421) and favPrepareSave returns ok = missing && recovery.missing = false (540), so addItem, removeByIdAndText and recentRecord refuse to save for good. The first-ever save writes .new while no primary exists (553-560), so a power cut or card pull there leaves exactly this state. A readable zero-length primary (for example made on a PC) is never editable either (231-234). Only deleting the files by hand recovers.
- Fix: When the primary is missing, treat a recognised-but-incomplete .new as replaceable in favReadFile and favPrepareSave (it never held an authoritative list), and treat a readable zero-length file as an empty editable list.
- Benefit: Removes a permanent 'save failed' state for favourites and recents; three condition edits, no added lines.
- Effort: S. Risk: Med. Confidence: Med (READ)

#### RIPTOPL-245: Art cache eviction frees a cover's pixels while the HIRES replay of the previous frame may still upload them

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/gfx/texcache.cpp:396-407`; `RiptOPL/src/gfx/texcache.cpp:467`; `RiptOPL/src/gfx/texcache.cpp:24-31`
- Problem: The LRU scan accepts any slot with lastUsed < guiFrameId, so a cover drawn in the previous frame (lastUsed == guiFrameId-1, typically the one that just scrolled off) is evicted and cacheClearItem frees texture.Mem in the middle of drawing the next frame. The repo itself states that in HIRES the DMA thread keeps replaying the previous frame, whose inline uploads reference those pixels, and guards theme and font frees with rmDiscardFrame for that reason; this per-frame eviction has no such guard, so the replay can read freed (and possibly already reused) memory.
- Fix: Start the scan with rtime = guiFrameId - 1 so a slot drawn in the previous frame is never evicted; in the rare case every slot was drawn last frame the claim simply happens one frame later.
- Benefit: Removes corrupted-cover flashes during scrolling in the 704-wide, 720p and 1080i HIRES modes (INFERRED from the repo's own HIRES model; not observed on hardware). No size change.
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-246: EthSupport::reconnectSmb clears the coalescing flags at its end, so a settings reconnect requested while it runs is dropped

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/sources/ethsupport.cpp:601-630`; `RiptOPL/src/sources/ethsupport.cpp:686-709`; `RiptOPL/src/ui/guisettings.cpp:1053-1056`
- Problem: requestReconnect (GUI thread, Network page OK) only sets reconnectRequested_ when reconnectQueued_ is 1 (692-696). reconnectQueued_ stays 1 for the whole reconnectSmb run on the IO worker (DHCP/link waits, logon, open-share poll of up to 90 s) and lines 628-629 then clear both flags, so a second OK pressed with corrected settings during that run never reconnects with them.
- Fix: Move the two clears (628-629) to the top of reconnectSmb, before it reads any settings, so a request that arrives during the run queues a fresh reconnect.
- Benefit: Fixes a lost settings apply (session stays on the old IP/share until another manual Reconnect); 0 lines, the two assignments move.
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-247: File Manager label table allocates 160 bytes for each of 512 entries; folder rows never hold more than 47

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/src/ui/guifilemgr.cpp:53`; `RiptOPL/src/ui/guifilemgr.cpp:58`; `RiptOPL/src/ui/guifilemgr.cpp:238`; `RiptOPL/src/ui/guifilemgr.cpp:619`
- Problem: fmLabel is malloc'd as FM_MAX x FM_ROW_TEXT = 512 x 160 = 80 KB while the File Manager is open. In a folder a label is only a card save title, clamped to FM_TITLE (48) at line 238, and is empty everywhere but a card root; only the handful of drive rows use the label text at all, and their row is clipped on screen long before 160 characters.
- Fix: Give fmLabel its own row width of about 80 bytes (enough for the drive words and any title) instead of FM_ROW_TEXT.
- Benefit: 40,960 B less heap while the File Manager is open INFERRED (512 x (160 - 80)); fewer FM_ERR_MEMORY failures on a fragmented heap
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-248: File Manager walks re-list big folders and probe every removed file twice on all devices

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/core/filemgr.cpp:225-248`; `RiptOPL/src/core/filemgr.cpp:468-477`
- Problem: fmChildren re-runs fmList (opendir, readdir and one stat per entry) on a 4x larger array each time a folder fills it: a 2000-entry folder is read 4 times with 64+256+1024+2000 stats instead of 2000. fmRemove follows every successful remove/rmdir with fmExists (a failed open plus a failed opendir) and pauses 50 ms per retry on any device, though its comment names only the card. Both run on the GUI thread inside fmPaste/fmDeleteSelection (guifilemgr.cpp:428-514), which shows a static text screen meanwhile; fmCopyTo also walks the tree once more for the progress total.
- Fix: Read a folder in one readdir pass in fmChildren, growing the array with realloc, instead of re-listing; in fmRemove do the fmExists check and the pause-retry only when fmIsMc(path), and take remove()/rmdir()'s result as is elsewhere.
- Benefit: Deleting a 2000-file ART folder on USB: ~4000 fewer fileXio calls from fmRemove plus ~1300 fewer stats and 3 fewer readdir passes (counts READ from the code; time per call INFERRED ~1 ms, so seconds).
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-249: IO drains poll once per ~0.25 s delay(1) spin: launch/exit sees an empty queue up to 0.25 s late; lwnbdctl's 1000-tick bound is ~4 min

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/app/opl.cpp:1269-1277`; `RiptOPL/src/app/opl.cpp:1328`; `RiptOPL/src/app/opl.cpp:1422`; `RiptOPL/src/app/opl.cpp:1962`; `RiptOPL/src/platform/ioman.cpp:380-386`; `RiptOPL/src/platform/util.cpp:941-950`; `RiptOPL/src/app/lwnbdctl.cpp:78`; `RiptOPL/src/app/lwnbdctl.cpp:150-154`
- Problem: deinitEx, deinit and miniDeinit drain through IoQueue::blockOps, which re-checks hasPending() only after each delay(1), a 0x01000000-iteration NOP spin that opl.cpp:1269-1272 itself puts at about a quarter second. A queued request that ends just after a check is noticed up to ~0.25 s later, on a black Please Wait screen, on every launch or exit that finds the queue busy. The same tick unit makes LWNBD_IO_DRAIN_TICKS = 1000 a ~250 s bound, so a stuck request freezes NBD start for minutes instead of failing it.
- Fix: In IoQueue::blockOps replace delay(1) with DelayThread(10000) (10 ms, yields the CPU) and restate the budgets in that unit: EXIT_IO_DRAIN_TICKS 100, LAUNCH_IO_DRAIN_TICKS 1000, and LWNBD_IO_DRAIN_TICKS at the intended bound. Update the unit comment at opl.cpp:1269-1275 to match.
- Benefit: Up to ~0.24 s less black screen per launch/exit whenever IO is still queued (INFERRED: delay(1) = 16.8M x 4 NOPs ~ 0.25 s at 294 MHz, poll step becomes 10 ms); NBD preflight bound drops from ~250 s to the intended value; no change when the queue is empty
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-250: On-screen keyboard moves the caret and backspaces one byte at a time, splitting multi-byte UTF-8 in an existing name

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/ui/diaeditors.cpp:298-307`; `RiptOPL/src/ui/diaeditors.cpp:360-368`; `RiptOPL/src/ui/diaeditors.cpp:395-403`; `RiptOPL/src/ui/diaeditors.cpp:112-126`; `RiptOPL/src/ui/menusys.cpp:306-316`
- Problem: L1/R1 do caret-- / caret++ and both backspaces memmove out exactly one byte, while the view scroll already steps by UTF-8 characters (diaKeybNextChar/PrevChar). Renaming a game (menusys.cpp:306-316) or file whose name holds multi-byte UTF-8 leaves orphan lead/continuation bytes after one backspace, or inserts a key inside a sequence; confirming writes that invalid name through gsOf()->rename.
- Fix: Use the existing helpers: caret = diaKeybPrevChar(text, caret) / diaKeybNextChar(text, caret, len) for L1/R1, and make both backspaces remove the bytes from diaKeybPrevChar(text, caret) to caret. ASCII text behaves exactly as now.
- Benefit: Prevents renames that write broken UTF-8 into game/file names; no new code, four expressions change (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-251: PS1 save icons: colour-0 texels meant to be transparent are drawn as opaque black

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/gfx/saveicon.cpp:631-636`; `RiptOPL/src/gfx/saveicon.cpp:1029-1037`; `RiptOPL/src/gfx/saveicon.cpp:1238-1239`; `RiptOPL/src/gfx/renderman.cpp:445-451`
- Problem: saveIconFromPs1CardImage writes palette colour 0x0000 as texel 0 and sets bit 15 only on other colours, i.e. it encodes PS1 transparency in the A1 bit of a CT16 texture. The frames are drawn through rmDrawPixmap -> drawQuad, which enables alpha blending and the alpha test only for CT32 (or CT32-CLUT) textures; for CT16 it turns both off, so every transparent texel is written as black and the icon shows on a black square.
- Fix: Decode the PS1 frames as CT32 (alpha 0 for colour 0, 0x80 otherwise) so drawQuad's existing CT32 path blends and alpha-tests them; the three frames grow from 1.5 KB to 3 KB. Update test_ps1_save_icons.py's expected texel values with it.
- Benefit: PS1 icons render with their intended transparency instead of a black box; +1.5 KB peak for a shown PS1 icon (INFERRED: 3 frames x 256 texels x 2 extra bytes).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-252: Unchecked eth_linkmode from conf_network.cfg makes ee_core index SmapLinkModeArgs[mode-1] out of bounds; ps2_ip can overrun g_ps2_ip[16]

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/ee_core/include/coreconfig.h:43-46`; `RiptOPL/ee_core/include/ee_core.h:44-52`; `RiptOPL/ee_core/src/util.c:84-100`; `RiptOPL/src/app/settingsio.cpp:1924`; `RiptOPL/src/app/settingsio.cpp:1949-1950`; `RiptOPL/src/platform/system.cpp:1561`; `RiptOPL/src/platform/system.cpp:1577-1580`
- Problem: settingsio.cpp:1924 reads eth_linkmode with configGetInt and no check; system.cpp:1561 truncates it into the u8 g_ps2_ETHOpMode. set_ipconfig (util.c:96-99, run at every game start from main.c:154) tests only != AUTO and then reads SmapLinkModeArgs[mode-1], a 4-entry local array, so eth_linkmode=5..255 (or -1) reads a stack word as a char* and strncpy copies 6 bytes from it: garbage SMAP args or an address error at game start. The menu treats these values as AUTO (netsupport.cpp:408-423). Second path: in UDPBD/UDPFS modes system.cpp:1580 sprintf's unchecked %d ps2_ip values (sscanf at settingsio.cpp:1950) into g_ps2_ip[16]; a field over 3 digits leaves it unterminated, strlen at util.c:85 then spans the netmask, and with a non-auto link mode g_ipconfig[64] overflows by up to ~15 bytes.
- Fix: In util.c:96 also require g_ps2_ETHOpMode < ETH_OP_MODE_COUNT (the unused enum end at ee_core.h:51), so out-of-range values act as AUTO like the menu. At system.cpp:1580 print the four values as (u8) with %u, as line 1577 already does, or use snprintf with sizeof(config->g_ps2_ip).
- Justification: Both changes stay on existing lines (one extra condition, one format change). They close an out-of-bounds read that can crash every launch and a .bss overrun, both caused by one bad config value.
- Benefit: Removes an out-of-bounds stack read (wild-pointer strncpy) at ee_core init and a g_ipconfig overrun of up to ~15 bytes (INFERRED from 16+16+16+9+6 byte layout); 0 lines net
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-253: Xerabora mode: the Hardcore filter on a game's achievement list always shows an empty page

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/sources/achievements.cpp:326-335`; `RiptOPL/src/sources/achievements.cpp:408-411`; `RiptOPL/src/ui/menusys.cpp:1238-1239`
- Problem: Square cycles achFilter 0..3 on any 'A' page (menusys.cpp:1239). In Xerabora mode achievementsXeraboraPage skips every entry with !a->hardcore when filter==3, but xeraEntryMember only fills hardcore for kind 'G'; for 'A' entries it stays 0 from the memset at achievements.cpp:371. So filter 3 (Hardcore) always lists nothing, with total 0.
- Fix: In Xerabora mode let Square cycle only All/Earned/Locked (filter modulo 3 when gRAMode is RA_MODE_XERABORA), or drop the filter==3 clause for kind 'A' if the xeRAbora /game document turns out to carry a per-achievement hardcore field to parse instead.
- Justification: One condition in the existing key handler removes a filter state that can never show data; it is smaller than parsing a field the document does not appear to provide.
- Benefit: Fixes a UI state that always shows an empty list; about 1 line changed
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-254: ZombieZone_patches scans 768 KB past the end of 32 MB RAM

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/ee_core/src/patches.c:408`; `RiptOPL/ee_core/src/util.c:117-121`; `RiptOPL/ee_core/src/tlb.c:84-92`
- Problem: ZombieZone_patches searches from 0x001c0000 for 0x01f00000 bytes, i.e. up to 0x020c0000; every other scan in patches.c stops at or below 0x01FC0000. If the scePadEnd pattern is not found below 0x02000000 (a different build under one of the four serials at patches.c:105-108), find_pattern_with_mask reads 0x02000000, which no TLB entry maps (the cached RAM entries end at 0x01FFFFFF, tlb.c:84-92), and the TLB miss stops the boot instead of skipping the patch.
- Fix: Change the size argument to 0x01e40000 (0x02000000 - 0x001c0000).
- Benefit: Removes a TLB-miss hang path at boot for four titles when the pattern is absent; one constant, no size change (INFERRED)
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-255: config.path stores the slot-resolved massN: form, so the boot read loses the device type and can wait about 14 s

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/app/settingsio.cpp:2368-2371`; `RiptOPL/src/app/settingsio.cpp:2419-2420`; `RiptOPL/src/app/settingsio.cpp:659-663`; `RiptOPL/src/app/settingsio.cpp:964-965`; `RiptOPL/src/sources/bdmboot.cpp:195-226`; `RiptOPL/src/sources/bdmboot.cpp:472-477`
- Problem: prepareCustomSettingsPath rewrites customSettingsTarget in place (usb:/CFG becomes massN:/CFG through bdmResolveBootDir), and that rewritten text is what writeConfigPathRedirect saves. At boot the untyped literal massN: goes to resolveLiteralMassSlot with the full budget. It loads USB, then MX4SIO, then iLink and ATA whatever the settings say, waits 3+3+8 s on the main thread when the device is absent, and accepts whichever device registered under that slot number, which depends on load order.
- Fix: Pass gCustomSettingsPath (the user's own spelling) to writeConfigPathRedirect at line 2420 instead of the resolved customSettingsTarget. The boot read already runs prepareCustomSettingsPath on whatever it reads.
- Benefit: With the settings device missing, the worst-case boot wait for a typed usb: path drops from about 14 s (3000+3000+8000 ms) to 3 s, with no unrequested MX4SIO/iLink/ATA loads (INFERRED from bdmboot.cpp budgets)
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-256: elfldr linkfile keeps each -ffunction-sections input as its own output section: ~4 KB of section headers in the embedded elfldr_irx

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/elfldr/linkfile:22-24`; `RiptOPL/elfldr/linkfile:53-78`; `RiptOPL/ps2.yaml:615-624`; `RiptOPL/src/platform/elfldr_noreset.cpp:220-237`
- Problem: elfldr is built with -ffunction-sections -fdata-sections (ps2.yaml:619) but the linker script only collects *(.text), *(.rodata), *(.data), *(.sbss), *(.bss), so every .text.<fn>, .rodata.<fn>.str1.8 and .bss.<var> input section becomes an orphan output section: size -A lists 73 sections for bin/elfldr.elf. The ELF is embedded whole as elfldr_irx (21,476 bytes, type D in the main ELF), but the parent only reads the ELF header, the program headers and PT_LOAD data (elfldr_noreset.cpp:220-237), so the section header table and .shstrtab are dead bytes held in EE RAM for the whole run.
- Fix: In elfldr/linkfile collect the suffixed sections into their parents: *(.text .text.*), *(.rodata .rodata.*), *(.data .data.*), *(.sbss .sbss.*), *(.bss .bss.*). Keep the flags and --gc-sections; nothing else changes.
- Benefit: INFERRED from MEASURED sizes: blob 21,476 B vs loadable span 16,900 B (0x84000..end of .data); ~75 headers x 40 B + ~1.4 KB of names shrink to ~10 headers, so ~3.9 KB less in the unpacked main ELF and in resident RAM; the packed file gains less.
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-257: elfldr wipeUserMem calls GetMemorySize() in the loop condition: 507,904 kernel calls on every child-loader launch

- Category: 5 (Performance)
- Location(s): `RiptOPL/elfldr/loader.c:219-229`; `POPSLoader/src/elf_loader/src/loader/src/loader.c:71-81`
- Problem: The loop condition i < GetMemorySize() is re-evaluated every 64 bytes; GetMemorySize is an external libkernel call (a kernel syscall, INFERRED) the compiler cannot hoist, so a 32 MB console makes (0x2000000-0x100000)/64 = 507,904 calls, and a 128 MB DTL-T10000 four times that. It runs on the EE in main() before SifLoadElf (loader.c:420) on every Neutrino, POPSTARTER, Ember and app handoff. POPSLoader's child loader has the same loop; Launchstation's loader.c:216-217 already reads the size once.
- Fix: Read GetMemorySize() once into a local before the loop and compare against it.
- Benefit: INFERRED: removes ~508K syscall round trips per launch; at an assumed 100-200 cycles each on the 294.9 MHz EE that is roughly 0.15-0.35 s of black screen per handoff (more on 128 MB units).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-258: vmc_groups.cpp: eight malformed title ids can never match a startup id

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/core/vmc_groups.cpp:443-444`; `RiptOPL/src/core/vmc_groups.cpp:464-465`; `RiptOPL/src/core/vmc_groups.cpp:472-473`; `RiptOPL/src/core/vmc_groups.cpp:498`; `RiptOPL/src/core/vmc_groups.cpp:614`; `RiptOPL/src/ui/guigame.cpp:493`; `RiptOPL/src/ui/guigame.cpp:529-530`
- Problem: getGroupIdForTitleId does an exact strcmp (vmc_groups.cpp:1465) against the game's startup id (guigame.cpp:493,529), which has the dotted 11-char form SLUS_207.54. Entries SLUS_20754, SLUS_20824, SCES_50422, SLES_50422, SCES_51154, SLES_51154, SLES-5350 and SSCES_540.95 lack the dot, use a dash or have an extra letter, so these games never get their series VMC and silently fall back to a per-game card.
- Fix: Correct the eight strings to the dotted form (SLUS_207.54, SLUS_208.24, SCES_504.22, SLES_504.22, SCES_511.54, SLES_511.54, SCES_540.95; SLES-5350 is most likely SLES_535.06, next to SCES_535.06 at line 496). None of the corrected ids is already in the table.
- Benefit: Minor bug fixed: 8 titles get their shared group card. 0 lines, 0 bytes.
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-259: oplIGRShutdown writes poff uncached with SIF_RPC_M_NOWBDC and no cache flush

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/ee_core/src/cd_igr_rpc.c:12-22`
- Problem: poffData is a 64-byte-aligned stack slot; its cache line can still be dirty in the write-back D-cache from earlier IGR-thread calls. The value is stored through UNCACHED_SEG and the call uses SIF_RPC_M_NOWBDC, so nothing writes back or invalidates that line; if it is evicted before the SIF DMA reads memory, the stale stack word replaces poff. CDVDMAN then powers the console off (poff != 0, cdvdman.c:289-295) on a normal IGR exit, or skips the power-off on the L3+R3 path (padhook.c:565,669). freeze_diag.c:185 and :214 call FlushCache(0) before the same kind of store for this reason.
- Fix: Store poff with a normal write and drop SIF_RPC_M_NOWBDC so SifCallRpc writes the send buffer back itself (no extra code); or call FlushCache(0) before the uncached store as freeze_diag.c does.
- Benefit: Removes a rare wrong-poff case on every IGR exit (unwanted power-off or missed power-off); no size change (INFERRED).
- Effort: S. Risk: Low. Confidence: Low (INFERRED)

#### RIPTOPL-260: .cht parser: an indented comment line becomes a blank-named cheat, and lines over 255 chars are split with one byte dropped

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/core/cheatman.cpp:261-271`; `RiptOPL/src/core/cheatman.cpp:342-352`; `RiptOPL/src/core/cheatman.cpp:326-335`; `RiptOPL/src/core/cheatman.cpp:381`
- Problem: For a line like ' // note', term_str cuts at '//' leaving ' ', trim_str returns -1 on an all-space string without clearing it, and strlen>0 makes it a cheat NAME: before a cheat's codes it renames the cheat to spaces; between codes it splits the cheat, and in Select mode the second half starts disabled. A line longer than 255 chars is capped, but buf advances by len+1 (256), so byte 255 is dropped and the tail is parsed as its own line (a long comment's tail becomes a cheat name).
- Fix: In trim_str set s[0] = NUL before returning -1 for an all-space string. In parse_buf compute the advance from lfIdx (lfIdx + 1, or strlen when there is no LF) before capping len, so the rest of an over-long line is skipped.
- Justification: One statement in trim_str and one changed expression; both fix wrong cheat names/splits from ordinary user-supplied .cht text (cht.tar packs are treated as untrusted input by the comment at 330-333). Names also key the remembered picks, so a wrong name restores the wrong cheat.
- Benefit: Correct parsing of indented comments and long lines; no size change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-261: diagSave keeps a static 16 KB copy buffer in .bss for the whole run

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/src/core/diagsave.cpp:17-21`; `RiptOPL/src/core/diagsave.cpp:32`
- Problem: static char copy[DIAG_SIZE] (16 KB) is reserved in every build although it is only used while DIAG.TXT is written: from the Drive Status 'save' action (drivestatus.cpp:161) and, with Debug on, before an MMCE launch (mmcesupport.cpp:1335,1436,1666).
- Fix: malloc(DIAG_SIZE) at the start of diagSave, return 0 if it fails, free it after the write.
- Justification: Adds one NULL check; returns 16 KB of EE RAM for the whole session for a rare debug save.
- Benefit: 16 KB .bss removed (INFERRED: char copy[16 * 1024]); no ELF file size change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-262: EthSource::update share picker: 64 KB ShareEntry_t sharelist[128] on the IO worker stack is a SIF DMA target with no cache maintenance

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/sources/ethsupport.cpp:829-850`; `RiptOPL/modules/network/smbman-ra/smb_fio.c:999-1000`; `RiptOPL/modules/network/smbman-ra/smb_fio.c:871-889`; `RiptOPL/src/platform/ioman.cpp:42-44`
- Problem: The share picker passes a stack array (128 x 512 B = 64 KB of the IO worker's 96 KB stack) as EE_addr; the shipped smbman_ra copies each entry into it with sceSifSetDma (smb_fio.c:1000, DMA_sendEE). Nothing writes back or invalidates the EE data cache for that range and the array is not 64-byte aligned, so a dirty or stale line from earlier deep stack use can override the DMA'd share names that are then read at 847-849.
- Fix: Align the array to 64 bytes and write back/invalidate its range (SifWriteBackDCache, the pattern fileXioRead uses for its receive buffer) before the GETSHARELIST devctl. Optionally lower maxent (only the 31-char name is kept) to give back most of the 64 KB of worker stack.
- Justification: Adds one cache-maintenance call and an alignment attribute: the IOP writes this buffer by DMA behind the EE cache, which is a correctness hole on real hardware; one line is the smallest way to make the read coherent.
- Benefit: Removes a non-coherent DMA read on the share-picker path; with maxent 32 the frame drops from 64 KB to 16 KB of the 96 KB worker stack (INFERRED: sizeof(ShareEntry_t) = 512 per tests/smb2-live/inc-sdk/ps2smb.h:79-83).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-263: HDD PS1 favourite launch and Ember settings save rebuild the whole VCD/Ember list on the GUI thread

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/sources/hddsupport.cpp:2319-2326`; `RiptOPL/src/sources/hddsupport.cpp:3367-3372`; `RiptOPL/src/sources/hddsupport.cpp:1699-1701`; `RiptOPL/src/sources/favsupport.cpp:1309-1310`
- Problem: When a PS1/Ember favourite is launched before this session built the HDD PS1 list (the HDD page never entered PS1/mixed view), launchVcd calls buildVcdGameList directly on the GUI thread with the IO worker blocked. That rebuild walks the APA table, mounts every __.POPS* and __.EMBER* partition, scans them, and runs cueGameSizeMB (opendir plus a stat per .bin) for every Ember game. No frame is drawn meanwhile, so the menu looks hung. menuSaveCueSettings at least prints a text screen first. launchVcd prints nothing.
- Fix: Before the cold rebuild in launchVcd, render the wait text with host::ui().renderTextScreen(_l(_STR_PLEASE_WAIT)), as menusys.cpp:1004 already does for saveCueSettings. The larger fix is to run the rebuild through guiHandleDeferedIO on the IO worker.
- Justification: Adds one line. Today the launch gives no feedback while a scan of several PFS mounts plus per-game directory walks runs on the render thread. A wait screen is the smallest change and uses an existing helper and string.
- Benefit: Removes a silent UI hang of roughly 0.3-2 s on the first HDD PS1 favourite launch per session (INFERRED: mount and directory-scan costs scale with containers and Ember games; not measured).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-264: When a font's 4 atlases are full, cacheGlyph drops the glyph and FreeType re-renders it on every draw and measure

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/gfx/fntsys.cpp:545-578`; `RiptOPL/src/gfx/fntsys.cpp:608-625`; `RiptOPL/include/fontsystem.h:16-20`
- Problem: Nothing is evicted. Once all ATLAS_MAX (4) 256x256 atlases of a font are full, glyphAtlasPlace returns 0 and cacheGlyph returns NULL with the entry still invalid. The character is not drawn, the text closes up around it, and every later renderString or calcDimensions call runs FT_Load_Char + FT_Render_Glyph for it again and walks all four atlas trees. At 720p (native 720 lines, renderman.cpp:74) the 17 px font is ~25 px, so a CJK glyph cell is ~25x26 and an atlas holds ~90-100, ~400 per font. Japanese.yml alone has 490 distinct CJK characters.
- Fix: On a placement failure, mark the entry valid with no allocation (as glyphAtlasPlace already does for empty bitmaps), so a full atlas costs nothing per frame. To make the characters show, raise ATLAS_MAX to 8: atlases are created lazily, so only a font that fills four pays 64 KB per extra atlas.
- Justification: The problem is missing CJK characters, plus FreeType re-rasterising every missing glyph on the GUI thread each frame. Caching the failure is one assignment. Raising ATLAS_MAX is a constant change that allocates nothing for fonts that never need it, which makes it the simplest way to fit a CJK UI at 720p.
- Benefit: Stops per-frame FreeType re-rendering of every glyph that does not fit (INFERRED: tens of microseconds each on the EE). With ATLAS_MAX 8, CJK UIs at 720p/576p keep all their characters (capacity arithmetic INFERRED).
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-265: configWrite restores the snapshot only when the file is shorter than the OLD file, so a save cut short after it grew stays truncated

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/core/config.cpp:1673-1685`
- Problem: After a failed save the damage test is curLen < originalLen. When the new content is longer than the old file and the write stops part way (short write on a flaky or full device, the #184 case this machinery exists for), a file with originalLen <= curLen < new length is left as is: the tail settings are lost and the last line is cut mid-value, though a good snapshot is in RAM. The restore write's result is also unchecked (1681) while the log says restored.
- Fix: Keep fileBuffer->totalQueued in a local before closeFileBuffer and restore when curLen differs from it (only on the path where the file was opened); leave the open-failure path without a restore as now.
- Justification: Replaces one comparison and adds one local. The concrete problem is a truncated settings file left on disk after a failed save while the intact original is held in RAM; comparing with the length that was meant to land is the smallest correct damage test.
- Benefit: Fixes a minor settings-loss case on failed saves to USB/HDD/MMCE/SMB; no size change (READ).
- Effort: S. Risk: Med. Confidence: Med (READ)

#### RIPTOPL-266: fntLoadDefault never sets a char size on the new default face (updateAspectRatioLocked runs while the font is still a local)

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/gfx/fntsys.cpp:393-412`; `RiptOPL/src/gfx/fntsys.cpp:489-511`; `RiptOPL/src/gfx/fntsys.cpp:648-677`; `RiptOPL/src/app/opl.cpp:1944-1952`; `RiptOPL/src/app/opl.cpp:1717-1719`; `RiptOPL/src/core/lang.cpp:454`; `RiptOPL/src/ui/guisplash.cpp:102-107`
- Problem: loadSlotLocked ends in updateAspectRatioLocked, which only calls FT_Set_Char_Size on fonts_[i]; loadDefault loads into the local newFont and copies it into fonts_[FNT_DEFAULT] afterwards, so the new default face has no size until some later fntUpdateAspectRatio or fntLoadFile. fntInit loads the built-in font this way: the __OPLDIAG autolaunch screen (opl.cpp:1944-1952) draws FNT_DEFAULT straight after fntInit, and the boot splash draws its version and status lines (guisplash.cpp:102-107) before _loadConfig reaches applyConfig's guiUpdateScreenScale. With no size, FreeType 2.14 loads glyphs unscaled and drops FT_LOAD_RENDER, so those glyphs are cached as valid but have no bitmap.
- Fix: Move the updateAspectRatioLocked() call out of loadSlotLocked: call it in loadDefault after the memcpy into fonts_[FNT_DEFAULT], and in loadFile after a successful loadSlotLocked.
- Justification: It moves an existing call rather than adding logic, at the cost of one extra line: the call has to run once the new font is in fonts_, otherwise the default face is never sized. This is the smallest correct fix.
- Benefit: Text drawn with the default font between fntInit and the first guiUpdateScreenScale is drawn at the right size instead of blank (the FreeType no-size behaviour is INFERRED from the 2.14.1 SDK version; the missing FT_Set_Char_Size is READ). Net +1 line.
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-267: texLoadJpgMemory has no size limit before jpeg_start_decompress: a large progressive JPG allocates MBs of RAM on the art thread

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/src/gfx/textures.cpp:1194-1196`; `RiptOPL/src/gfx/textures.cpp:1208-1212`
- Problem: The PNG path refuses images over 1024 px before libpng allocates rows (png_set_user_limits, 1035). The JPG path has no bound. Downscaling to 1/8 does not help a progressive JPEG: libjpeg keeps the whole coefficient image in RAM (about 3 bytes per pixel at 4:2:0, 6 at 4:4:4), and the linked memory manager has no backing store. A 2000x2000 progressive cover or background (common for web downloads) therefore takes about 12 MB of heap on the art worker, next to its staged file buffer, while the GUI thread allocates in parallel.
- Fix: Right after jpeg_read_header, refuse a progressive image above a pixel bound (for example about one megapixel). Or set info.mem->max_memory_to_use once after jpeg_create_decompress so that libjpeg fails with an error instead of allocating.
- Justification: One line closes an unbounded allocation driven by untrusted art files. The PNG side already has the same guard for the same reason (YAOPL 07c868d comment at 1033-1034).
- Benefit: Caps the JPG decoder's transient heap use. INFERRED: progressive 2000x2000 4:2:0 = 93,750 blocks x 128 B = 12 MB; 3000x3000 = 27 MB fails outright after a long attempt.
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-268: Legacy app records leave argv1 uninitialised, and the argv1 that dedup donates to them is never passed at launch

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/sources/appsupport.cpp:696-721`; `RiptOPL/src/sources/appsupport.cpp:374-381`; `RiptOPL/src/sources/appsupport.cpp:1323-1342`; `RiptOPL/src/sources/appsupport.cpp:1354`
- Problem: addAppsLegacyList mallocs each node and never sets app.argv1 (696-721), yet appDedupList reads appsList[dup].argv1[0] of the legacy survivor (378), which is uninitialised heap. Even when the scanned duplicate's argv1 is copied in, config() for a legacy row never sets CONFIG_ITEM_ALTSTARTUP from argv1 (1323-1342; only the scanned branch does, 1354). An app listed in both conf_apps.cfg and a title.cfg with argv1= is shown as the legacy row and starts without its argument, contrary to the comment at 355-356.
- Fix: Set app->app.argv1[0] = '\0' in addAppsLegacyList. In config()'s legacy branch, set CONFIG_ITEM_ALTSTARTUP from appsList[id].argv1 when it is non-empty. If donation is not wanted, delete lines 378-381 instead.
- Justification: Adds 2-3 lines. They remove an uninitialised read and make the documented argv1 donation actually reach the launch. Otherwise the argument from title.cfg is lost whenever the app is also in conf_apps.cfg.
- Benefit: Deterministic dedup; launch argument kept for apps present in both channels (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-269: A save request abandoned by the 30 s deferred-IO timeout still runs later with lscstatus 0

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/app/settingsio.cpp:2215-2221`; `RiptOPL/src/app/settingsio.cpp:2555-2569`; `RiptOPL/src/ui/guiwait.cpp:99-110`; `RiptOPL/src/platform/ioman.cpp:179-185`
- Problem: When guiHandleDeferedIO gives up after OPL_DEFERRED_IO_TIMEOUT_MS it only clears lscstatus. The queued IO_CUSTOM_SIMPLEACTION request stays in the queue, so _saveConfig still runs when the worker reaches it. With lscstatus 0 it runs the rest of the save path anyway: the BDM re-resolve and retry (bdmResolveBootDir with its full budget); on a bare boot, the whole trySaveAlternateDevice sweep including bdmHDDIsPresent(5000); and on an APA boot with a staged home it mounts pfs1:, writes nothing and commits the HDD home selector, which strands the next boot on a home without the settings.
- Fix: Return at the top of _saveConfig when lscstatus is 0.
- Justification: The one-line guard closes a path where a stale request blocks the single IO worker for seconds and can commit an APA home selection without its settings. The comment at 2604-2608 states that the timeout happens in practice.
- Benefit: Avoids seconds of wasted IO-worker time and a possible wrong-home commit after a timed-out save (INFERRED)
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-270: BgmPlayer::quiesce() signals and wakes semaphore/thread IDs it does not own when the player is not live

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/core/bgm.cpp:820-842`; `RiptOPL/src/core/bgm.cpp:720-726`; `RiptOPL/src/core/bgm.cpp:759-764`; `RiptOPL/include/bgmplayer.h:35-36`; `RiptOPL/src/app/opl.cpp:1325`; `RiptOPL/src/app/opl.cpp:1419`
- Problem: deinitEx()/deinit() call bgmQuiesce() on every launch and exit, and quiesce() does SignalSema(inSema_), SignalSema(outSema_), WakeupThread(threadId_) without checking live_. With BGM never started these IDs are 0; with BGM enabled but no usable bgm.ogg, start() created the objects and teardown() deleted them (759-764, 720-726), leaving stale IDs the kernel can hand to later CreateSema/CreateThread calls. A launch then signals or wakes another subsystem's object.
- Fix: Return early from quiesce() when live_ is 0.
- Justification: Two lines. The player must only touch kernel objects it created; the guard closes the stale-ID signal on the common BGM-on-without-music and BGM-off paths. stop() needs no change because all its callers already gate it on isBgmPlaying() or the running flags.
- Benefit: Removes a spurious SignalSema x2 and WakeupThread on unowned IDs during every launch/exit teardown; consequence depends on ID reuse (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-271: PADEMU poll issues ds34bt/ds34usb get_status SIF RPCs every frame for port 1 (and every multitap slot), which the menu never enables

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/core/pad.cpp:466-482`; `RiptOPL/src/core/pad.cpp:672-677`; `RiptOPL/src/platform/system.cpp:430-446`; `RiptOPL/modules/ds34bt/ee/libds34bt.c:72-83`; `RiptOPL/modules/ds34usb/ee/libds34usb.c:61-72`; `RiptOPL/modules/ds34usb/iop/ds34usb.c:940-959`; `RiptOPL/modules/ds34bt/iop/ds34bt.c:1675-1685`; `RiptOPL/src/ui/gui.cpp:311`
- Problem: readOne() runs for every opened pad on the GUI thread each frame (Gui::pollPads, gui.cpp:311) and calls ds34bt_get_status and ds34usb_get_status, each a synchronous SifCallRpc. padCount_ includes empty ports, so a normal console makes 4 RPCs per frame with no DS3/DS4 attached. system.cpp:431 loads both drivers with pad mask 1 and the IOP modules set enabled = (enable >> pad) & 1, so port 1 can never be RUNNING: its two RPCs per frame are always wasted. A multitap repeats port 0's calls once per slot.
- Fix: Run the PADEMU block in readOne() and the loop body in pademuStopAll() only for pad->port == 0 && pad->slot == 0, the one pad system.cpp enables (note next to ds3pads that the two must change together).
- Justification: The added condition removes half of the idle-case per-frame SIF RPC traffic from the GUI thread; the code itself documents synchronous SIF RPCs on the GUI thread stalling the menu under IOP load (sfx.cpp:264-267). One comparison is the smallest change that does it.
- Benefit: 2 of 4 synchronous SIF RPCs per frame removed in the common case = about 120 RPC/s at 60 Hz off the GUI thread; more with a multitap (INFERRED from the call counts above, cost per RPC not measured).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-272: httpclient's single RPC client, static buffers and one IOP stream are used by the IO-worker catalog fetch and the GUI-thread launch probe at once

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/platform/httpclient.cpp:9-12`; `RiptOPL/src/platform/httpclient.cpp:95-155`; `RiptOPL/src/sources/httpsupport.cpp:596`; `RiptOPL/src/sources/httpsupport.cpp:794-797`; `RiptOPL/src/ui/menuitems.cpp:174-192`
- Problem: httpclient.cpp has one SifRpcClient, one RpcTxBuffer/RpcRxBuffer and one streamBuffer, and the IOP keeps one live stream (httpclient.h:22-24). HttpSource::update fetches games.csv on the IO worker (httpsupport.cpp:596, via IO_MENU_UPDATE_DEFFERED), while HttpSource::launch runs httpProbeImage on the GUI thread (httpsupport.cpp:794) without blocking IO. menuitems.cpp:174-175 notes that source lists may refresh during the launch hold. A launch pressed while a refresh is in flight (start-up, or a view toggle that marks HTTP dirty) lets the two sessions overwrite each other's RPC arguments and stream. The probe then fails with the range error, or the refresh fails or keeps stale rows.
- Fix: Serialize the probe against the worker the way HddSource does (hddsupport.cpp:2319-2333): ioBlockOps(1) before httpProbeImage in HttpSource::launch and ioBlockOps(0) right after it, including on its failure return.
- Justification: Adds two calls. The race is between the only two users of a single-session RPC client and IOP stream. Blocking the IO queue around the probe reuses an existing pattern and is smaller than locking every Http* call and the Begin/Read/Close sessions in httpsupport.cpp.
- Benefit: Removes an intermittent HTTP launch failure or corrupted refresh when launching during a catalog refresh; +2 lines (INFERRED).
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-273: vsIo seeks and transfers each 512-byte page separately even on images without ECC, doubling the I/O calls of every card-image access on the GUI thread

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/core/vmcsaves.cpp:173-184`
- Problem: vsIo issues one lseek plus one read or write per page, ppc times per cluster. Without the ECC spare (raw == pageLen, the only kind that can be written, 238-241) a cluster's pages are contiguous, so one lseek and one transfer of csize bytes does the same. Every directory entry (vsEnt 316-329), FAT cluster (vsOpen 265, vsFlushFat 281) and data cluster (vsReadChain 366, vsWriteVmc 553 and 589) goes through it, synchronously from the VMC Saves dialog loop (guigame.cpp:435-474) on the GUI thread, which shows nothing while a copy runs.
- Fix: In vsIo, when v->ecc is 0, do one lseek to abs * csize and one read or write of csize bytes; keep the per-page loop only for ECC images.
- Justification: Adds one branch: it halves the lseek/read/write calls (each a fileXio RPC on the PS2, INFERRED) for standard 2-page clusters, which is the main cost of copying a large save while the UI is frozen; it adds no state.
- Benefit: Calls per 1 KB cluster 4 -> 2: an 8 MB save export drops from ~32 K to ~16 K calls (INFERRED: 8192 clusters x 2 pages x 2 calls); listing and FAT loads halve too.
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-274: favWriteFile rewrites favourites.bin/recents.bin even when the new image is byte-identical (every relaunch of the last game, no-match removals)

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/sources/favsupport.cpp:528-545`; `RiptOPL/src/sources/favsupport.cpp:547-570`; `RiptOPL/src/sources/favsupport.cpp:1486-1521`; `RiptOPL/src/sources/favsupport.cpp:1065-1076`; `RiptOPL/src/ui/menuitems.cpp:88-101`
- Problem: One save on the GUI thread costs: the caller's favReadFile read, favPrepareSave's second read and parse of the same primary (528-545), a .new write plus read-back, a primary write plus read-back, and a .new delete (547-570). recentRecord (1486-1521), run before every launch by itemRecordRecent (menuitems.cpp:88-101) when Favourites, All Games or Last-played sort is on, always writes, even when the launched game is already first and the result is byte-identical; removeByIdAndText also writes when no record matched (1065-1076).
- Fix: Pass the new image to favPrepareSave; when the complete primary it already loads is byte-identical (same size, memcmp equal), return success without writing. This covers the replay and no-match cases in one place.
- Justification: Saves memory-card writes and launch delay on the commonest launch (replaying the last game) and on no-op removals; the check reuses the primary image favPrepareSave already reads, about 3 lines.
- Benefit: Per skipped save: 2 card file writes, 2 read-backs and a .new create/delete avoided (INFERRED from 547-570); ~+3 lines.
- Effort: S. Risk: Med. Confidence: High (READ)

#### RIPTOPL-275: Boot splash waits for the sample count read from a theme's sound/boot.adp header, unchecked: a non-ADPCM file holds the splash for hours

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/core/sfx.cpp:137-143`; `RiptOPL/src/core/sfx.cpp:175-186`; `RiptOPL/src/core/sfx.cpp:231-242`; `RiptOPL/src/ui/guisplash.cpp:183-188`; `RiptOPL/src/ui/guisplash.cpp:206-210`
- Problem: SoundEffects::load() sets duration_ms from the u32 at bytes 12-15 of a theme's boot.adp before, and regardless of, audsrv_load_adpcm, with no magic or range check. BootSplash::run() waits duration minus the fade before fading out, and that comparison is the loop's only exit. A WAV renamed to boot.adp has 'fmt ' there (544,501,094), giving 12,346,964 ms: the menu sits on the splash for about 3.4 hours at every boot while Boot Sound is on, until the user knows to hold START.
- Fix: In load(), keep the header value only when audsrv_load_adpcm succeeded (else 0), and clamp duration_ms to a small bound such as 10 s, as THEME_HOLD_MAX_S already does for the theme hold.
- Justification: The clamp is one comparison and closes a boot-time hang reachable from a user-supplied theme file; the success check reuses the existing return value.
- Benefit: Removes an hours-long boot splash hang from a bad theme sound file (arithmetic: 544501094 / 44100 * 1000 = 12.3M ms); about 3 lines added.
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-276: Cancel on a focused int/bool/enum row restores the value the page opened with, discarding an earlier confirmed edit of the same row

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/ui/dia.cpp:839-855`; `RiptOPL/src/ui/dia.cpp:863-868`; `RiptOPL/src/ui/dia.cpp:1298-1301`; `RiptOPL/src/ui/diaitems.cpp:97-111`
- Problem: diaHandleInput's Cancel calls diaResetValue, which sets current = def; def is the value diaSetInt stored when the page opened (diaitems.cpp:105-106) and edits never update it. Sequence: set an IP octet 192 -> 10 and confirm, focus it again, press Up once, press Cancel: the row shows 192, not 10. Same for enums. Inherited Open PS2 Loader behaviour.
- Fix: When Confirm gives a row focus (dia.cpp:1298-1301) remember cur->intvalue.current in a local, and on Cancel restore that value for UI_INT/UI_BOOL/UI_ENUM instead of def; def stays the page-open value diaHasChanges needs.
- Justification: Adds one local and one assignment: def cannot be reused because diaHasChanges (diaitems.cpp:59-80) depends on it meaning the page-open value, so the pre-edit value has to be kept somewhere; this is the smallest place.
- Benefit: Cancel undoes only the current edit; about 3 lines added (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-277: Module storage end never checked against 0xD0000; RA work area is bounded only at 1 MB while IGR cheats use the 0xD0000 rule

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/platform/system.cpp:1435-1447`; `RiptOPL/src/platform/system.cpp:1455-1464`; `RiptOPL/src/app/host.cpp:138-146`; `RiptOPL/ee_core/linkfile:19-20`
- Problem: The linkfile reserves 0x97000-0xD0000 (228 KB) for modules and says 0xD0000+ is used by some games. The IGR cheats code respects that, but nothing checks the modules from sendIrxKernelRAM or the RA work area placed after them, and raWorkBytes caps its card packet at 0x100000. In an RA edition, an SMB2 game with VMC, BT pad emulation, MMCEIGR and telemetry already needs ~220 KB of IRX, and RA_EE_WORK_BYTES (about 13 KB plus 4 bytes per watch) then ends above 0xD0000. A game that writes that range overwrites the RA buffers or the module images ee_core reloads on an in-game IOP reset.
- Fix: Use one rule: cap the RA work area at 0xD0000 like the cheats. Use 0xD0000 as the low-memory limit in raWorkBytes, and in sysLaunchLoaderElf drop the reservation (telemetry off for that launch, logged) when modules plus list would end above it.
- Justification: Adds one comparison, the same one the cheats block makes ten lines later. The alternative is RAM the linkfile documents as game-used holding live buffers and IRX images.
- Benefit: Prevents in-game corruption for RA launches whose storage ends past 0xD0000 (INFERRED: IRX sizes from evidence/RiptOPL/raw/irx-modules.tsv sum to ~219.6 KB; room to 0xD0000 is 233,472 B)
- Effort: S. Risk: Med. Confidence: Low (INFERRED)

#### RIPTOPL-278: AppSource::rename can drop the app's config: legacy branch removes the key via a pointer into the freed node and before validating newName

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/sources/appsupport.cpp:944-965`; `RiptOPL/src/core/config.cpp:447-474`; `RiptOPL/src/core/config.cpp:221-227`
- Problem: Legacy branch: configRemoveKey(configApps, cur->key) frees cur at config.cpp:464 and the loop keeps comparing later nodes against that key, a read after free on every legacy rename. Then configSetStr(newName) fails when the name is empty or contains '=' (configKeyValidate, config.cpp:221-227; '=' is on the on-screen keyboard), and configWrite saves conf_apps.cfg without the app, so it disappears. Scanned branch: the configRead result is ignored, so after a failed read configWrite rewrites title.cfg with only title=, which loses boot= and argv1=. The app then drops out of the list because appScanCallback needs boot.
- Fix: Legacy branch: return early when newName is empty or contains '='. Copy cur->key into a local buffer and pass that copy to configRemoveKey. Scanned branch: call configSetStr/configWrite only when configRead succeeded.
- Justification: Adds about 4 lines. They close a use-after-free and two ways a rename silently deletes the user's app entry or title.cfg, and they are the smallest checks that do so.
- Benefit: No freed-memory read per legacy rename; no lost conf_apps.cfg entry or title.cfg keys on rename (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-279: VMC Saves actions and Load from disc block the UI thread for seconds with no wait screen

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/ui/guigame.cpp:436-445`; `RiptOPL/src/ui/guigame.cpp:451-458`; `RiptOPL/src/ui/guigame.cpp:466-468`; `RiptOPL/src/ui/guigame.cpp:1825-1826`
- Problem: Export, Export as PSU, Import and Delete in guiGameShowVMCSaves call vmcSaves* directly on the menu thread; these read and write the card image one 512-byte page per lseek+read/write (vmcsaves.cpp:173-184), so a large save takes seconds. Load from disc calls sysGetDiscID, which waits for disc detection, sceCdDiskReady and a standby spin-up (system.cpp:498-535). Neither draws anything first, so the dialog's last frame stays frozen until the call returns.
- Fix: Call guiRenderTextScreen(_l(_STR_PLEASE_WAIT)) right before each blocking call, as fmUnpackPsu already does (guifilemgr.cpp:558).
- Justification: Four one-line calls to an existing helper turn a frozen screen of several seconds into a visible wait message; moving the work to the IO thread would be far more code.
- Benefit: No frozen frame during multi-second card-image or disc I/O; durations INFERRED from page-at-a-time image I/O and CD spin-up, not measured
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-280: RA unlock pulse forces PMODE to circuit 1 only and leaves it there

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/ee_core/src/ra_pulse.c:26-28`; `RiptOPL/ee_core/src/ra_pulse.c:111-123`; `RiptOPL/ee_core/src/ra_overlay.c:185-210`; `RiptOPL/ee_core/src/main.c:127-152`
- Problem: For 60 frames the pulse writes PMODE with EN1=1, EN2=0, SLBG=1, and on the last frame writes RA_OVL_PMODE(0xFF), relying on the game rewriting PMODE on its next flip (ra_pulse.c:112-115). A game that shows its picture through read circuit 2 only shows circuit 1 during the pulse, and one that writes PMODE only at start-up stays on circuit 1 afterwards. The card path (ra_overlay.c:185-210) already treats circuit 2 as a normal case and reads the tracked PMODE.
- Fix: When the GS tracker is on (GSM, cards or In-Game Menu, main.c:127-152), save GSMSourceGSRegs.pmode when the pulse starts and write that value back on the last frame instead of RA_OVL_PMODE(0xFF); skip the pulse's PMODE writes when the saved value has EN1 clear. Keep the current behaviour when nothing was tracked.
- Justification: Adds one saved word and two conditions; without them an unlock can leave a circuit-2 game, or one that sets PMODE once, on the wrong display circuit.
- Benefit: Avoids a wrong or black display after an unlock notice in RA builds for such games (INFERRED, not tested on hardware)
- Effort: S. Risk: Med. Confidence: Low (INFERRED)

#### RIPTOPL-281: VMC Saves lists and File Manager selection arrays hold about 30 KB of .bss for the whole run

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/src/ui/guigame.cpp:326-331`; `RiptOPL/src/ui/guifilemgr.cpp:374`; `RiptOPL/src/ui/guifilemgr.cpp:489`
- Problem: vmcSavesCard[64], vmcSavesFolder[64] (166-byte vmc_save_t each), vmcSavesFolderLabels and the two item arrays are file-scope statics used only while guiGameShowVMCSaves runs, and fmToClipboard and fmDeleteSelection each keep their own static int sel[512] although both only run inside guiShowFileManager. Together they pin 30,792 bytes of EE RAM from boot to exit for two rarely opened modal dialogs.
- Fix: Allocate the VMC Saves arrays at the top of guiGameShowVMCSaves and free them on return. In the File Manager, drop both static sel arrays and allocate one selection buffer with the other FM buffers in guiShowFileManager (or put sel on the 128 KB main stack).
- Justification: The fix adds one malloc/free pair per dialog. It returns 30 KB of permanently reserved RAM, and the File Manager already allocates its larger buffers this way (guifilemgr.cpp:618-621), so no new pattern is introduced.
- Benefit: 30,792 B .bss freed MEASURED (raw/riptopl-nm-size-sorted.full.txt: vmcSavesCard 0x2980, vmcSavesFolder 0x2980, vmcSavesFolderLabels 0x1340, two Items arrays 0x104, two sel 0x800); no ELF size change
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-282: rabadge refresh stats two files per list row on every rebuild, including an smb0: share probe, on the single I/O worker

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/sources/rabadge.cpp:118-139`; `RiptOPL/src/sources/rabadge.cpp:179-190`
- Problem: For each row, watchListExists() stats <prefix>RA/<serial>.wl and, when that is missing, smb0:RA/<serial>.wl. A rebuild of an N-game USB list therefore costs up to 2N fileXio round trips, and N of them go over the network when an SMB share is mounted, even when neither RA directory exists. This runs inside updateMenuFromGameList on the I/O worker that also serves cover art (opl.cpp:745-747), so every list rebuild delays art by that many round trips.
- Fix: In refresh(), stat the two directories (<prefix>RA and smb0:RA) once before the loop and skip the per-row probe for a side whose directory is missing; keep the per-row stats only where the directory exists (favourites keep the current path).
- Justification: Adds about 6 lines. The concrete problem is up to 2N stat round trips per list rebuild on the shared I/O thread; one directory stat per side removes all of them for devices never checked and for setups without smb0:RA, which is the simplest change that keeps the loader's lookup order.
- Benefit: INFERRED: a 500-game USB list with no RA folders goes from up to 1,000 stat RPCs (500 over SMB if mounted) to 2 per rebuild; at roughly 1-3 ms per SMB stat that is about 0.5-1.5 s less I/O-thread time per rebuild (timing not measured).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-283: Changelog-style narrative comments and one comment duplicated from hddint.h

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/sources/hddsupport.cpp:361-364`; `RiptOPL/include/hddint.h:98-101`; `RiptOPL/src/sources/hddsupport.cpp:666-681`; `RiptOPL/src/sources/hddsupport.cpp:727-741`; `RiptOPL/src/sources/hddsupport.cpp:751-760`; `RiptOPL/src/sources/hddsupport.cpp:973-984`; `RiptOPL/src/sources/hddsupport.cpp:1084-1098`; `RiptOPL/src/sources/hddsupport.cpp:1854-1859`
- Problem: Many blocks narrate history (reviewer names, issue numbers, 'used to', 'the old path', user reports) rather than the current rule, e.g. 666-681 and 727-741 are 15-16 lines each around a few statements. The comment at 361-364 is a verbatim copy of hddint.h 98-101.
- Fix: Delete the duplicated comment at 361-364 and cut each narrative block to the one or two sentences that state the current invariant; the history is in git.
- Benefit: ~60 comment lines removed; no ELF change
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-284: Contradictory comments on bdmReadDriverName say a no-buffer probe guards the read; the code removed it

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/include/bdmsupport.h:205-213`; `RiptOPL/src/sources/bdmsupport.cpp:240-263`; `RiptOPL/src/sources/bdmsupport.cpp:271-292`; `RiptOPL/src/sources/bdmsupport.cpp:653`
- Problem: The header says the implementation 'asks without a buffer first', and the block above the function ends 'Gate on it, then ask for the full name'; the 22-line block inside the function says that probe was retracted, and line 293 issues the buffered ioctl directly. bdmProbeMassSlots:653 still calls it a 'Guarded reader'. About 55 comment lines describe two opposite contracts for one 3-line function.
- Fix: Replace the three blocks with one short accurate comment (the read is unguarded; the fork bdmfs_fatfs guards the NULL copy) and fix the 'Guarded reader' remark.
- Benefit: ~45 comment lines removed (INFERRED); stops callers relying on a guard that does not exist.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-285: Stale history and investigation comments in guigameid.cpp and drawArtHud

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/ui/guigameid.cpp:12-15`; `RiptOPL/src/ui/guigameid.cpp:190-195`; `RiptOPL/src/ui/guioverlay.cpp:136-142`; `RiptOPL/src/ui/guioverlay.cpp:149-151`; `RiptOPL/src/ui/guioverlay.cpp:154-190`
- Problem: guigameid.cpp keeps fork-commit history and a NOTE(rebuild) about a helper that no longer exists and 'checklist item 48'. drawArtHud carries ~50 lines of debugging narrative, including 'THIS IS THE FIELD TO READ NOW' and 'an in-RAM index of the art folder becomes worth building', while the IX field documented a few lines later is that index, already built.
- Fix: Delete the history paragraphs and the NOTE(rebuild) block; cut the drawArtHud comment to a short legend of the HUD fields.
- Benefit: ~45 comment lines removed; no ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-286: ioman: 24-line banner about a removed constant, orphaned doc comment, unused IO_ERR_UNKNOWN_REQUEST_TYPE

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/platform/ioman.cpp:16-39`; `RiptOPL/include/ioman.h:8`; `RiptOPL/include/ioman.h:38-40`; `RiptOPL/src/platform/ioman.cpp:106-111`
- Problem: ioman.cpp:16-39 narrates why a MAX_IO_REQUESTS constant that no longer exists was removed and names gProcSemaId and texcache.c, which are not in the code any more (the semaphore is procSema_). ioman.h:38-40 documents a 'removes all requests of a given type' function that has no prototype or definition. IO_ERR_UNKNOWN_REQUEST_TYPE has no user. IoQueue::process checks req for NULL although its only caller passes head_ after testing it, and its 'invalidate the request' comment describes nothing.
- Fix: Delete the banner (a one-line 'no queue cap by design' note is enough), the orphaned doc comment, the unused macro, the NULL check and the stale comment.
- Benefit: ~31 lines removed, almost all comments; no ELF change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-287: Dead declarations and a no-op return-address save in ee_core assembly (asm.S, cheat_engine.S, crt0.S, dve_reg.h)

- Category: 1 (Dead code)
- Location(s): `RiptOPL/ee_core/src/asm.S:37-40`; `RiptOPL/ee_core/src/asm.S:44-45`; `RiptOPL/ee_core/src/asm.S:64-66`; `RiptOPL/ee_core/src/cheat_engine.S:27-30`; `RiptOPL/ee_core/src/cheat_engine.S:43-45`; `RiptOPL/ee_core/src/cheat_engine.S:54`; `RiptOPL/ee_core/src/cheat_engine.S:64`; `RiptOPL/ee_core/src/cheat_engine.S:108`; `RiptOPL/ee_core/src/cheat_engine.S:570-571`; `RiptOPL/ee_core/src/cheat_engine.S:608-609`; `RiptOPL/ee_core/src/cheat_engine.S:617-628`; `RiptOPL/ee_core/src/crt0.S:23`; `RiptOPL/ee_core/include/dve_reg.h:15`
- Problem: asm.S declares .extern memset/memcpy/strlen/strncpy/g_argc/g_argv/Install_IGR that it never uses (g_argc/g_argv exist nowhere) and .globl g_argbuf, which has no label anywhere. cheat_engine.S keeps PS2RD ERL leftovers: F_* defines nobody tests, the 'exported when built as ERL' comment, .globl j_SetupThread with no label, an unused eh_jal label, and engine_info (no reader; its .rodata is already discarded). crt0.S declares _isInit (the code uses isInit). dve_get_reg's C prototype has no C caller. HookSetupThread saves and restores ra around code that makes no call.
- Fix: Delete those lines; in HookSetupThread delete the sp adjust and the ra sd/ld (lines 570-571, 608-609).
- Benefit: ~30 lines; engine_info .rodata (4 B) already dropped by gc-sections (MEASURED, eecore-discarded-firstparty.txt:9); ra save removal saves 4 instructions = 16 bytes ram84 text (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-288: plasma: GCC<=3 asm branch, commented gradient list, history comment and forwarders

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/gfx/plasma.cpp:31`; `RiptOPL/src/gfx/plasma.cpp:40-49`; `RiptOPL/src/gfx/plasma.cpp:84-92`; `RiptOPL/src/gfx/plasma.cpp:231-241`; `RiptOPL/include/plasma.h:6`; `RiptOPL/include/plasma.h:21-25`
- Problem: The #else branch of VU0MixVec is for GCC 3 register syntax; both supported toolchains (ps2build GCC and LLVM, which defines __GNUC__ 4) take the first branch. Lines 84-92 are commented-out pseudo-code, 231-240 is a change-history note ending in a needless alias (blend = blendColor), plasma.h:6 names a gone path (src/plasma.cpp), and ease() only forwards to the private smoothStep.
- Fix: Delete the #if/#else/#endif and keep the modern asm; delete the commented gradient lines and the history note, use blendColor directly; fix the path in the comment; make smoothStep public as ease (or the reverse) and drop the forwarder.
- Benefit: ~30 lines removed, INFERRED; no ELF change (the deleted asm branch is never compiled).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-289: elfldr/Makefile duplicates the ps2.yaml elfldr target and is reached only from the legacy top-level Makefile

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/elfldr/Makefile:1-27`; `RiptOPL/ps2.yaml:615-624`
- Problem: The real build compiles elfldr from ps2.yaml:615-624 (same source, defines, gc-sections and linkfile). elfldr/Makefile is invoked only by the legacy top-level Makefile (Makefile:429 clean, :579-584 bin2c rule), which is not the build.
- Fix: Delete elfldr/Makefile together with the legacy Makefile tree (or now, if the legacy Makefile is being retired piecewise).
- Benefit: 27 lines of build clutter; no shipped change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-290: Commented-out code, dead log and stale TODO comments in fntsys.cpp

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/gfx/fntsys.cpp:549`; `RiptOPL/src/gfx/fntsys.cpp:558`; `RiptOPL/src/gfx/fntsys.cpp:561`; `RiptOPL/src/gfx/fntsys.cpp:569`; `RiptOPL/src/gfx/fntsys.cpp:576`; `RiptOPL/src/gfx/fntsys.cpp:603-606`; `RiptOPL/src/gfx/fntsys.cpp:612-615`; `RiptOPL/src/gfx/fntsys.cpp:672`; `RiptOPL/src/gfx/fntsys.cpp:683-691`; `RiptOPL/src/gfx/fntsys.cpp:757-762`
- Problem: Four commented-out LOG lines, a commented-out 'should never happen' safeguard, a 'Face is NULL' log that then carries on to load the glyph anyway (FT_Load_Char fails on a NULL face and the error path logs again), a LOG with a stray argument (576), and old TODO/design notes (672, 683-691, 757-762) about rendering rework that never happened.
- Fix: Delete the commented-out lines, the NULL-face log block, the stray LOG argument, and the stale TODO/notes comments.
- Benefit: ~25 comment/dead lines removed; the NULL-face log string leaves .rodata (a few bytes, INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-291: Stale history and contradictory comments in mmcesupport.cpp and netsupport.cpp/.h

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/sources/mmcesupport.cpp:981-982`; `RiptOPL/src/sources/mmcesupport.cpp:1372-1373`; `RiptOPL/src/sources/mmcesupport.cpp:1553`; `RiptOPL/src/sources/mmcesupport.cpp:1646-1655`; `RiptOPL/src/sources/mmcesupport.cpp:1712-1721`; `RiptOPL/src/sources/netsupport.cpp:4-8`; `RiptOPL/include/netsupport.h:12`
- Problem: Several comments only record where code used to be ('moved ABOVE', 'hoisted above', 'moved verbatim out of src/ethsupport.c'), and netsupport.h names ethsupport.c/httpsupport.c, which are now .cpp. The Neutrino GameID comment at 1646-1655 says the native OPL-core launch deliberately does NOT send a GameID, which the code at 1399-1414 and the 'CORRECTED HISTORY' essay at 1712-1721 contradict.
- Fix: Delete the 'moved/hoisted' notes and the history essay at 1712-1721; cut 1646-1655 to one line saying Neutrino has no mcemu so the card must be told the game; fix the file names in netsupport.h.
- Benefit: ~25 comment lines removed; no binary change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-292: About 20 include lines in settingsio.cpp are leftovers of opl.cpp's include list that this file does not use

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/app/settingsio.cpp:13`; `RiptOPL/src/app/settingsio.cpp:15`; `RiptOPL/src/app/settingsio.cpp:20`; `RiptOPL/src/app/settingsio.cpp:24-25`; `RiptOPL/src/app/settingsio.cpp:29-32`; `RiptOPL/src/app/settingsio.cpp:36`; `RiptOPL/src/app/settingsio.cpp:40-41`; `RiptOPL/src/app/settingsio.cpp:44`; `RiptOPL/src/app/settingsio.cpp:46`; `RiptOPL/src/app/settingsio.cpp:50`; `RiptOPL/src/app/settingsio.cpp:52-54`; `RiptOPL/src/app/settingsio.cpp:56`; `RiptOPL/src/app/settingsio.cpp:58`; `RiptOPL/src/app/settingsio.cpp:61-62`; `RiptOPL/src/app/settingsio.cpp:68-71`
- Problem: No identifier from launchdiag.h, guigame.h, textures.h, dia.h, dialogs.h, extern_irx.h, ethsupport.h, udpfssupport.h, appsupport.h, folderbrowse.h, cuesupport.h, rabadge.h, cheatman.h, xparam.h, delaythread.h, fileXio_rpc.h or libds34bt/libds34usb.h appears in the file body; fntsys.h is also pulled in unconditionally by opl.h:46, so its __OPLDIAG include is redundant. Several include comments are false here (62: fileXio 'used by BDM autolaunch'; 46 folderDepth; 50 cueIsCueEntry). The same block appears in opl.cpp, autolaunch.cpp, lwnbdctl.cpp, appscan.cpp, menuitems.cpp (dupes lead, INFERRED same cause).
- Fix: Delete these include lines (and the NEWLIB_PORT_AWARE define that only serves fileXio_rpc.h), then build both toolchains to confirm nothing relied on them transitively.
- Benefit: ~25 source lines removed, faster compile of this unit; no ELF change (INFERRED: declarations only).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-293: BdmSource::needsUpdate stats CD/DVD and opens ul.cfg on the connect pass although the result is already 1

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/sources/bdmsupport.cpp:1844-1861`; `RiptOPL/src/sources/bdmsupport.cpp:1816-1825`; `RiptOPL/src/sources/bdmsupport.cpp:1625-1626`; `RiptOPL/include/bdmsupport.h:43-44`; `RiptOPL/src/sources/bdmdevices.cpp:413`; `RiptOPL/src/sources/bdmdevices.cpp:449-455`
- Problem: bdmUpdateDeviceData returns only 1, -1 or 0 (bdmdevices.cpp 413, 449, 455), and needsUpdate returns on 0 and -1 (1817-1823), so lines 1844-1861 run only with result == 1. The two stat() calls and sbIsSameSize (open/size/close of ul.cfg) cannot change the return value, and bdmModifiedCDPrev/DVDPrev are read nowhere else. update() then calls sbReadList, which reads the same folders and ul.cfg again.
- Fix: Delete lines 1844-1861, the two struct fields bdmModifiedCDPrev/bdmModifiedDVDPrev and their initialisation at 1625-1626; drop the then-unused struct stat st.
- Benefit: About 5 IOP RPCs (2 stat, open, lseek, close) less per device connect pass on the IO worker; ~22 lines removed (INFERRED from the cited ranges)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-294: ioprp.cpp: patch_CDVDFSV and patch_EESYNC duplicate patch_CDVDMAN; Align_offsets NULL checks never fire

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/platform/ioprp.cpp:23-27`; `RiptOPL/src/platform/ioprp.cpp:72-94`; `RiptOPL/src/platform/ioprp.cpp:99-118`
- Problem: patch_CDVDMAN(image, entry, src, size) is already the generic memcpy-plus-size; patch_CDVDFSV and patch_EESYNC are the same two lines with a fixed blob. Align_offsets is called once (line 60) with &offset_in and &offset_out, so its two NULL tests are always true. Upstream OPL code.
- Fix: Delete patch_CDVDFSV and patch_EESYNC and their prototypes and call patch_CDVDMAN with cdvdfsv_irx/size_cdvdfsv_irx and eesync_irx/size_eesync_irx; drop the NULL checks (or inline the two align steps into the loop).
- Benefit: ~22 lines removed; no ELF change, the static inline helpers already compile to the same code (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-295: ArtWorker::refused is never written; the HUD's R counter is dead and five one-field debug getters feed one HUD line

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/artworker.h:49-52`; `RiptOPL/src/gfx/artload.cpp:164-203`; `RiptOPL/include/texcache.h:191-206`; `RiptOPL/src/ui/guioverlay.cpp:136-148`
- Problem: gArtW.refused has no writer anywhere (only read at artload.cpp:181), so cacheDebugCounters always returns 0 for it; drawArtHud receives it in r and never prints it (format at guioverlay.cpp:211 has no R), while its comment still explains R. The remaining HUD values are fetched through five separate getters (cacheDebugTransientFail, KeyTooLong, Counters, Dropped, LastLoad) whose only caller is drawArtHud.
- Fix: Delete the refused field, the refused out-parameter of cacheDebugCounters and the R text in the HUD comment. Optionally merge the five getters into one that fills all counters (or let guioverlay read gArtW/gArtQ directly).
- Benefit: ~10 lines for the dead field/param, ~15 more if the getters merge; a few dozen bytes of .text and 4 bytes of .data (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-296: Atlas supports five pixel formats but the only shipped caller uses GS_PSM_T8

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/gfx/atlas.cpp:14-35`; `RiptOPL/src/gfx/atlas.cpp:144-146`; `RiptOPL/include/atlas.h:21-23`; `RiptOPL/include/atlas.h:32-35`
- Problem: Atlas::create takes a psm and copyData switches over CT32/CT24/CT16/CT16S/T8 through pixelSize, but the single product caller is fntsys.cpp:534 with GS_PSM_T8; the CT32 path is exercised only by tests/host/test_atlas.cpp:66.
- Fix: Drop the psm parameter and pixelSize, fix the atlas at GS_PSM_T8 with 1 byte per pixel, and remove the CT32 case from the host test.
- Benefit: ~20 lines; a small switch and branch out of glyph placement (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-297: CueSupport class has one stateless method, a global instance and a forwarder

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/include/cuesupport.h:220-235`; `RiptOPL/src/sources/cuesupport.cpp:33-35`; `RiptOPL/src/sources/cuesupport.cpp:912-913`; `RiptOPL/src/sources/cuesupport.cpp:956-959`
- Problem: CueSupport has no data members and a single method gameHasImage; its only caller is the free cueGameHasImage, which every backend calls. The comment at cuesupport.cpp:33-34 says the instance 'owns only the extension table', but the class owns nothing.
- Fix: Delete the class, gCueSupport and the forwarder; make the gameHasImage body the body of cueGameHasImage.
- Benefit: ~20 source lines removed (16 header lines, ~5 cpp lines) (INFERRED); no ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-298: Menu/ee_core constants declared twice: unused COMPAT_MODE copies in ee_core.h and patches.h; cheat limits and code_t also in cheatman.h

- Category: 2 (Duplication)
- Location(s): `RiptOPL/ee_core/include/ee_core.h:59-60`; `RiptOPL/ee_core/include/ee_core.h:63`; `RiptOPL/ee_core/include/ee_core.h:65-66`; `RiptOPL/ee_core/include/patches.h:14`; `RiptOPL/include/iosupport.h:97-104`; `RiptOPL/ee_core/include/cheat_engine.h:27-30`; `RiptOPL/ee_core/include/cheat_api.h:17-26`; `RiptOPL/ee_core/include/igrcheats.h:26`; `RiptOPL/include/cheatman.h:39-42`; `RiptOPL/include/cheatman.h:53-62`
- Problem: COMPAT_MODE_1/2/5/7/8 in ee_core.h are used by no ee_core file (the menu uses its own iosupport.h copy), and patches.h:14 repeats COMPAT_MODE_4 although its only includer (syshook.c) never uses it and patches.c gets it from ee_core.h. MAX_HOOKS/MAX_CODES/MAX_CHEATLIST/MAX_IMAGEWORDS and code_t, which define the cheat list the menu hands ee_core, exist in cheat_engine.h/cheat_api.h and again in cheatman.h, and igrcheats.h:26 hard-codes MAX_CODES a third time.
- Fix: Delete ee_core.h:59-60,63,65-66 and patches.h:14. Optionally move the four cheat limits and code_t into igrcheats.h (plain C, already included by cheatman.cpp and ee_core), use them for IGR_CHEAT_SET_CODES, delete both other copies and point .github/scripts/test_cheat_capacity.py at igrcheats.h.
- Justification: The optional move only relocates existing definitions to the one header both sides already include; it removes two copies of a cross-program size contract that must stay equal (cheatman.cpp fills MAX_CHEATLIST words that cheat_api.c:87 walks).
- Benefit: 6 lines removed for the COMPAT part; ~14 more with the optional cheat move. Macros only, no ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-299: Small redundant statements in supportbase.cpp (duplicate includes, impossible conditions, repeated assignments)

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/sources/supportbase.cpp:29-31`; `RiptOPL/src/sources/supportbase.cpp:105`; `RiptOPL/src/sources/supportbase.cpp:587-591`; `RiptOPL/src/sources/supportbase.cpp:602-603`; `RiptOPL/src/sources/supportbase.cpp:619`; `RiptOPL/src/sources/supportbase.cpp:677`; `RiptOPL/src/sources/supportbase.cpp:692-693`; `RiptOPL/src/sources/supportbase.cpp:797-800`; `RiptOPL/src/sources/supportbase.cpp:1061-1063`; `RiptOPL/src/sources/supportbase.cpp:1493`; `RiptOPL/src/sources/supportbase.cpp:1644-1659`; `RiptOPL/src/sources/supportbase.cpp:2702`
- Problem: cheatman.h and gui.h are included twice (19/29, 15/31). In isValidIsoName the else-if (size >= 5) is always true after the guard at 99. In sbReadList count is forced >= 0 at 590-591, so 602-603 and the count > 0 ? count : 0 at 619/677 are no-ops and count = 0 at 693 is a dead store. result = -1 is set twice (797, 800). fakemodule_flags takes three statements for one value. The else-if test at 1493 repeats the negation of 1486. sbGetCheatSearchLog returns a static array that cannot be NULL. vmcArgs->arg[slot][0] is already cleared at 2673 before 2702.
- Fix: Delete the second includes, the always-true and repeated tests, the dead store and the duplicate assignments; set fakemodule_flags in one statement; read cheatSearchLog directly in sbCheatsNotFoundText and delete sbGetCheatSearchLog.
- Benefit: ~20 lines removed (READ, counted from the cited lines); no ELF change (the compiler already folds these).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-300: Stale comments describing the removed built-in <Coverflow> theme, the old family count and a stranded thmLoad contract

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/gfx/themes.cpp:180-181`; `RiptOPL/src/gfx/themes.cpp:986-987`; `RiptOPL/src/gfx/themes.cpp:1180-1183`; `RiptOPL/src/gfx/themes.cpp:1530-1540`; `RiptOPL/src/gfx/themes.cpp:1805-1808`; `RiptOPL/src/gfx/themes.cpp:2045-2056`; `RiptOPL/src/gfx/themes.cpp:2087-2089`; `RiptOPL/include/uidraw.h:10`; `RiptOPL/include/themecatalog.h:6-7`
- Problem: <Coverflow> is no longer a theme (rebuildNames 1886-1897 lists only <OPL> and disk themes; it is a view), yet 1530-1540, 1805-1808, 2045-2056 ('id count_ + 1') and 2087-2089 ('the flows above count_') describe it as a separate built-in theme. 180-181 says 8 families while the code walks 12; 986 says four element lists (twelve). 1180-1183 documents thmLoad's return value but sits above the glyph-set block, 170 lines before thmLoad. uidraw.h:10 says 'linked list' for array arguments; themecatalog.h:6 names src/themes.cpp.
- Fix: Delete the history in these comments and keep one line each where a rule still applies (built-in view cfgs must not set colour keys; only disk themes have paths); move the return-value sentence above thmLoad.
- Benefit: ~20 source lines of comment removed; no binary change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-301: Thirteen SMB2-only diagLog breadcrumbs wrapped around SMB init steps

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/sources/ethsupport.cpp:130-136`; `RiptOPL/src/sources/ethsupport.cpp:150-154`; `RiptOPL/src/sources/ethsupport.cpp:162-168`; `RiptOPL/src/sources/ethsupport.cpp:368-372`; `RiptOPL/src/sources/ethsupport.cpp:524-528`; `RiptOPL/src/sources/ethsupport.cpp:563-565`; `RiptOPL/src/sources/ethsupport.cpp:583-584`
- Problem: Every SMB2 init step is bracketed by `if (gSMBDialect == SMB_DIALECT_V2) diagLog("SMB2 init: X enter")` and a matching 'returned %d' line, in every build (diagLog is always compiled, include/diaglog.h:13). The enter/return pairs look like leftover bring-up tracing for the smb2man work; the outcome already ends in gNetworkStartup and the LOG lines.
- Fix: Keep the two smb2LogIopFree calls and one line with the final LOGON/ECHO/OPENSHARE result; delete the per-step enter/return pairs and their gSMBDialect guards.
- Benefit: ~20 lines and roughly 400 bytes of .rodata format strings removed (INFERRED: ~10 strings of ~40 bytes).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-302: elfldr child LAUNCH_DIAG colour markers are permanently compiled out (LAUNCH_DIAG hard-coded 0)

- Category: 1 (Dead code)
- Location(s): `RiptOPL/elfldr/loader.c:140-155`; `RiptOPL/elfldr/loader.c:396`; `RiptOPL/elfldr/loader.c:455`; `RiptOPL/elfldr/loader.c:469`
- Problem: include/launchdiag.h:41 defines LAUNCH_DIAG 0 and no build passes another value (it is a #define, not a -D), so LAUNCHDIAG_CHILD_PAINT is always ((void)0). The block exists for a past UDPBD launch-hang triage build; .github/scripts/test_handoff_probe.py:78-83 only checks that it stays gated.
- Fix: Delete the child marker block and the three LAUNCHDIAG_CHILD_PAINT calls (and the test_handoff_probe.py check for them); the wider launchdiag feature in include/launchdiag.h and src/platform/launchdiag.cpp is the same dormant switch and belongs to the units that own those files.
- Benefit: 19 source lines in loader.c; no ELF change since it compiles to nothing (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-303: lwnbdctl.cpp small clutter: duplicated bounded pad restart, stray banners, pointless temp copy, stale notes

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/app/lwnbdctl.cpp:192-203`; `RiptOPL/src/app/lwnbdctl.cpp:225-239`; `RiptOPL/src/app/lwnbdctl.cpp:72-75`; `RiptOPL/src/app/lwnbdctl.cpp:275-277`; `RiptOPL/src/app/lwnbdctl.cpp:255-262`; `RiptOPL/src/app/lwnbdctl.cpp:171-172`; `RiptOPL/src/app/lwnbdctl.cpp:181`
- Problem: padInit(0) plus the 100-try startPads/delay(50) loop and its LOG are written twice (load and unload paths). The file ends with an 'Init/Deinit' banner (275-277) with nothing after it and opens its only code with an empty banner (72-75). handleLwnbdSrv copies a translated string into temp[256] with "%s" just to pass it to guiMsgBox. The NOTE(rebuild) about the fork's 3-arg deinitAllSupport and the '/* gHDDStartMode ? */' remark describe nothing actionable.
- Fix: Add one static restartPads(const char *who) in lwnbdctl.cpp for the padInit+bounded loop and call it twice; delete both banners, the temp buffer (pass _l(_STR_RUNNINGNBD) directly) and the two stale notes.
- Justification: The helper replaces two copies of the same 9-line sequence in one file, so it removes code overall.
- Benefit: ~18 lines removed; 256 B of stack in handleLwnbdSrv (INFERRED: char temp[256]).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-304: ra_overlay.c keeps its own copy of struct GSMSourceGSRegs from igs_api.h

- Category: 2 (Duplication)
- Location(s): `RiptOPL/ee_core/src/ra_overlay.c:41-60`; `RiptOPL/ee_core/include/igs_api.h:126-142`
- Problem: ra_overlay.c redeclares the GSM register record as struct ra_gs_source_regs with a 'keep in sync' comment, although igs_api.h already declares struct GSMSourceGSRegs and the extern for the same gsm_engine.S symbol.
- Fix: Include igs_api.h (or declare the extern from it) in ra_overlay.c and delete the private struct and comment.
- Benefit: ~18 lines removed and one less layout to keep in sync; no ELF change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-305: Debug-only bdmTypedRootStem duplicates bdmTypedPathToken and probes a 'cf' alias that does not exist

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/bdmsupport.cpp:348-364`; `RiptOPL/include/bdmtyped.h:17-33`
- Problem: bdmTypedRootStem (inside #ifdef __DEBUG) is the same switch as bdmTypedPathToken plus a BDM_TYPE_PCCF -> "cf" case, while bdmtyped.h:17-18 states PC CARD CF never got a typed path, so the extra case only makes the diagnostic open cf0:..cf7: for nothing.
- Fix: Delete bdmTypedRootStem and call bdmTypedPathToken at bdmsupport.cpp:371.
- Benefit: 17 source lines removed; debug builds only (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-306: themeint.h: BG_REQUEST_EXTRA_IDLE_FRAMES is unused and sits under an orphaned cover-cache comment

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/themeint.h:11-27`
- Problem: BG_REQUEST_EXTRA_IDLE_FRAMES has no reader anywhere in the repo. The 12-line comment above it (11-22) describes the cover cache depth and warming (and cites texcache.c), which COVER_CACHE_SLOTS at lines 74-76 already documents; it is left over from a constant that moved.
- Fix: Delete lines 11-27 (the stale cache comment, the background-delay comment and the unused constant).
- Benefit: 17 lines removed; no ELF change (constexpr int).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-307: Commented-out USBLD size calculation and TODO in sbReadList, superseded by sbPopulateConfigIn

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/sources/supportbase.cpp:650-665`
- Problem: A 12-line block-commented loop computing USBLD part sizes, with a TODO calling it slow. The same per-part size is now computed on demand in sbPopulateConfigIn (1470-1479), behind the size-stat gate.
- Fix: Delete lines 650-665.
- Benefit: 16 lines removed (READ); no ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-308: The GSMSourceGSRegs layout is declared four separate ways (array + SRC_ indexes, two identical structs, asm offsets)

- Category: 2 (Duplication)
- Location(s): `RiptOPL/ee_core/src/igrmenu.c:547-557`; `RiptOPL/ee_core/src/igrovl_stub.c:22-23`; `RiptOPL/ee_core/include/igs_api.h:119-142`; `RiptOPL/ee_core/src/ra_overlay.c:41-60`
- Problem: igrmenu.c and igrovl_stub.c declare 'extern u64 GSMSourceGSRegs[11]' with private SRC_* indexes, igs_api.h declares struct GSMSourceGSRegs, and ra_overlay.c declares a field-for-field copy, struct ra_gs_source_regs, with a 'keep this in sync' comment; all mirror the Source_* offsets in gsm_defines.h. Four C views of one asm object must be kept in step by hand.
- Fix: Declare struct GSMSourceGSRegs / GSMSourceSetGsCrt and their externs once in ee_core/include/gsm_api.h (already included by padhook.c and gsm_api.c), include it from igs_api.h, ra_overlay.c, igrovl_stub.c and igrmenu.c, and delete ra_gs_source_regs, the local externs and the SRC_* indexes (the host test then uses the struct too).
- Benefit: ~16 source lines (ra_overlay.c 14-line struct, 2 externs in igrovl_stub.c, igrmenu.c externs and SRC_ defines replaced by field names) and one place to keep in sync with gsm_defines.h; no ELF change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-309: cheat_api.c: two impossible bounds checks in LinkImage and single-use inline wrappers around SetSyscall

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/ee_core/src/cheat_api.c:50-51`; `RiptOPL/ee_core/src/cheat_api.c:55-56`; `RiptOPL/ee_core/src/cheat_api.c:122-137`
- Problem: In LinkImage, p is gImage+1 at line 50 and gImage+2 at line 55 while pEnd is gImage+MAX_IMAGEWORDS (1024, cheat_engine.h:30), so both checks are always false; the checks at 59, 62 and 66 are the ones that bound the walk. Install_HookSetupThread and Remove_HookSetupThread are one-caller inline wrappers of one or two statements each, with banner comments.
- Fix: Delete the checks at lines 50-51 and 55-56; put the wrapper bodies directly into EnableCheats and DisableCheats and delete the wrappers.
- Benefit: ~16 lines; INFERRED 2 compare+branch pairs (~16 bytes) of ram84 text; wrappers are inlined already, no change
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-310: util.cpp leftovers: unused max(), unused htab table, commented-out LOG lines, redundant else

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/platform/util.cpp:703-707`; `RiptOPL/include/util.h:46`; `RiptOPL/src/platform/util.cpp:728`; `RiptOPL/src/platform/util.cpp:560-561`; `RiptOPL/src/platform/util.cpp:569`; `RiptOPL/src/platform/util.cpp:578`; `RiptOPL/src/platform/util.cpp:592`; `RiptOPL/src/platform/util.cpp:596`; `RiptOPL/src/platform/util.cpp:617`; `RiptOPL/src/platform/util.cpp:906-907`
- Problem: max() has no caller anywhere in the C/C++ sources (min() does). static const htab[] is never referenced. readFileBuffer carries seven commented-out upstream debug LOG lines. In sysDeleteFolder the else branch sets result = 0, which it already is.
- Fix: Delete max() and its prototype, htab, the commented-out LOG lines and the else branch.
- Benefit: ~16 lines removed; max() is already dropped by -ffunction-sections + --gc-sections and htab is static, so no ELF change (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-311: Checks for states that cannot occur in cacheLoadImage, the timed drains, Atlas::create, ArtIndex and drawCoverFlow

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/gfx/artload.cpp:515-517`; `RiptOPL/src/gfx/artload.cpp:542-551`; `RiptOPL/src/gfx/artload.cpp:450-451`; `RiptOPL/src/gfx/artload.cpp:503-504`; `RiptOPL/src/gfx/artload.cpp:897-898`; `RiptOPL/src/gfx/atlas.cpp:73-74`; `RiptOPL/src/gfx/artindex.cpp:335`; `RiptOPL/src/gfx/artindex.cpp:381`; `RiptOPL/src/gfx/coverflow.cpp:304`
- Problem: cacheLoadImage tests !req, !entry, !cache and !list, but its only caller skips NULL (artload.cpp:788) and the only enqueue (texcache.cpp:440-447) fills them from values already checked non-NULL (texcache.cpp:229, 422). The negative-waitTicks 'wait forever' branch has no caller (all pass 0, 500 or 2000). Placement new on non-NULL memory cannot yield NULL (atlas.cpp:73). slotFor/build never return NULL (artindex.cpp:149-301). coverflow's leavingIndex bounds test is implied by i == leavingIndex with i already in range. cacheEnd re-derives the ms value it computed at line 877.
- Fix: Delete these checks; make the two wait loops plain counted loops; log the saved budget value in cacheEnd.
- Benefit: ~15 lines; a few instructions per art load (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-312: GameIdBarcode: public header and class for a single file-local use

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/include/guigameid.h`; `RiptOPL/src/ui/guigameid.cpp:88-89`; `RiptOPL/src/ui/guigameid.cpp:96-98`; `RiptOPL/src/ui/guigameid.cpp:205-212`
- Problem: include/guigameid.h is included only by guigameid.cpp; the class is constructed once in guiShowGameID (210-211) and draw() is called only from show(). The serial is also capped at 11 characters twice (cleanSerial 88-89 and buildPacket 96-98).
- Fix: Delete include/guigameid.h and turn the class into static functions (or a file-local struct) inside guigameid.cpp; drop the second 11-character cap in buildPacket.
- Benefit: ~15 source lines net (INFERRED: 27-line header removed, ~12 lines of declarations kept in the .cpp); no ELF change expected (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-313: HttpSupport::clearCatalog duplicates setCatalog(NULL, NULL, 0); httpParseCatalog has an if/else with identical branches

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/httpsupport.cpp:385-396`; `RiptOPL/src/sources/httpsupport.cpp:182-185`
- Problem: clearCatalog frees games_/extras_ (with NULL checks free() does not need), NULLs them and zeroes the count, which is exactly setCatalog(NULL, NULL, 0); its only caller is HttpSource::cleanUp (884). In httpParseCatalog both branches of 'if (nl < end) *nl = 0; else *nl = 0;' do the same store.
- Fix: Delete clearCatalog (and its declaration, httpsupport.h:50) and call setCatalog(NULL, NULL, 0) in cleanUp; replace the if/else with the single store and its comment.
- Benefit: ~15 source lines removed; no measurable ELF change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-314: Small dead code in retrogem: always-true alnum recheck, impossible gidlen check, header-only export, five unused includes

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/sources/retrogem.cpp:97-99`; `RiptOPL/src/sources/retrogem.cpp:58-61`; `RiptOPL/src/sources/retrogem.cpp:628-629`; `RiptOPL/include/retrogem.h:15-17`; `RiptOPL/src/sources/retrogem.cpp:8`; `RiptOPL/src/sources/retrogem.cpp:11-12`; `RiptOPL/src/sources/retrogem.cpp:15`; `RiptOPL/src/sources/retrogem.cpp:17`
- Problem: Lines 97-99 recheck isalnum on cleaned[0..3], which line 78 already guarantees; retrogemTitlePrefixChar (58-61) is just isalnum and is otherwise used only by the legacy parser (finding 5). gidlen <= 0 (628-629) cannot happen after the gameID[0] != 0 test at 622. retrogemNormalizeGameID is declared in retrogem.h but called only inside retrogem.cpp (and by a test that compiles the source text). stdlib.h, fcntl.h, unistd.h, gui.h and vcdsupport.h provide nothing this file uses.
- Fix: Delete lines 97-99 and 628-629, replace retrogemTitlePrefixChar with isalnum (or delete it with finding 5), make retrogemNormalizeGameID static and drop its prototype, and remove the five includes.
- Benefit: ~15 source lines removed; no meaningful ELF change (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-315: Small duplications inside artindex.cpp: picCmp equals hashCmp, repeated free triples, repeated reject preamble

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/gfx/artindex.cpp:81-89`; `RiptOPL/src/gfx/artindex.cpp:104-112`; `RiptOPL/src/gfx/artindex.cpp:255-273`; `RiptOPL/src/gfx/artindex.cpp:309-330`; `RiptOPL/src/gfx/artindex.cpp:368-376`
- Problem: picCmp and hashCmp are the same unsigned compare (Pic.hash is Pic's first member, so hashCmp already works on a Pic array). The readError and overflowed exits both free the same three buffers and return. mayExist and findPicture repeat the backslash/smb/udpfs reject, the owner-thread check and splitPath.
- Fix: Delete picCmp and sort pics with hashCmp; merge the two failure exits; move the shared reject/owner/split preamble into one private helper that both call.
- Benefit: ~15 lines; tens of bytes of .text (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-316: Stale and displaced comments in supportbase.cpp

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/sources/supportbase.cpp:45-46`; `RiptOPL/src/sources/supportbase.cpp:1498-1503`; `RiptOPL/src/sources/supportbase.cpp:1606-1625`; `RiptOPL/src/sources/supportbase.cpp:1755-1758`; `RiptOPL/src/sources/supportbase.cpp:1948-1955`; `RiptOPL/src/sources/supportbase.cpp:2548-2550`
- Problem: 45-46 is a history note on a forward declaration that is only needed because sbFileExists is defined at 2111. 1498-1503 narrates the old single-caller state (hddsupport.c). The sbCheatsMissingContinue doc block (1606-1624) sits above CHT_TAR_MEMBER_MAX and the search-log code, 40 lines from its function, and its NOTE(rebuild) says the search log is still to come though 1627-1665 already append it. 1757 names src/tar.c (the file is src/platform/tar.cpp). The sbNeutrinoInstallComplete comment (1948-1954) sits above sbLastPathSeparator. 2548-2550 says the resolver probes mc0/mc1 for the bdm + mmce paths; it now runs custom -> game device -> mc for five backends.
- Fix: Move sbFileExists above its first use and delete the forward declaration and its note; delete the NOTE(rebuild) paragraph and the historical narration; move the two displaced comments onto their functions; correct the tar file name and the resolver description in a line each.
- Benefit: ~15 lines removed (READ: 2 + 4 + 3 + ~6 of narration); comments then match the code.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-317: lang.cpp builds the custom font path twice through two heap buffers and repeats the ttf/otf attempt

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/core/lang.cpp:55-83`; `RiptOPL/src/core/lang.cpp:208-216`
- Problem: lngLoadFromFile mallocs a copy of the directory part of the .lng path only to pass it to lngLoadFont (its only caller), which mallocs again to compose dir + font_<name>.ttf, then repeats the same snprintf/LOG/fntLoadDefault block for .otf. lngInitFallbackFont (424-441) already does the same kind of try-list with a loop and a stack buffer.
- Fix: Pass the .lng path and its directory length to lngLoadFont, build the name with %.*s into one buffer, and loop over the two extensions; delete the dir malloc/copy in lngLoadFromFile.
- Benefit: ~15 source lines and one malloc/free pair per language switch removed (INFERRED from the cited ranges)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-318: Case-insensitive suffix test implemented three times in the main ELF

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/retrogem.cpp:409-419`; `RiptOPL/src/ui/guifilemgr.cpp:74-78`; `RiptOPL/src/platform/httpcatalog.cpp:118-128`
- Problem: retrogemHasExt, fmHasSuffix and httpEndsWithNoCase all test whether a string ends with a suffix ignoring case (httpEndsWithNoCase hand-rolls tolower and requires a strictly longer string). All three are compiled into the main riptopl ELF.
- Fix: Keep one (the 4-line fmHasSuffix form) in an existing shared util source and use it in the other two files; keep httpcatalog's strict-length rule at its call sites only if it matters.
- Justification: A shared helper is allowed here because the same logic exists in three places and one 4-line copy replaces about 25 lines.
- Benefit: ~15 net source lines removed; tiny .text saving (INFERRED)
- Effort: S. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-319: Copied include blocks and comments in the bdm split files name functions those files do not use; one comment contradicts bdmdevices.cpp

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/sources/bdmboot.cpp:10-36`; `RiptOPL/src/sources/bdmdevices.cpp:9-36`; `RiptOPL/src/sources/bdmlaunch.cpp:9-39`; `RiptOPL/src/sources/bdmlaunch.cpp:652-654`; `RiptOPL/src/sources/bdmdevices.cpp:139-142`
- Problem: The three files carry the include list of bdmsupport.cpp with comments for ethGetModulesLoaded, udpfsGetModulesLoaded, libViewActive and mkdir (and mmceSendGameID in bdmboot/bdmdevices), none of which these files call. bdmlaunch.cpp:652-654 says bdmEnumerateDevices now waits for MX4SIO, while bdmdevices.cpp:139-142 says that synchronous wait was removed.
- Fix: Trim each include list to what the file uses (confirm with a compile) and delete the false comments; correct or delete bdmlaunch.cpp:652-654.
- Benefit: ~15 lines removed; build time only, no ELF change (INFERRED)
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-320: GSM engine: dead instructions, unused GSMAdapts fields, unused labels and a duplicate include

- Category: 1 (Dead code)
- Location(s): `RiptOPL/ee_core/src/gsm_engine.S:1055`; `RiptOPL/ee_core/src/gsm_engine.S:1069`; `RiptOPL/ee_core/src/gsm_engine.S:1295-1296`; `RiptOPL/ee_core/src/gsm_engine.S:1299-1300`; `RiptOPL/ee_core/src/gsm_engine.S:676`; `RiptOPL/ee_core/src/gsm_engine.S:695`; `RiptOPL/ee_core/src/gsm_engine.S:69`; `RiptOPL/ee_core/include/gsm_defines.h:89-92`; `RiptOPL/ee_core/src/gsm_engine.S:1119`; `RiptOPL/ee_core/src/gsm_engine.S:1257`; `RiptOPL/ee_core/src/gsm_engine_adv.S:14-15`
- Problem: 'la $t1, op_t' (1055, 1069) is overwritten by every BD handler before any read; 'mfbpc $a3' (1295) is never read; Enable_GSBreakpoint reloads TRACK_ONLY into $t0 (1299-1300) although $t0 still holds it from 1275; Adapted_DISPLAY1/2 are stored (676, 695) but nothing reads them and SMODE2_adaptation is never used, so GSMAdapts only needs Interlace_FRAME_Mode_Flag; labels B_likely and end_GSHandler have no reference; gsm_engine_adv.S repeats the ABI_EABI64 define and include its only includer (gsm_engine.S:14-15, :1260) already did.
- Fix: Delete the two 'la $t1, op_t', the mfbpc and its sync.p, the second la/lbu of TRACK_ONLY, the two Adapted_DISPLAY stores, the two unused labels and gsm_engine_adv.S:14-15; shrink GSMAdapts to the one flag byte and drop the unused .equ lines in gsm_defines.h.
- Benefit: 11 instructions = 44 bytes of eecore .text and 16 bytes of .data (GSMAdapts 18 B padded to 32 by .p2align 4, 1 B fits in 16) (INFERRED); two fewer stores on every trapped DISPLAY write; ~15 lines.
- Effort: S. Risk: Med. Confidence: Med (READ)

#### RIPTOPL-321: Include blocks copied on file splits keep headers the file no longer uses (artload.cpp, texcache.cpp, coverflow.cpp)

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/gfx/artload.cpp:16-19`; `RiptOPL/src/gfx/texcache.cpp:13-16`; `RiptOPL/src/gfx/coverflow.cpp:11-21`
- Problem: artload.cpp:2-21 and texcache.cpp:3-22 are the same include list. In artload.cpp bdmsupport.h, appsupport.h, favsupport.h and hddsupport.h are unused: the symbols their own comments name (bdmModeIsSIO2, appGetArtMode, favGetArtMode, hddGetArtArchivePath) are used only in texcache.cpp:154-205. In texcache.cpp tar.h, pad.h and sound.h are unused. coverflow.cpp copied themes.cpp's list and uses nothing from fntsys.h, lang.h, pad.h, sound.h, favsupport.h, vcdsupport.h, libview.h or rabadge.h.
- Fix: Delete the unused #include lines (and their stale 'used for X' comments).
- Benefit: ~15 lines; no ELF change, slightly faster compiles and clearer dependencies (INFERRED)
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-322: bdmboot.cpp repeats the identity poll loop and the massN:+tail path composition

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/bdmboot.cpp:180-189`; `RiptOPL/src/sources/bdmboot.cpp:216-225`; `RiptOPL/src/sources/bdmboot.cpp:158-161`; `RiptOPL/src/sources/bdmboot.cpp:49-54`; `RiptOPL/src/sources/bdmboot.cpp:70-73`; `RiptOPL/src/sources/bdmboot.cpp:120-123`
- Problem: resolveLiteralMassSlot has the same 10-line 'poll readLiteralMassIdentity until budget' loop twice (known-type branch and tier loop) plus its first iteration once more at 158-161. The 'mass%d:' + tail-with-or-without-leading-slash snprintf is written out in three functions.
- Fix: Fold the known-type case into the tier loop (one tier with that type) so one poll loop remains, and use one static path-compose helper for the three snprintf sites.
- Benefit: ~15 lines removed
- Effort: S. Risk: Med. Confidence: Med (READ)

#### RIPTOPL-323: AppSource::shutdown is a copy of cleanUp; config() repeats its media/format/size tail in both branches

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/appsupport.cpp:1431-1443`; `RiptOPL/src/sources/appsupport.cpp:1417-1429`; `RiptOPL/src/sources/appsupport.cpp:1335-1342`; `RiptOPL/src/sources/appsupport.cpp:1357-1364`
- Problem: shutdown() has the same body as cleanUp() except the log text. In config() the legacy and scanned branches end with the same MEDIA/FORMAT/SIZE block that differs only in the path passed to appGetELFSize.
- Fix: Make shutdown() call cleanUp(itemList, 0) as HttpSource::shutdown already does (httpsupport.cpp:887-889). In config(), keep a size-path pointer per branch and set MEDIA/FORMAT/SIZE once after the if/else.
- Benefit: ~14 lines removed
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-324: EthSupport exposes its private fields through 12 reference accessors

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/include/ethsupport.h:75-88`; `RiptOPL/include/ethsupport.h:120-131`
- Problem: prefix(), base(), setBase(), ulSizePrev(), modifiedCDPrev(), modifiedDVDPrev(), gameCount(), games(), ps1GameCount(), ps1Games(), ps1ScanFailed(), ps1ScanRetries() return mutable references (or raw pointers) to private members, and EthSource assigns through them (e.g. ethsupport.cpp:729-735, 815-818). The fields are public in all but syntax.
- Fix: Make those members public (drop the trailing underscore or keep it) and delete the 12 accessors, or make EthSource a friend; either removes a layer that encapsulates nothing.
- Benefit: ~14 header lines removed (INFERRED); call sites change from gameCount() to gameCount_.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-325: Five copies of the 'mode slot -> bdmDeviceType == X' check

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/bdmsupport.cpp:859-876`; `RiptOPL/src/sources/bdmsupport.cpp:1157-1164`; `RiptOPL/src/sources/bdmsupport.cpp:1229-1243`
- Problem: bdmSupportIsUDPBD, bdmModeIsUDPBD, bdmModeIsSIO2, bdmModeIsPCCF and bdmModeIsUSB each repeat the BDM_MODE..BDM_MODE_LAST range check, the priv NULL check and a compare of bdmDeviceType against one constant.
- Fix: Add one static bdmModeDeviceType(mode) returning the slot's bdmDeviceType or BDM_TYPE_UNKNOWN, and make each public predicate a one-line return of a compare against it.
- Justification: One 6-line static helper replaces five 6-8 line bodies of the same checks, so it removes code overall.
- Benefit: ~14 source lines removed (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-326: GSM_1080P is always defined, yet guigame.cpp keeps #ifdef/#else guards for a build without it

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/ui/guigame.cpp:924-928`; `RiptOPL/src/ui/guigame.cpp:975-980`; `RiptOPL/src/ui/guigame.cpp:1584`; `RiptOPL/src/ui/guigame.cpp:1620`; `RiptOPL/src/ui/guigame.cpp:1634-1636`; `RiptOPL/src/ui/guigame.cpp:1894-1896`; `RiptOPL/src/ui/guigame.cpp:2262-2266`; `RiptOPL/ps2.yaml:609`; `RiptOPL/ps2.yaml:644`
- Problem: Both eecore and the main ELF get GSM_1080P from the base defines (ps2.yaml:609, 644), no edition or configuration script removes it, and test_ps2build_configurations.py:61 asserts it is present. The #else branch at 926-927 (28-mode list) and the seven #ifdef/#endif pairs are for a build that cannot be produced. gsm.cpp:142 and ee_core gsm_engine.S:182 / gsm_engine_adv.S:140 carry the same guard.
- Fix: Drop the GSM_1080P #ifdef/#else/#endif lines in guigame.cpp (keep the 1080p code and GSM_VMODE_LAST_REAL_IDX 30); do the same in gsm.cpp and the two .S files.
- Benefit: ~14 preprocessor lines removed in guigame.cpp (counted from the cited ranges); no ELF change (INFERRED: the define is always on)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-327: RA hash workers repeat the log open/close pair on every exit; the two deferred entry points repeat the queue tail

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/supportbase.cpp:2944-2945`; `RiptOPL/src/sources/supportbase.cpp:2953-2954`; `RiptOPL/src/sources/supportbase.cpp:2984-2985`; `RiptOPL/src/sources/supportbase.cpp:3022-3023`; `RiptOPL/src/sources/supportbase.cpp:3045-3046`; `RiptOPL/src/sources/supportbase.cpp:3104-3105`; `RiptOPL/src/sources/supportbase.cpp:3116-3117`; `RiptOPL/src/sources/supportbase.cpp:3090-3094`; `RiptOPL/src/sources/supportbase.cpp:3132-3136`; `RiptOPL/src/sources/supportbase.cpp:3139-3146`
- Problem: sbHashGame and sbHashHdlGame both start with raHashLogOpen(path); raHashSetStepLog(&raHashStep) and end every return path with raHashSetStepLog(NULL); raHashLogClose() (5 copies). Both run only from sbHashGameDeferredWorker. sbHashGameDeferred and sbHashHdlDeferred end with the same 5-line ioPutRequest/busy-reset tail.
- Fix: Open the log and set the step hook once in sbHashGameDeferredWorker before dispatch and clear/close once after; drop the pairs from the two workers. Share the queue tail in one small static function.
- Benefit: ~14 lines removed (READ: 7 pairs = 14 lines, plus ~4 from the shared tail, minus ~4 added).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-328: Small dead declarations in ee_core headers (Remove_Padhook prototype, DEV9 register macros, unused glyph/struct/asm constants)

- Category: 1 (Dead code)
- Location(s): `RiptOPL/ee_core/include/padhook.h:25`; `RiptOPL/ee_core/include/padhook.h:29-35`; `RiptOPL/ee_core/include/padhook.h:84`; `RiptOPL/ee_core/include/igrmfont.h:17`; `RiptOPL/ee_core/include/igrovl.h:28`; `RiptOPL/ee_core/include/coreconfig.h:12`; `RiptOPL/ee_core/include/gsm_defines.h:30`; `RiptOPL/ee_core/include/gsm_defines.h:92`
- Problem: Remove_Padhook is prototyped but defined nowhere. DEV9_R_1460..1474 are used by no ee_core file (cdvdman/dev9.c uses the SDK's own DEV9_R_* names). IGR_LIBPAD_NONE, G_POINTER, the forward declaration struct igr_cheats (igr_cheats_t is an anonymous struct), the .equ GS_BGCOLOUR and SMODE2_adaptation are never referenced; coreconfig.h:12 is a commented-out include.
- Fix: Delete those lines.
- Benefit: ~14 lines removed; declarations only, no ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-329: Small hand-copied loops in vcdsupport.cpp: id character-class checks, last-separator search, write-all loop

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/vcdsupport.cpp:79-83`; `RiptOPL/src/sources/vcdsupport.cpp:136-144`; `RiptOPL/src/sources/vcdsupport.cpp:380-388`; `RiptOPL/src/sources/vcdsupport.cpp:397-403`; `RiptOPL/src/sources/vcdsupport.cpp:784-786`; `RiptOPL/src/sources/vcdsupport.cpp:1495-1500`; `RiptOPL/src/sources/vcdsupport.cpp:1606-1610`; `RiptOPL/src/sources/vcdsupport.cpp:1155-1167`; `RiptOPL/src/sources/vcdsupport.cpp:1192-1201`
- Problem: The '4 alphanumerics' id check is written four times (vcdExtractGameId, both forms of vcdCanonDisplayId, vcdPopsPartitionTitleOffset form B) and the 'AAAA-NNNNN' digit run twice; 'last of / or \\' is computed three times (vcdDirSep, the custom-ELF folder in equipBdma, the source folder before icon stamping); vcdSafeCopyFile and vcdSafeWriteFile each carry the same write-until-done loop.
- Fix: Add one static id-prefix check (4 alnum) and one dash-form check used by the three parsers, one static last-separator function that vcdDirSep and both equipBdma sites use, and let vcdSafeCopyFile call a shared write-all loop that vcdSafeWriteFile also uses.
- Justification: Each helper replaces 2-4 identical copies inside this one file and removes more lines than it adds (about 14 net); behaviour is unchanged.
- Benefit: ~14 net source lines removed (INFERRED from the cited ranges); negligible ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-330: Stale file names, a misplaced comment, commented-out code and a history note in bdmsupport/cuesupport

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/include/bdmsupport.h:221`; `RiptOPL/src/sources/bdmsupport.cpp:507-508`; `RiptOPL/src/sources/bdmsupport.cpp:986-991`; `RiptOPL/src/sources/bdmsupport.cpp:1149`; `RiptOPL/src/sources/bdmsupport.cpp:1521`; `RiptOPL/src/sources/bdmsupport.cpp:2156-2157`; `RiptOPL/src/sources/bdmsupport.cpp:2331-2340`; `RiptOPL/include/cuesupport.h:62`; `RiptOPL/include/cuesupport.h:72`
- Problem: Comments cite src/pad.c, bdmsupport.c ~1219, opl.c, system.c and vcdsupport.c, none of which exist (the sources are now pad.cpp, opl.cpp, system.cpp, vcdsupport.cpp). The comment at 986-991 documents bdmForceDeviceRefresh but sits above bdmRequestPccfProbe. 2156-2157 is commented-out code, and 2331-2340 narrates a bug in a previous version of bdmFindPartition.
- Fix: Delete the commented-out code and the history note, move the 986-991 comment above bdmForceDeviceRefresh (1001), and correct or drop the stale file references.
- Benefit: ~14 comment lines removed (INFERRED); no binary change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-331: Unused macros in riptlite.h: LITE_DEVICE_NAME (8 definitions), LITE_HAS_TCPIP, and LITE_HAS_NBD which only feeds LITE_HAS_TCPIP

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/riptlite.h:23`; `RiptOPL/include/riptlite.h:29`; `RiptOPL/include/riptlite.h:35`; `RiptOPL/include/riptlite.h:44`; `RiptOPL/include/riptlite.h:52`; `RiptOPL/include/riptlite.h:58`; `RiptOPL/include/riptlite.h:65`; `RiptOPL/include/riptlite.h:71`; `RiptOPL/include/riptlite.h:87-89`; `RiptOPL/include/riptlite.h:106`; `RiptOPL/include/riptlite.h:112-113`
- Problem: LITE_DEVICE_NAME is defined once per device and never read; LITE_HAS_TCPIP is never tested; LITE_HAS_NBD appears only inside the LITE_HAS_TCPIP definition. Nothing in src, include, tools or .github/scripts names any of them outside riptlite.h.
- Fix: Delete the eight LITE_DEVICE_NAME lines, LITE_HAS_TCPIP with its comment, and both LITE_HAS_NBD definitions with their comment.
- Benefit: ~14 header lines; no ELF change (preprocessor only, INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-332: buildVcdGameList grows its two parallel arrays with three copies of a paired realloc block

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/hddsupport.cpp:1568-1579`; `RiptOPL/src/sources/hddsupport.cpp:1633-1646`; `RiptOPL/src/sources/hddsupport.cpp:1715-1730`
- Problem: Each phase (PP one-game rows, pooled __.POPS VCDs, __.EMBER folders) repeats the same realloc(newGames)/check/assign, realloc(newParts)/check/assign sequence with scanIncomplete=1; break, differing only in the element count and which scratch buffers it frees.
- Fix: Add a static helper that grows both arrays to a given count and returns 0/1; each site becomes a one-line call plus its own frees and break.
- Justification: The helper (~12 lines) replaces 42 lines of copies with ~15 lines of calls, a net reduction.
- Benefit: ~14 net lines removed (INFERRED from the cited ranges)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-333: ee_core/debug_colors_doc.sh: doc generator that nothing runs and whose output is not in the repo

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/ee_core/debug_colors_doc.sh:1-14`
- Problem: The script greps DBGCOL/DBGCOL_BLNK calls into a Markdown colour table, but no workflow, tool, doc or Makefile mentions it, and its output heading appears nowhere else in the repo.
- Fix: Delete the file.
- Benefit: 14 lines removed; not shipped.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-334: httpLastError is written in nine places but never read by shipped code

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/sources/httpsupport.cpp:49-52`; `RiptOPL/src/sources/httpsupport.cpp:211`; `RiptOPL/src/sources/httpsupport.cpp:262`; `RiptOPL/src/sources/httpsupport.cpp:268`; `RiptOPL/src/sources/httpsupport.cpp:274`; `RiptOPL/src/sources/httpsupport.cpp:279`; `RiptOPL/src/sources/httpsupport.cpp:302`; `RiptOPL/src/sources/httpsupport.cpp:326`; `RiptOPL/src/sources/httpsupport.cpp:347`; `RiptOPL/include/httpsupport.h:59-61`
- Problem: The comments say httpLastError holds an error 'the UI can name', but no UI code reads it: the only read anywhere is the host test tools/pc/http/tests/test_catalog_limits.py:77, which stubs the symbol itself. The UI shows fixed strings (_STR_HTTP_ERR_*) instead.
- Fix: Delete the variable, its nine assignments and the comment text that promises it to the UI; drop the matching stub and assert from test_catalog_limits.py (or keep only the row-limit return check there).
- Benefit: ~14 source lines removed; 4 bytes .bss and a few stores (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-335: mc0/mc1 POPSTARTER folder tables and the mkdir-then-verify step are written out several times

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/vcdsupport.cpp:1321`; `RiptOPL/src/sources/vcdsupport.cpp:1344-1345`; `RiptOPL/src/sources/vcdsupport.cpp:1358-1364`; `RiptOPL/src/sources/vcdsupport.cpp:2025-2034`; `RiptOPL/src/sources/vcdsupport.cpp:2685-2687`; `RiptOPL/src/sources/vcdsupport.cpp:2819-2837`; `RiptOPL/src/sources/vcdsupport.cpp:2156`
- Problem: The same {"mc0:/POPSTARTER","mc1:/POPSTARTER"} table is declared in vcdFindPopstarterMc and again in vcdResolvePopstarterMc, and vcdPopstarterNetDirs is a 10-line function that snprintf's the same two strings into a caller buffer of 2x96 bytes with a maxDirs argument that is always 2. vcdEnsurePopstarterNetDir and vcdResolvePopstarterMc:1358-1364 both do mkdir then opendir to confirm; the mc0:/ mc1:/ root list appears at 1345 and inline at 2156.
- Fix: Keep one file-scope static const table of the two POPSTARTER folders (and one of the two card roots), iterate it directly in vcdResolvePopstarterNetHome, delete vcdPopstarterNetDirs, and have vcdResolvePopstarterMc call vcdEnsurePopstarterNetDir instead of its own mkdir/opendir pair.
- Benefit: ~14 source lines and a 192-byte stack buffer per network-config read removed (INFERRED: char dirs[2][96] at 2685).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-336: updateISOGameList: redundant change-detection branches and an unreachable 'Last game deleted' branch

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/sources/supportbase.cpp:238-262`; `RiptOPL/src/sources/supportbase.cpp:270`; `RiptOPL/src/sources/supportbase.cpp:295-298`; `RiptOPL/src/sources/supportbase.cpp:482-492`
- Problem: modified can only become 1 when head != NULL && count > 0 (both branches at 240 and 261), so the test at 270 is always true and the remove() at 295-298 never runs; 257-259 re-assigns 0. In scanForISO, passing &cache when no cache loaded gives the same decision as NULL (count 0), and freeISOGameListCache is a no-op on the empty cache, so 482-483 and 487-492 need no cacheLoaded branches. Upstream-derived (Open PS2 Loader).
- Fix: Compute modified as (head && count > 0) && (cache == NULL || count != cache->count || mismatch); delete the else at 257-259, the condition at 270 and the dead else at 295-298; in scanForISO call updateISOGameList(path, &cache, ...) and freeISOGameListCache unconditionally.
- Benefit: ~14 lines removed (READ: 3 + 4 + 2 + ~5 from the collapsed branches); no behaviour change, since the removed branch is unreachable today.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-337: drawAttributeImage tests 'built-in theme' two different ways; one exact test makes the inner glyph fallback dead

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/gfx/themeelems.cpp:1535-1541`; `RiptOPL/src/gfx/themeelems.cpp:1557-1571`
- Problem: The outer test uses thmGetFilePath(thmGetGuiValue())==NULL (the selected theme id, which can differ from gTheme while a switch is pending or abandoned), so the disk-theme branch carries a second fallback keyed on cache->prefix()==NULL, which is exact for the element being drawn (the cache was created with themePath at line 888). Both draw the same embedded glyph.
- Fix: Use 'attributeImage->cache->prefix() == NULL' as the single built-in test at 1535 and delete the inner fallback block and its comment (1557-1571).
- Benefit: ~14 source lines removed and one thmGetFilePath call per AttributeImage per frame.
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-338: httpcatalog.cpp hand-rolls case-insensitive compares that strcasecmp already does

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/platform/httpcatalog.cpp:118-136`; `RiptOPL/src/platform/httpcatalog.cpp:219-222`
- Problem: httpEndsWithNoCase lower-cases both strings by hand in a 19-line loop, and the CD/DVD media token test spells out each letter in both cases; strcasecmp (used elsewhere in src, e.g. supportbase.cpp:101) does both in one call and exists on newlib and on the host where tools/pc/http/tests builds this file.
- Fix: Make httpEndsWithNoCase 'ls > lx && strcasecmp(s + ls - lx, suffix) == 0' and the media test strcasecmp(m, "CD") / strcasecmp(m, "DVD"); include <strings.h>.
- Benefit: ~14 lines removed; a few dozen bytes of .text (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-339: Dead stores in guiGameShowCompatConfig/VMC menu; compatMode, dmaMode, coreLoader, hexid etc. are statics used as temporaries

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/ui/guigame.cpp:41-44`; `RiptOPL/src/ui/guigame.cpp:85-86`; `RiptOPL/src/ui/guigame.cpp:92`; `RiptOPL/src/ui/guigame.cpp:598-599`; `RiptOPL/src/ui/guigame.cpp:819`; `RiptOPL/src/ui/guigame.cpp:1817-1835`
- Problem: guiGameShowCompatConfig recomputes compatMode (1818-1823) and reads dmaMode, coreLoader, hexid and altStartup (1832-1835), but guiGameApplyConfig re-reads every one of them from the dialog before use (1858-1862, 1866, 1881, 1997, 2003) and nothing else reads them. coreLoader at 819 is never read (598 overwrites it first), and the ternary at 599 is an identity: guiGameResolveEffectiveCore already maps key 2 to gDefaultCoreLoader (262). configSourceID, dmaMode, compatMode, coreLoader, hexid, altStartup and hexDiscID are each written before read inside one function.
- Fix: Delete 1818-1823 and 1832-1835 (keep only the COMPAT_LOADFROMDISC branch), delete line 819, replace the 599 ternary with the core value, and turn the listed statics into function locals.
- Benefit: ~13 source lines removed and ~120 bytes of .bss moved to stack (INFERRED: 4 ints + hexid[32] + altStartup[32] + hexDiscID[15] + configSourceID); removes cross-dialog shared state
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-340: Header clutter in guisettings.cpp: duplicate sound.h include, four unneeded forward declarations with a copy-pasted comment, stray comment

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/ui/guisettings.cpp:49`; `RiptOPL/src/ui/guisettings.cpp:59`; `RiptOPL/src/ui/guisettings.cpp:68-78`
- Problem: include/sound.h is included at 33 and again at 49. guiShowVcdListConfig, guiShowCoverflowConfig, guiShowArtworkConfig and guiShowMmceConfig are all defined (1241, 1881, 1776, 357) before their only uses (2952, 2688, 2684, 2517), so their forward declarations and the four copies of 'was declared in the header before it became file-local' serve no purpose. Only guiConfirmVideoMode (defined 3412, used 2769) needs one. '// Last Played Auto Start' above <time.h> is unrelated to that include, which is there for clock().
- Fix: Delete line 49, the comment at 59, and lines 68-78. Keep the guiConfirmVideoMode declaration at 66, without its comment.
- Benefit: ~13 lines removed; no ELF change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-341: Unused declarations in system.cpp: local createVMCparam_t, ELF_MAGIC, guiEnd/menuEnd/thmEnd/rmEnd prototypes

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/platform/system.cpp:69-76`; `RiptOPL/src/platform/system.cpp:99`; `RiptOPL/src/platform/system.cpp:132-135`; `RiptOPL/src/platform/system.cpp:1315-1319`; `RiptOPL/src/platform/system.cpp:1331`
- Problem: createVMCparam_t is a stale local copy (no VMC_flags) left after the VMC code moved to sysvmc.cpp, which uses genvmc.h's type; ELF_MAGIC is never referenced; the four teardown prototypes are never called in system.cpp (opl.cpp calls them via gui.h/menusys.h/themes.h/renderman.h). sysPlaceIgrOverlay meanwhile hand-checks the ELF magic byte by byte and uses a literal 1 for PT_LOAD next to the ELF_PT_LOAD constant.
- Fix: Delete the typedef, ELF_MAGIC and the four prototypes. Optionally use ELF_PT_LOAD at 1319 and 1331.
- Benefit: ~13 lines removed; no ELF change (declarations only)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-342: cacheCancelPendingImageLoads is an empty inline stub whose single call does nothing

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/texcache.h:238-247`; `RiptOPL/src/gfx/themes.cpp:1826-1827`
- Problem: cacheCancelPendingImageLoads() has an empty body; its only caller is `if (curT == NULL) cacheCancelPendingImageLoads();` at themes.cpp:1826-1827, whose own comment says it is a plain no-op. The ten-line warning comment exists only to stop someone filling the stub in again.
- Fix: Delete the stub, its comment, and the two-line call in themes.cpp; the warning becomes unnecessary once there is nothing to fill in. Update the cross-reference comment in include/artworker.h:74.
- Benefit: ~13 lines removed; no ELF change (INFERRED: empty static inline already compiles to nothing).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-343: hddPartitionMountable duplicates hddPartitionMountableAt, whose only caller probes then mounts again

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/hddsupport.cpp:436-449`; `RiptOPL/src/sources/hddsupport.cpp:1247-1257`; `RiptOPL/src/sources/hddsupport.cpp:1326-1332`
- Problem: hddPartitionMountable(p) is byte-for-byte hddPartitionMountableAt(hddPrefix, p, FIO_MT_RDWR). The At variant has one caller, commitOplHomeSelection, which mounts/unmounts pfs1: as a probe and then immediately mounts the same partition again at 1331 and fails the same way if that mount fails, so the probe adds nothing.
- Fix: Delete hddPartitionMountableAt. In commitOplHomeSelection replace the probe plus mount with fileXioUmount("pfs1:") followed by the existing fileXioMount check.
- Benefit: ~13 lines removed and one mount/unmount cycle less per Settings save of the APA home (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-344: igs_api.c: hand-rolled ClearBuffer, duplicate CT16S case, unused Screenshot parameters and always-0 return

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/ee_core/src/igs_api.c:71-77`; `RiptOPL/ee_core/src/igs_api.c:778`; `RiptOPL/ee_core/src/igs_api.c:750-757`; `RiptOPL/ee_core/src/igs_api.c:168`; `RiptOPL/ee_core/src/igs_api.c:738`; `RiptOPL/ee_core/src/igs_api.c:680`; `RiptOPL/ee_core/src/igs_api.c:789`; `RiptOPL/ee_core/include/igs_api.h:178`
- Problem: ClearBuffer is a byte loop where memset (already used by ee_core, modmgr.c:46) does; the CT16S case repeats the CT16 case body; Screenshot never reads its dimensions and pixel_size parameters; InGameScreenshot always returns 0 and its only caller (padhook.c:648) ignores it.
- Fix: Replace ClearBuffer with memset and delete it; let CT16 fall through to CT16S; drop the two unused Screenshot parameters; make InGameScreenshot void.
- Benefit: ~13 lines removed in the extras-only eecore (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-345: selfupdate.cpp error paths: five identical "%s" cases in suReport and free(f.data) repeated on six exits of suInstallWorker

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/ui/selfupdate.cpp:361-389`; `RiptOPL/src/ui/selfupdate.cpp:269`; `RiptOPL/src/ui/selfupdate.cpp:284-294`; `RiptOPL/src/ui/selfupdate.cpp:319-349`
- Problem: suReport formats into a 256-byte stack buffer with snprintf("%s") in five cases only to pass a fixed language string to guiMsgBox; only the SWAP and GITHUB cases need formatting. suInstallWorker frees f.data separately on each of its six exit paths (285, 291, 321, 331, 341, 344) because f.data is initialised only after the first two gotos.
- Fix: In suReport, pick the string id for the plain cases and call guiMsgBox(_l(id), 0, NULL) once, keeping snprintf only for SWAP and GITHUB. In suInstallWorker, set f.data = NULL at the top and free it once at the done label, deleting the five other free calls.
- Benefit: ~13 source lines (INFERRED: suReport 29 -> ~20 lines, five free calls removed); negligible ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-346: trySaveAlternateDevice: gBootDir copy and the whole host: fast path are dead (only called when gBootDir is empty)

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/app/settingsio.cpp:2052-2055`; `RiptOPL/src/app/settingsio.cpp:2058-2069`; `RiptOPL/src/app/settingsio.cpp:2452-2454`
- Problem: The only caller (2454) is inside if (gBootDir[0] == '\0'), so the gBootDir branch at 2052-2053 never runs, and oplBootDirIsHostFs() (util.cpp:980-985, returns 0 unless gBootDir starts with host) is always false there, so the 11-line host: write-and-return block never runs. A host: boot never reaches this function at all because its gBootDir is non-empty.
- Fix: Delete the gBootDir branch (use getcwd only) and delete the oplBootDirIsHostFs() branch with its comment, starting the boot-device chain at the hdd/pfs test.
- Benefit: ~13 source lines removed; small code-size saving (INFERRED, static function body shrinks).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-347: vsLoadFolder stats every file twice and keeps a separate malloc'd name list

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/core/vmcsaves.cpp:636`; `RiptOPL/src/core/vmcsaves.cpp:645-647`; `RiptOPL/src/core/vmcsaves.cpp:660-667`; `RiptOPL/src/core/vmcsaves.cpp:702-705`
- Problem: The readdir pass stats each entry (660) and copies its name into a malloc'd list (645-647, 667); the second pass rebuilds the same path and stats it again only for st_mtime (702-705). On a real memory card (File Manager Paste as PSU, guifilemgr.cpp:448) each stat is a card directory lookup. The list also costs a malloc and eight free(list) calls on the error paths.
- Fix: In the first pass write the name, and the stamp from the stat already made, straight into s->files[names] (count still advances only once data is allocated, so vsSaveFree is unaffected; set s->created before the loop for the fallback), then delete list, its malloc, its frees and the second stat.
- Benefit: One stat per file saved (up to 64 per save); ~13 lines removed (INFERRED from lines 636, 645-647, eight free(list) lines and 702-705, minus 2 added).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-348: Art path composition (prefix+folder/value_suffix or folder+value_suffix, then texDiscoverArt) is repeated in five game sources

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/mmcesupport.cpp:650-663`; `RiptOPL/src/sources/mmcesupport.cpp:1766-1774`; `RiptOPL/src/sources/httpsupport.cpp:861-877`; `RiptOPL/src/sources/bdmsupport.cpp:2088-2092`; `RiptOPL/src/sources/udpfssupport.cpp:1208-1213`; `RiptOPL/src/sources/hddsupport.cpp:2954-2958`
- Problem: Each image() override builds the same two snprintf formats ('%s%s/%s_%s' with a prefix, '%s%s_%s' without) into a 256-byte buffer and calls texDiscoverArt; mmceTryLoadImage is already this exact helper but is static and has a single caller (MmceSource::image, which then just returns its result).
- Fix: Move mmceTryLoadImage to supportbase.cpp as one shared helper (prefix, folder, isRelative, value, suffix, tex) and make the five image() overrides call it (udpfs passes its case-mapped folder). Delete the per-source copies and the long repeated 'ART LIVES IN THE ART FOLDER' comments.
- Justification: The helper already exists (mmceTryLoadImage); making it shared replaces five 5-line copies with one-line calls, a net reduction, and keeps the art lookup rule in one place.
- Benefit: ~12 net source lines removed (5 copies x ~4 lines = 20, minus ~8 for the shared helper); a few hundred bytes of duplicated code (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-349: Commented-out 'currently unused' icon enum values and externs, half of them since re-enabled

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/textures.h:36-42`; `RiptOPL/src/gfx/textures.cpp:57-61`
- Problem: The block lists UP/DOWN/L1/L2/R1/R2 as unused, but L1_ICON and R1_ICON are live enum values at textures.h:121-122 (with L1_png/R1_png externs at textures.cpp:62,64). The remaining names (up/down/L2/R2) have no asset in gfx/ and no reference anywhere.
- Fix: Delete both commented-out blocks.
- Benefit: 12 lines removed (7 + 5); no ELF change
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-350: EnableGSTracker/DisableGSTracker repeat EnableGSM/DisableGSM except one flag

- Category: 2 (Duplication)
- Location(s): `RiptOPL/ee_core/src/gsm_api.c:193-214`; `RiptOPL/ee_core/src/gsm_api.c:233-245`; `RiptOPL/ee_core/include/gsm_api.h:17-20`
- Problem: EnableGSTracker is EnableGSM with track_only = 1 instead of 0. DisableGSTracker is DisableGSM plus 'track_only = 0', which is redundant because both enable functions set track_only before installing the hooks. Callers: main.c:140,145,151 and padhook.c:588,592,596.
- Fix: Make one EnableGSM(int track_only) and delete EnableGSTracker; delete DisableGSTracker and call DisableGSM at padhook.c:592 and 596.
- Benefit: ~12 lines; INFERRED ~40 bytes of ram84 text (two small non-static functions merged)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-351: GUI_OP_ADD_HINT deferred op is never queued: dead enum, union member and handler case

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/gui.h:18`; `RiptOPL/include/gui.h:51-55`; `RiptOPL/src/ui/gui.cpp:281-286`
- Problem: No code creates a GUI_OP_ADD_HINT op: the name appears only in the enum and the handleOp case, and the union's hint member is read only by that case. Hints are added directly under menuHintsLock in opl.cpp:366-404 and themeelems.cpp:2280-2323.
- Fix: Delete GUI_OP_ADD_HINT, the hint struct in gui_update_t and the case in Gui::handleOp.
- Benefit: ~12 lines removed; a few bytes of .text (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-352: ModalScreen::begin is virtual but never overridden; MessageBoxScreen::wrapText only forwards to fntWrapText

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/include/guimodal.h:18-19`; `RiptOPL/include/guimodal.h:38-39`; `RiptOPL/src/ui/guimodal.cpp:52`; `RiptOPL/src/ui/guimodal.cpp:70-84`; `RiptOPL/src/ui/guimodal.cpp:125`
- Problem: No subclass overrides begin(); it only plays SFX_MESSAGE. wrapText is a private static whose body is a single fntWrapText call (and guiDrawMsgText at 115-120 calls fntWrapText directly with the same width).
- Fix: Delete begin() and call sfxPlay(SFX_MESSAGE) at the top of run(); delete wrapText and call fntWrapText in the MessageBoxScreen constructor.
- Benefit: ~12 lines removed and one vtable slot; negligible ELF change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-353: Pasted blocks inside saveicon.cpp: VMC open/size/parse/close, want-key publish, settle-and-queue

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/gfx/saveicon.cpp:702-707`; `RiptOPL/src/gfx/saveicon.cpp:893-898`; `RiptOPL/src/gfx/saveicon.cpp:1063-1072`; `RiptOPL/src/gfx/saveicon.cpp:1122-1127`; `RiptOPL/src/gfx/saveicon.cpp:1099-1104`; `RiptOPL/src/gfx/saveicon.cpp:1129-1133`
- Problem: siLoadFromVmc (702-707) and the PS1 slot loop (893-898) both open a file, lseek to the end for its size, run a card parser through siFdRead and close. siSetWant (1063-1072) and saveIconSelectPs1 (1122-1127) both publish siWantKey/siWantVmc under siLock, differing only in siWantStartup and siWantPs1. The 'settled and not shown/queued -> ioPutRequestUnlessWaiting(siLoadAction), clear siQueuedKey on failure' block appears at 1099-1104 and 1129-1133.
- Fix: Give siSetWant a ps1-kind argument and use it from saveIconSelectPs1; move the settle-and-queue test into one static function called from both select functions; let the PS1 loop open the image through a small shared open-and-parse helper used by siLoadFromVmc too.
- Justification: Three pairs of pasted blocks (about 34 lines) collapse to three short helpers and their calls, net about 12 lines fewer, and the want/queue rules can no longer drift apart between the PS1 and PS2 paths.
- Benefit: ~12 lines removed net (INFERRED from the cited ranges); no measurable ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-354: Prefix separator rule re-implemented six times (cueSep, vcdSep and four inline copies)

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/cuesupport.cpp:37-45`; `RiptOPL/src/sources/cuesupport.cpp:126-127`; `RiptOPL/src/sources/vcdsupport.cpp:773-777`; `RiptOPL/src/sources/ethsupport.cpp:1035-1036`; `RiptOPL/src/sources/favsupport.cpp:1270`; `RiptOPL/src/sources/supportbase.cpp:1354-1355`
- Problem: 'backslash if the prefix ends with one, else slash' is written as cueSep, as vcdSep (identical body) and inline in saveIconPs1Directory (which could call cueSep in the same file), EthSource::launchVcd, favsupport and supportbase. The cueSep comment says it is kept local on purpose, but the two bodies are byte-identical.
- Fix: Declare one static inline helper in supportbase.h (e.g. sbPrefixSep) and use it in all six places; delete cueSep and vcdSep (or make them aliases while the source-text tests that extract `static char cueSep(` are updated).
- Benefit: ~12 lines removed (INFERRED from cited ranges); no ELF change worth stating.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-355: RA_PROBE lab probe ladder left in ee_core: always 0, no build sets it

- Category: 1 (Dead code)
- Location(s): `RiptOPL/ee_core/include/ra.h:7-11`; `RiptOPL/ee_core/src/iopmgr.c:238-246`; `RiptOPL/ee_core/src/iopmgr.c:342`; `RiptOPL/ee_core/src/ra.c:242-244`; `RiptOPL/ee_core/src/ra.c:290`; `RiptOPL/ee_core/src/ra.c:368-370`; `RiptOPL/ee_core/src/ra.c:395`
- Problem: ra.h defines RA_PROBE 0 'in every real build' for a lab experiment (X-Men Origins hunt, iopmgr.c:238-241); no ps2.yaml, tools/*.py, workflow or Makefile ever defines it, so every RA_PROBE test is a constant and the comments describe modes nobody can select.
- Fix: Delete the RA_PROBE define and comment, the lab comment in iopmgr.c, and drop the RA_PROBE terms from the conditions (the code they guard stays as if RA_PROBE were 0).
- Benefit: ~12 lines removed; constant-folded today, so no ELF change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-356: Small duplications in dia.cpp: single-use diaGetLocalisedText, repeated enum clamp, duplicated string-commit branch

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/ui/dia.cpp:49-55`; `RiptOPL/src/ui/dia.cpp:133`; `RiptOPL/src/ui/dia.cpp:186-197`; `RiptOPL/src/ui/dia.cpp:954-960`; `RiptOPL/src/ui/dia.cpp:940-950`; `RiptOPL/src/ui/dia.cpp:698-699`
- Problem: diaGetLocalisedText has one caller (diaLabelText). diaHandleInput re-counts and clamps the enum index (954-960) the way diaEnumText already does (186-197). The string editor has two identical commit blocks (942-943 and 947-948) that differ only in whether the handler or diaShowKeyb produced the text. 'int renderX = x;' is a pointless copy.
- Fix: Inline diaGetLocalisedText into diaLabelText; share one 'enum count' helper between diaEnumText and diaHandleInput; choose the editor with one conditional expression and commit once; pass x directly.
- Benefit: ~12 lines removed; no measurable size change (static functions) (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-357: Small redundancies: menuRequestInfoSize repeated six times, menuReinitMainMenu forwarder, unused menuSetParentalLockCheckState return value

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/ui/menusys.cpp:2896-2918`; `RiptOPL/src/ui/menusys.cpp:1292-1295`; `RiptOPL/src/ui/menusys.cpp:2218-2226`
- Problem: menuHandleInputInfo calls menuRequestInfoSize after each of six navigation branches, each with the same comment. menuReinitMainMenu only forwards to the static menuInitMainMenu. menuSetParentalLockCheckState returns the previous state, but none of its four callers (menusys.cpp:1022, 3264, 3272; guisettings.cpp:1671) reads it.
- Fix: Record 'moved' in the navigation branches and call menuRequestInfoSize once; make menuInitMainMenu the exported function and delete the forwarder; make menuSetParentalLockCheckState return void and drop wasEnabled.
- Benefit: ~12 lines removed (5 calls and comments, 4-line forwarder, 3 lines of return plumbing) (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-358: Stale and change-history comments in menusys.cpp describe code that is gone

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/ui/menusys.cpp:563-565`; `RiptOPL/src/ui/menusys.cpp:803-810`; `RiptOPL/src/ui/menusys.cpp:2464-2469`
- Problem: 563-565 says _menuLoadConfig serves the game-settings path 'gameMenuLoadConfig', but gameMenuLoadConfig (976) now calls menuLoadConfig directly. 808-810 says MENU_APP_CONFIG_IDLE_FRAMES 'does not exist here', but it is defined and used at 2426-2433. 803-807 and 2464-2469 narrate older versions ('the old version had two paths', 'READ BY NOTHING ... the third field in this tree') rather than the current code.
- Fix: Delete the NOTE(rebuild) block and the history sentences; correct the _menuLoadConfig comment to name its real caller (_menuRequestConfig when a waiter is pending).
- Benefit: ~12 comment lines removed (INFERRED); no ELF change
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-359: Stale and misplaced comments in resolveBootDirToMass (MMCE no-op stub NOTE, detached MMCE paragraph, wrong hdd-branch claim)

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/app/settingsio.cpp:1373-1376`; `RiptOPL/src/app/settingsio.cpp:1383-1386`; `RiptOPL/src/app/settingsio.cpp:1405-1410`
- Problem: 1405-1410 says mmceLoadModules() is still a no-op stub so the poll always times out, but mmceLoadModules calls MmceSupport::loadModules, a real loader (mmcesupport.cpp:733-762). 1373-1376 describes the MMCE branch but sits above the network branch, separated from it. 1383-1386 says the hdd branch blanks gBootDir and re-homes to configInit(NULL); the hdd branch (1348-1361) keeps the launch identity.
- Fix: Delete the NOTE(rebuild) paragraph and the detached MMCE paragraph; trim 1383-1386 to the cdrom case only.
- Benefit: ~12 comment lines removed; no binary change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-360: Stale comments and markers in dialogs.cpp and gui.h describing code that is gone

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/ui/dialogs.cpp:2263-2268`; `RiptOPL/src/ui/dialogs.cpp:944-947`; `RiptOPL/src/ui/dialogs.cpp:1067-1069`; `RiptOPL/src/ui/dialogs.cpp:14-42`; `RiptOPL/include/gui.h:238-240`
- Problem: dialogs.cpp:2263-2268 is a six-line note about a removed row (gEnableAnalogNav, which no longer exists anywhere); 944-947 and 1067-1069 narrate the removed diaDisplayConfig. All 29 UIString stores at 14-42 carry __attribute__((unused)) although each is referenced by a DIA_STRING row. gui.h:239 says guiShowRANotices() is called each frame; no such function exists (the notice is drawn by gNotifications.drawRA in gui.cpp:360).
- Fix: Delete the history comments and the 29 unused attributes; correct the gui.h comment to name drawRA.
- Benefit: ~12 comment lines removed and 29 lines shortened; no ELF change
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-361: Stale comments, an unreachable clamp and an unused include in vcdsupport.cpp/.h

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/sources/vcdsupport.cpp:24`; `RiptOPL/src/sources/vcdsupport.cpp:28`; `RiptOPL/src/sources/vcdsupport.cpp:31`; `RiptOPL/src/sources/vcdsupport.cpp:33`; `RiptOPL/src/sources/vcdsupport.cpp:228-229`; `RiptOPL/src/sources/vcdsupport.cpp:621-622`; `RiptOPL/src/sources/vcdsupport.cpp:692-693`; `RiptOPL/src/sources/vcdsupport.cpp:1031-1032`; `RiptOPL/src/sources/vcdsupport.cpp:2069-2070`; `RiptOPL/include/vcdsupport.h:9`; `RiptOPL/include/vcdsupport.h:113-114`
- Problem: textures.h is included for texDiscoverLoad/ERR_BAD_FILE, neither of which the file uses; other include comments name guiWarning and bdmGetDeviceRootByType (not used) and a function that 'never existed'. 228-229 is an orphaned header for vcdScanOpenDir; 621-622 documents a function that is not there; 692-693 is a clamp its own comment calls unreachable (ISO_GAME_NAME_MAX 160 < VCD_NAME_MAX-1); 1031-1032 says the art miss-memo invalidation lives in vcdScanOpenDir, but no such code exists anywhere; 2069-2070 describes a parser above vcdUdpfsAddress. The header says BDMA equip lives in system.c/opl.c and that vcdFillGameList frees *outGames first; both are wrong.
- Fix: Delete the orphaned/false comments and the unreachable clamp, drop the textures.h include (confirm by compiling), and correct the include comments and the two header comments.
- Benefit: ~12 source lines removed; no ELF change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-362: Stale, doubled and misplaced comments in sysneutrino.cpp, sysdisc.cpp and system.h

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/platform/sysneutrino.cpp:190-198`; `RiptOPL/src/platform/sysneutrino.cpp:263-278`; `RiptOPL/src/platform/sysneutrino.cpp:1186-1190`; `RiptOPL/src/platform/sysneutrino.cpp:1363-1365`; `RiptOPL/src/platform/sysdisc.cpp:109-120`; `RiptOPL/include/system.h:81-86`
- Problem: sysneutrino.cpp:190-192 and 193-198 are two versions of the same comment on neutrinoArgHasActiveFlag. 263-268 (describes the old sysLaunchNeutrino body) and 269-278 (describes sysSyncNeutrinoUdpfsToml at 382) sit above sysLastPathSeparator. 1186-1189 says the limit is '14, NOT sizeof(argv)' directly above argvMax = sizeof(argv)/sizeof(argv[0]) = 32. 1363-1365 is a placeholder saying nothing happens here. sysdisc.cpp:109-117 documents sysLaunchDisc/sysGetDiscBootPath but sits above sysDiscProbeWait. system.h:81-82 describe sysNeutrinoArgsPreflight (line 96) but sit above the cheats comment.
- Fix: Delete 190-192, 263-268, 1363-1365 and the wrong first sentence of 1186-1189; move 269-278 above sysSyncNeutrinoUdpfsToml, sysdisc.cpp:109-117 above sysGetDiscBootPath, and system.h:81-82 above line 96.
- Benefit: About 12 lines removed and comments that match the code; no binary change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-363: TAR_KIND_CFG is wired to nothing in the program, and the tar.h/tar.cpp banners still say only ART is wired

- Category: 8 (Half-finished or redundant features)
- Location(s): `RiptOPL/include/tar.h:4-6`; `RiptOPL/include/tar.h:53-57`; `RiptOPL/include/tar.h:67`; `RiptOPL/src/platform/tar.cpp:1`; `RiptOPL/src/platform/tar.cpp:9-11`; `RiptOPL/src/platform/tar.cpp:61`; `RiptOPL/tests/host/test_tar.cpp:99-103`
- Problem: No source under src/ references TAR_KIND_CFG or TarEntryCfg; the kind is kept 'so it can be wired later' and is exercised only by tests/host/test_tar.cpp:99-103. The banners claim RiptOPL wires ONLY the ART kind, but CHT is live (supportbase.cpp:1761-1784) and THM theme archives are live (themes.cpp, sfx.cpp, bgm.cpp, textures.cpp, fntsys.cpp).
- Fix: Delete TAR_KIND_CFG, TarEntryCfg, its gTarInfo row, the 'CFG/' alternative in the prefix strip and the CFG part of the host test; rewrite the two banners in one line each to say ART, CHT and THM are used.
- Benefit: ~12 lines removed and stale guidance corrected; ~20 bytes .rodata (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-364: Three forwarding wrappers: getGameImageTexture, thmFindElemBySuffix, thmGetItemSource

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/gfx/themeelems.cpp:1020-1023`; `RiptOPL/src/gfx/themeelems.cpp:1355-1361`; `RiptOPL/src/gfx/themeelems.cpp:1395-1398`; `RiptOPL/include/themeint.h:92-94`; `RiptOPL/include/themeint.h:108`
- Problem: getGameImageTexture only calls getGameImageTextureEx(..., 0); thmFindElemBySuffix only calls thmFindElemBySuffixEx(..., 0); thmGetItemSource ignores item and returns menu->item->userdata. This is C++, so default arguments cover the first two, and the third is a one-field read.
- Fix: Give getGameImageTextureEx a default isPriority = 0 and thmFindElemBySuffixEx a default declaredOnly = 0 (renaming them to the short names), delete the two wrappers; replace thmGetItemSource(menu, item) with menu->item->userdata at its 7 call sites and delete it. The stubs in .github/scripts/test_psbbn_flow.py:121,215-216 need the same rename.
- Benefit: ~12 source lines and 3 prototypes removed; two tiny call frames (INFERRED, gc-sections keeps them since they are called).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-365: appIsPopstarterElf has an unused title parameter and a redundant SB. pre-check; appTitleIsPs1Elf hand-rolls a substring match

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/sources/appsupport.cpp:178-189`; `RiptOPL/src/sources/appsupport.cpp:264`; `RiptOPL/src/sources/appsupport.cpp:1169`; `RiptOPL/src/sources/appsupport.cpp:29-43`
- Problem: title is discarded with (void)title; the comment keeps it only for a 'host-test contract'. The appIsPopstarterSmb call at 183-184 accepts SB.<x>.ELF (VCD_PREFIX_SMB is "SB.", vcdsupport.h:25), which lines 194-201 accept anyway. appTitleIsPs1Elf checks '[','P|p','S|s','1',']' character by character over 10 lines.
- Fix: Drop the title parameter and lines 183-184 (callers at 264 and 1169 pass one argument); write appTitleIsPs1Elf as a loop with strncasecmp(title + i, "[ps1]", 5).
- Benefit: ~12 lines removed
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-366: appsupport.cpp checks for states that cannot occur

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/sources/appsupport.cpp:470-471`; `RiptOPL/src/sources/appsupport.cpp:481-484`; `RiptOPL/src/sources/appsupport.cpp:494-503`; `RiptOPL/src/sources/appsupport.cpp:533-534`
- Problem: appSetResolvedStartup/appSetArtSource are only called with &node->app (721, 767) or after the NULL check; appResolveLegacyMassStartup returns NULL only for a NULL input, and appBuildStartupPath never returns NULL (path, boot or a member buffer), so 499-503 is dead; appGetBoot returns appGetELFName which never returns NULL (481); and with appItemCount >= 1 the power-of-two loop already makes size >= 2 (533-534).
- Fix: Delete the four checks.
- Benefit: ~12 lines removed
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-367: iopmgr.c __DECI2_DEBUG module loads: no ps2build configuration defines __DECI2_DEBUG

- Category: 1 (Dead code)
- Location(s): `RiptOPL/ee_core/src/iopmgr.c:183-186`; `RiptOPL/ee_core/include/modules.h:33-34`; `RiptOPL/src/platform/system.cpp:993-1000`
- Problem: The DRVTIF/TIFINET loads after an IOP reset and the matching packing in system.cpp exist only under __DECI2_DEBUG, which only the legacy Makefiles (Makefile:340, ee_core/Makefile:44) and the tools/syntax_sweep.py syntax check set; tools/build_configuration.py has no DECI2 configuration.
- Fix: Delete the __DECI2_DEBUG branch in ResetIopSpecial (leaving the TTY_UDP/TTY_PPC chain) and the two module ids, together with the rest of the DECI2 chain in src/platform (brief open question 5).
- Benefit: ~12 lines removed across these locations (more in the wider DECI2 chain); no shipped ELF change
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-368: menuwatch.cpp: freeze and crash reports repeat the EE thread table, the log tail and the write-and-colour tail

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/ui/menuwatch.cpp:202-215`; `RiptOPL/src/ui/menuwatch.cpp:345-348`; `RiptOPL/src/ui/menuwatch.cpp:436-451`
- Problem: buildReport (202-215) and crashReport (436-446) each contain the same header line, the same 1..255 ReferThreadStatus loop with the same format string (differing only in which thread is marked and the sema line), and the same diagCopy/terminate/append block. watchThread (345-348) and crashReport (448-451) repeat the same mc0-then-mc1 writeReport call with green/orange screen colours, differing only in the file name. addEpcSamples already shows the shared-helper pattern for the PC samples.
- Fix: Move the thread table plus log tail into one static helper taking the thread id to mark and its label (with the sema line kept, it is harmless in the crash report), and the two-card write plus colour into one helper taking the file name; call both from buildReport/watchThread and crashReport.
- Justification: Both helpers replace two copies that already exist in this file, so the file gets shorter; no behaviour change other than the crash report also listing semaphore counts.
- Benefit: ~12 source lines (INFERRED: 14+4 duplicated lines become two helpers of ~10 lines plus 4 call lines); diagnostic builds only, so no shipped-ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-369: ra_overlay.c draw_card/tracked_display: never-used pmode out-parameter, repeated validity check, dead x/y save-restore

- Category: 1 (Dead code)
- Location(s): `RiptOPL/ee_core/src/ra_overlay.c:185`; `RiptOPL/ee_core/src/ra_overlay.c:207-208`; `RiptOPL/ee_core/src/ra_overlay.c:232-233`; `RiptOPL/ee_core/src/ra_overlay.c:253-258`; `RiptOPL/ee_core/src/ra_overlay.c:261-264`; `RiptOPL/ee_core/src/ra_overlay.c:309-310`
- Problem: tracked_display's only caller (line 221) passes NULL for pmode. tracked_display only returns a framebuffer that already passed valid_display_fb, so the check at 232-233 cannot fail. The block at 261-310 saves x/y, zeroes them and restores them, but nothing in between reads x or y (all sprite/text3x5 calls use literals). The pixel-size expression is evaluated three times at 253-258.
- Fix: Drop the pmode parameter and its two lines, the second valid_display_fb check, the pixel_x/pixel_y save-zero-restore, and compute the pixel size once.
- Benefit: ~12 lines removed; RA builds only; small .text saving (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-370: raii::UniqueFile/FileCloser are used only by the host test

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/raii.h:62-70`; `RiptOPL/tests/host/test_raii.cpp:58-61`
- Problem: No product source uses UniqueFile or FileCloser; the only references are tests/host/test_raii.cpp:58 and 61, which test the helper itself.
- Fix: Delete FileCloser and UniqueFile from raii.h and their test case.
- Benefit: ~9 header lines plus the test lines; no ELF change (unused template).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-371: ranet.cpp: the RAA1 OK reply is parsed twice by copy, re-scanned a second time in askPc, and trailing-space trimming is written three times

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/ranet.cpp:625-635`; `RiptOPL/src/sources/ranet.cpp:649-650`; `RiptOPL/src/sources/ranet.cpp:1051-1059`; `RiptOPL/src/sources/ranet.cpp:602-604`
- Problem: askPc (625-635) and xeraboraIdentify (1051-1059) carry the same five-or-three-number sscanf of 'RAA1 OK' plus title trim. askPc then sscanfs total/chunks again at 649 although bytes/ch from 625/629 already hold them. The 'while len>0 && s[len-1]==' '' trim appears at 603-604, 634-635 and 1057-1058.
- Fix: One static raaParse(rx, &bytes, &chunks, &nach, &unlocked, &unsupported) returning the trimmed title, used by both; use its bytes/chunks in askPc instead of the second sscanf.
- Justification: The parse exists twice; one helper replaces both and the redundant rescan.
- Benefit: ~12 lines removed (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-372: renderman leftovers: unused __VRAM_SIZE, orphan comments, commented-out ATE, redundant rmEnd prototype, needlessly exported gDefaultAlpha

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/gfx/renderman.cpp:15-16`; `RiptOPL/src/gfx/renderman.cpp:115-119`; `RiptOPL/src/gfx/renderman.cpp:271`; `RiptOPL/src/gfx/renderman.cpp:397`; `RiptOPL/src/gfx/renderman.cpp:462-463`; `RiptOPL/src/platform/system.cpp:135`; `RiptOPL/include/renderman.h:66`
- Problem: __VRAM_SIZE has no use anywhere. Lines 115-119, 397 and 462-463 are headings and descriptions of members and templates that moved to include/renderer.h (where the same comments sit again, renderer.h:77-92). Line 271 is commented-out code. system.cpp:135 re-declares rmEnd although it includes renderman.h (system.cpp:45). gDefaultAlpha is exported in renderman.h but only renderman.cpp uses it.
- Fix: Delete the macro, the orphan comments, the commented-out line and the extra prototype; make gDefaultAlpha static in renderman.cpp and drop its extern.
- Benefit: ~12 lines removed. gDefaultAlpha's 8-byte .rodata copy is already discarded by --gc-sections (MEASURED, riptopl-discarded-firstparty.txt), so no ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-373: vmcsaves.cpp re-implements its own helpers: vsReadIn's cluster loop, vsLoadFolder's name check, read/write loops

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/core/vmcsaves.cpp:425-435`; `RiptOPL/src/core/vmcsaves.cpp:358-374`; `RiptOPL/src/core/vmcsaves.cpp:643-644`; `RiptOPL/src/core/vmcsaves.cpp:767-770`; `RiptOPL/src/core/vmcsaves.cpp:145-169`
- Problem: vsReadIn's inner loop (427-434) is vsReadChain (364-372) for a shorter size. vsLoadFolder's name test (643) is a weaker copy of vsNameOk (767-770, which also rejects '.' and '..'). vsReadAll and vsWriteAll (145-169) differ only in read versus write.
- Fix: Replace vsReadIn's loop with 'return vsReadChain(v, e.cluster, buf, n) ? n : -1'; move vsNameOk above vsLoadFolder and call it at 643; fold vsReadAll/vsWriteAll into one function with a writing flag, as vsIo already does at 173.
- Benefit: ~12 source lines; under 200 bytes of code (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-374: Unused includes and stale 'Theme Sound Effects' banners in sound.cpp and sfx.cpp

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/core/sound.cpp:8-10`; `RiptOPL/src/core/sound.cpp:16-20`; `RiptOPL/src/core/sfx.cpp:9-10`; `RiptOPL/src/core/sfx.cpp:21-22`; `RiptOPL/src/core/sfx.cpp:50-52`
- Problem: After the split into sound.cpp/sfx.cpp/bgm.cpp, sound.cpp still includes timer.h, vorbis/vorbisfile.h, delaythread.h, themes.h and pad.h and uses none of them (only audsrv, LOG, the bgm flags and volume globals); sfx.cpp includes vorbisfile.h and delaythread.h without using Ogg or DelayThread. sound.cpp:19-20 heads gAudioInitialized with a 'Theme Sound Effects' banner, duplicated at sfx.cpp:21-22 above a one-line comment that says the same; sfx.cpp:50-52 are blank.
- Fix: Delete the seven unused includes, the banner in sound.cpp, one of the two in sfx.cpp, and the blank lines.
- Benefit: ~12 lines; faster compile (vorbisfile.h pulls in the codec headers); no ELF change (INFERRED)
- Effort: S. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-375: xparam.cpp: the same DCACHE_OFF / CPU_DELAY loads, DECKARD check and rom0 probes are written out several times

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/core/xparam.cpp:12-13`; `RiptOPL/src/core/xparam.cpp:77-82`; `RiptOPL/src/core/xparam.cpp:96-114`; `RiptOPL/src/core/xparam.cpp:123-143`; `RiptOPL/src/core/xparam.cpp:166-168`; `RiptOPL/src/core/xparam.cpp:196-228`
- Problem: The memcpy(params_DCACHE_OFF)+SifLoadModule pair appears three times (79-80, 109-110, 225-226) and the CPU_DELAY pair twice (97-98, 112-113); ApplyExtraXParamTitle returns from each branch instead of setting flags. The DECKARD bit test and its 6-line comment are repeated (123-130, 196-203), and open/close probes of rom0:XPARAM2 and rom0:XPARAM appear four times. params_DCACHE_OFF and params_CPU_DELAY are writable globals used only here.
- Fix: In ApplyExtraXParamTitle compute two flags (dcacheOff, cpuDelay) from the three title lists and issue each load once at the end; let the special-disc path at 222-227 reuse that tail. Keep one copy of the DECKARD comment. Make the two params_ arrays static const.
- Benefit: ~12 source lines; a few dozen bytes of code (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-376: Defensive argument checks in cuesupport static settings helpers that their only caller already rules out

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/sources/cuesupport.cpp:581-592`; `RiptOPL/src/sources/cuesupport.cpp:652-653`
- Problem: cueRewriteSettings re-validates NULL pointers, negative sizes and wantCount==0 although its only caller (cueRewriteSettingsFile:695-730) has already rejected wantCount<=0 and passes two fresh buffers; the wantCount==0 copy branch is reached only by test_ember_settings.py:415. cueWantSetting checks list/count/key/values for NULL though all nine calls pass locals and string literals.
- Fix: Delete the wantCount==0 branch and the NULL checks (keep the setting range check in cueWantSetting); adjust the one harness case that passes wantCount 0, or keep it since the loop would still copy an LF-only input unchanged.
- Benefit: ~11 lines removed (INFERRED from cited ranges).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-377: 'if (guiDrawBGSettings() == 0) guiDrawBGPlasma();' repeated at all 10 call sites

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/ui/guisettings.cpp:1936-1944`; `RiptOPL/src/ui/guisettings.cpp:1375-1376`; `RiptOPL/src/ui/guisettings.cpp:3113-3114`; `RiptOPL/src/ui/guisettings.cpp:3189-3190`; `RiptOPL/src/ui/menusys.cpp:2181-2182`; `RiptOPL/src/ui/menusys.cpp:2928-2929`; `RiptOPL/src/ui/menusys.cpp:3297-3298`; `RiptOPL/src/ui/diaeditors.cpp:200-201`; `RiptOPL/src/ui/diaeditors.cpp:477-478`; `RiptOPL/src/ui/diaeditors.cpp:589-590`; `RiptOPL/src/ui/dia.cpp:630-631`; `RiptOPL/include/gui.h:143`
- Problem: Every caller of guiDrawBGSettings uses its return value only to draw the plasma when there is no settings background, so the fallback is written out ten times.
- Fix: Have guiDrawBGSettings call guiDrawBGPlasma itself when the theme has no SETTINGS_BG texture and return void; replace each two-line call site with one call.
- Benefit: ~10 lines removed and ten compare/branch sequences (INFERRED, well under 100 bytes).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-378: Bounds guards that can never fail in convertCompatmaskToModes and the core-argument appends of sysRunNeutrinoLaunch

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/platform/sysneutrino.cpp:113-137`; `RiptOPL/src/platform/sysneutrino.cpp:1195-1210`; `RiptOPL/src/platform/sysneutrino.cpp:1231-1247`; `RiptOPL/src/platform/sysneutrino.cpp:1275`; `RiptOPL/src/platform/sysneutrino.cpp:1286`; `RiptOPL/src/platform/sysneutrino.cpp:1293-1297`; `RiptOPL/src/platform/sysneutrino.cpp:1307`; `RiptOPL/src/platform/sysneutrino.cpp:1327`; `RiptOPL/src/platform/sysneutrino.cpp:1341`; `RiptOPL/src/platform/sysneutrino.cpp:1358`
- Problem: result[COMPAT_MODE_COUNT + 2] is 10 bytes (COMPAT_MODE_COUNT is 8, iosupport.h:106) and at most 5 digits are appended, so all five pos guards are always true. argv has 32 slots and at most 13 core entries are appended before appendArgTokens (argv0, bsd, bsdfs, dvd, -qb, -gc, -dbc, -logo, -gsm, -elf, two -mc, -cwd), so every argc < argvMax test before line 1370 is always true; the real limit is the 14-entry/256-byte check at 1401-1412.
- Fix: Drop the pos guards and the 'bounds-guarded so widening the set later cannot overflow' comment; drop the argc < argvMax tests on the core appends before appendArgTokens (keep the ones after it at 1375, 1382, 1389, which can trigger).
- Benefit: About 10 lines and simpler conditions; no measurable ELF change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-379: FrameClock keeps a second pair of timestamps for the debug FPS meter

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/include/guiframe.h:7-12`; `RiptOPL/include/guiframe.h:30-34`; `RiptOPL/src/ui/guiframe.cpp:24-28`; `RiptOPL/src/ui/guiframe.cpp:41-57`
- Problem: Under __DEBUG, endFrame stores prev_/cur_ with a second clock() call right after computing nowUs, so cur_ - prev_ is the same frame length already held in lastUs_. The header also carries a six-line investigation banner about a swallowed navigation step rather than what the class does.
- Fix: Delete prev_, cur_ and the __DEBUG block in endFrame; compute fps() from lastUs_. Shorten the header banner to one line.
- Benefit: ~10 source lines removed; one clock() syscall per frame fewer in debug builds (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-380: Misplaced and stale comments in favsupport.cpp, folderbrowse, gamesource.h and hddsupport.h

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/sources/favsupport.cpp:31`; `RiptOPL/src/sources/favsupport.cpp:574-576`; `RiptOPL/src/sources/favsupport.cpp:866-867`; `RiptOPL/src/sources/favsupport.cpp:1360-1361`; `RiptOPL/src/sources/folderbrowse.cpp:9`; `RiptOPL/include/folderbrowse.h:16`; `RiptOPL/include/gamesource.h:64-66`; `RiptOPL/include/hddsupport.h:82-86`; `RiptOPL/include/hddsupport.h:128-131`
- Problem: favsupport.cpp:574-576 describes favResolve but sits above favFindVisibleSource (favResolve has its own comment at 689); :31 has two comments merged on one include line; :866-867, :1361, folderbrowse.cpp:9 and folderbrowse.h:16 name opl.c, themes.c and vcdsupport.c, which are now .cpp files. gamesource.h:64-66 documents ps1SaveDir but sits above rowPrefix. hddsupport.h:82-86 and :128-131 narrate the history of a removed macro and a review thread instead of describing the code.
- Fix: Delete favsupport.cpp:574-576 and the stray half of :31, fix the file names, move gamesource.h:64-66 above ps1SaveDir (line 73), and cut the two hddsupport.h history blocks to one line each.
- Benefit: ~10 lines removed; comments match the code (no ELF change)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-381: NULL guards for impossible arguments in the static cover-cache split helpers

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/gfx/themes.cpp:218-219`; `RiptOPL/src/gfx/themes.cpp:252-253`; `RiptOPL/src/gfx/themes.cpp:264-265`; `RiptOPL/src/gfx/themes.cpp:331-332`; `RiptOPL/src/gfx/themes.cpp:382-383`
- Problem: isDecoratorCoverImage, cloneImageCache, replaceSharedCoverCache, splitFilteredDecoratorCoverCaches and separateVcdCoverCache are static and only called with a live theme (validateGUIElems(newT) or a caller that already dereferenced it), non-NULL family addresses, gameImage already tested non-NULL (272, 390) and a source cache already tested non-NULL (294-297, 390-400).
- Fix: Delete these guards; keep the list/extended checks in splitDecoratorCoverCache, whose argument can be a slot pointer.
- Benefit: ~10 source lines removed; trivial .text.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-382: Repeated autolaunch teardown and MX4SIO quiesce blocks inside bdmlaunch.cpp

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/bdmlaunch.cpp:359-365`; `RiptOPL/src/sources/bdmlaunch.cpp:500-506`; `RiptOPL/src/sources/bdmlaunch.cpp:964-972`; `RiptOPL/src/sources/bdmlaunch.cpp:102-108`; `RiptOPL/src/sources/bdmlaunch.cpp:196-202`
- Problem: miniDeinit + free/NULL of gAutoLaunchBDMGame and gAutoLaunchDeviceData is written out three times in this file; the 'SDC device: cacheAbortMmceImageLoadsTimed(500) else warn and return' block is written twice.
- Fix: One static helper for the autolaunch release (used by abortLaunch, tryLaunch and launch) and one for the MX4SIO quiesce check.
- Justification: Three and two identical copies; the helpers are shorter than the copies they replace.
- Benefit: ~10 lines removed
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-383: Six near-identical sysLaunchLoaderElf calls with '|= 0' no-op flag lines and a stale TODO

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/sources/bdmlaunch.cpp:975-998`
- Problem: Each driver branch repeats the same nine-argument sysLaunchLoaderElf call, differing only in the mode string, fakemodule flags and (for cf) the mcemu arguments; the iLink and MX4SIO branches OR in 0 (983 carries 'TODO! fake ilinkman ?').
- Fix: Have the branches only pick the mode string and flags (and zero the mcemu size for cf), then make one sysLaunchLoaderElf call; delete the '|= 0' lines and the TODO.
- Benefit: ~10 lines removed
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-384: Small dead parameters and redundant branches in system.cpp launch code

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/platform/system.cpp:705-711`; `RiptOPL/src/platform/system.cpp:733`; `RiptOPL/src/platform/system.cpp:755`; `RiptOPL/src/platform/system.cpp:1398`; `RiptOPL/src/platform/system.cpp:792-793`; `RiptOPL/src/platform/system.cpp:1218-1224`
- Problem: patchlist_t.mode is "" in every iop_patch_list row, so the mode test at 733 (and the mode_str argument to addIopPatch) never filters; GetModStorageLocation's compatFlags parameter is never read; the BDM_ATA_MODE branch sets CORE_IRX_HDD exactly like the final else at 806-807; sysPrintEECoreConfig (debug builds) logs enforceLanguage twice.
- Fix: Delete the mode field, the mode clause and addIopPatch's mode_str parameter; drop the compatFlags parameter; delete the BDM_ATA_MODE else-if; delete the second enforceLanguage LOG.
- Benefit: ~10 lines and 7 string literals removed; a few bytes of .rodata (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-385: Three copies of the four Neutrino folder/file case variants

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/supportbase.cpp:2042-2047`; `RiptOPL/src/sources/supportbase.cpp:2491-2496`; `RiptOPL/src/sources/supportbase.cpp:2519-2528`
- Problem: sbNeutrinoProbeGameDevice ("%sNEUTRINO/neutrino.elf" etc.), sbResolveMigratedApaNeutrino (relative names) and sbProbeNeutrinoMc (8 absolute mc0/mc1 strings, mixing "mc0:NEUTRINO/" from NEUTRINO_PATH with "mc0:/neutrino/") each spell out the same four case combinations, in three different orders.
- Fix: Keep one static table of the four relative names; build the game-device and mc0:/mc1: candidates from it with snprintf in the existing loops.
- Benefit: ~10 lines removed (INFERRED: 18 table lines become 6 plus a format call); one probe order everywhere.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-386: Unreachable defensive branches in favsupport.cpp: editable NULL tests and the selectedItemFlags mode fallback

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/sources/favsupport.cpp:250-251`; `RiptOPL/src/sources/favsupport.cpp:267-268`; `RiptOPL/src/sources/favsupport.cpp:322-323`; `RiptOPL/src/sources/favsupport.cpp:971-977`
- Problem: favParseImage writes *editable unconditionally at line 231, and both callers (favReadPath 387, favImageValid 520) pass a non-NULL pointer, so the three later editable != NULL tests are always true. In selectedItemFlags, ownerView returns NULL only for an invalid index (already rejected at 962) or a NULL owner, and no record is ever stored with a NULL owner (ingest skips them at 835-839; favGatherAdd stores the non-NULL src from gatherPass 1627-1629), so the APP/HDD fallback at 971-977 never runs.
- Fix: Delete the three NULL tests (keep the assignments) and the fallback block after the ownerView call, returning 0 when ownerView is NULL.
- Benefit: ~10 lines removed; negligible code size (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-387: VMC size 8/16/32/64 MB to dialog-index mapping spelled out as if-chains in several places

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/ui/guigame.cpp:168-179`; `RiptOPL/src/ui/guigame.cpp:552-559`; `RiptOPL/src/ui/guigame.cpp:292`; `RiptOPL/src/ui/guigame.cpp:753`
- Problem: The index<->MB mapping is written as an if/else chain in guiGameRefreshVMCConfig (MB to index) and again in guiGameShowVMCConfig (index to MB), and the valid-size test is repeated at 292 and 753; the label list is at 540.
- Fix: Use one static const int table {8, 16, 32, 64}: index to MB becomes a table lookup, MB to index a short loop that also serves as the validity test.
- Benefit: ~10 source lines removed (INFERRED from the cited ranges)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-388: _loadConfig: lscstatus is always CONFIG_ALL, so the network-only retry is unreachable; dead stores at 1777-1778 and 1976

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/app/settingsio.cpp:1560`; `RiptOPL/src/app/settingsio.cpp:1911-1919`; `RiptOPL/src/app/settingsio.cpp:1777-1778`; `RiptOPL/src/app/settingsio.cpp:1976`
- Problem: _loadConfig has one caller (opl.cpp:1719, once at boot) and lscstatus is static-initialised to CONFIG_ALL (0xFF, config.h:21), so if (lscstatus & CONFIG_OPL) is always true and the tryAlternateDevice retry at 1916-1919 (guarded by !(lscstatus & CONFIG_OPL)) can never run; the comment at 1916 admits no caller asks for network alone. gEnableUDPBD/gNetBootProtocol written at 1777-1778 are not read before being rewritten at 1828-1829. lscret = result at 1976 is never read: saveConfig resets lscret to 0 (2559) before any read.
- Fix: Use CONFIG_ALL directly in _loadConfig instead of lscstatus, drop the always-true CONFIG_OPL test, delete the network-only retry (1912-1919), delete lines 1777-1778 and 1976.
- Benefit: ~10 source lines removed; no measurable size change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-389: favStrdup and hiddenAppend re-implement strdup, which the rest of the repo already uses

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/favsupport.cpp:47-54`; `RiptOPL/src/sources/favsupport.cpp:841`; `RiptOPL/src/sources/favsupport.cpp:1597`; `RiptOPL/src/sources/gamemanage.cpp:81-84`
- Problem: favStrdup is malloc(strlen+1)+memcpy, and hiddenAppend does malloc+strcpy by hand; both behave exactly like newlib strdup, which opl.cpp:802, lang.cpp:181/494 and vcdsupport.cpp:312/334 already call.
- Fix: Delete favStrdup and call strdup at its two call sites; use strdup in hiddenAppend.
- Benefit: ~10 lines removed; a few dozen bytes of .text (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-390: guiCheckNotifications / guiResetNotifications forward to gNotifications, which gui.cpp already calls directly

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/ui/guinotify.cpp:202-211`; `RiptOPL/include/gui.h:173`; `RiptOPL/include/guiint.h:13`
- Problem: The two forwarders have three callers in total (opl.cpp:359, opl.cpp:418, guisettings.cpp:2630), while gui.cpp:326-327 calls gNotifications.reset()/check() directly, so the same operation has two names.
- Fix: Include guinotify.h in opl.cpp and guisettings.cpp, call gNotifications.check()/reset() there, and delete the two forwarders and their prototypes.
- Benefit: ~10 lines removed (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-391: guiGameVMCSavesSync duplicates vmcRebuildSync (PFS device sync of a path)

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/ui/guigame.cpp:400-411`; `RiptOPL/src/sources/vmcrebuild.cpp:89-103`
- Problem: Both functions take the part of a path up to ':' when it starts with "pfs", copy it into a 16-byte buffer and call fileXioSync(device, 0), logging a failure; the only difference is that vmcRebuildSync reads gVMCRebuild.card and returns the result. guigame.cpp already includes vmcrebuild.h and vmcrebuild.cpp is in the main ELF (src/sources).
- Fix: Give vmcRebuildSync a path parameter, export it from vmcrebuild.h, and delete guiGameVMCSavesSync, calling the exported one at its three call sites (445, 458, 468).
- Benefit: ~10 source lines net (INFERRED: 12-line function removed, 1 prototype and a parameter added); ~100 bytes .text (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-392: guiSetBdmaSettings / guiBdmaUpdater: forwarding wrapper, dead fallback, duplicated visibility block, stale comment

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/ui/guisettings.cpp:1101-1114`; `RiptOPL/src/ui/guisettings.cpp:1196-1201`; `RiptOPL/src/ui/guisettings.cpp:2868-2872`; `RiptOPL/src/ui/guisettings.cpp:2898-2900`; `RiptOPL/src/ui/guisettings.cpp:2945`
- Problem: guiSettingsPopstarterUpdater only forwards to guiBdmaUpdater, its sole caller. guiBdmaUpdater falls back to diaBdmaConfig when activeDialog() is NULL, but it only runs during the POPSTARTER page after compose() set activeDialog_ (2911, 2136). guiSetBdmaSettings repeats the updater's four setVisible lines. The page comment names a non-existent guiShowBdmaConfig and calls BDMA a chained sub-dialog, but it is composed inline.
- Fix: Pass guiBdmaUpdater straight to diaExecuteDialog at 2945 and delete the wrapper. Use activeDialog() without the fallback. Replace 1198-1201 with a guiBdmaUpdater(1) call. Correct the comment to name only the two real sub-dialogs.
- Benefit: ~10 lines removed (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-393: include/tar.h included with a 'tarInvalidate' comment in ten files that use nothing from it

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/ui/menuitems.cpp:44`; `RiptOPL/src/ui/guiwait.cpp:42`; `RiptOPL/src/ui/guimodal.cpp:43`; `RiptOPL/src/ui/guigameid.cpp:55`; `RiptOPL/src/ui/gui.cpp:49`; `RiptOPL/src/sources/appscan.cpp:45`; `RiptOPL/src/app/autolaunch.cpp:44`; `RiptOPL/src/app/lwnbdctl.cpp:45`; `RiptOPL/src/app/opl.cpp:50`; `RiptOPL/src/gfx/texcache.cpp:13`
- Problem: These files include tar.h (most with the comment 'tarInvalidate -- re-arm the .tar probe') but contain no tar* call or Tar/TAR_ identifier besides that comment; only settingsio.cpp and guisettings.cpp call tarInvalidate.
- Fix: Delete the ten #include lines.
- Benefit: 10 lines removed; no ELF change
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-394: iopmgr.c small redundancies: RA_TelemetryWanted ignores its argument, a no-op else, duplicated ETH/HTTP loads

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/ee_core/src/iopmgr.c:51-55`; `RiptOPL/ee_core/src/iopmgr.c:389-394`; `RiptOPL/ee_core/src/iopmgr.c:262-274`
- Problem: RA_TelemetryWanted takes config, casts it to void and returns RA_SnapBytes() > 0 (3 callers: 167, 243, 342). The else at 389-394 sets ra_snap_iop to 0, which line 341 already did. ETH_MODE and HTTP_MODE repeat the same guarded SMSTCPIP+SMAP load pair (264-266, 271-273).
- Fix: Drop the unused parameter (or call RA_SnapBytes() > 0 directly); delete the else branch; merge the ETH and HTTP cases so the stack loads once and only ETH adds SMBINIT. Update the source-text checks in test_ra_launch_resources.py:22, test_netblock_oplcore.py:164 and test_udpfs_files_oplcore.py:152 that match these lines.
- Benefit: ~10 lines removed; no meaningful ELF change (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-395: menuRenameGame and menuDeleteGame repeat the same Favourites refusal and NULL-support blocks

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/ui/menusys.cpp:284-295`; `RiptOPL/src/ui/menusys.cpp:322-323`; `RiptOPL/src/ui/menusys.cpp:530-541`; `RiptOPL/src/ui/menusys.cpp:559-560`
- Problem: Both actions open with the same two 6-line blocks (FAV_MODE page -> _STR_FAV_MSG, favourited row -> _STR_FAV_PERSISTENCE_MSG) differing only in the verb string id, and both end with the same 'NULL Support object. Please report' message box.
- Fix: Move the two refusals into one static function taking the row and the verb string id, returning nonzero when it refused; call it from both actions.
- Justification: Two 12-line copies become one ~12-line function plus two one-line calls.
- Benefit: ~10 lines removed (INFERRED from cited ranges)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-396: padhook.h declares Remove_Padhook (no definition), six DEV9 register macros and IGR_LIBPAD_NONE that nothing uses

- Category: 1 (Dead code)
- Location(s): `RiptOPL/ee_core/include/padhook.h:25`; `RiptOPL/ee_core/include/padhook.h:29-35`; `RiptOPL/ee_core/include/padhook.h:84`
- Problem: Remove_Padhook has a prototype but no definition anywhere; DEV9_R_1460..DEV9_R_1474 and IGR_LIBPAD_NONE are never referenced by ee_core (the IOP dev9.c uses same-named offsets from ps2sdk's dev9regs.h, not this header).
- Fix: Delete the Remove_Padhook prototype, the DEV9 register block and IGR_LIBPAD_NONE.
- Benefit: ~10 header lines removed; no binary change
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-397: Eight includes in themes.cpp that nothing in the file uses

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/gfx/themes.cpp:14-15`; `RiptOPL/src/gfx/themes.cpp:17-19`; `RiptOPL/src/gfx/themes.cpp:24-29`
- Problem: pad.h, sound.h, favsupport.h, vcdsupport.h (comment claims vcdDisplayName), libview.h (comment claims libViewActive), rabadge.h, <time.h> and <math.h> are included, but none of their functions, macros or types appear in themes.cpp; the commented users live in themeelems.cpp now.
- Fix: Delete the eight includes (and the RETROACHIEVEMENTS #ifdef around rabadge.h).
- Benefit: 10 source lines; slightly faster compile; no ELF change.
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-398: Three functions repeat the 'live __common pfs0: else borrow pfs1:' sequence; the comment above them describes the wrong one

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/hddsupport.cpp:2071-2092`; `RiptOPL/src/sources/hddsupport.cpp:2094-2107`; `RiptOPL/src/sources/hddsupport.cpp:2109-2124`
- Problem: hddVisitCommonPops, hddEquipPopstarterBdmaFromCommon and hddInstallPopstarterMcFromCommon each test whether the live home is __common (use pfs0:) or mount hdd0:__common read-only on pfs1:, run one call, and unmount. hddVisitCommonPops already is the generic form. The comment at 2071-2076 documents hddInstallPopstarterMcFromCommon (pfs1: scratch, custom ELF, mc?:/POPSTARTER externals) but sits above hddVisitCommonPops.
- Fix: Implement the other two through hddVisitCommonPops (a captureless lambda plus a small arg struct, or pass the root and append POPS/ inside), keeping their own module-ready checks; move the 2071-2076 comment to hddInstallPopstarterMcFromCommon or delete it.
- Benefit: ~10 lines removed and one place for the __common mount rule (INFERRED)
- Effort: S. Risk: Med. Confidence: Med (READ)

#### RIPTOPL-399: Dead art bookkeeping: ImageCache::requestQueued and the never-written gArtW.refused counter

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/texcache.h:100-101`; `RiptOPL/src/gfx/texcache.cpp:520`; `RiptOPL/include/texcache.h:191`; `RiptOPL/include/artworker.h:49-52`; `RiptOPL/src/gfx/artload.cpp:174-184`; `RiptOPL/src/ui/guioverlay.cpp:143-147`
- Problem: requestQueued() (texcache.h:101) has no caller anywhere; the enqueue path increments activeRequests_ directly (texcache.cpp:520). gArtW.refused (artworker.h:52) is never written by any code, is copied out by cacheDebugCounters' `refused` parameter (artload.cpp:180-181), and the HUD reads it into `r` (guioverlay.cpp:143,147) but never prints `r` (the format at guioverlay.cpp:211 shows Q, A, D, X only).
- Fix: Delete requestQueued (or call it at texcache.cpp:520 and drop the friend's direct access; deleting is shorter). Delete the refused field and its comment, the refused parameter of cacheDebugCounters, and the `r` variable in the HUD.
- Benefit: ~9 lines removed; 4 bytes of .bss for the field (INFERRED, volatile int). No other ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-400: Every guiDrawBGSettings caller writes the same plasma fallback (10 sites)

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/ui/dia.cpp:630-631`; `RiptOPL/src/ui/diaeditors.cpp:200-201`; `RiptOPL/src/ui/diaeditors.cpp:477-478`; `RiptOPL/src/ui/diaeditors.cpp:589-590`; `RiptOPL/src/ui/guisettings.cpp:1375-1376`; `RiptOPL/src/ui/guisettings.cpp:1936-1944`; `RiptOPL/src/ui/guisettings.cpp:3113-3114`; `RiptOPL/src/ui/guisettings.cpp:3189-3190`; `RiptOPL/src/ui/menusys.cpp:2181-2182`; `RiptOPL/src/ui/menusys.cpp:2928-2929`; `RiptOPL/src/ui/menusys.cpp:3297-3298`
- Problem: guiDrawBGSettings returns 0 when the theme has no SETTINGS_BG texture, and all ten callers respond with the same 'if (guiDrawBGSettings() == 0) guiDrawBGPlasma();'. No caller uses the return value for anything else.
- Fix: Make guiDrawBGSettings draw the plasma itself when the texture is missing and return void; each call site becomes one line.
- Benefit: ~9 lines removed; ten compare-and-branch sequences become one (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-401: No-op statements in guisettings.cpp: write-only guiPopsNetReadPending, re-enabling rows that are already enabled/visible

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/ui/guisettings.cpp:1322`; `RiptOPL/src/ui/guisettings.cpp:1344`; `RiptOPL/src/ui/guisettings.cpp:1363`; `RiptOPL/src/ui/guisettings.cpp:1367`; `RiptOPL/src/ui/guisettings.cpp:2504-2505`; `RiptOPL/src/ui/guisettings.cpp:2509`; `RiptOPL/src/ui/guisettings.cpp:2662-2664`
- Problem: guiPopsNetReadPending is assigned in four places and never read (guiPopsNetReadInFlight is what gets tested). The diaSetEnabled(...,1) calls for CFG_ENABLEBDMHDD, CFG_HDDMODE and CFG_MMCEMODE, and diaSetVisible(UICFG_COVERFLOW_BUTTON,1), act on a freshly composed copy whose templates already have enabled=1, visible=1 (dialogs.cpp 510, 515, 539, 1021), and nothing in the repo ever disables or hides those ids.
- Fix: Delete guiPopsNetReadPending and its assignments, and the four no-op diaSetEnabled/diaSetVisible calls with the comment above the last one.
- Benefit: ~9 lines removed, 4 bytes .bss (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-402: focusHashValue re-implements the FNV-1a loop that ArtIndex::hashName already has

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/gfx/artworker.cpp:13-22`; `RiptOPL/src/gfx/artindex.cpp:54-67`
- Problem: artworker.cpp:13-22 is the same FNV-1a loop (same basis and prime) as ArtIndex::hashName, minus the case folding; the focus hash only detects a selection change, so case folding would not matter (artworker.h:89-93: a collision only keeps some obsolete prefetch).
- Fix: Expose the existing hash (for example as a free function next to the artIndex* API) and call it from ArtWorker::updateFocus; delete focusHashValue.
- Benefit: ~9 lines removed (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-403: renameVcd repeats the pfs1: mount/unmount block for Ember and pooled VCD rows

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/hddsupport.cpp:1977-2001`
- Problem: The Ember branch (1977-1987) and the pooled-VCD branch (1992-2001) build the same hdd0:<part> source, unmount and RDWR-mount pfs1:, call one rename function and unmount; only cueRenameGame vs vcdRenameFileInDir differs.
- Fix: Handle the PP.* label case first, then do the mount once and call cueRenameGame or vcdRenameFileInDir depending on isEmber.
- Benefit: ~9 lines removed (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-404: CheatLiveFits only forwards to the header inline igrCheatsFit, and costs an igrovl API slot

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/ee_core/src/cheat_api.c:181-184`; `RiptOPL/ee_core/include/cheat_api.h:15`; `RiptOPL/ee_core/include/igrovl.h:34`; `RiptOPL/ee_core/include/igrovl_side.h:20`; `RiptOPL/ee_core/src/igrovl_stub.c:34`; `RiptOPL/ee_core/src/igrmenu.c:1028`
- Problem: CheatLiveFits(b, at) is 'return igrCheatsFit(b, at)'. igrCheatsFit is a static inline in igrcheats.h that reads only the cheat block passed in (no ee_core state), and igrmenu.c already includes igrcheats.h (igrmenu.c:49). Its only caller (igrmenu.c:1028) could call igrCheatsFit directly in both the in-core and overlay builds; the wrapper exists only to be exported through igrovl_api_t.
- Fix: Delete CheatLiveFits, its prototype, the igrovl_api_t member, the igrovl_side.h macro and the igrovl_stub.c table entry; call igrCheatsFit at igrmenu.c:1028. Bump IGROVL_ABI since the struct layout changes.
- Benefit: 8 lines; INFERRED a few dozen bytes of ee_core ram84 text (the out-of-line copy of the igrCheatsFit loop) plus one 4-byte API table word in IGR_OVERLAY builds
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-405: Dialog backdrop (full-screen rect + two white rules) copy-pasted at 7 sites

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/ui/guimodal.cpp:146-149`; `RiptOPL/src/ui/guimodal.cpp:228-231`; `RiptOPL/src/ui/guimodal.cpp:278-281`; `RiptOPL/src/ui/guimodal.cpp:309-312`; `RiptOPL/src/ui/guimodal.cpp:400-402`; `RiptOPL/src/ui/guisettings.cpp:3116-3118`; `RiptOPL/src/ui/guisettings.cpp:3462-3465`
- Problem: The same three calls (rmDrawRect full screen with gColBlack or gColDarker, rmDrawLine at y=75 and y=410) are repeated at five places in guimodal.cpp and two in guisettings.cpp.
- Fix: Add one guiDrawDialogFrame(u64 backdrop) next to guiDrawMsgText in guimodal.cpp and call it from the seven sites.
- Justification: Seven identical 3-line blocks become seven one-line calls plus a ~6-line helper: net ~8 lines fewer, and the dialog geometry lives in one place.
- Benefit: ~8 net lines removed (7x3 - 7 - 6); no ELF change of note (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-406: FavSupport::artMode and FavSource::image duplicate the favourite-to-art-value match

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/favsupport.cpp:935-951`; `RiptOPL/src/sources/favsupport.cpp:1336-1348`
- Problem: Both loops decide whether favourite i matches an art value the same way: a name-addressed favourite matches on its text or on vcdExtractGameId(text), anything else matches when favOwnerHasId and the owner's startup equals the value. The two copies (about 10 identical lines each) must stay in step for texcache's device routing to agree with the actual read, as the artMode comment at 903-908 states.
- Fix: Add one private FavSupport::matchesArtValue(int i, item_list_t *owner, const char *value) holding the match and call it from both loops.
- Justification: The comment at 903-908 requires both to use the same matching; one shared predicate (about 12 lines) replaces two copies (about 20 lines), so it removes code and keeps the two paths from drifting.
- Benefit: ~8 lines net removed; no measurable size change (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-407: Half-rate tick constant 147456 defined twice (pad.cpp, sfx.cpp) with the same rationale comment; rumble timing rationale repeated in sfx.cpp

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/core/pad.cpp:23-28`; `RiptOPL/src/core/sfx.cpp:268-271`; `RiptOPL/src/core/sfx.cpp:276-279`; `RiptOPL/src/core/sfx.cpp:320-324`
- Problem: CLOCKS_PER_MILISEC (pad.cpp:28) and SFX_CLOCKS_PER_MS (sfx.cpp:271) are the same deliberate half-rate value with near-identical explanations. sfx.cpp:320-324 repeats the 35 ms / ERM spin-up rationale already given at :276-279.
- Fix: Define the half-rate constant once in a shared header next to pad.h's API (or have sfx.cpp use pad.cpp's) and keep one explanation; delete the repeated rumble paragraph in sfxRumble.
- Benefit: ~8 lines; no ELF change (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-408: Leftover declarations and comments: unused externs and '//foo' in util.c, commented-out WipeUserMemory call in syshook.c

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/ee_core/src/util.c:15-21`; `RiptOPL/ee_core/src/util.c:190`; `RiptOPL/ee_core/src/syshook.c:119`
- Problem: util.c declares cdvdman_irx, size_cdvdman_irx, cdvdfsv_irx, size_cdvdfsv_irx and _end but uses none of them; line 190 is a stray '//foo' comment. syshook.c:119 is a commented-out call that line 118 already explains.
- Fix: Delete the five extern lines, the '//foo' line and the commented-out call.
- Benefit: ~8 lines removed; no ELF change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-409: Main-menu IDs MENU_SAVE_CHANGES, MENU_MMCE and MENU_RETIRED_NETWORK_UPDATE are never added; MENU_SAVE_CHANGES handler is unreachable

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/ui/menusys.cpp:53-54`; `RiptOPL/src/ui/menusys.cpp:58`; `RiptOPL/src/ui/menusys.cpp:62`; `RiptOPL/src/ui/menusys.cpp:2395-2396`; `RiptOPL/.github/scripts/test_settings_back_keeps_edits.py:418-421`
- Problem: menuInitMainMenu (1264-1290) is the only place items are appended to mainMenu and it never appends MENU_SAVE_CHANGES, MENU_MMCE or MENU_RETIRED_NETWORK_UPDATE, so the MENU_SAVE_CHANGES branch in menuHandleInputMenu can never run. The IDs are only in-memory row tags (never persisted), so the 'keep IDs stable' reservation protects nothing; a source-text test enforces the retired name.
- Fix: Delete the three enum values, the MENU_SAVE_CHANGES branch, and the test assertion that requires MENU_RETIRED_NETWORK_UPDATE.
- Benefit: ~8 lines removed (enum 3, comment 1, branch 2, test 2) (INFERRED); no ELF change worth stating
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-410: Notifications::show repeats the snprintf / cut-after-colon / render block three times

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/ui/guinotify.cpp:118-140`
- Problem: The config, theme and language notices are the same six lines differing only in the format string id and argument.
- Fix: Add a file-static helper taking (string id, argument, y) that formats, truncates after the first ':' and renders, and call it three times.
- Justification: Three identical 6-line blocks become three calls plus one ~7-line helper: net ~8 lines fewer.
- Benefit: ~8 net lines removed (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-411: RA_EXPERIMENTAL_CARD_DMA compile switch is always 1 in every build

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/include/ra_features.h:4-11`; `RiptOPL/ee_core/src/main.c:142`; `RiptOPL/ee_core/src/padhook.c:590`; `RiptOPL/ee_core/src/ra_overlay.c:369`; `RiptOPL/ee_core/src/ra_overlay.c:377`; `RiptOPL/src/app/host.cpp:136`
- Problem: ra_features.h defaults the switch to 1 under #ifndef and nothing in ps2.yaml, tools/ or workflows defines it; only the host test test_caduceus_overlay.py:118 rewrites it to 0. The user-facing control is the separate gRAExperimentalCards setting.
- Fix: Delete the macro and its #ifndef block and drop the '&& RA_EXPERIMENTAL_CARD_DMA' / '!RA_EXPERIMENTAL_CARD_DMA ||' terms at the five use sites, together with the compile_disabled case in the host test.
- Benefit: ~8 source lines and one dead build option; no ELF change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-412: Small redundancies in udpfssupport.cpp (double include, impossible NULL checks, identical cleanUp/shutdown, repeated guards)

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/sources/udpfssupport.cpp:5`; `RiptOPL/src/sources/udpfssupport.cpp:17`; `RiptOPL/src/sources/udpfssupport.cpp:91`; `RiptOPL/src/sources/udpfssupport.cpp:654-670`; `RiptOPL/src/sources/udpfssupport.cpp:1052-1057`; `RiptOPL/src/sources/udpfssupport.cpp:1232-1252`
- Problem: include/lang.h is included at 5 and 17. In launch(), 'game != NULL' at 1052 and 1057 can never be false: activeGame (277-287) returns &emptyGame_ or &games[id], 1047 already returned on emptyGame_, and 1071 dereferences game unconditionally. cleanUp (1232-1241) and shutdown (1244-1252) have the same body. update() tests 'if (answeredBefore)' three times in a row (654, 667, 669). '#if defined(RIPTOPLITE) && !LITE_HAS_UDPFS' (91) can be '#if !LITE_HAS_UDPFS' because riptlite.h defines LITE_HAS_UDPFS 1 for full builds (riptlite.h:101).
- Fix: Delete the second lang.h include and the two 'game != NULL &&' terms; let shutdown call cleanUp(itemList, 0); put the ART discovery, folder creation and THM/LNG discovery under one 'if (answeredBefore)' block in the same order; simplify the #if.
- Benefit: ~8 lines removed, simpler conditions; no binary change worth counting (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-413: Stale and orphan comments in pad.cpp/padmanager.h left from the C-to-class conversion

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/core/pad.cpp:34`; `RiptOPL/src/core/pad.cpp:47-48`; `RiptOPL/src/core/pad.cpp:71-77`; `RiptOPL/src/core/pad.cpp:581-582`; `RiptOPL/src/core/pad.cpp:641`; `RiptOPL/src/core/pad.cpp:713`; `RiptOPL/include/padmanager.h:25-26`; `RiptOPL/src/ui/gui.cpp:308`
- Problem: pad.cpp:34 and :71-77 are comments for variables that now live in padmanager.h (the 'Declared up here' text declares nothing). :47 names gPadReadyEmptyRun, which no longer exists (it is maxEmptyRun_). :581 says initialize() has five bail-outs, :596 correctly says four. :641, :713, padmanager.h:25-26 and gui.cpp:308 refer to padRumbleRealign, pad.c and readPads instead of rumbleRealign, pad.cpp and read().
- Fix: Delete pad.cpp:34 and :71-77; correct the names and the bail-out count in the others.
- Benefit: ~8 lines removed, comments match the code; no ELF change
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-414: Stale comments naming removed functions, files and states in the splash, wait, libview and menusys files

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/ui/guisplash.cpp:72`; `RiptOPL/src/ui/guisplash.cpp:104`; `RiptOPL/src/ui/guiwait.cpp:162-165`; `RiptOPL/include/libview.h:79`; `RiptOPL/include/config.h:125`; `RiptOPL/include/menusys.h:82-83`; `RiptOPL/include/menusys.h:98`
- Problem: guisplash.cpp:72 names guiDrawBootLogo and :104 names gBootStickyLabel; neither name exists anywhere else in the repo (the code is BootSplash::drawLogo and status.line()). guiwait.cpp:162-165 is an orphaned comment block describing a shutdown fade that sits above a different function. libview.h:79 and config.h:125 point to libview.c / src/libview.c (the file is src/ui/libview.cpp). menusys.h:82-83 is the same doc comment twice (old and new); :98 says toggleView is 'NULL until it lands' although itemExecToggleView exists.
- Fix: Delete or correct these comment lines.
- Benefit: ~8 lines removed; no code change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-415: achievements.cpp small redundancies: repeated request reset, double key wipe, inline hex decode

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/sources/achievements.cpp:596-601`; `RiptOPL/src/sources/achievements.cpp:625-627`; `RiptOPL/src/sources/achievements.cpp:761-762`; `RiptOPL/src/sources/achievements.cpp:721`; `RiptOPL/src/sources/achievements.cpp:728`; `RiptOPL/src/sources/achievements.cpp:205-210`; `RiptOPL/src/platform/util.cpp:716-726`
- Problem: loadXerabora sets requestKind='A', requestPage=0, requestFilter=0 twice on the image path, though requestImage (761-762) already set kind and page. The key buffer is wiped at 721 and again unconditionally at the done label (728). The \u escape decodes hex with a three-way inline ternary although util.cpp already has fromHex.
- Fix: Set requestFilter = 0 in requestImage and delete both reset blocks (keep the goto). Delete the memset at 721. Use fromHex for the \u digits.
- Benefit: ~8 lines removed; no ELF change worth measuring (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-416: cueEmberFolder() is a speculative accessor returning a constant

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/sources/cuesupport.cpp:47-53`; `RiptOPL/include/cuesupport.h:112-113`
- Problem: cueEmberFolder only returns EMBER_FOLDER_DEFAULT; its comment says a configurable folder 'can arrive later as a setting'. No such setting exists; the header tells callers to use the accessor, never the macro.
- Fix: Delete cueEmberFolder and use EMBER_FOLDER_DEFAULT (or rename it EMBER_FOLDER) at its five uses; update the five test scripts that extract or stub the accessor.
- Benefit: ~8 lines removed (INFERRED); no ELF change (static, inlined).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-417: drawItemsList: identical title-draw code in both decorator branches and the decorator test recomputed per row

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/gfx/themeelems.cpp:2017-2018`; `RiptOPL/src/gfx/themeelems.cpp:2060-2086`
- Problem: Lines 2075-2078 and 2082-2085 are the same fntRenderStringFit/fntRenderString pair, and the branch condition at 2060-2061 is exactly hasDecorator computed at 2017-2018, so isHiddenArtworkCache (two strcmp) runs again for every visible row each frame.
- Fix: Test hasDecorator for the decorator icon block only, then draw the title once after it; delete the else branch.
- Benefit: ~8 source lines removed; one isHiddenArtworkCache call per visible row per frame saved.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-418: elfldr_noreset: two forwarding wrappers and a partition parameter that is ignored

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/platform/elfldr_noreset.cpp:163-165`; `RiptOPL/src/platform/elfldr_noreset.cpp:249-262`; `RiptOPL/include/system.h:119-120`; `RiptOPL/src/sources/appsupport.cpp:1074`; `RiptOPL/src/sources/appsupport.cpp:1257`; `RiptOPL/src/sources/appsupport.cpp:1296`; `RiptOPL/src/platform/system.cpp:1259`; `RiptOPL/src/platform/system.cpp:1301`; `RiptOPL/src/platform/sysneutrino.cpp:1430`
- Problem: sysLoadELF (static) only forwards to sysLoadELFCommon with cleanupHdd 0, and is called only by sysLoadELFKeepIOP; sysLoadELFApp only forwards to sysLoadELFCommon. The partition argument is discarded with (void)partition 'for API compatibility', yet all six callers are in this repo and three pass "".
- Fix: Delete sysLoadELF; have sysLoadELFKeepIOP call sysLoadELFCommon(..., 0, 0) directly (or make sysLoadELFApp the implementation). Drop the partition parameter from both public functions and their six call sites, and from the signature mocks in test_app_launch_args.py and test_neutrino_argv_budget.py.
- Benefit: ~8 lines and one call layer removed; no measurable ELF change (static wrapper is inlined, INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-419: games.bin cache helpers compute status codes that the only caller never distinguishes

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/sources/hddsupport.cpp:3097-3144`; `RiptOPL/src/sources/hddsupport.cpp:3146-3198`; `RiptOPL/src/sources/hddsupport.cpp:1860-1869`
- Problem: hddLoadGameListCache returns 1, 0, EIO, ENOMEM, -1 or ENOENT but its one caller only tests cacheRet == 0 (1864). hddUpdateGameListCache tracks a result variable (EIO/0/1) that its one caller (1869) discards. hddLoadGameListCache also frees the list it is given although the caller always passes a zeroed one.
- Fix: Make hddUpdateGameListCache void and drop its result bookkeeping; have hddLoadGameListCache return 0/-1 only; drop the leading hddFreeHDLGamelist(cache) or keep it if other callers are added.
- Benefit: ~8 lines removed (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-420: igrmWaitClear duplicates igrmWaitFor with want = 0

- Category: 2 (Duplication)
- Location(s): `RiptOPL/ee_core/src/igrmenu.c:709-716`; `RiptOPL/ee_core/src/igrmenu.c:774-781`; `RiptOPL/ee_core/src/igrmenu.c:746`; `RiptOPL/ee_core/src/igrmenu.c:1119-1120`
- Problem: igrmWaitClear(reg, bits) is the same 10,000,000-iteration poll as igrmWaitFor(reg, bits, 0); both are static in igrmenu.c and igrmRead already calls igrmWaitFor(..., 0) for the same CHCR STR test that igrmSendLine does with igrmWaitClear.
- Fix: Delete igrmWaitClear, move igrmWaitFor above igrmSendLine and call igrmWaitFor(reg, bits, 0) at the three call sites.
- Benefit: 8 lines; one fewer function in ee_core / igrovl.elf (a few dozen bytes if not inlined, INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-421: opl.h clutter: commented-out BLURT debug macro, duplicate <debug.h>, comments naming the deleted opl.c

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/include/opl.h:559-565`; `RiptOPL/include/opl.h:43`; `RiptOPL/include/opl.h:186`; `RiptOPL/include/opl.h:189-190`
- Problem: Lines 559-565 are a commented-out BLURT debug macro (blurttext/BLURT appear nowhere else). <debug.h> is included at both line 33 and line 43. Lines 186 and 189 say the APA migrated path lives in opl.c; there is no opl.c and NEUTRINO_MIGRATED_APA_PATH is in src/app/settingsio.cpp:1126.
- Fix: Delete the BLURT comment block and the second #include <debug.h>; change 'opl.c' to 'settingsio.cpp' in the two comments.
- Benefit: ~8 lines removed; no ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-422: oplGetAppImageByMode is a one-line forwarder to gSourceRegistry.appImageByMode

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/sources/appscan.cpp:76-82`; `RiptOPL/include/opl.h:70`; `RiptOPL/src/sources/appsupport.cpp:1389-1391`
- Problem: The free function only forwards its seven arguments to SourceRegistry::appImageByMode (opl.cpp:145). Its only callers are the two calls in appsupport.cpp.
- Fix: Call gSourceRegistry.appImageByMode directly from appsupport.cpp (including sourceregistry.h) and delete the forwarder, its comment and its opl.h prototype.
- Benefit: ~8 lines removed; one call level less per app cover lookup (negligible)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-423: pfsDiagHddCheckResult and pfsDiagPs2fsResult are stored only for a LOG that release builds compile out

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/sources/hddsupport.cpp:463-489`; `RiptOPL/src/sources/hddsupport.cpp:1046`; `RiptOPL/src/sources/hddsupport.cpp:1063`; `RiptOPL/include/hddint.h:104-105`
- Problem: The two fields are written by armPfsDiagFailure/clearPfsDiagFailure and read only inside logPfsDiagState's LOG call. LOG(...) expands to nothing unless __DEBUG (include/ioman.h:75-80), so in shipped builds they are write-only state and logPfsDiagState is an empty function. Only pfsDiagReason is used by real logic (762, 771).
- Fix: Delete the two fields and the hddCheckResult/ps2fsResult parameters of armPfsDiagFailure; if the values are wanted in debug logs, LOG them at the two arm call sites.
- Benefit: ~8 lines and 8 bytes of .bss removed (INFERRED: two ints)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-424: tarClose duplicates tarInvalidate and is used only by host tests; tarLoadFromAnyDevice is exported but only called inside tar.cpp

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/platform/tar.cpp:460-465`; `RiptOPL/src/platform/tar.cpp:609-613`; `RiptOPL/include/tar.h:73-75`; `RiptOPL/tests/host/test_tar.cpp:81`
- Problem: tarClose and tarInvalidate have the same body (tarCloseInternal + clear s_inactive); production calls only tarInvalidate (settingsio.cpp:2480, guisettings.cpp:1804) and tarClose appears only in tests/host/test_tar.cpp:81,92,159. tarLoadFromAnyDevice's only callers are tar.cpp:476 and 513.
- Fix: Delete tarClose and switch the host test to tarInvalidate; make tarLoadFromAnyDevice static and drop its prototype.
- Benefit: ~8 lines removed; no ELF change claimed (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-425: vcdEnsureBdmaForLaunch and vcdApplyUsbModeForLaunch repeat the failure-toast ladder and re-inline vcdBdmaLaunchReady

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/vcdsupport.cpp:1879-1887`; `RiptOPL/src/sources/vcdsupport.cpp:1906-1914`; `RiptOPL/src/sources/vcdsupport.cpp:1848`; `RiptOPL/src/sources/vcdsupport.cpp:1899`; `RiptOPL/src/sources/vcdsupport.cpp:1835-1838`
- Problem: The -4/-5/-2/else warning ladder (8 lines) is copied verbatim in both launch helpers. Both fast-path conditions are vcdBdmaLaunchReady(mode) written out again: at 1899 the extra 'mode == VCD_BDMA_USBEXFAT &&' guard is redundant because vcdBdmaManualPairMatches already returns 0 for FAT32.
- Fix: Call vcdBdmaLaunchReady(mode) at 1848 and 1899, and move the ladder into one small static function called from both places.
- Justification: The helper replaces two identical 8-line copies with one, a net removal of about 6 lines; no behaviour change.
- Benefit: ~8 source lines removed (INFERRED: 2x8 ladder lines -> 1x9 helper + 2 calls; 2 shorter conditions).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-426: IGR_CheckInputs keeps the removed polling worker's DI/EI and kernel-mode bracketing and comments, but runs only in the VBLANK handler

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/ee_core/src/padhook.c:303-306`; `RiptOPL/ee_core/src/padhook.c:371-378`; `RiptOPL/ee_core/src/padhook.c:409-413`
- Problem: IGR_CheckInputs is static and its only caller is IGR_Intc_Handler (padhook.c:699), i.e. interrupt context, already masked and in kernel mode. Its DI()/EI(), ee_kmode_enter/exit and the comments about 'the polling worker', 'the user-mode worker stack' and 'the low-priority worker' describe the thread design that the 72-89 banner says was removed.
- Fix: Delete the DI()/EI() pair and the ee_kmode_enter/exit pair in IGR_CheckInputs and reword the three stale comments to the handler context; drop the matching unprotectedReads/unprotectedKmode expectations in .github/scripts/test_igr_polling.py (lines 168-182, 234-236, 487, 495).
- Benefit: ~8 lines and four stale comment lines removed; a few instructions per VBLANK saved (INFERRED)
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-427: hddIsApaMbrHybrid re-implements hddDetectNonSonyFileSystem's sector read and APA checks

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/hddsupport.cpp:817-835`; `RiptOPL/src/sources/hddsupport.cpp:893-903`
- Problem: Both functions malloc 1 KB, issue the same ATA_DEVCTL_READ_PARTITION_SECTOR devctl on xhdd0:, then test the APA magic, hddApaHeaderValid and the 0x55AA signature. The malloc of a fixed 1 KB scratch buffer is also unnecessary on the IO worker's 96 KB stack.
- Fix: Factor the read into one static helper filling a caller-provided 1 KB buffer (or a stack array), and use it from both functions.
- Benefit: ~8 lines removed and two heap allocations per probe avoided (INFERRED)
- Effort: S. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-428: Dead and redundant lines in bdmUpdateDeviceData

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/sources/bdmdevices.cpp:222`; `RiptOPL/src/sources/bdmdevices.cpp:231`; `RiptOPL/src/sources/bdmdevices.cpp:341`; `RiptOPL/src/sources/bdmdevices.cpp:353-362`
- Problem: Two commented-out LOG lines; bdmDeviceType is set to UNKNOWN at 341 and overwritten at 347; the ATA test is made twice (353-355 and 357); and the else-if/else at 359-362 cannot take its last branch because 326-332 already returned when the driver name is empty.
- Fix: Delete 222, 231 and 341; merge the two ATA ifs; drop the unreachable final else (and the now-redundant driver test).
- Benefit: ~7 lines removed
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-429: GSM dialog read-back (five diaGetInt + 1080p confirm) duplicated in guiGameSaveGSMGlobalConfig and guiGameApplyConfig

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/ui/guigame.cpp:1628-1636`; `RiptOPL/src/ui/guigame.cpp:1888-1896`
- Problem: The same nine lines (diaGetInt of ENABLEGSM, GSMVMODE, X/Y offset, FIELDFIX, then guiGameConfirmGSM1080p under GSM_1080P) appear in both save legs.
- Fix: Move the nine lines into one static helper and call it from both functions.
- Justification: The identical block exists twice; a helper removes about 7 lines net.
- Benefit: ~7 source lines net (INFERRED: 18 lines replaced by a ~9-line helper and two calls)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-430: Source re-apply cascade written twice (Game Sources apply and restoreSourceState)

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/ui/guisettings.cpp:2541-2544`; `RiptOPL/src/ui/guisettings.cpp:2549-2557`; `RiptOPL/src/ui/guisettings.cpp:3090-3100`
- Problem: Both places run the same sequence: request a PCCF probe if gEnablePCCF changed, then bdmForceDeviceRefresh, applyConfig(-1,-1,0), menuReinitMainMenu, and the All Games / Show Hidden / loadFavourites chain. That is about 11 identical lines given the previous allGames, showHidden and PCCF values.
- Fix: Move the sequence into one static helper taking the three previous values and call it from both places.
- Benefit: ~7 lines net removed; one copy of the logic to keep in step (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-431: achievements.h exports four functions only achievements.cpp calls

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/include/achievements.h:39-47`; `RiptOPL/src/sources/achievements.cpp:62`; `RiptOPL/src/sources/achievements.cpp:352`; `RiptOPL/src/sources/achievements.cpp:397`; `RiptOPL/src/sources/achievements.cpp:485`
- Problem: achievementsParse, achievementsParseXerabora, achievementsXeraboraPage and achievementsParseGameId are declared in the public header but called only inside achievements.cpp (lines 723, 435, 643, 530) and by the two Python test harnesses, which paste the .cpp text into the same translation unit ahead of the test code.
- Fix: Make the four functions static and delete their prototypes and comments from achievements.h.
- Benefit: ~7 header lines removed; lets the compiler inline or drop them (small, INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-432: fmIsMcPath is a third verbatim copy of the mc0:/mc1: path test (fmIsMc, suIsMc)

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/ui/guifilemgr.cpp:69-72`; `RiptOPL/src/core/filemgr.cpp:82-85`; `RiptOPL/src/ui/selfupdate.cpp:117-120`
- Problem: The same one-line body exists as static fmIsMc in core/filemgr.cpp, static fmIsMcPath in guifilemgr.cpp (which already includes filemgr.h) and static suIsMc in the updater overlay's selfupdate.cpp.
- Fix: Make fmIsMc non-static with a prototype in include/filemgr.h; delete fmIsMcPath and suIsMc and call fmIsMc.
- Benefit: ~7 source lines net (INFERRED); small .text saving only if the compiler did not inline all copies (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-433: gameMenuLoadConfig ignores its UIItem parameter; 17 callers pass it and menusys.h keeps an include-cycle workaround for it

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/ui/menusys.cpp:970-977`; `RiptOPL/include/menusys.h:7-12`; `RiptOPL/include/menusys.h:145`; `RiptOPL/src/ui/guigame.cpp:881-898`
- Problem: gameMenuLoadConfig does (void)ui and forwards to menuLoadConfig after padRumbleFlush. Callers in guigame.cpp pass dialog arrays (diaGSConfig, diaCheatConfig, ...) and the rest pass NULL. The only reason menusys.h forward-declares struct UIItem (with a 5-line comment about the dia.h/opl.h/menusys.h include cycle) is this prototype.
- Fix: Drop the parameter (gameMenuLoadConfig(void)), update the 17 call sites, and delete the forward declaration and its comment in menusys.h.
- Benefit: ~7 header lines removed and 17 call sites simplified (INFERRED); no ELF change
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-434: igrmLen re-implements strlen, which both ee_core and igrovl.elf already provide

- Category: 2 (Duplication)
- Location(s): `RiptOPL/ee_core/src/igrmenu.c:154-160`; `RiptOPL/ee_core/igrovl/igrovl_main.c:43-50`
- Problem: igrmLen is a byte-loop strlen used at seven places in igrmenu.c. ee_core.h includes <string.h> and ee_core already calls strlen (iopmgr.c, patches.c, util.c); the overlay defines its own strlen for igrmenu.c (igrovl_main.c:43-50, 'igrmenu.c's only libc call'), so strlen is linked in both builds anyway.
- Fix: Delete igrmLen and call strlen.
- Benefit: 7 lines; likely no ELF change since the compiler already emits a strlen call for the loop (INFERRED from the igrovl_main.c comment).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-435: nbns.h: IOP import macros and NBNS_RPC_ID_COUNT are unused

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/nbns.h:5`; `RiptOPL/include/nbns.h:20-25`
- Problem: nbnsman_IMPORTS_start/end and I_nbnsFindName appear only in nbns.h (no imports.lst or IRX imports nbnsman; the menu reaches nbns over RPC), and NBNS_RPC_ID_COUNT is never read.
- Fix: Delete the #ifdef _IOP import block and NBNS_RPC_ID_COUNT.
- Benefit: 7 lines removed; no binary change (INFERRED, macros only).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-436: thmLoad re-implements thmReadViewTemplate for the built-in theme

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/gfx/themes.cpp:1433-1440`; `RiptOPL/src/gfx/themes.cpp:1341-1351`
- Problem: The four-way configReadBuffer selection at 1433-1440 is the body of thmReadViewTemplate (1341-1351) with builtinView for view.
- Fix: Replace 1433-1440 with thmReadViewTemplate(themeConfig, builtinView).
- Benefit: 7 source lines removed; a few dozen bytes .text (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-437: vcdsupport.h: unused VCD_PREFIX_HDD and stale, orphan and repeated comments

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/include/vcdsupport.h:26`; `RiptOPL/include/vcdsupport.h:9`; `RiptOPL/include/vcdsupport.h:328-329`; `RiptOPL/include/vcdsupport.h:73-75`; `RiptOPL/include/vcdsupport.h:347-348`
- Problem: VCD_PREFIX_HDD ("", marked deferred) is referenced nowhere. Line 9 says the BDMA equip lives in system.c / opl.c, but vcdEquipBdma is declared here and defined in vcdsupport.cpp:1674. Lines 328-329 describe a render-thread getter that has no declaration after it (rowIdCached has its own comment at 334). The vcdInvalidateGameIds comment (73-75) is repeated word for word at 325-327, and 347 names a non-existent requestDisplayId.
- Fix: Delete VCD_PREFIX_HDD, the orphan comment at 328-329 and one of the duplicated invalidate comments; correct line 9 and the name at 347.
- Benefit: ~7 lines removed; header comments match the code. No binary effect.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-438: Unused or duplicate includes and a duplicate prototype in themeelems.cpp

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/gfx/themeelems.cpp:4-6`; `RiptOPL/src/gfx/themeelems.cpp:15`; `RiptOPL/src/gfx/themeelems.cpp:26`; `RiptOPL/src/gfx/themeelems.cpp:1104-1106`
- Problem: themeint.h is included twice (lines 4 and 6); themecatalog.h (class ThemeCatalog), sound.h (audio/sfx/bgm) and time.h are included but nothing in the file uses them; the local prototype of thmGetElemForItem at 1106 repeats themeint.h:95, which is already included.
- Fix: Delete the second themeint.h include, the themecatalog.h, sound.h and time.h includes, and the local prototype with its comment (1104-1106).
- Benefit: 7 lines removed; slightly faster compile; no ELF change.
- Effort: S. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-439: Commented-out code lines in igs_api.c, modmgr.c and padhook.c

- Category: 1 (Dead code)
- Location(s): `RiptOPL/ee_core/src/igs_api.c:63`; `RiptOPL/ee_core/src/modmgr.c:91-92`; `RiptOPL/ee_core/src/modmgr.c:103-104`; `RiptOPL/ee_core/src/padhook.c:1087`
- Problem: Dead alternatives kept as comments: an abandoned FindEmptyArea workaround, a disabled size round-up with its banner, a disabled SifWriteBackDCache with its explanation, and the old pointer increment above its replacement.
- Fix: Delete these commented-out lines (keep a one-line note at modmgr.c:103 if the write-back reasoning is wanted).
- Benefit: ~6 comment lines removed; no binary change
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-440: InitGSMConfig: the legacy per-game and global branches are the same five reads on different sets

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/core/gsm.cpp:73-86`
- Problem: Lines 73-79 read ENABLEGSM and the four GSM keys from configSet when $GSMSource is present and not MIXED; lines 81-86 read the same five keys the same way from configGame when $GSMSource is absent. The only difference is which set is read.
- Fix: Pick the set once (configSet if the source key was found, else configGame) and keep one copy of the five reads.
- Benefit: ~6 source lines removed (INFERRED from the two 6-line blocks); negligible ELF change
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-441: Redundant stores and always-true checks in texcache.cpp

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/gfx/texcache.cpp:34-37`; `RiptOPL/src/gfx/texcache.cpp:39`; `RiptOPL/src/gfx/texcache.cpp:41`; `RiptOPL/src/gfx/texcache.cpp:385`
- Problem: cacheClearItem memsets the whole entry (line 33) and then sets Mem, Vram, Clut, VramClut, qr and UID to zero again (34-37, 39, 41). At 385 `cache->suffix_ != NULL` is always true: cacheInitCache refuses a NULL suffix (line 50) and fails unless suffix_ was allocated (67-69).
- Fix: Delete the six zero stores after the memset (keep ClutStorageMode and lastUsed = -1) and the suffix_ NULL test.
- Benefit: ~6 lines removed; a few instructions per cleared slot (INFERRED). Trivial.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-442: Stale comments and leftovers in the dialog files

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/ui/cfgtitle.cpp:3`; `RiptOPL/src/ui/dia.cpp:891`; `RiptOPL/src/ui/dia.cpp:1177`; `RiptOPL/src/ui/dia.cpp:1320-1323`; `RiptOPL/include/dia.h:117-119`; `RiptOPL/include/dialogs.h:204-205`; `RiptOPL/src/ui/diaeditors.cpp:681-684`
- Problem: Comments name files that no longer exist (cfgtitle.c, dia.c, dialogs.c, gui.c); dia.cpp:1177 is a NOTE(rebuild) placeholder for a missing Debug-Colors HUD line; dia.cpp:1320-1323 narrates a past bug with a hard-coded '300 ms' that diaScrollDelay replaced; diaeditors.cpp ends with blank lines and an orphan separator comment.
- Fix: Fix the file names, delete the NOTE(rebuild) line and the bug narrative (one short line is enough, or none after finding 5), and delete the trailing separator.
- Benefit: ~6 lines removed; cosmetic (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-443: Stale comments in elfldr/loader.c, fontsystem.h, ioqueue.h and iosupport.h name files, flavours and plans that no longer exist

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/elfldr/loader.c:24-30`; `RiptOPL/elfldr/loader.c:140`; `RiptOPL/include/fontsystem.h:6`; `RiptOPL/include/ioqueue.h:6`; `RiptOPL/include/iosupport.h:22-24`
- Problem: loader.c:24-30 justifies the LOADFILE-first order by the '-WOPLSDK flavour', which no workflow or tool builds any more; loader.c:140 names src/launchdiag.c (now src/platform/launchdiag.cpp); fontsystem.h:6 and ioqueue.h:6 name src/fntsys.cpp and src/ioman.cpp (now src/gfx/ and src/platform/); iosupport.h:22-24 marks MMCE_MODE, FAV_MODE and UDPFS_MODE as 'reserved ... returns with checklist item', but mmcesupport.cpp, favsupport.cpp and udpfssupport.cpp are implemented.
- Fix: Delete the stale wording (keep one line in loader.c saying LOADFILE stays first for ioman-visible devices) and correct the two file paths.
- Benefit: ~8 lines of misleading comment; cosmetic.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-444: Stale comments in ethsupport.h, favsupport.h and ethsupport.cpp

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/include/ethsupport.h:22`; `RiptOPL/include/favsupport.h:13`; `RiptOPL/include/favsupport.h:160-164`; `RiptOPL/src/sources/ethsupport.cpp:1566-1571`; `RiptOPL/src/sources/cuesupport.cpp:10`
- Problem: ethDeinitModules is called 'otherwise unused' but is called from lwnbdctl.cpp:86 and EthSource::cleanUp/shutdown; favsupport.h names favsupport.c/opl.c/gui.c (now .cpp) and documents an isVcd parameter that is now `kind`; EthSource::image says texDiscoverLoad 'above' already tried, but the call is texDiscoverArt and keeps a needless temporary; cuesupport.cpp cites vcdsupport.c.
- Fix: Correct or delete these comments; return texDiscoverArt(...) directly in EthSource::image.
- Benefit: ~6 lines removed, comments made accurate (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-445: bdmWaitForDevice re-implements bdmDeviceIsPresent's Dopen probe

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/bdmdevices.cpp:458-497`
- Problem: Both functions format mass%d:/ and Dopen/Dclose it; the wait loop repeats the probe inline. Each has one caller (bdmsupport.cpp:2294 and 2304).
- Fix: Make bdmWaitForDevice loop on bdmDeviceIsPresent(deviceId).
- Benefit: ~6 lines removed
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-446: config.cpp re-checks conditions its only callers already guarantee

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/core/config.cpp:1117-1118`; `RiptOPL/src/core/config.cpp:1284-1285`; `RiptOPL/src/core/config.cpp:1322-1323`; `RiptOPL/src/core/config.cpp:1639`
- Problem: configReadFileBuffer re-checks fileBuffer/configSet for NULL although both callers (configReadBuffer 1186-1194, configRead 1421, 1479) return earlier on NULL. configBuildWoplPath re-checks path although its only call (1439) is under filename() != NULL (1436). configOpenFailureIsAbsent re-checks path although its only call (1347) follows the NULL return at 1339. At 1639, intended != NULL already implies the !singleOpenDevice && not-pfs test, because intended is only allocated under that same test at 1608.
- Fix: Delete the three NULL checks and reduce the 1639 condition to !ok && intended != NULL.
- Benefit: ~6 source lines removed; a few bytes of ELF (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-447: rahash.cpp: hex digit table and lowercase-hex validation each written twice

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/rahash.cpp:171-181`; `RiptOPL/src/sources/rahash.cpp:877`; `RiptOPL/src/sources/rahash.cpp:897-901`; `RiptOPL/src/sources/rahash.cpp:915-917`; `RiptOPL/src/sources/rahash.cpp:928-930`
- Problem: raVcdWatchKey re-declares the hex table and loop of md5_hex to produce 14 of its digits; raVcdGuardPath and raVcdValidContentHash run the same isxdigit/!isupper loop over 14 and 32 characters.
- Fix: In raVcdWatchKey call md5_hex into a 33-byte temp and copy 14 characters after 'P'; give the two validators one shared lower-hex loop (or have raVcdGuardPath call a length-parameterised raVcdValidContentHash).
- Justification: Reuses the existing md5_hex; the shared validator replaces two identical loops, so code shrinks.
- Benefit: ~6 lines removed (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-448: sbNeutrinoDeinitException only forwards to sbLoaderDeinitException

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/include/supportbase.h:130-131`; `RiptOPL/src/sources/supportbase.cpp:2085-2088`
- Problem: The 'backward-compatible' wrapper returns sbLoaderDeinitException(neutrinoPath) unchanged; its eight callers (bdmlaunch.cpp:499, ethsupport.cpp:1275, mmcesupport.cpp:1675, hddsupport.cpp:2443,2453,2502, udpfssupport.cpp:1186) could call the real function.
- Fix: Delete the wrapper and its prototype, rename the eight calls to sbLoaderDeinitException, and update the string match in .github/scripts/test_handoff_probe.py:74.
- Benefit: ~6 source lines removed; no meaningful ELF change (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-449: sbNeutrinoDeinitException only forwards to sbLoaderDeinitException

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/sources/supportbase.cpp:2085-2088`; `RiptOPL/include/supportbase.h:130-131`
- Problem: The function body is return sbLoaderDeinitException(neutrinoPath); the header calls it a backward-compatible wrapper. Its 7 callers (bdmlaunch.cpp:499, ethsupport.cpp:1275, hddsupport.cpp:2443,2453,2502, mmcesupport.cpp:1675, udpfssupport.cpp:1186) can call sbLoaderDeinitException directly, as the VCD legs already do.
- Fix: Replace the 7 calls with sbLoaderDeinitException and delete the wrapper and its prototype; update the string match in .github/scripts/test_handoff_probe.py:74.
- Benefit: 6 lines removed (READ); no ELF change worth stating.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-450: siNameLaunchable re-implements cueNameLaunchable, which saveicon.cpp already includes

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/gfx/saveicon.cpp:650-655`; `RiptOPL/src/sources/cuesupport.cpp:94-119`
- Problem: siNameLaunchable (saveicon.cpp:651-655) applies exactly cueNameLaunchable's rules (non-empty, not '.', not starting with '..', no '/', '\\' or ':', length <= CUE_NAME_LAUNCH_MAX), and its own comment names cueNameLaunchable. saveicon.cpp already includes include/cuesupport.h (line 645), which declares it (cuesupport.h:143), and cuesupport.cpp is in the main ELF.
- Fix: Delete siNameLaunchable and call cueNameLaunchable at saveicon.cpp:875.
- Benefit: ~6 lines removed; a few dozen bytes of .text (INFERRED, static function body).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-451: BdmSource::textId/iconId reclassify the driver string with strcmp chains although bdmDeviceType is already stored

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/sources/bdmsupport.cpp:2100-2140`
- Problem: Both functions run up to five bdmDriverIs* strcmp tests on pDeviceData->bdmDriver, which is the same classification bdmDetermineDeviceType already stored in bdmDeviceType when the driver was read (bdmdevices.cpp:347); textId even mixes in a bdmDeviceType test for PCCF. The comment at 2117 names a 'bdmGetIconId' that no longer exists.
- Fix: Switch on pDeviceData->bdmDeviceType in both functions and drop the stale name in the comment.
- Benefit: ~6 source lines removed and no string compares on each menu draw call of the tab label/icon (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-452: dir + name joining with the trailing '/' or ':' test is written inline four more times beside configBuildPath and fmJoin

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/core/config.cpp:618-628`; `RiptOPL/src/core/diagsave.cpp:30-31`; `RiptOPL/src/core/lang.cpp:372-376`; `RiptOPL/src/core/lang.cpp:431-435`; `RiptOPL/src/core/filemgr.cpp:102-112`
- Problem: configBuildPath (static) and fmJoin (public) both join a directory and a name, adding '/' only when the directory does not already end in '/' or ':' (fmJoin also accepts '\\' and uses '\\' on smb). diagSave, lngScanBootDirectory and lngInitFallbackFont each re-implement the same end-character test inline. Edge cases differ slightly: an empty prefix gives a bare name in configBuildPath and diagSave but "/name" in fmJoin.
- Fix: Export one of the existing helpers (configBuildPath semantics, or fmJoin moved next to the other util path code) and call it from diagsave.cpp and the two lang.cpp sites instead of the inline copies.
- Benefit: ~6 source lines removed (INFERRED: three inline 2-4 line blocks become single calls; no new helper is written)
- Effort: S. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-453: igrovlFlushCache is a forwarding wrapper that exists only because the overlay API types FlushCache as returning int

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/ee_core/src/igrovl_stub.c:25-29`; `RiptOPL/ee_core/src/igrovl_stub.c:40`; `RiptOPL/ee_core/include/igrovl.h:40`
- Problem: igrovl_api_t declares 'int (*FlushCache)(int op)', so igrovl_stub.c wraps the kernel's void FlushCache in igrovlFlushCache returning 0. No caller in the overlay uses the result: igrmenu.c and igrmbox.c only call FlushCache(0) as a statement (via igrovl_side.h:26).
- Fix: Type the field as void (*FlushCache)(int op) in igrovl.h, put FlushCache itself in igrovlApi, and delete igrovlFlushCache.
- Benefit: 6 lines and one small function in ee_core's RA editions (~16 bytes, INFERRED).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-454: thmBlockPresent re-implements thmViewBlockPresent's presence rule

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/gfx/themes.cpp:828-848`; `RiptOPL/src/gfx/themeviews.cpp:52-64`
- Problem: Both build '<name>_enabled' and '<name>_type' keys and apply the same rule (switched off = present, else present iff a _type exists); thmBlockPresent only adds the unfiltered-ItemsList claimsSlot flag.
- Fix: Have thmBlockPresent return 0 when thmViewBlockPresent is 0 and compute claimsSlot from the _type lookup only.
- Benefit: ~6 source lines removed (INFERRED); trivial .text.
- Effort: S. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-455: Five functions exported through headers but used only inside guisettings.cpp

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/gui.h:142`; `RiptOPL/include/gui.h:156`; `RiptOPL/include/gui.h:171`; `RiptOPL/include/guiint.h:20-21`; `RiptOPL/src/ui/guisettings.cpp:419`; `RiptOPL/src/ui/guisettings.cpp:449`; `RiptOPL/src/ui/guisettings.cpp:823`; `RiptOPL/src/ui/guisettings.cpp:1515`; `RiptOPL/src/ui/guisettings.cpp:1821`
- Problem: guiShowColorsConfig, guiShowNetConfig, guiShowPopsNetConfig, guiCopyNameList and guiFreeNameList have no caller outside guisettings.cpp; the header prototypes are left over from before the split out of gui.cpp.
- Fix: Make the five functions static and delete their five prototypes from gui.h and guiint.h.
- Benefit: 5 header lines removed; lets the compiler inline single-use code (no measurable ELF change; --gc-sections already used per BUILD-EVIDENCE.md:146) (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-456: HddSource::cleanUp repeats the list-free block in its abandoned-art branch

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/hddsupport.cpp:2984-2997`
- Problem: Both branches free hdlGames, free the VCD list and call hddDvrCleanUp; they differ only in the keepMounts argument and in whether the unmounts and cleanUpSupportModules follow.
- Fix: Do the frees once with hddDvrCleanUp(gArtAbandoned ? 1 : exception & (KEEP_DVR_MOUNTS_EXCEPTION | KEEPIOP_EXCEPTION)), then return early when gArtAbandoned before the unmounts.
- Benefit: ~5 lines removed (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-457: MmceSupport::artPrimary is always a plain copy of prefix

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/sources/mmcesupport.cpp:630-648`; `RiptOPL/src/sources/mmcesupport.cpp:842`; `RiptOPL/src/sources/mmcesupport.cpp:1768`; `RiptOPL/include/mmceint.h:62`
- Problem: refreshArtRoots clears artPrimary, appends '/' to prefix, then copies prefix into artPrimary; artPrimary is read only by MmceSource::image. It is rewritten at the same points as prefix, so it is not a race-safe snapshot either, and the plural 'ArtRoots' is left from a removed multi-root art search.
- Fix: Delete artPrimary; keep only the trailing-slash normalisation (rename refreshArtRoots to something like normalisePrefix) and pass prefix (or a local copy) to the art loader.
- Benefit: 40 bytes .bss (char artPrimary[40], INFERRED) and ~5 source lines removed.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-458: Small redundant checks in drawGameImage and initMutableImage

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/gfx/themeelems.cpp:1234-1239`; `RiptOPL/src/gfx/themeelems.cpp:1247-1249`; `RiptOPL/src/gfx/themeelems.cpp:1257-1259`; `RiptOPL/src/gfx/themeelems.cpp:860-869`
- Problem: drawGameImage opens 'if (isBackground)' twice in a row (1234 and 1239); selImg and cfImg are tested for NULL right after covElem->extended / cfElem->extended were tested non-NULL; initMutableImage has two consecutive identical 'if (type != ELEM_TYPE_BACKGROUND)' blocks.
- Fix: Merge the two isBackground blocks and the two type != BACKGROUND blocks; delete the 'selImg != NULL &&' and 'cfImg != NULL &&' terms.
- Benefit: ~5 source lines removed; no ELF change expected (INFERRED, the compiler folds these).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-459: Stale or garbled comments in rawatch.cpp and supportbase.h

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/sources/rawatch.cpp:119-121`; `RiptOPL/include/supportbase.h:48-51`; `RiptOPL/include/supportbase.h:57`; `RiptOPL/include/supportbase.h:99-103`
- Problem: rawatch.cpp:119-121 says CI builds 'make release'; CI builds with ps2build and __OPLDIAG is a ps2build configuration (brief: build_configuration.py:45-52). supportbase.h:48-51 is a broken sentence; :57 points at supportbase.c (the file is supportbase.cpp); :99-103 put the sbNeutrinoLoadCheats description above the sbNeutrinoHandCheats prototype, so the two comments describe the wrong lines.
- Fix: Delete the 'make release' rationale, fix the sbReadList sentence and file name, and put each Neutrino cheats comment on its own prototype (or drop the duplicate wording).
- Benefit: ~5 lines removed; clarity only
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-460: bdmIsReadOnlyPath re-parses 'massN:' instead of using bdmParseMassSlot

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/sources/bdmsupport.cpp:887-896`; `RiptOPL/include/bdmtyped.h:37-55`
- Problem: bdmIsReadOnlyPath hand-rolls the 'mass' prefix and digit parse that bdmParseMassSlot in bdmtyped.h (already included at bdmsupport.cpp:11) provides; its loop also accumulates any number of digits into a signed int, which bdmParseMassSlot bounds to two.
- Fix: Replace the body with slot = bdmParseMassSlot(path, NULL); return slot >= 0 && bdmModeIsReadOnly(BDM_MODE + slot). The source-extracting test .github/scripts/test_udpbd_read_only.py:57 must then also include bdmtyped.h.
- Benefit: ~5 source lines removed (INFERRED); removes a signed-overflow on a path with a long digit run.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-461: hddDvrScan: redundant gEnableDVR re-check, a root copy and a counter that only feeds a LOG

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/sources/hddsupport.cpp:96-97`; `RiptOPL/src/sources/hddsupport.cpp:222`; `RiptOPL/src/sources/hddsupport.cpp:241`; `RiptOPL/src/sources/hddsupport.cpp:249-253`; `RiptOPL/src/sources/hddsupport.cpp:265`; `RiptOPL/src/sources/hddsupport.cpp:271`
- Problem: hddDvrEnsureDrivers has one caller, hddDvrScan, which has already returned when !gEnableDVR, so the check at 96-97 is dead. root[32] is a plain copy of roots[part]; `scanned` is only printed by a LOG that release builds drop.
- Fix: Delete the gEnableDVR test in hddDvrEnsureDrivers, use roots[part] directly instead of root[], and drop `scanned` (log the fixed 6 directories if needed).
- Benefit: ~5 lines removed (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-462: loadCoreSupportModules re-tests supportModulesLoaded after already returning on it

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/sources/hddsupport.cpp:982-986`; `RiptOPL/src/sources/hddsupport.cpp:1018`; `RiptOPL/src/sources/hddsupport.cpp:1072`
- Problem: The function returns 1 at 985-986 when supportModulesLoaded is set, and nothing between there and 1018 sets it, so `if (!supportModulesLoaded)` at 1018 is always true; the comment at 982-984 still describes the guard as covering only the module loads.
- Fix: Delete the redundant if and its closing brace (de-indent the block) and the comment lines 982-984.
- Benefit: ~5 lines removed; no ELF change (compiler folds it, INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-463: normalizeMcBootDir is a one-caller wrapper for two calls

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/app/opl.cpp:2010-2017`; `RiptOPL/src/app/opl.cpp:2088`
- Problem: normalizeMcBootDir only calls resolveMcWildcardBootDir() and normalizeMcPath(gBootDir, ...) and has a single caller in main.
- Fix: Inline the two calls at main:2088 and delete the wrapper, keeping its comment there.
- Benefit: ~5 lines removed; no ELF change, the static wrapper is already inlined (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-464: psbbnflow pageName and grid.cpp:482 re-implement menuItemGetText

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/gfx/psbbnflow.cpp:187-191`; `RiptOPL/src/gfx/grid.cpp:482`
- Problem: Both compute text_id >= 0 ? _l(text_id) : text, which is menuItemGetText (menusys.cpp:1762). The comment at psbbnflow.cpp:187 says this avoids calling up into menusys, but the same theme layer already calls menuItemGetText (themeelems.cpp:1887).
- Fix: Delete pageName and call menuItemGetText at psbbnflow.cpp:235,237,240 and grid.cpp:482.
- Benefit: ~5 lines removed, INFERRED.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-465: saveIconFromPs1CardImage serial matching is used only by the host test

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/gfx/saveicon.cpp:588-589`; `RiptOPL/src/gfx/saveicon.cpp:601-602`; `RiptOPL/src/gfx/saveicon.cpp:897`; `RiptOPL/include/saveicon.h:80-83`
- Problem: The only shipped caller passes serial = NULL (saveicon.cpp:897), so the two-pass loop (589) and the folder-name match (601-602) never run in the ELF; only .github/scripts/test_ps1_save_icons.py:125 passes a serial.
- Fix: Drop the serial parameter, the pass loop and the match, and update the header comment and the host test call.
- Benefit: ~5 lines removed; no measurable ELF change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-466: suNetworkUp repeats netTimeFetch's network bring-up and 40 x 250 ms DHCP wait loop

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/ui/selfupdate.cpp:210-225`; `RiptOPL/src/sources/nettime.cpp:141-153`
- Problem: selfupdate.cpp:217-224 and nettime.cpp:145-153 are the same sequence: ethLoadInitModules(), then up to 40 polls of ethGetNetConfig for a non-zero IP with DelayThread(250 ms), with the same comment. The nettime.cpp:141-144 comment also claims modules loaded by Check for Updates keep ps2ip's start-up address, but suNetworkUp calls the same ethLoadInitModules(), so that remark is stale.
- Fix: Put the load-and-wait loop once in ethsupport.cpp next to ethGetNetConfig (returning 0 or -1) and call it from both; drop the stale 'another path (Check for Updates)' sentence in nettime.cpp.
- Justification: Two identical copies exist in the main ELF; one helper is smaller than two copies and keeps the timeout in one place.
- Benefit: ~5 source lines net and a stale comment fixed (INFERRED: two 8-line copies become one ~9-line helper and two calls); a few dozen bytes of .text in updater builds (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-467: Dotted-quad IP read/write repeated 6x and 5x in _loadConfig/_saveConfig (plus 3x in config.cpp)

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/app/settingsio.cpp:1930-1931`; `RiptOPL/src/app/settingsio.cpp:1935-1936`; `RiptOPL/src/app/settingsio.cpp:1949-1956`; `RiptOPL/src/app/settingsio.cpp:2276-2283`; `RiptOPL/src/app/settingsio.cpp:2289-2293`; `RiptOPL/src/core/config.cpp:760-765`
- Problem: Each IP key is read with its own configGetStr + sscanf("%d.%d.%d.%d") pair and written with its own snprintf + configSetStr pair; the same write pair appears three more times in config.cpp's IPCONFIG.DAT import. Only the RA host IP (1531-1540) range-checks the result.
- Fix: Add one configGetIp(set, key, int[4]) and one configSetIp(set, key, const int[4]) next to configGetStr/configSetStr and use them at every site.
- Justification: Eleven two-line pairs in settingsio.cpp and three in config.cpp become one-line calls (~14 lines saved) for ~10 lines of helpers, a small net reduction; the getter can also carry the 0-255 check the RA copy already does, at no extra cost per site.
- Benefit: ~4-6 net source lines and a few hundred bytes of repeated call sequences (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-468: Stale includes of folderbrowse.h (4 files) and hddsupport.h (artload.cpp) that use nothing from them

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/sources/appscan.cpp:43`; `RiptOPL/src/app/autolaunch.cpp:42`; `RiptOPL/src/app/settingsio.cpp:46`; `RiptOPL/src/app/lwnbdctl.cpp:43`; `RiptOPL/src/gfx/artload.cpp:19`
- Problem: Four files include folderbrowse.h with the copied comment 'folderDepth -- favourites suppression inside subfolders' but reference no folder* function or FOLDER_* constant; folderbrowse.h includes nothing, so nothing depends on it transitively. artload.cpp includes hddsupport.h 'for hddGetArtArchivePath' but never calls it (texcache.cpp does).
- Fix: Delete the four folderbrowse.h includes. Delete the hddsupport.h include in artload.cpp after a build confirms it does not rely on the headers hddsupport.h pulls in (iosupport.h, hdd.h, mcemu.h).
- Benefit: 5 lines removed; less rebuild coupling; no ELF change (INFERRED)
- Effort: S. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-469: texcache.cpp include list copied from artload.cpp, with headers whose symbols it never uses

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/gfx/texcache.cpp:7-16`; `RiptOPL/src/gfx/artload.cpp:6-15`
- Problem: texcache.cpp lines 1-22 are the same include block as artload.cpp 1-21 (the file was split from it). texcache.cpp uses none of what several of them are commented as providing: pad.h ('getKeyPressed') and sound.h ('BGM low-water admission') have no matching use, and there is no tar*, artIndex*, tex*() or io*() call in the file.
- Fix: Delete the includes texcache.cpp does not need (at least pad.h, sound.h, tar.h, artindex.h; check textures.h, ioman.h, util.h with a compile) and their misleading comments.
- Benefit: ~5 lines removed; slightly faster compile; no ELF change (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-470: CAP_SOURCE_ID / hasSourceId duplicate the default GameSource::sourceId

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/include/gamesource.h:21`; `RiptOPL/include/gamesource.h:92`; `RiptOPL/src/sources/gamesource.cpp:33`; `RiptOPL/src/ui/libview.cpp:158-163`
- Problem: The only hasSourceId caller, libListSourceId, returns id when the flag is clear, which is exactly what the base GameSource::sourceId returns. Every source that sets CAP_SOURCE_ID (APP, ETH, BDM, HDD, MMCE, UDPFS) overrides sourceId, and the two that do not (HTTP, FAV) use the base default, so the flag never changes the result.
- Fix: Delete CAP_SOURCE_ID and hasSourceId, call the virtual directly in libListSourceId, and drop the flag from the six constructors.
- Benefit: ~4 lines plus six constructor flag terms removed; no behaviour change (READ)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-471: CfgTitles has a free-function forwarding layer; cfgTitleForRow and cfgTitleClean are used only by a host test

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/ui/cfgtitle.cpp:268-274`; `RiptOPL/src/ui/cfgtitle.cpp:230`; `RiptOPL/include/cfgtitle.h:35-41`
- Problem: Each public CfgTitles method is mirrored by a cfgTitle* wrapper over the single global gCfgTitles. Production uses only cfgTitleRefresh, cfgTitleForget (opl.cpp), cfgTitleDisplayName (menusys.cpp, host.cpp) and cfgTitleSortName (menusys.cpp); cfgTitleForRow and cfgTitleClean are called only inside cfgtitle.cpp and by .github/scripts/test_cfg_titles.py.
- Fix: Delete the cfgTitleForRow and cfgTitleClean wrappers and their prototypes, call CfgTitles::clean directly at cfgtitle.cpp:230, and point the host test at gCfgTitles.forRow / CfgTitles::clean.
- Benefit: ~4 lines removed; a smaller public surface (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-472: GSM_1080P is defined by every ps2build build, but gsm_engine comments still call DTV_1080P a variant-only build

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/ee_core/src/gsm_engine.S:182`; `RiptOPL/ee_core/src/gsm_engine.S:204-205`; `RiptOPL/ee_core/src/gsm_engine_adv.S:138-139`; `RiptOPL/ps2.yaml:609`; `RiptOPL/ps2.yaml:644`
- Problem: ps2.yaml puts GSM_1080P in the base defines of eecore and the main ELF, and no tools/ script removes it, so every shipped build compiles DTV_1080P. The comments say it is 'experimental 1080p variant build only' and 'absent from every default build', which is false and misleads readers about what ships.
- Fix: Rewrite the three comments to state the code is in every build (or delete the 'variant only' sentences). The #ifdef GSM_1080P gates themselves only serve the legacy Makefile's GSM1080P=0 escape hatch; drop them together with the matching gates in src/ if that hatch is retired.
- Benefit: Removes ~4 lines of false comment; up to 4 more lines (the #ifdef/#endif pairs here) if the gate is retired; no ELF change.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-473: RA #ifdef in updateMenuFromGameList duplicates the row-text assignment

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/app/opl.cpp:796-810`
- Problem: Both branches of the RETROACHIEVEMENTS #ifdef assign gup->submenu.text with the same expression (801 and 809); only the raText/raBadged lines are RA-specific.
- Fix: Assign submenu.text once before the #ifdef and keep only the badge lines inside it; the #else branch disappears.
- Benefit: ~4 lines removed; no ELF change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-474: Stale comments in config.cpp: upstream FIXME, a false claim about re-emitting empties, and references to deleted C files/functions

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/core/config.cpp:17-18`; `RiptOPL/src/core/config.cpp:792-793`; `RiptOPL/src/core/config.cpp:918-920`; `RiptOPL/src/core/config.cpp:1536`
- Problem: The FIXME at 17-18 asks to replace fileXioGetStat with newlib stat for GMT time, but the only use (1513) is an existence check. 792-793 says configWrite re-emits empty values for wOPL keys via cfgWriteLibconfigLine; cfgWriteLibconfig (1059-1113) only walks stored entries and empties are dropped on read (865), so nothing is re-emitted. 918-920 cite guigame.c:1059 and config.c, and 1536 cites system.c sysSyncNeutrinoIp; those C files are gone and sysSyncNeutrinoIp exists nowhere in src/.
- Fix: Delete the FIXME, delete the false sentence at 792-793, and drop the dead file:line and function references (or point them at the current .cpp names).
- Benefit: ~4 comment lines removed or corrected; no binary change
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-475: settings.cpp: two adjacent #ifdef RETROACHIEVEMENTS blocks, stale file references, and screenPosStore/screenPosRecall exported but file-local

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/core/settings.cpp:64-67`; `RiptOPL/src/core/settings.cpp:3-5`; `RiptOPL/include/settings.h:7`; `RiptOPL/src/core/settings.cpp:257-274`; `RiptOPL/include/opl.h:430-431`
- Problem: Lines 66-67 close and reopen the same #ifdef RETROACHIEVEMENTS. The header comment says the special-case load/save code stays in opl.cpp; it is in src/app/settingsio.cpp (settingsLoad at :1571, settingsSave at :2223). settings.h:7 and opl.cpp:1485 point to src/settings.cpp (the file is src/core/settings.cpp). screenPosStore and screenPosRecall are called only inside settings.cpp but are declared in opl.h.
- Fix: Merge the two #ifdef blocks, correct the file names in the comments, make screenPosStore/screenPosRecall static and drop their opl.h prototypes.
- Benefit: ~4 lines; no ELF change (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-476: udpfssupport.h exposes the UDPFS backend's private cache class, VMC struct and mcemu.h to about 25 includers

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/include/udpfssupport.h:4-69`
- Problem: udpfs_vmc_infos_t, class UdpfsSupport, the UdpfsSource forward declaration, extern gUdpfsSupport and UDPFS_MODE_UPDATE_DELAY are used in src/ only by udpfssupport.cpp, yet the header (and with it include/mcemu.h and <time.h>) is included by about 25 files, most of which want only udpfsGetModulesLoaded.
- Fix: Move those declarations and the mcemu.h/<time.h> includes into udpfssupport.cpp, leaving the header with the five function prototypes and udpfsShowPendingNotice; update the header regexes in test_udpfs_files_oplcore.py:203 and the tests that read the header.
- Benefit: ~4 lines removed net (extern, forward declaration, two includes); smaller public interface and fewer recompiles. No ELF change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-477: writeConfigPathRedirect(configGetDir()) after an alternate-device save can never write

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/app/settingsio.cpp:2452-2458`; `RiptOPL/src/app/settingsio.cpp:340-343`
- Problem: The call at 2458 sits inside if (gBootDir[0] == '\0'), and writeConfigPathRedirect returns 0 at 340-343 exactly when gBootDir is empty, because it deliberately refuses relative writes. The comment promises that the next boot skips the hunt, but that never happens: each bare-launch save only logs a refusal.
- Fix: Delete the call and its two-line comment.
- Benefit: 4 lines removed; one LOG line per bare-launch save gone (READ)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-478: HttpSource constructor stores MODE_FLAG_NO_UPDATE in enabled, so HTTP reads as enabled before init

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/sources/httpsupport.cpp:510-516`; `RiptOPL/src/ui/guisettings.cpp:593-595`
- Problem: The constructor sets enabled = MODE_FLAG_NO_UPDATE (0x04, iosupport.h:94) to preserve an old positional-initializer slip, as its own comment says. Every other source starts disabled and sets enabled = 1 in init. guiRefreshCfgTitles tests http->enabled and so queues an HTTP list update even when the HTTP source was never initialised.
- Fix: Delete the constructor body (lines 512-515) so enabled starts at 0 like the other sources; init() already sets it to 1.
- Benefit: 4 lines removed and one spurious deferred HTTP update request avoided on settings apply when HTTP is not started (INFERRED: HttpSource::update returns early when the protocol is not HTTP).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-479: MODE_FLAG_NO_UPDATE is a retired flag that is set in three places and never tested

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/iosupport.h:94`; `RiptOPL/src/sources/appsupport.cpp:598`; `RiptOPL/src/sources/favsupport.cpp:974`; `RiptOPL/src/sources/httpsupport.cpp:513-515`
- Problem: The macro's own comment says the updater bit is retired. It is OR'd into the APP flags (appsupport.cpp:598, favsupport.cpp:974) and assigned to HttpSource::enabled (httpsupport.cpp:515), but no code tests flags & MODE_FLAG_NO_UPDATE.
- Fix: Delete the macro, drop it from the two flag expressions, and in HttpSource write the plain nonzero value instead (after checking enabled is only tested for truth), deleting the comment at 513-514.
- Benefit: ~4 lines and one misleading flag; no size change (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-480: PS2 logo and ZSO-on-HDD reads issue one fileXio devctl per 512-byte sector

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/platform/util.cpp:799-806`; `RiptOPL/src/platform/zso.c:14-23`
- Problem: CheckPS2Logo's HDD branch reads the 24 KB logo area with 48 hddReadSectors(lba + k, 1, ...) calls, and zso.c ReadHDDSectors reads every 2048-byte ISO sector as four single-sector calls. hddReadSectors takes a sector count (hdd.cpp:101-112) and rahash.cpp already reads 16 sectors (8 KB) per call to amortise the EE/IOP round trip. Runs on the launch path of HDD games (hddsupport.cpp:2790-2799).
- Fix: Read in chunks of up to 16 HDD sectors per hddReadSectors call, as rahash.cpp does: 3 calls for the logo instead of 48, one call per ISO sector in ReadHDDSectors instead of four.
- Benefit: 48 -> 3 fileXio RPCs for the HDD logo check and 4x fewer for menu-side ZSO-on-HDD reads; a few tens of ms per HDD launch (INFERRED, not measured); ~4 lines shorter.
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-481: Screenshot() asks DMA channel 1 for 8192 qwords when the frame is under 128 KB, then divides by slices == 0

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/ee_core/src/igs_api.c:193-197`; `RiptOPL/ee_core/src/igs_api.c:272-306`
- Problem: slices = qwords >> 13 is 0 for any frame below 131072 bytes (for example 256x240 PSMCT16 = 122880 B, or the 64x64 minimum that the range checks at 193-197 accept). The do-while still runs once with QWC = 8192 + 0, so the channel waits for more data than the BITBLT sends and would write up to 128 KB past the buffer, and line 304 then evaluates 255 * i / slices with slices 0 (GCC MIPS traps on integer division by zero by default).
- Fix: Loop over the remaining qword count, sending min(remaining, 8192) per pass, and drop the slices/remainder arithmetic and the fade division with it.
- Benefit: Fixes a hang with interrupts disabled (DI at 683) on small frames; the loop gets ~4 lines shorter (INFERRED: over-long VIF1 read never completing is hardware behaviour not tested here).
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-482: include/httpsupport.h is included by four UI files that use nothing from it

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/httpsupport.h:80-88`; `RiptOPL/src/ui/guiwait.cpp:30`; `RiptOPL/src/ui/guimodal.cpp:31`; `RiptOPL/src/ui/guigameid.cpp:43`; `RiptOPL/src/ui/gui.cpp:37`
- Problem: Each of these four files includes httpsupport.h with the copied comment 'httpTestServer() -- the Network page Test button', but none of them references httpTestServer, httpGetObject, httpNormalizeBasePath, gHttpSupport or any HTTP_ constant; only guisettings.cpp (593, 627, 632) uses them.
- Fix: Delete the four include lines (all four include opl.h directly; a build confirms nothing else was pulled in transitively).
- Benefit: 4 lines removed; less header parsing; no ELF change
- Effort: S. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-483: AppServices::applyConfig is never called; host.h header comments are stale

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/host.h:68`; `RiptOPL/src/app/host.cpp:81`; `RiptOPL/.github/scripts/hostmap.py:19`; `RiptOPL/include/host.h:5-6`; `RiptOPL/include/host.h:50`
- Problem: No code calls host::app().applyConfig (0 matches for '.applyConfig(' in src, tests and .github), yet the vtable keeps OplApp::applyConfig (16 B in nm). The header says 'three small interfaces' in 'src/host.cpp' while there are four, in src/app/host.cpp, and line 50's 'The GUI-wide lock' comment is detached from lock()/unlock() at 55-56.
- Fix: Delete applyConfig from AppServices, OplApp and hostmap.py; fix the header comment to four interfaces in src/app/host.cpp and move the lock comment above lock().
- Benefit: 16 B text + 4 B vtable slot (MEASURED: _ZN..OplApp11applyConfigEiii 0x10 in nm); ~3 lines.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-484: BootStatus::active_ duplicates text_[0] != 0; guiboot.h names a wrong source path

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/include/guiboot.h:6`; `RiptOPL/include/guiboot.h:21`; `RiptOPL/src/ui/guiboot.cpp:18`; `RiptOPL/src/ui/guiboot.cpp:23`; `RiptOPL/src/ui/guiboot.cpp:50`
- Problem: set(NULL) clears both text_ and active_, set(s) sets both, and line() requires active_ AND text_[0] != 0, so active_ never changes the result. The header points at src/guiboot.cpp; the file is src/ui/guiboot.cpp.
- Fix: Delete active_ and test text_[0] alone; fix the path in the comment.
- Benefit: 3 lines removed, 1 byte of state (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-485: DeferredOpQueue serial counter and enqueue return value are never read

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/guiqueue.h:15-17`; `RiptOPL/include/guiqueue.h:32`; `RiptOPL/src/ui/guiqueue.cpp:13`; `RiptOPL/src/ui/guiqueue.cpp:55`
- Problem: enqueue returns scheduled_++ (or -1 on OOM), passed back through Gui::defer (gui.cpp:187) and guiDeferUpdate (gui.cpp:490), but all nine guiDeferUpdate call sites discard the result.
- Fix: Delete scheduled_ and make enqueue/defer/guiDeferUpdate return void.
- Benefit: ~3 lines and one int field removed (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-486: Inline confirm/cancel ternaries re-implement guiConfirmKey/guiCancelKey/guiConfirmIcon/guiCancelIcon defined in the same file

- Category: 2 (Duplication)
- Location(s): `RiptOPL/src/ui/guimodal.cpp:130-132`; `RiptOPL/src/ui/guimodal.cpp:152-154`; `RiptOPL/src/ui/guimodal.cpp:292-294`; `RiptOPL/src/ui/guimodal.cpp:316-319`; `RiptOPL/src/ui/guimodal.cpp:430`; `RiptOPL/src/ui/guioverlay.cpp:75-83`; `RiptOPL/src/ui/gui.cpp:399`; `RiptOPL/src/ui/gui.cpp:418`; `RiptOPL/src/ui/guisettings.cpp:3448`; `RiptOPL/src/ui/guisettings.cpp:3478-3479`
- Problem: guimodal.cpp:192-210 defines the confirm/cancel key and icon helpers (their comment says 'Everything asks here instead'), yet MessageBoxScreen, RemoveSettingsPrompt, CheatScreen and Overlays::drawSubMenuHints still spell out 'gSelectButton == KEY_CIRCLE ? ...' (about 30 such ternaries repo-wide, e.g. menusys.cpp, diaeditors.cpp, dia.cpp, themeelems.cpp).
- Fix: Replace the ternaries with guiConfirmKey()/guiCancelKey()/guiConfirmIcon()/guiCancelIcon(); in drawSubMenuHints drop the icon array and pass the two helpers directly.
- Benefit: Simpler, consistent button mapping; ~3 lines removed in guioverlay.cpp, otherwise line-neutral (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-487: PadManager::initialize always returns 1, so startOne's failure check is dead

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/core/pad.cpp:188`; `RiptOPL/src/core/pad.cpp:209`; `RiptOPL/src/core/pad.cpp:296`; `RiptOPL/src/core/pad.cpp:1144-1146`; `RiptOPL/include/padmanager.h:62`
- Problem: Every return in initialize() (lines 209, 235, 248, 256, 296) returns 1. Its two callers are startOne (1144-1146, 'if (!initialize(pad)) return 0;', never taken) and read() at 943, which ignores the value.
- Fix: Make initialize() void and delete the check in startOne.
- Benefit: ~3 lines; compiler likely folds the check already (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-488: Unused names in hddsupport.h: APA_IOCTL2_GETHEADER, HDD_LOADMODULES_STATUS_COUNT, hdd_loadmodules_status

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/hddsupport.h:16-17`; `RiptOPL/include/hddsupport.h:79-80`
- Problem: APA_IOCTL2_GETHEADER, the enum value HDD_LOADMODULES_STATUS_COUNT and the typedef name hdd_loadmodules_status appear nowhere else in the repo; hddLoadModules returns int and callers compare against the other enum values.
- Fix: Delete the macro and its comment, the COUNT value, and the typedef name (leave the enum anonymous).
- Benefit: 3 lines removed; no ELF change (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-489: eecoreInit takes argc/argv it never reads and always returns 0, used as an argv offset

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/ee_core/src/main.c:40`; `RiptOPL/ee_core/src/main.c:165`; `RiptOPL/ee_core/src/main.c:170`; `RiptOPL/ee_core/src/main.c:178-181`
- Problem: eecoreInit ignores both parameters and its only return is 0, so argOffset in main is always 0 and argv[argOffset], argc - 1 - argOffset and &argv[1 + argOffset] are plain argv[0], argc - 1 and &argv[1].
- Fix: Make eecoreInit void with no parameters, delete argOffset and pass argv[0], argc - 1, &argv[1] directly.
- Benefit: ~3 lines removed; no ELF change worth counting
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-490: freeze_diag.c: fetch_iop's return value is ignored and the IGR RPC id literal is duplicated

- Category: 1 (Dead code)
- Location(s): `RiptOPL/ee_core/src/freeze_diag.c:26`; `RiptOPL/ee_core/src/freeze_diag.c:154-156`; `RiptOPL/ee_core/src/freeze_diag.c:201-202`; `RiptOPL/ee_core/src/freeze_diag.c:239-240`; `RiptOPL/ee_core/src/cd_igr_rpc.c:15`
- Problem: fetch_iop returns 1/0 but its only caller (line 239) discards it and tests report.iop_status instead. The IGR RPC server id 0x80000598 is written as IGR_RPC_SERVER in freeze_diag.c and as a bare literal in cd_igr_rpc.c, although freeze_diag.c already includes cd_igr_rpc.h.
- Fix: Make fetch_iop void (drop the return statements' values and the 'Returns' sentence); define the server id once in cd_igr_rpc.h and use it in both files.
- Benefit: ~3 lines, trivial; no measurable size change
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-491: ps2cnf CNFGetKey: unreachable EOF branch, and no terminator for a 64-character BOOT2 path

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/platform/ps2cnf.cpp:54-67`; `RiptOPL/src/sources/supportbase.cpp:116-130`
- Problem: The loop condition already stops at '\0', so the '*line == '\0'' branch at 62-64 can never run (its own comment admits it duplicates the earlier check). When the token fills all CNF_PATH_LEN_MAX (64) bytes the loop exits without writing a terminator, and GetStartupExecName then scans its 64-byte ps2disc_boot for ':' or '\0' past the end. Only a malformed SYSTEM.CNF with a 64+ char BOOT2 value triggers it.
- Fix: Loop to CNF_PATH_LEN_MAX - 1, delete the unreachable branch, and always write the terminator after the loop.
- Benefit: 3 lines removed and an out-of-bounds read on a malformed disc closed (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-492: saveIconDraw keeps a redundant per-vertex colour array siCol

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/src/gfx/saveicon.cpp:812`; `RiptOPL/src/gfx/saveicon.cpp:1165-1166`; `RiptOPL/src/gfx/saveicon.cpp:1282-1283`; `RiptOPL/src/gfx/saveicon.cpp:1300-1311`
- Problem: Each frame siShade fills siCol in model order and the sort pass copies it into siDrawCol. The model is a plain (non-indexed) triangle list, so every vertex is copied exactly once and the shade could be computed straight into siDrawCol[dst]. siCol is an extra 8-byte-per-vertex buffer held, like the other work buffers, until saveIconReset (only called when the setting is switched off, guisettings.cpp:1795).
- Fix: Delete siCol: compute siShade for src directly into siDrawCol[dst] in the sorted copy loop and drop its malloc, free and NULL checks.
- Benefit: Up to 48,000 bytes less heap held (INFERRED: SAVEICON_MAX_VERTS 6000 x sizeof(u64)); one array and one store/load per vertex per frame removed; ~3 lines.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-493: vcdReadBdmaMode still accepts an interim 'udpfsnet' marker that this repo never writes

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/sources/vcdsupport.cpp:1401-1402`; `RiptOPL/.github/scripts/test_pops_udpfs.py:197`
- Problem: The marker writer only emits vcdBdmaSuffix tokens ('udpfs' for UDPFS); 'udpfsnet' is read-side compatibility for an interim PR marker. git log -S'udpfsnet' shows only the commit that added this read. A card carrying it would still need the exact current pair sizes to be accepted, so dropping the alias only turns that case into one re-equip.
- Fix: Delete the two-line alias and the source-text assertion that pins it.
- Benefit: 2 source lines and one test line removed (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-494: Globals gFadeDelay and toggleSfx are never used

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/app/opl.cpp:293-294`
- Problem: gFadeDelay and toggleSfx are defined and never read, written or declared anywhere else in the repo.
- Fix: Delete both definitions.
- Benefit: 2 lines; 8 bytes of .bss (INFERRED: two int).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-495: IO_CACHE_LOAD_ART is never used, yet IO_REQ_TYPE_COUNT sizes the per-type counters for it

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/opl.h:61-64`; `RiptOPL/include/ioqueue.h:84-85`
- Problem: IO_CACHE_LOAD_ART (3) appears nowhere but its define; art loading moved to its own worker (artload.cpp). IO_REQ_TYPE_COUNT is 4 only because of it, so ioqueue's pendingByType_/totalByType_ keep a slot for a type that is never registered or queued.
- Fix: Delete IO_CACHE_LOAD_ART and set IO_REQ_TYPE_COUNT to 3.
- Benefit: 2 lines; 8 B of IoQueue state (INFERRED: two int arrays lose one 4-byte slot each).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-496: ISO_GAME_FNAME_MAX is defined in supportbase.h and used nowhere in the product

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/supportbase.h:11`
- Problem: git grep -nw ISO_GAME_FNAME_MAX finds only this define and a private re-definition inside .github/scripts/test_iso_cache.py:57,106.
- Fix: Delete the define.
- Benefit: 1-2 source lines removed; no binary change
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-497: Small leftovers in textures: unused ERR_MISSING_ALPHA, mutable maxSize, identity ternaries, dead (void)filePath

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/textures.h:200`; `RiptOPL/src/gfx/textures.cpp:145`; `RiptOPL/src/gfx/textures.cpp:588`; `RiptOPL/src/gfx/textures.cpp:1405`; `RiptOPL/src/gfx/textures.cpp:1408`; `RiptOPL/src/gfx/textures.cpp:1437`
- Problem: ERR_MISSING_ALPHA appears nowhere else in the repo. maxSize is a non-const static that is never written (only read at 369). (void)filePath silences an unused-parameter warning for a parameter used at line 600. '(result >= 0) ? 0 : result' in texDiscoverArt/texDiscoverLoad is the identity: every loader path returns 0 or a negative ERR_* code.
- Fix: Delete ERR_MISSING_ALPHA and the (void)filePath line; make maxSize constexpr; return result directly at 1405, 1408 and 1437.
- Benefit: 2 lines removed, 3 simplified; a few bytes (one load per texSizeValidate call) (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-498: Two label ids that nothing addresses: CFG_LBL_POPSTARTER_PATH and CFG_LBL_VCD_USB_BDMA

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/ui/dialogs.cpp:619`; `RiptOPL/src/ui/dialogs.cpp:701`; `RiptOPL/include/dialogs.h:113`; `RiptOPL/include/dialogs.h:127`
- Problem: Both ids are given to static labels but no diaSet*/diaGet* call or other code uses them; the label rows would behave the same with id 0, like the other plain labels.
- Fix: Set both label ids to 0 and delete the two enum values.
- Benefit: 2 lines removed; no ELF change
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-499: gm_delete_plan_t files[].what is written and never read

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/gamemanage.h:40`; `RiptOPL/src/sources/gamemanage.cpp:231`
- Problem: gmAdd stores what for each planned file, but gmDeleteExtras only unlinks path and menuDeleteEverything (menusys.cpp:446-500) reads only parts/cfg/art/cht/shared; the per-type counters already carry the type information.
- Fix: Delete the what field and keep only the counter increments in gmAdd.
- Benefit: 256 bytes less per heap-allocated plan (64 entries x 4-byte int, INFERRED) and 1-2 lines removed
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-500: retrogemGetDiscImageGameID retries a .VCD at offset 0 (raw and cooked) after the 1 MiB-offset read fails; both retries cannot match

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/sources/retrogem.cpp:558-579`; `RiptOPL/src/sources/vcdsupport.cpp:436-450`; `RiptOPL/src/sources/vcdsupport.cpp:467-470`; `RiptOPL/src/sources/vcdsupport.cpp:3027-3036`
- Problem: For isVcd, when the 0x100000-offset layout finds no serial (no SYSTEM.CNF, as on PSX.EXE titles, a non-serial BOOT, or a read error), the function goes on to reopen the file twice and probe offset 0 as raw and as cooked. Offset 0 of a POPStarter VCD is its 1 MiB header, not disc sector 0, so neither probe can reach the disc PVD. The cooked probe reads up to 16 sectors. On the IO worker, resolveRowId repeats a failed resolve up to 4 times per row (VCD_ID_MAX_RETRIES 3, vcdsupport.cpp:264). The launch path repeats it again on the main thread.
- Fix: Delete the offset-0 fallbacks for VCDs: make retrogemGetVcdGameID return retrogemTryImageLayout(path, PS1_VCD_IMAGE_OFFSET, PS1_IMAGE_RAW_2352, ...) directly, and drop the isVcd parameter from retrogemGetDiscImageGameID.
- Benefit: Per failed VCD resolve: 2 fopen/fclose and up to 17 sector reads (1 raw + 16 cooked x 2048 B) avoided, times up to 4 attempts per row per session plus 1 at launch (INFERRED from the code, not measured). About 2 lines removed.
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-501: Checks for ELEM_TYPE_COVERFLOW can never match: Coverflow elements are created as ELEM_TYPE_GAME_IMAGE

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/gfx/themeelems.cpp:366-367`; `RiptOPL/src/gfx/themeelems.cpp:1380`
- Problem: themes.cpp:784 creates a Coverflow element with initBasic(..., ELEM_TYPE_GAME_IMAGE, ...) and no code assigns ELEM_TYPE_COVERFLOW to elem->type, so the '!= ELEM_TYPE_COVERFLOW' term in thmElemBox and the '&& e->type != ELEM_TYPE_COVERFLOW' term in thmFindElemBySuffixEx are always true. The comment above thmFindElemBySuffixEx (1366-1367) already says Coverflow is typed GAME_IMAGE.
- Fix: Delete the ELEM_TYPE_COVERFLOW comparisons at 367 and 1380.
- Benefit: Two dead comparisons removed; trivial .text.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-502: Stale comments naming a removed function (guiAlignMenuHints) and old .c files, plus a misplaced and a commented-out line

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/src/gfx/themeelems.cpp:2256`; `RiptOPL/src/gfx/themeelems.cpp:2273-2279`; `RiptOPL/src/gfx/themeelems.cpp:2311`; `RiptOPL/src/ui/menusys.cpp:151`; `RiptOPL/src/gfx/themeelems.cpp:35-38`; `RiptOPL/src/gfx/themeelems.cpp:2222`; `RiptOPL/include/themes.h:119`; `RiptOPL/include/themes.h:320`; `RiptOPL/src/gfx/themeelems.cpp:2143`
- Problem: guiAlignMenuHints no longer exists (uidraw.cpp has only guiAlignSubMenuHints) but three comments here and one in menusys.cpp explain the code by it; comments cite opl.c, textures.c, menusys.c and themes.c, which are now .cpp; themes.h:320 ends thmHintRank's line with a comment that belongs to thmGlyphSetRefresh (317); 2143 is a commented-out assignment.
- Fix: Reword the guiAlignMenuHints comments to name hintEntryWidth/drawHintText, fix the file names, move the 320 trailing comment to line 317, and delete line 2143.
- Benefit: Comment accuracy only; 1 line removed.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-503: autolaunch.cpp: mass0 retry waits delay(6) twice per attempt, so the documented ~12 s bound is ~24 s

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/app/autolaunch.cpp:203-207`; `RiptOPL/src/app/autolaunch.cpp:240-249`
- Problem: When mass0: does not open, the retry branch runs delay(6) and i--, and then the loop tail runs delay(6) again, so each of the AUTOLAUNCH_MASS0_RETRIES = 8 attempts (oplint.h:10) costs 12 ticks. The comment at 203-205 sizes the bound as 8 x 6 ticks of ~0.25 s = ~12 s; the real black-screen wait before falling back to the menu is ~24 s plus the initial delay(8).
- Fix: Delete the delay(6) inside the retry branch (line 243); the loop-tail delay(6) already spaces the attempts.
- Benefit: No-device BDM autolaunch falls back to the menu after ~12 s instead of ~26 s (INFERRED: 8 x 12 + 8 = 104 ticks vs 8 x 6 + 8 = 56 ticks at ~0.25 s per tick). 1 line removed.
- Effort: S. Risk: Low. Confidence: High (INFERRED)

#### RIPTOPL-504: bdmFatfsOpenErrno is exported in util.h but only used inside util.cpp

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/util.h:39`; `RiptOPL/src/platform/util.cpp:436`
- Problem: The only call is util.cpp:492 in the same file as the definition (util.cpp:436); the prototype in util.h has no other user.
- Fix: Make bdmFatfsOpenErrno static in util.cpp and delete its util.h prototype.
- Benefit: 1 line; lets the compiler inline it (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-505: hddInit(item_list_t *) is declared but never defined; two comments still refer to it

- Category: 1 (Dead code)
- Location(s): `RiptOPL/include/hddsupport.h:117`; `RiptOPL/src/sources/hddsupport.cpp:2971`; `RiptOPL/src/sources/hddsupport.cpp:3039`
- Problem: The EE prototype hddInit(item_list_t *) has no definition anywhere; the init path is HddSource::init (1458). The only other hddInit is the unrelated IOP APA driver entry in modules/hdd/apa. The comments 'This may be called, even if hddInit() was not' name the missing function.
- Fix: Delete the prototype and change the two comments to say HddSource::init (or delete them).
- Benefit: 1 prototype removed; comments corrected; no ELF change
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-506: Boot runs PadManager::start() twice: init() discards the first startPads() result and its retry loop always calls it again

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/app/opl.cpp:1686`; `RiptOPL/src/app/opl.cpp:1699-1708`; `RiptOPL/src/core/pad.cpp:1136-1191`
- Problem: opl.cpp:1686 calls startPads() and ignores the result; padStatus is still 0 (opl.cpp:1668), so the loop at 1701 calls startPads() again at once. start() has no guard against re-entry: it re-issues padPortOpen on ports that are already open and runs initialize() again for every pad, which resends padSetMainMode(LOCK), padEnterPressMode and padSetActAlign with up to seven 25 ms bounded waits per DualShock, on the main thread before the first menu frame.
- Fix: Delete the call at opl.cpp:1686; the loop below makes the first call and retries only if no port opened.
- Benefit: One pad init per connected DualShock less at boot: up to about 7 x 25 ms = 175 ms of polled waits plus three pad config commands (INFERRED from waitReady bounds, not measured); 1 line removed.
- Effort: S. Risk: Med. Confidence: Med (INFERRED)

#### RIPTOPL-507: apply_capcom_protection_patch passes the opcode value to SyncDCache; the flush is not needed

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/ee_core/src/patches.c:262-264`
- Problem: SyncDCache((void *)opcode, &opcode + 4) uses the value 0x10000025 as the start of the range, so the range starts above its end (a stack address). The flush is also unnecessary: smem_write comes from ps2sdk libkernel smem.o (pulled in by patches.o, evidence/RiptOPL/raw/eecore-archive-members.txt:20) and copies with CPU stores through the IOP memory window, so the source never has to be written back to RAM.
- Fix: Delete the SyncDCache line.
- Benefit: 1 line and one inverted-range kernel cache call removed (INFERRED)
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-508: apply_capcom_protection_patch passes the opcode value, not its address, to SyncDCache

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/ee_core/src/patches.c:262-264`
- Problem: SyncDCache((void *)opcode, ...) uses 0x10000025 as the start address instead of &opcode, so the call does not write back the stack word it is meant to. smem_write copies with the CPU, so the line appears unnecessary rather than harmful.
- Fix: Delete the SyncDCache line (or pass &opcode if a write-back turns out to be needed).
- Benefit: 1 line removed; removes a cache operation on a bogus range (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-509: Comments say DTV_1080P is variant-only, but GSM_1080P is a base define of eecore and the main ELF

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/ee_core/src/gsm_engine_adv.S:136-139`; `RiptOPL/ee_core/src/gsm_engine.S:182`; `RiptOPL/ee_core/src/gsm_engine.S:204-205`; `RiptOPL/ps2.yaml:609`; `RiptOPL/ps2.yaml:644`
- Problem: gsm_engine_adv.S:138-139 says DTV_1080P is compiled only into a dedicated GSM1080P variant and absent from every default build; gsm_engine.S:182 and 204-205 say the same and that a mainline build is bit-for-bit unaffected. ps2.yaml defines GSM_1080P for eecore (:609) and riptopl (:644), and .github/scripts/test_ps2build_configurations.py:61 asserts it, so the hardware-unvalidated 1080p raster ships in every build behind the guigame.cpp confirmations. The comments misstate the shipped risk.
- Fix: Correct the three comments to say GSM_1080P is a base define (one line each), or drop GSM_1080P from the base defines if variant-only was the intent.
- Benefit: Accurate safety statement for shipped code; no size change (INFERRED)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-510: HDD launch saves last_played to conf_last.cfg before the points where the launch can still be abandoned

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/sources/hddsupport.cpp:2389-2392`; `RiptOPL/src/sources/hddsupport.cpp:2401-2407`; `RiptOPL/src/sources/hddsupport.cpp:2720-2723`; `RiptOPL/src/sources/hddsupport.cpp:2750-2751`; `RiptOPL/src/sources/hddsupport.cpp:2761-2767`
- Problem: Both legs write last_played and call saveConfig(CONFIG_LAST) before steps that can return to the menu without launching. In the Neutrino leg these are the cheat back-out (2401-2402) and the argument preflight (2406-2407). In the native leg they are a sbPrepare failure (2750-2751) and the cheat back-out (2765-2766). Each aborted launch costs a config write (usually to the memory card) and marks a game that never ran as last played.
- Fix: Move each gRememberLastPlayed block below its leg's last abort point: after 2407 in hddTryNeutrinoLaunch and after the cheat block in launch. This only reorders lines.
- Benefit: Saves one conf_last.cfg write per aborted HDD launch and keeps the last-played marker correct. No lines change (INFERRED: CONFIG_LAST normally lives with the settings on mc?:OPL).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-511: Stale comments in vmcsaves: header names vmcsaves.c; vsCopy's static save is ~3.9 KB, not ~2.5 KB

- Category: 7 (Build/CI/repo clutter)
- Location(s): `RiptOPL/include/vmcsaves.h:4`; `RiptOPL/src/core/vmcsaves.cpp:1123`
- Problem: vmcsaves.h:4 says 'see vmcsaves.c', but the file is src/core/vmcsaves.cpp. The comment at vmcsaves.cpp:1123 says vs_save_t is ~2.5 KB, but it is 64 vs_file_t of 60 bytes (name[33]+pad, size, two 8-byte stamps, 4-byte pointer) plus ~56 bytes of header, about 3.9 KB of .bss.
- Fix: Fix both comments (vmcsaves.cpp; ~3.9 KB).
- Benefit: Cosmetic; 0 lines.
- Effort: S. Risk: Low. Confidence: High (INFERRED)

#### RIPTOPL-512: VMC Saves re-reads both the card image and the SAVES folder after every button, even a declined delete

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/ui/guigame.cpp:377-382`; `RiptOPL/src/ui/guigame.cpp:473`
- Problem: The loop in guiGameShowVMCSaves calls guiGameRefreshVMCSaves after every result, which runs vmcSavesList (opens the image and reads up to 64 icon.sys) and vmcSavesListFolder (opendir, stat and an icon.sys read per entry). An export changes only the folder, an import or delete only the card, and a delete the user declines (guiMsgBox returns 0 at line 466) or a failed action changes nothing, yet both lists are rebuilt on the UI thread each time.
- Fix: Refresh only the side the action wrote (folder after export, card after import or delete), and skip the refresh when the action did not run or failed.
- Benefit: One of two directory rescans saved per action, and both on a declined delete; a few hundred card-image page reads on slow USB/HDD images INFERRED (64 saves x directory entry + icon.sys)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-513: disc_key.h sysReadDiscKey is static inline with static RPC buffers, so system.cpp and sysdisc.cpp each carry their own copy

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/include/disc_key.h:40-59`; `RiptOPL/src/platform/system.cpp:7`; `RiptOPL/src/platform/sysdisc.cpp:7`
- Problem: sysReadDiscKey is defined static inline in a header and declares static request[16] and response[64] buffers (64-byte aligned); both system.cpp (:527) and sysdisc.cpp (:230) include it, so the main ELF holds two request and two response buffers and two inlined copies of the RPC sequence.
- Fix: Define sysReadDiscKey once (non-inline) in sysdisc.cpp and leave only its prototype in disc_key.h.
- Benefit: MEASURED: nm shows 4 x 64 B .bss symbols (two request, two response), so 128 B .bss saved, plus one inlined copy of the call sequence (INFERRED ~100-200 B .text).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-514: fmCardTitle reads 964 bytes of each icon.sys when the title needs the first 260

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/ui/guifilemgr.cpp:228-238`
- Problem: On a memory card root, fmLoad reads every save's icon.sys through mcman into a 964-byte stack buffer; vmcSavesTitle only looks at bytes 0..0xC0+68 (ICON_SYS_HEAD, vmcsaves.cpp:46,1061), which the check at line 237 already uses. fmLoad runs again after every paste, delete, rename and new folder, so the extra page read per save repeats.
- Fix: Size iconSys to 0xC0 + 68 (260 bytes) and read only that much, as vmcsaves.cpp does for its own lists.
- Benefit: About one 512-byte card page read less per save per card-root listing and 704 B less stack INFERRED (964 bytes span two pages, 260 one)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-515: guiAlignSubMenuHints measures a missing glyph as 20+2 px but guiDrawIconAndText draws it as 0 px

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/gfx/uidraw.cpp:67-73`; `RiptOPL/src/gfx/uidraw.cpp:14-29`
- Problem: For a NULL glyph texture (a use_default=0 theme without circle.png/cross.png, the case the comment at :68-69 names) the measure uses w = 20 and subtracts rmWideScale(20)+2, while the drawer uses w = 0 and does not advance x. Right-aligned hint rows (dia.cpp:795, guioverlay.cpp:78, guisettings.cpp:3155) then end about 22 px short of their margin per missing glyph and centred rows (guisettings.cpp:3126, themeelems.cpp:2338) sit about 11 px off.
- Fix: Use 0 width and skip the +2 gap for a NULL texture in guiAlignSubMenuHints, matching guiDrawIconAndText.
- Benefit: Hint rows line up when a theme lacks a glyph; no size change
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-516: netTestLink uses an absolute clock() deadline and an overflowing millisecond product, unlike the wrap-safe form used elsewhere in the file

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/sources/ranet.cpp:899-914`
- Problem: deadline = t0 + 3 * CLOCKS_PER_SEC and the test clock() < deadline break when the 32-bit microsecond clock wraps (about every 71.6 min): if t0 is within 3 s of the wrap the loop never runs and the link test reports no answer. ms = (clock() - t0) * 1000 / CLOCKS_PER_SEC overflows once the elapsed time passes 4.29 s, which a last ask() started near the deadline can reach. The same file and its test say clock() is a 32-bit microsecond counter and use the elapsed form for httpGet.
- Fix: Loop while (clock_t)(clock() - t0) < 3000 * (CLOCKS_PER_SEC / 1000), as httpGet does, and compute ms as (clock() - t0) / (CLOCKS_PER_SEC / 1000).
- Benefit: Removes a false 'no answer' in the link test around each clock wrap and a wrong round-trip figure; no size change (INFERRED).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-517: sysDeleteFolder leaks its file list when a subfolder delete fails, and sprintf's to an unchecked malloc

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/platform/util.cpp:873-881`; `RiptOPL/src/platform/util.cpp:909-933`
- Problem: The collected DirentToDelete list (nodes and filenames) is freed only inside the result >= 0 branch, so when a recursive sysDeleteFolder of a subfolder fails (r < 0 at 879-880) every node of the parent's list is leaked. The path buffer at 873 is used by sprintf without a NULL check. Reached from AppSource::deleteGame (appsupport.cpp:930) when the user deletes an app folder.
- Fix: Always walk and free the list; only skip the unlink calls when result < 0. Check the malloc at 873 before the sprintf.
- Justification: The NULL check stops a NULL write on allocation failure; the leak fix only moves the existing free loop.
- Benefit: A few bytes per file leaked per failed app delete, plus one NULL-pointer write path removed; tiny.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-518: sysLaunchLoaderElf sprintf's unvalidated int ps2_ip[] with %d into char g_ps2_ip[16]

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/platform/system.cpp:1577-1582`; `RiptOPL/ee_core/include/coreconfig.h:43-45`; `RiptOPL/src/app/settingsio.cpp:1949-1950`; `RiptOPL/src/core/config.cpp:752-754`
- Problem: For UDPBD/UDPFS launches, line 1580 formats int ps2_ip[4] with %d into g_ps2_ip[16]. The values come from sscanf("%d") of conf_network.cfg or IPCONFIG.DAT with no range check, so an octet over 255 or a negative one ("192.168.100.1000") writes past the field. The worst case is 47 chars + NUL, which stays inside ip+netmask+gateway, and the next two sprintfs rewrite those. ee_core is still left with an unterminated IP that runs into the netmask string.
- Fix: Use snprintf(config->g_ps2_ip, sizeof(config->g_ps2_ip), ...) with (u8) casts and %u, as line 1577 already formats the DHCP address.
- Benefit: Closes a small out-of-field write from config-file input (READ; effect limited to a wrong in-game IP)
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-519: texStageExternalFileIntoMemory reads up to 8 MB before refusing a file whose size it already knows is too big

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/gfx/textures.cpp:639-659`; `RiptOPL/src/gfx/textures.cpp:681-698`
- Problem: When lseek reports a size over TEX_STAGE_MAX_SIZE, or malloc(size) fails, the fast path falls through to the chunked loop. The loop then reads about 8 MB in 32 KB reads and grows its buffer to 8 MB by realloc (4 MB + 8 MB live at once) before it refuses with ERR_BAD_FILE. The size probe had already given the answer. This is reachable for any oversized art or theme PNG on mass/pfs/smb (texLoadAll staged arm, texLoadJpgFile).
- Fix: When the size probe succeeded, do not enter the loop: return ERR_BAD_FILE for size > TEX_STAGE_MAX_SIZE and ERR_FILE_IO when malloc(size) fails. Keep the loop for MMCE (shortReadIsEof), where lseek is not available.
- Benefit: Avoids an 8 MB read (about 8 s on USB 1.1, INFERRED) and a 12 MB realloc peak for an oversized file; rare trigger.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-520: thmLoadResource takes a psm parameter it never uses

- Category: 1 (Dead code)
- Location(s): `RiptOPL/src/gfx/themes.cpp:1061-1074`; `RiptOPL/src/gfx/themes.cpp:1261`; `RiptOPL/src/gfx/themes.cpp:1703`; `RiptOPL/src/gfx/themes.cpp:1717-1721`; `RiptOPL/src/gfx/themes.cpp:1733`; `RiptOPL/src/gfx/themes.cpp:1745-1750`; `RiptOPL/src/gfx/themes.cpp:1797-1801`; `RiptOPL/src/gfx/themes.cpp:1811`
- Problem: The body (1063-1073) never reads psm; all 11 call sites pass GS_PSM_CT32.
- Fix: Delete the psm parameter and the GS_PSM_CT32 argument at every call site.
- Benefit: No line change; one register argument less per call (INFERRED, a few bytes).
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-521: utf8.h: the 400-byte static utf8d table is linked twice (fntsys.cpp and textshape.cpp)

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/include/utf8.h:21-49`; `RiptOPL/src/gfx/fntsys.cpp:12`; `RiptOPL/src/gfx/textshape.cpp:12`
- Problem: utf8.h defines utf8d as a static const array and utf8Decode as a static function, so each including translation unit gets its own copy. The shipped ELF holds two 400-byte _ZL5utf8d objects.
- Fix: Define the table and utf8Decode once (for example in fntsys.cpp) and leave only a prototype in utf8.h, or make both inline (inline constexpr table) so the linker keeps one copy.
- Benefit: 400 B .rodata. MEASURED: two _ZL5utf8d entries of size 0x190 in evidence/RiptOPL/raw/riptopl-nm-size-sorted.full.txt:1757-1758.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-522: AttributeImage (built-in theme) and BdmIndex look up the internal texture id by name every frame

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/gfx/themeelems.cpp:1506-1515`; `RiptOPL/src/gfx/themeelems.cpp:1534-1540`; `RiptOPL/src/gfx/themeelems.cpp:1917-1925`; `RiptOPL/src/gfx/textures.cpp:349-362`; `RiptOPL/misc/conf_theme_OPL.cfg:238-286`
- Problem: On the built-in theme, drawAttributeImage calls thmAttributeTexId every frame (snprintf for 'value/asset' values, then texLookupInternalTexId, a linear strcmp over the ~129 internal texture names) although the value only changes when currentConfigId changes. The built-in info page has 7 AttributeImage elements, so the GUI thread repeats up to 7 snprintf plus about 900 strcmp per frame for an unchanged result; drawBDMIndex does one snprintf plus the same scan per frame on the main page.
- Fix: In drawAttributeImage resolve the texId once inside the existing currentConfigId != config->uid() block and keep it in the element (currentCacheId is unused on the built-in branch), then draw from the stored id. Leave drawBDMIndex as it is unless the same edit is made there for free.
- Justification: No new state is needed for AttributeImage: the uid-gated refresh block already exists and currentCacheId is idle on that branch, so the per-frame work is removed by moving one call.
- Benefit: About 7 x 129 strcmp + up to 7 snprintf per frame removed on the built-in info page, roughly 50 us per frame at 294 MHz (INFERRED: ~15 cycles per mismatching strcmp).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-523: BdmSource::config retries mkdir of <home>/CFG on every row settle once the folder exists

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/sources/bdmsupport.cpp:2055-2066`; `RiptOPL/src/ui/menusys.cpp:587-606`
- Problem: On a read-only UDPBD/UDPFS page, config() remembers the CFG folder in madeCfgDir only when mkdir returns 0. From the second boot on the folder already exists, mkdir fails, nothing is latched, and the mkdir is issued again on every config() call. _menuLoadConfigAsync calls config() each time the cursor settles on a row (menusys.cpp 587-606), so each settle costs one extra mkdir RPC on the settings home, usually the memory card.
- Fix: Also latch madeCfgDir when mkdir fails because the folder already exists (errno EEXIST), so the one-mkdir-per-folder intent in the comment holds across boots.
- Benefit: One fileXio mkdir RPC (memory card or USB) less per row settle on read-only network pages (INFERRED; EEXIST mapping of the mc/mass drivers not verified)
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-524: EE_RESET_SUFFIX_BYTES is 7 but ResetIopSpecial writes 8 bytes after the payload (latent one-byte stack overflow of command[])

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/ee_core/include/reset_arg_bounds.h:4-17`; `RiptOPL/ee_core/src/iopmgr.c:80-92`; `RiptOPL/ee_core/src/iopmgr.c:437-439`; `RiptOPL/.github/scripts/test_reset_arg_bounds.py:95-96`
- Problem: ResetIopSpecial accepts arglen up to sizeof(command) - 7, then writes the NUL at command[arglen] and _strcpy's "host0:" with its terminator (util.c:24-27 copies strlen+1) to command[arglen+1..arglen+7], so arglen = sizeof(command)-7 writes command[sizeof(command)], one past the stack array. eeResetPayloadLength repeats the same 7 for commandCapacity, and the test pins it ((19,80,16) == 9 means 9+8 = 17 bytes into 16). Not reachable today: the only caller passes packetCapacity = sizeof(SifCmdResetData_t.arg), which caps the payload at 70 for an 80-byte arg (INFERRED SDK size), below the 73 the guard allows.
- Fix: Set EE_RESET_SUFFIX_BYTES to 8 and fix its comment (NUL, "host0:" and its NUL). Change the test's (19,80,16) case to expect -1 and (20,80,16) stays -1.
- Benefit: Corrects a latent off-by-one in the reset-command bound; no size change (INFERRED)
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-525: Launch Disc waits use absolute clock()+budget deadlines that break at the 32-bit clock wrap

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/platform/sysdisc.cpp:134-137`; `RiptOPL/src/platform/sysdisc.cpp:185-188`; `RiptOPL/src/platform/sysdisc.cpp:197-198`
- Problem: sysDiscSettle and the NODISC re-detect compute deadline = clock() + budget and loop while clock() < deadline. The repo itself says clock() is a 32-bit microsecond counter (ratls_ps2.cpp:167, nettime.cpp:72) and that only the elapsed form (clock() - start) survives the wrap (guiwait.cpp:97-98). If Launch Disc is chosen in the last 1-4 s before a wrap (every ~71.6 min), the deadline overflows, every wait ends at once, and a disc that is still identifying or spun down is reported as 'disc launch error'.
- Fix: Keep a start tick and compare elapsed time, as guiwait.cpp:99-103 does: (clock() - start) < budgetTicks for the three tests. Same line count.
- Benefit: Removes a rare spurious Launch Disc failure (about 4 s in every 71.6 min of uptime, about 0.1% of launches; INFERRED). No size change.
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-526: Share picker in EthSource::update treats ERROR_ETH_LINK_FAIL/DHCP_FAIL as listable and replaces the link error with a share-list error

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/sources/ethsupport.cpp:832-838`; `RiptOPL/src/sources/ethsupport.cpp:863-866`; `RiptOPL/include/iosupport.h:52-60`
- Problem: With no share name set, update() only skips the GETSHARELIST devctl when gNetworkStartup < ERROR_ETH_SMB_OPENSHARE (303). ERROR_ETH_LINK_FAIL (310) and ERROR_ETH_DHCP_FAIL (311), set by netApplyConfig on a pulled cable or failed DHCP (netsupport.cpp:119-143), pass the gate, the devctl fails on the unconnected driver and 864-865 overwrite the status and message with ERROR_ETH_SMB_LISTSHARES.
- Fix: Also return early when gNetworkStartup is above ERROR_ETH_SMB_LISTGAMES (the link/DHCP codes), so only the open-share and list errors retry the listing.
- Benefit: Correct error message and no pointless SMB RPC after a link or DHCP failure on the share-picker page; one condition changed, 0 lines.
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-527: Theme reloads can run out of font slots: new theme's fonts load before the old theme's are released (15 slots, up to 16 fonts per theme)

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/gfx/themes.cpp:1129-1174`; `RiptOPL/src/gfx/themes.cpp:1841-1843`; `RiptOPL/include/themes.h:9`; `RiptOPL/include/fontsystem.h:21`; `RiptOPL/src/gfx/fntsys.cpp:442-459`; `RiptOPL/src/gfx/fntsys.cpp:469-486`
- Problem: fntLoadFile takes the first free slot of 1..15 (slot 0 is the default font) and the language fallback font holds one. thmLoad loads all the new theme's fonts (up to THM_MAX_FONTS = 16) while the old theme still holds its own, and releases those only at publish. A theme with 8 or more font files, or two consecutive themes whose fonts add up to more than 14, gets FNT_ERROR for the rest: those keys silently fall back to fonts[0] after a view switch, Widescreen or language reload, and each such TTF is still read whole from the device and then freed.
- Fix: Lower THM_MAX_FONTS to 7 so old + new + fallback always fit the 15 slots, or, if themes really use more fonts, raise FNT_MAX_COUNT to 2 * THM_MAX_FONTS + 2.
- Benefit: Font choice no longer changes across reloads for font-heavy themes and no wasted TTF reads; lowering the constant also shrinks theme_t by 36 bytes (9 ints)
- Effort: S. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-528: appLookupByStartup scans the whole list linearly after every hash-table miss

- Category: 3 (Overengineering)
- Location(s): `RiptOPL/src/sources/appsupport.cpp:565-583`
- Problem: The hash table holds every app with a non-empty startup, so a completed probe sequence that misses proves no row matches; the linear loop at 578-581 is only needed when the table could not be allocated, yet it runs on every miss (art worker path via image()).
- Fix: Run the linear scan only when appArtLookupTable is NULL (make it the else branch).
- Benefit: Removes an O(n) strcmp pass per art miss; ~0 lines
- Effort: S. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-529: disc_key.h defines sysReadDiscKey as static inline with static RPC buffers, so two translation units each get their own copy

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/include/disc_key.h:40-59`; `RiptOPL/src/platform/system.cpp:527`; `RiptOPL/src/platform/sysdisc.cpp:230`
- Problem: The header is included and called in system.cpp and sysdisc.cpp, so the function body and its 64-byte request and 64-byte response buffers (both 64-byte aligned statics) are emitted twice in the main ELF.
- Fix: Make sysReadDiscKey an ordinary function defined once (for example in sysdisc.cpp) and leave only its prototype and the libcdvd externs in disc_key.h.
- Benefit: 128 B .bss (2 x 64-byte static arrays, INFERRED) plus one copy of a ~20-line function (~100-150 B .text, INFERRED); no lines removed.
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-530: freeze_diag.c shutdown_cd copies only .server, so the shutdown argument is DMAed to IOP address 0

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/ee_core/src/freeze_diag.c:212-216`
- Problem: shutdown_with_deadline fills the second client with shutdown_cd.server = igr_cd.server only. ps2sdk SifCallRpc sends the argument to cd->buf (the server buffer address that SifBindRpc stores in the client), which stays NULL here, so the 4-byte argument goes to IOP address 0 and cbrpc_shutdown (cdvdfsv.c:244) reads whatever its buffer last held. It works today only because the preceding FREEZE_DIAG_RPC_FNO call left 0 in that buffer; the IOP is rebooted right after.
- Fix: Copy the whole bound client (shutdown_cd = igr_cd) instead of only the server pointer, so buf and cbuf come along.
- Benefit: The shutdown argument reaches the server and no stray SIF write lands at IOP address 0; INGAMEDIAG builds only; no size change (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-531: Select on an image-opened achievements page skips achievementsForget, so the cached library and HTTPS 'unreachable' latch are reused

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/sources/achievements.cpp:425-426`; `RiptOPL/src/sources/achievements.cpp:496-500`; `RiptOPL/src/sources/achievements.cpp:656-660`; `RiptOPL/src/ui/menusys.cpp:1224-1229`
- Problem: achievements.cpp says Select (achievementsForget) retries the online lookup and refetches, but the Select handler for a card page whose target is still 'image' (any non-READY state, e.g. ACH_NOT_PLAYED after an HTTPS failure) only calls achievementsRequestImage. loadXerabora then re-hashes the ISO, skips HTTPS because xeraHttpsUnreachable is still set, and xeraFetch returns the cached library because xeraStale is 0, so the 'Select refreshes this page' prompt can never change the result in that session.
- Fix: Call achievementsForget() before achievementsRequestImage/achievementsRequestVcd in the image branch of the Select handler (menusys.cpp:1225-1229).
- Justification: One added call makes the image-page refresh do what the on-screen prompt and the achievements.cpp comment promise; no new state.
- Benefit: Refresh on a NOT_PLAYED/UNSUPPORTED card page actually refetches (RA editions, Xerabora mode); +1 line.
- Effort: S. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-532: TarEntryCfg/TarEntryCht declare filename[20] but the parser writes 24 bytes; names loaded from the sidecar are never re-terminated

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/include/tar.h:53-63`; `RiptOPL/src/platform/tar.cpp:254`; `RiptOPL/src/platform/tar.cpp:357-359`; `RiptOPL/src/platform/tar.cpp:164-171`
- Problem: nameMax = entrySize - sizeof(TarEntryBase) - 1. For the CFG/CHT entries that is 40 - 16 - 1 = 23 (sizeof is 40 because of the u64 member, INFERRED alignment), so strncpy writes 23 chars and a NUL at [23] into a filename[20]. tar.h:41-49 documents and fixes exactly this for TarEntryArt but not here, and the CHT kind is live (supportbase.cpp:1761). Separately, tarReadCache rejects a corrupt rawSize but never terminates names, so a damaged *_cache.bin makes tarFind's strcasecmp/strrchr read past the entry, and past the heap block for the last one.
- Fix: Declare filename[24] in TarEntryCfg and TarEntryCht (sizeof stays 40). In the existing validation loop of tarReadCache also set the name byte at nameMax to 0.
- Justification: The added line in tarReadCache sits in the loop that already rejects corrupt rawSize from the same untrusted sidecar. It is the smallest way to bound the string scans that follow.
- Benefit: Removes an out-of-declared-bounds write on every CHT/CFG entry and an OOB read on a corrupt sidecar (READ; practical impact small)
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-533: askPc keeps broadcasting every RAG1 chunk request in Xerabora discovery mode and accepts each chunk from any responder

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/sources/ranet.cpp:515-523`; `RiptOPL/src/sources/ranet.cpp:585`; `RiptOPL/src/sources/ranet.cpp:658-693`; `RiptOPL/src/sources/ranet.cpp:339-344`
- Problem: Without a companion IP, pcTarget leaves `to` as 255.255.255.255 and askPc never narrows it after RAQ1 answers, so all chunk requests are broadcast and ask() accepts each reply from whichever host answers first (the source filter applies only to unicast). With two xeRAbora PCs on the LAN holding different versions of a set, chunks of the same index and length from different PCs can be stitched into one list that passes the length checks.
- Fix: After raqAsk returns 0, if `to` is the broadcast address set to.sin_addr.s_addr = replyIP_ so the chunk requests go to, and are only accepted from, the PC that confirmed the game.
- Justification: Adds one line; it closes the mixed-source list case and stops broadcasting up to 23 chunk requests per check, and it is the smallest change that ties the transfer to one peer.
- Benefit: Chunked watch list comes from one verified PC; fewer broadcast datagrams (INFERRED). No size change.
- Effort: S. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-534: Cut then Paste of a .psu onto a memory card unpacks it but leaves the .psu in place and drops it from the clipboard

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/ui/guifilemgr.cpp:449-452`; `RiptOPL/src/ui/guifilemgr.cpp:469-479`
- Problem: In fmPaste the .psu-onto-card branch runs vmcSavesPsuToFolder whether the clipboard is Copy or Cut. With Cut, a successful unpack counts as done, so the entry is removed from the clipboard (line 479), but the source .psu is never deleted, which contradicts HINT_FM_CUT ('Paste here moves them'). The user sees a move that was really a copy.
- Fix: When fmClipCut is set, delete the source .psu after a successful unpack (one fmDelete call); otherwise leave the entry on the clipboard.
- Justification: The single added call makes Cut behave as its hint promises; the alternative of documenting the exception would add more text than code.
- Benefit: Correct Cut semantics for .psu onto a memory card; no data loss today, only an unexpected leftover file
- Effort: S. Risk: Med. Confidence: High (READ)

#### RIPTOPL-535: hddDvrScan publishes its full 128-entry candidate array (44 KB) whatever the ISO count

- Category: 4 (Size and memory)
- Location(s): `RiptOPL/src/sources/hddsupport.cpp:236-238`; `RiptOPL/src/sources/hddsupport.cpp:268-273`
- Problem: Each DVR scan callocs HDD_DVR_MAX_ISOS records and, when any directory was readable, publishes that allocation as the live list even when count is 0 or a handful. The memory stays held until the next scan or teardown.
- Fix: Before hddDvrPublish, shrink candidate to count with realloc, or free it and publish NULL when count is 0.
- Justification: Adds one or two lines. It returns about 44 KB of EE heap for the life of the session on a DESR with the DVR view enabled, where the list is usually empty or short.
- Benefit: Up to 45,056 bytes of EE RAM returned (INFERRED: sizeof(hdd_dvr_iso_t) = 128 + 224 = 352 bytes x 128). Applies only to DESR hardware with gEnableDVR.
- Effort: S. Risk: Low. Confidence: High (READ)

#### RIPTOPL-536: ImageCache::requestFinished decrements activeRequests_ without protection against the GUI thread's increment

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/include/texcache.h:103-107`; `RiptOPL/src/gfx/texcache.cpp:520`; `RiptOPL/src/gfx/artload.cpp:369-370`; `RiptOPL/src/gfx/artload.cpp:385-386`
- Problem: activeRequests_ is the guard that stops cacheDestroyCache freeing content[] under an in-flight request. The art thread (priority 72) does a plain volatile read-modify-write decrement with interrupts enabled; the vsync interrupt wakes the GUI thread (priority 31), which preempts it and may run activeRequests_++ for the same cache before the decrement is stored. The increment is then lost, the counter under-counts for the rest of the cache's life, and a later theme switch can free content[] while a request still points into it.
- Fix: Do the decrement inside a DIntr()/EIntr() bracket in requestFinished (or inside gArtQ.release's existing bracket); the GUI-side increment cannot be preempted by the lower-priority art thread and needs nothing.
- Justification: Two lines close a lost-update race on the only guard against the theme-switch use-after-free that texcache.h:125-133 describes; the code already uses the same DIntr bracket for its queue counters.
- Benefit: Removes a rare race that defeats the use-after-free guard (INFERRED window: about two instructions per completed request against a 60 Hz interrupt). No size change.
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-537: Overscan read from the settings file is never clamped; >= 1000 makes displayWidth_ 0 and unScaleX divides by zero

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/gfx/renderman.cpp:784-787`; `RiptOPL/src/gfx/renderman.cpp:789-803`; `RiptOPL/src/core/settings.cpp:88`; `RiptOPL/src/core/settings.cpp:301-302`
- Problem: The dialog limits Overscan to 0..100 (dialogs.cpp:1087), but settingsLoad and screenPosLoad copy overscan and overscan_N from the cfg file unchecked, and setOverscan uses the value as is. With overscan >= 1000 (a hand-edited or damaged settings_riptopl.cfg) displayXOff_ reaches half the width, displayWidth_ becomes 0 or negative, and every text measurement through rmUnScaleX divides by it (a divide-by-zero trap on GCC MIPS builds, INFERRED) on every boot until the file is fixed.
- Fix: Clamp overscan to 0..100 at the top of Renderer::setOverscan.
- Justification: One clamp at the single consumer covers both cfg keys and the dialog; it turns an unrecoverable boot fault from a bad cfg value into the nearest valid setting.
- Benefit: Removes a boot-time crash/garbage layout from an out-of-range cfg value (INFERRED). Trigger needs a hand-edited file, so rated low.
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-538: cuePrepareLaunch skips the 256-byte argv pool check for a folder launch, so an over-long Ember argv is refused only after deinit

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/sources/cuesupport.cpp:311-313`; `RiptOPL/src/sources/cuesupport.cpp:331-336`; `RiptOPL/src/sources/ethsupport.cpp:1092-1095`; `RiptOPL/src/platform/elfldr_noreset.cpp:190-205`
- Problem: The pool check (2*strlen(emberElf)+strlen(argument)+3 > 256) runs only after a discs.txt manifest; with no manifest line 313 returns 1 first. Over SMB the ELF path is up to 52 chars (smb0: + 31-char ETH prefix + EMBER/ember.elf), NFS 56, so a folder name of 150-160 chars (allowed by the scan cap ISO_GAME_NAME_MAX, cuesupport.cpp:502) exceeds the pool; sysLoadELFKeepIOP then refuses after host::app().deinit has already torn the menu down.
- Fix: Apply the existing pool check on the no-manifest path too (run it before returning 1 at line 313), so the message is shown while the GUI is still live.
- Justification: The check already exists; it only has to cover the second return path. The concrete problem is a refused handoff after teardown, which leaves the user with no menu.
- Benefit: Turns a post-teardown launch failure into an on-screen message for long names on network prefixes; about 2 lines moved/added.
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-539: lngGetFilePath(0) reads languages[-1]; the language popup calls it with the current id, which can be 0

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/core/lang.cpp:550-556`; `RiptOPL/src/ui/guinotify.cpp:134-135`; `RiptOPL/src/ui/guinotify.cpp:33-36`
- Problem: Notifications::check only ever sets showLang_ (never clears it), and show() re-reads lngGetGuiValue() every frame for 5 s and passes it to lngGetFilePath, which indexes languages[langID - 1] with no check. Pick a language in Settings (popup armed), then switch back to English within the 5 s: guiLangID is 0, lngGetFilePath reads the word before the heap array as a char * and snprintf %s dereferences it (garbage text or an EE exception).
- Fix: Return NULL from lngGetFilePath for langID < 1 and skip the language line in show() when it is NULL (as the theme line already does via thmGetFilePath at guinotify.cpp:29).
- Justification: Two-line guard; the concrete problem is an out-of-bounds pointer read reachable from the Settings language switch.
- Benefit: Removes a rare menu crash path; no size change.
- Effort: S. Risk: Low. Confidence: Med (READ)

#### RIPTOPL-540: cacheGlyph runs one or two FreeType cmap lookups for every non-ASCII character on every draw and measure once a fallback font is loaded

- Category: 5 (Performance)
- Location(s): `RiptOPL/src/gfx/fntsys.cpp:586-591`; `RiptOPL/src/gfx/fntsys.cpp:608-610`; `RiptOPL/src/core/lang.cpp:424-441`
- Problem: The fallback test (two FT_Get_Char_Index calls) runs before the glyph-cache lookup, so a cached glyph still pays it on every renderString and calcDimensions call. fntRenderStringFit measures a string several times a frame. This runs on the GUI thread whenever the optional font_CJK/font_Japanese file was found (lngInitFallbackFont), for every character >= 0x80: every CJK glyph of a CJK UI font, and twice for every CJK game title character drawn from the fallback.
- Fix: In cacheGlyph, return the active font's own valid entry first, then the fallback font's valid entry for the same codepoint. A fallback entry only becomes valid through this redirect, and every font load flushes all caches. Only on a miss in both, run the two cmap lookups as now.
- Justification: It adds one extra cache probe (~3 lines) to remove up to two cmap binary searches per character per pass on the GUI thread. It is the smallest change that makes cached glyphs free.
- Benefit: Removes ~1-2 FT_Get_Char_Index binary searches per drawn or measured CJK character per frame (INFERRED ~0.5-1 us each, so ~0.5-2 ms per frame on a CJK game list). Applies only when the fallback font exists.
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-541: games.bin records are trusted unvalidated; strcpy at launch copies a possibly unterminated startup into filename[32]

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/sources/hddsupport.cpp:3112-3124`; `RiptOPL/src/sources/hddsupport.cpp:2786-2787`
- Problem: hddLoadGameListCache derives the record count from the file size, ignoring any remainder, and publishes the raw bytes as hdl_game_info_t without terminating partition_name, name or startup. The native launch then does strcpy(filename[32], game->startup). A truncated, corrupted or foreign-layout games.bin can carry an unterminated startup. The copy then runs through the following flag, LBA and size bytes and can write past the 32-byte stack buffer.
- Fix: Use snprintf(filename, sizeof(filename), "%s", game->startup) at 2787. In hddLoadGameListCache, reject a file whose size is not a whole number of records, and force a NUL at the end of the three string fields of each loaded record.
- Justification: Adds about 4 lines. games.bin is a file on the user's disk that is read back as raw structs and reaches an unbounded strcpy. The size check and three terminators close that path at the one place the file enters the program.
- Benefit: Closes a stack overflow reachable from a corrupt cache file. Applies only with the opt-in cache enabled (INFERRED: overflow needs at least 32 non-zero bytes from offset 98 of a record).
- Effort: S. Risk: Low. Confidence: Med (INFERRED)

#### RIPTOPL-542: Unchecked malloc/calloc in theme element init (initMutableText, initImageTexture, initMutableImage, initBasic, initItemsList and others)

- Category: 6 (Bugs and security)
- Location(s): `RiptOPL/src/gfx/themeelems.cpp:211-217`; `RiptOPL/src/gfx/themeelems.cpp:226-227`; `RiptOPL/src/gfx/themeelems.cpp:275-277`; `RiptOPL/src/gfx/themeelems.cpp:703-704`; `RiptOPL/src/gfx/themeelems.cpp:722-724`; `RiptOPL/src/gfx/themeelems.cpp:767-768`; `RiptOPL/src/gfx/themeelems.cpp:773-775`; `RiptOPL/src/gfx/themeelems.cpp:810-811`; `RiptOPL/src/gfx/themeelems.cpp:1608-1610`; `RiptOPL/src/gfx/themeelems.cpp:2123-2131`; `RiptOPL/src/gfx/coverflow.cpp:360`; `RiptOPL/src/gfx/grid.cpp:674`
- Problem: These allocations are written through immediately with no NULL check, so an allocation failure during a theme (re)load is a NULL write and a crash rather than a disabled element. The same file does check in initGameCountText (509-519), thmParseDeviceList (79-81) and thmWallpaperLoad (1766-1768), and coverflow.cpp:360 / grid.cpp:674 test initMutableImage's result for NULL, which it can never return because it dereferences the malloc result first.
- Fix: Return NULL on allocation failure from initImageTexture, initImageInternalTexture (their callers already treat NULL as no texture) and initMutableImage (Coverflow/Grid already handle NULL; add the same early return in initStaticImage, initGameImage, initAttributeImage and initBackground). Leave initBasic/initItemsList as they are unless addGUIElem is changed too.
- Justification: A theme reload happens mid-session (view switch, settings change) while art caches hold the heap; a NULL write there takes the menu down. Only a few early returns are added, and they make the existing NULL guards in coverflow.cpp and grid.cpp reachable.
- Benefit: Crash becomes a disabled element under OOM at theme load; about 10 lines added. Failure of these small allocations is unlikely (INFERRED).
- Effort: S. Risk: Low. Confidence: Med (READ)

## Stale branches and tags (list only)

Listed for information; nothing here is a recommendation to delete without checking. Snapshot of the remote at audit time: 290 branches besides the default, 5 tags. "Open PR" marks a branch that is the head of an open pull request.

### Branches fully merged into rebuild/main (67)

| Branch | Last commit | Note |
|---|---|---|
| `claude/project-thread-6vq8sh` | 2026-10-27 |  |
| `claude/project-thread-8mgzui` | 2026-10-27 |  |
| `claude/project-thread-ng8s1m` | 2026-10-27 |  |
| `claude/project-thread-rxkpyt` | 2026-10-27 |  |
| `claude/project-thread-uu434q` | 2026-10-27 |  |
| `claude/project-thread-7os0js` | 2026-10-28 |  |
| `claude/project-thread-8p1nns` | 2026-10-28 |  |
| `claude/project-thread-8p1nns-lba48` | 2026-10-28 |  |
| `claude/project-thread-8p1nns-ripps2-docs` | 2026-10-28 |  |
| `claude/project-thread-8p1nns-views-docs` | 2026-10-28 |  |
| `claude/project-thread-8p1nns-ya2` | 2026-10-28 |  |
| `claude/project-thread-djicky` | 2026-10-28 |  |
| `claude/project-thread-j4ngtl` | 2026-10-28 |  |
| `claude/project-thread-jbitz4` | 2026-10-28 |  |
| `claude/project-thread-rupufe` | 2026-10-28 |  |
| `claude/project-thread-skxuai` | 2026-10-28 |  |
| `claude/project-thread-skxuai-grid` | 2026-10-28 |  |
| `claude/project-thread-skxuai-views` | 2026-10-28 |  |
| `claude/project-thread-vrn490` | 2026-10-28 |  |
| `claude/project-thread-xtzetq` | 2026-10-28 |  |
| `claude/merge-reviewed-batch` | 2026-10-29 |  |
| `claude/position-per-mode` | 2026-10-29 |  |
| `claude/project-thread-559h02` | 2026-10-29 |  |
| `claude/project-thread-lh5rt2` | 2026-10-29 |  |
| `claude/project-thread-nkjtgl` | 2026-10-29 |  |
| `claude/project-thread-qefkwr` | 2026-10-29 |  |
| `claude/project-thread-s5yoo8` | 2026-10-29 |  |
| `claude/ra-xerabora-1.0.1` | 2026-10-29 |  |
| `claude/review-followups` | 2026-10-29 |  |
| `claude/smap-tx-base` | 2026-10-29 |  |
| `claude/smb-read-closed` | 2026-10-29 |  |
| `claude/udpbd-read-only` | 2026-10-29 |  |
| `claude/udpfs-files-docs` | 2026-10-29 |  |
| `claude/ya02-iso9660-records` | 2026-10-29 |  |
| `claude/ya03-zso-validation` | 2026-10-29 |  |
| `claude/ya06-ds2-handoff` | 2026-10-29 |  |
| `claude/ya07-ds34usb-transfers` | 2026-10-29 |  |
| `claude/ya09-mmce-silent-switch` | 2026-10-29 |  |
| `claude/ya12-bdm-index-dots` | 2026-10-29 |  |
| `claude/ya14-mmce-igr-strings` | 2026-10-29 |  |
| `claude/ya17-usb-host-stop` | 2026-10-29 |  |
| `codex/ya01-history-filename-bounds` | 2026-10-29 |  |
| `codex/ya04-font-terminator` | 2026-10-29 |  |
| `codex/ya05-cheat-capacity` | 2026-10-29 |  |
| `codex/ya15-disc-key-semaphore` | 2026-10-29 |  |
| `codex/ya16-vmc-fragment-diagnostics` | 2026-10-29 |  |
| `claude/project-thread-h361ue` | 2026-10-30 |  |
| `claude/project-thread-h361ue-allgames-hide` | 2026-10-30 |  |
| `claude/project-thread-h361ue-library` | 2026-10-30 |  |
| `claude/project-thread-h361ue-views` | 2026-10-30 |  |
| `codex/fork-gaps-refresh-20261029-docs` | 2026-10-30 |  |
| `claude/project-thread-sanilb` | 2026-10-31 |  |
| `claude/project-thread-jiysgr-listcfg2` | 2026-11-03 |  |
| `codex/ci-action-warnings-20261103` | 2026-11-03 |  |
| `codex/controller-report-clarification` | 2026-11-03 |  |
| `codex/counter-allocation-safety` | 2026-11-03 |  |
| `codex/counter-review` | 2026-11-03 |  |
| `codex/docs-edition-captures` | 2026-11-03 |  |
| `codex/docs-ui-captures` | 2026-11-03 |  |
| `codex/mmce-ember-scan-retry` | 2026-11-03 |  |
| `docs/fix-app-handoff-path-scope` | 2026-11-03 |  |
| `feature/riptopllvm-release` | 2026-11-03 |  |
| `claude/project-thread-4dvtik` | 2026-11-04 |  |
| `codex/console-ilink-id` | 2026-11-04 |  |
| `fix/release-upload-retry-502-20261104` | 2026-11-04 |  |
| `fix/saved-theme-render-queue` | 2026-11-04 |  |
| `investigate/saved-theme-boot-freeze-20261103` | 2026-11-04 |  |

### Unmerged branches with no commit in the 30 days before the audit (3)

| Branch | Last commit | Ahead | Behind | Note |
|---|---|---:|---:|---|
| `codex/docs-audit-site` | 2026-10-01 | 187 | 4282 |  |
| `ps2build` | 2026-10-01 | 70 | 868 |  |
| `test/korium-audit-remediation` | 2026-10-12 | 134 | 775 |  |

### Tags (5)

| Tag | Date |
|---|---|
| `rolling-incebtion` | 2026-11-16 |
| `rolling-korium` | 2026-11-16 |
| `rolling-lite` | 2026-11-16 |
| `rolling-llvm` | 2026-11-16 |
| `rolling-ra` | 2026-11-16 |

## Files not read line by line (and why)

Vendored or generated content, reviewed structurally:

- language string tables: 36 files, 24210 lines. Not reviewed yet (config, build usage, size and whether it is needed), not a line-by-line read.
- lwIP-derived SMSTCPIP network stack (upstream OPL/SMS): 57 files, 16720 lines. Not reviewed yet (config, build usage, size and whether it is needed), not a line-by-line read.
- vendored FatFs (ChaN) sources: 7 files, 24099 lines. Not reviewed yet (config, build usage, size and whether it is needed), not a line-by-line read.
- vendored mbedTLS library: 280 files, 205284 lines. Not reviewed yet (config, build usage, size and whether it is needed), not a line-by-line read.

Not read as text:

- 365 files, 71.0 MB: binary file (covered by the asset/clutter inventory, not read as text). Mostly under `gfx` (142), `ship` (114), `docs` (71), `modules` (15), `audio` (9), `third_party` (9), `misc` (5).
- 6 files, 0.0 MB: empty file. Mostly under `ship` (6).

Not read yet (reading still in progress): 1342 files in 50 chunks.
