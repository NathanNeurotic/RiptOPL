# Claude audit: RiptOPL

Status: IN PROGRESS. 42 of 154 reading passes are in; finding IDs are provisional and will be renumbered until this line says COMPLETE.

- Repository: NathanNeurotic/RiptOPL
- Audited branch: rebuild/main
- Audited commit: b00e773bf28c1790b458ab53f25caad69e79f237
- Audit date: 2026-11-16
- Mode: read-only review. This branch adds only the `Claude-audit/` folder; no source, build or workflow file is changed.

All `path:line` references are pinned to the audited commit above. Each finding was raised by a reader that read the cited files line by line, then re-checked by a second, independent pass that re-read the cited lines and re-ran the reference searches. Findings that did not survive that check were dropped.

## Totals

- Findings in this report: 21. 276 more raised findings are waiting for their second check and are not shown yet.
- Raised by readers: 297. Dropped as wrong or unsupported on re-check: 0. Corrected on re-check: 9. Merged as duplicates: 0.

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

Not read yet (reading still in progress): 1438 files in 65 chunks.
